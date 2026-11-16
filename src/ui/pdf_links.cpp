#include "ui/pdf_links.hpp"

#include "ui/background_workers.hpp"

#include <QElapsedTimer>
#include <QModelIndex>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfLinkModel>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

// A worker task reads pages until this much time has gone, so a page render never waits
// long for PDFium's lock.
constexpr qint64 kSliceMs = 12;
// An outline is copied this deep and this long at most (a broken file can loop).
constexpr int kMaxOutlineDepth = 64;
constexpr int kMaxOutlineEntries = 50000;

// The links of `page`, annotations first (the order Qt PDF lists them), then addresses
// PDFium found written in the text. A link over several rectangles is one entry per
// rectangle.
std::vector<PdfPageLink> read_page_links(QPdfLinkModel& model, bool& loaded, int page) {
  if (model.page() != page) {
    // A page that fails to load leaves the model as it was: `loaded` says whether it reset.
    loaded = false;
    model.setPage(page);
  }
  std::vector<PdfPageLink> links;
  if (!loaded) {
    return links;
  }
  const int rows = model.rowCount(QModelIndex());
  for (int row = 0; row < rows; ++row) {
    const QPdfLink link =
        model.data(model.index(row), static_cast<int>(QPdfLinkModel::Role::Link)).value<QPdfLink>();
    PdfPageLink entry;
    if (link.url().isEmpty()) {
      if (link.page() < 0) {
        continue;
      }
      entry.target = {link.page(), link.location()};
    } else {
      entry.url = link.url();
    }
    const auto rectangles = link.rectangles();
    for (const QRectF& rectangle : rectangles) {
      entry.area = rectangle.normalized();
      if (entry.area.isEmpty()) {
        continue;
      }
      // Qt PDF lists a text address once per rectangle, each time with every rectangle so
      // far: keep one of each.
      if (std::find(links.begin(), links.end(), entry) == links.end()) {
        links.push_back(entry);
      }
    }
  }
  return links;
}

void copy_outline(const QPdfBookmarkModel& model, const QModelIndex& parent, int depth, int& budget,
                  std::vector<PdfOutlineEntry>& into) {
  const int rows = model.rowCount(parent);
  for (int row = 0; row < rows && budget > 0; ++row) {
    const QModelIndex index = model.index(row, 0, parent);
    PdfOutlineEntry entry;
    entry.title = model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Title)).toString().simplified();
    entry.target.page = model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Page)).toInt();
    entry.target.location = model.data(index, static_cast<int>(QPdfBookmarkModel::Role::Location)).toPointF();
    if (entry.target.page < 0) {
      entry.target = {};
    }
    --budget;
    if (depth + 1 < kMaxOutlineDepth) {
      copy_outline(model, index, depth + 1, budget, entry.children);
    }
    into.push_back(std::move(entry));
  }
}

}  // namespace

bool pdf_link_url_opens(const QUrl& url) {
  if (!url.isValid()) {
    return false;
  }
  const QString scheme = url.scheme().toLower();
  if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
    return !url.host().isEmpty();
  }
  return scheme == QStringLiteral("mailto") && !url.path().isEmpty();
}

QString pdf_link_address(const QUrl& url) {
  // The encoded form: what a browser's address bar takes back unchanged.
  const QString encoded = QString::fromUtf8(url.toEncoded());
  return encoded.isEmpty() ? url.toString() : encoded;
}

std::vector<PdfOutlineEntry> read_pdf_outline(QPdfDocument& document) {
  std::vector<PdfOutlineEntry> outline;
  QPdfBookmarkModel model;
  model.setDocument(&document);
  int budget = kMaxOutlineEntries;
  copy_outline(model, QModelIndex(), 0, budget, outline);
  return outline;
}

PdfLinkIndex::PdfLinkIndex(QObject* parent) : QObject(parent) {}

PdfLinkIndex::~PdfLinkIndex() {
  stop_worker();
}

void PdfLinkIndex::stop_worker() {
  if (in_flight_.valid()) {
    in_flight_.wait();
  }
}

void PdfLinkIndex::set_document(QPdfDocument* document) {
  stop_worker();
  ++document_generation_;
  document_ = document;
  const int count = document_ != nullptr ? std::max(0, document_->pageCount()) : 0;
  pages_.assign(static_cast<std::size_t>(count), PageState{});
  wanted_.clear();
}

void PdfLinkIndex::request_pages(const std::vector<int>& pages) {
  wanted_.clear();
  for (const int page : pages) {
    if (page >= 0 && page < static_cast<int>(pages_.size()) && !pages_[static_cast<std::size_t>(page)].read &&
        std::find(wanted_.begin(), wanted_.end(), page) == wanted_.end()) {
      wanted_.push_back(page);
    }
  }
  pump();
}

const std::vector<PdfPageLink>* PdfLinkIndex::page_links(int page) const {
  if (page < 0 || page >= static_cast<int>(pages_.size()) || !pages_[static_cast<std::size_t>(page)].read) {
    return nullptr;
  }
  return &pages_[static_cast<std::size_t>(page)].links;
}

bool PdfLinkIndex::busy() const {
  return busy_ || !wanted_.empty();
}

void PdfLinkIndex::pump() {
  if (busy_ || document_ == nullptr || wanted_.empty()) {
    return;
  }
  busy_ = true;
  QPdfDocument* document = document_;
  const std::uint64_t generation = document_generation_;
  // The model lives and dies on the worker: it reads through PDFium behind Qt PDF's lock,
  // like a render, and nothing else ever sees it.
  in_flight_ = launch_async([this, document, wanted = wanted_, generation] {
    std::vector<std::pair<int, std::vector<PdfPageLink>>> done;
    try {
      QElapsedTimer timer;
      timer.start();
      QPdfLinkModel model;
      bool loaded = false;
      QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&loaded] { loaded = true; });
      model.setDocument(document);
      for (const int page : wanted) {
        done.emplace_back(page, read_page_links(model, loaded, page));
        if (timer.elapsed() >= kSliceMs) {
          break;
        }
      }
    } catch (...) {
      // What was read stands; the rest is asked for again.
    }
    QMetaObject::invokeMethod(
        this,
        [this, done = std::move(done), generation]() mutable {
          busy_ = false;
          if (generation != document_generation_) {
            pump();
            return;
          }
          if (done.empty() && !wanted_.empty()) {
            // The read failed outright: give the page up as one without links, or it would
            // be asked for forever.
            done.emplace_back(wanted_.front(), std::vector<PdfPageLink>{});
          }
          std::vector<int> ready;
          for (auto& [page, links] : done) {
            auto& state = pages_[static_cast<std::size_t>(page)];
            if (!state.read) {
              state.read = true;
              state.links = std::move(links);
              ready.push_back(page);
            }
            std::erase(wanted_, page);
          }
          for (const int page : ready) {
            emit page_links_ready(page);
          }
          pump();
        },
        Qt::QueuedConnection);
  });
}

}  // namespace patchy::ui
