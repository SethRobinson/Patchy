#include "ui/pdf_page_ocr.hpp"

#include "ui/background_workers.hpp"

#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfSelection>
#include <QSize>
#include <QSizeF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

namespace patchy::ui {

namespace {

// A page is read at 300 pixels per inch, less when that would pass this many pixels (a
// poster-sized page): what recognition is measured at (docs/ocr-benchmark.md).
constexpr double kReadPpi = 300.0;
constexpr double kMaxReadPixels = 8.0e6;

// The page on white paper (PDFium paints none), turned the way the view shows it.
QImage page_image(QPdfDocument* document, int page, int quarter_turns) {
  const QSizeF points = document->pagePointSize(page);
  if (points.isEmpty()) {
    return {};
  }
  double scale = kReadPpi / 72.0;
  const double pixels = points.width() * scale * points.height() * scale;
  if (pixels > kMaxReadPixels) {
    scale *= std::sqrt(kMaxReadPixels / pixels);
  }
  const QSize size(std::max(1, static_cast<int>(std::lround(points.width() * scale))),
                   std::max(1, static_cast<int>(std::lround(points.height() * scale))));
  QPdfDocumentRenderOptions options;
  options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
  QImage rendered = document->render(page, size, options);
  if (rendered.isNull()) {
    return {};
  }
  const int turns = ((quarter_turns % 4) + 4) % 4;
  if (turns != 0) {
    rendered = rendered.transformed(QTransform().rotate(90.0 * turns));
  }
  QImage opaque(rendered.size(), QImage::Format_RGB32);
  opaque.fill(Qt::white);  // color-literal-ok: the paper a page is printed on
  QPainter painter(&opaque);
  painter.drawImage(0, 0, rendered);
  painter.end();
  return opaque.convertToFormat(QImage::Format_RGBA8888);
}

}  // namespace

// One run's engine: opened on the worker by the first page that needs it, then reused.
struct PdfPageOcr::Session {
  std::shared_ptr<ocr::Engine> engine;
  std::string language;
  std::unique_ptr<ocr::Recognizer> recognizer;
  ocr::Cancel cancel;
};

PdfPageOcr::PdfPageOcr(QObject* parent) : QObject(parent) {}

PdfPageOcr::~PdfPageOcr() {
  stop_worker();
}

void PdfPageOcr::stop_worker() {
  if (session_ != nullptr) {
    session_->cancel.request();
  }
  if (in_flight_.valid()) {
    in_flight_.wait();
  }
}

void PdfPageOcr::set_document(QPdfDocument* document) {
  stop_worker();
  ++generation_;
  busy_ = false;
  document_ = document;
  const int count = document_ != nullptr ? std::max(0, document_->pageCount()) : 0;
  states_.assign(static_cast<std::size_t>(count), PageState::Unread);
  texts_.assign(static_cast<std::size_t>(count), nullptr);
  recognized_ = 0;
  session_.reset();
  queue_.clear();
  running_ = false;
  done_ = 0;
  total_ = 0;
  last_error_.clear();
}

PdfPageOcr::PageState PdfPageOcr::page_state(int page) const {
  return page >= 0 && page < static_cast<int>(states_.size()) ? states_[static_cast<std::size_t>(page)]
                                                             : PageState::Unread;
}

std::shared_ptr<const PdfOcrPageText> PdfPageOcr::page_text(int page) const {
  return page >= 0 && page < static_cast<int>(texts_.size()) ? texts_[static_cast<std::size_t>(page)] : nullptr;
}

void PdfPageOcr::start(std::shared_ptr<ocr::Engine> engine, std::string language, const std::vector<int>& pages,
                       int quarter_turns) {
  if (document_ == nullptr || engine == nullptr) {
    return;
  }
  // The page in flight belongs to the old run: its answer is dropped.
  stop_worker();
  ++generation_;
  busy_ = false;
  session_ = std::make_shared<Session>();
  session_->engine = std::move(engine);
  session_->language = std::move(language);
  quarter_turns_ = quarter_turns;
  queue_.clear();
  for (const int page : pages) {
    if (page >= 0 && page < static_cast<int>(states_.size()) &&
        states_[static_cast<std::size_t>(page)] == PageState::Unread &&
        std::find(queue_.begin(), queue_.end(), page) == queue_.end()) {
      queue_.push_back(page);
    }
  }
  done_ = 0;
  total_ = static_cast<int>(queue_.size());
  last_error_.clear();
  running_ = true;
  emit progress_changed();
  pump();
}

void PdfPageOcr::stop() {
  if (!running_) {
    return;
  }
  queue_.clear();
  if (session_ != nullptr) {
    session_->cancel.request();
  }
  if (!busy_) {
    end_run(true);
  }
  // Else the page in flight answers (cancelled) and ends the run.
}

void PdfPageOcr::end_run(bool stopped) {
  running_ = false;
  session_.reset();
  queue_.clear();
  emit progress_changed();
  emit finished(stopped);
}

void PdfPageOcr::pump() {
  if (busy_ || !running_ || document_ == nullptr) {
    return;
  }
  if (queue_.empty()) {
    end_run(session_ != nullptr && session_->cancel.requested());
    return;
  }
  const int page = queue_.front();
  queue_.pop_front();
  busy_ = true;
  QPdfDocument* document = document_;
  const std::shared_ptr<Session> session = session_;
  const int quarter_turns = quarter_turns_;
  const std::uint64_t generation = generation_;
  in_flight_ = launch_async([this, document, session, page, quarter_turns, generation] {
    PageState state = PageState::Unread;
    std::shared_ptr<const PdfOcrPageText> text;
    bool failed = false;
    QString error;
    try {
      if (!document->getAllText(page).text().trimmed().isEmpty()) {
        state = PageState::OwnText;
      } else if (!session->cancel.requested()) {
        const QImage image = page_image(document, page, quarter_turns);
        if (image.isNull()) {
          state = PageState::NoText;
        } else {
          if (session->recognizer == nullptr) {
            session->recognizer = session->engine->open(session->language);
          }
          const ocr::ImageView view{image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                                    ocr::PixelLayout::Rgba8};
          const ocr::Result result = session->recognizer->recognize(view, &session->cancel);
          auto read = std::make_shared<PdfOcrPageText>(pdf_ocr_page_text(
              result, pdf_ocr_image_to_page(image.size(), document->pagePointSize(page), quarter_turns)));
          state = read->has_text() ? PageState::Recognized : PageState::NoText;
          if (read->has_text()) {
            text = std::move(read);
          }
        }
      }
    } catch (const ocr::RecognitionCancelled&) {
      state = PageState::Unread;  // not read: a later run takes it again
    } catch (const std::exception& failure) {
      failed = true;
      error = QString::fromUtf8(failure.what());
    } catch (...) {
      failed = true;
    }
    QMetaObject::invokeMethod(
        this,
        [this, page, state, text = std::move(text), failed, error, generation]() mutable {
          if (generation != generation_) {
            return;  // a newer run or another document owns the state now
          }
          busy_ = false;
          const auto slot = static_cast<std::size_t>(page);
          if (state != PageState::Unread && slot < states_.size()) {
            states_[slot] = state;
            texts_[slot] = std::move(text);
            if (state == PageState::Recognized) {
              ++recognized_;
            }
            ++done_;
            emit page_done(page);
            emit progress_changed();
          }
          if (failed) {
            // The engine is not going to do better on the next page: the run ends here.
            last_error_ = error.isEmpty() ? QStringLiteral("text recognition failed") : error;
            queue_.clear();
          }
          pump();
        },
        Qt::QueuedConnection);
  });
}

}  // namespace patchy::ui
