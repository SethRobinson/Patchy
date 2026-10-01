#include "ui/pdf_import.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/memory_info.hpp"
#include "ui/pdf_export.hpp"
#include "ui/ui_profile.hpp"

#include "formats/pdf_document_io.hpp"

#include "ui/background_workers.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QFile>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QSpinBox>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The real PDF importer, built only where the optional Qt PDF add-on is present (see
// pdf_import_stub.cpp for the other half). Qt PDF wraps PDFium; Patchy only ever renders
// pages to images, so nothing here interprets PDF vector content.

namespace patchy::ui {
namespace {

constexpr double kPointsPerInch = 72.0;
constexpr int kMinResolutionPpi = 12;
constexpr int kMaxResolutionPpi = 1200;
// One page at 1200 ppi on a large sheet would allocate gigabytes, so the render size is
// capped per axis and the effective resolution drops to match (reported as a notice).
constexpr int kMaxRenderedPagePixels = 20000;
constexpr int kThumbnailHeight = 96;
// Set on a page-list row whose thumbnail has not been rendered yet.
constexpr int kThumbnailPendingRole = Qt::UserRole + 1;

QString settings_key(const char* leaf) {
  return QStringLiteral("imports/pdf") + QLatin1String(leaf);
}

QPdfDocumentRenderOptions render_options(const PdfImportOptions& options) {
  QPdfDocumentRenderOptions render;
  QPdfDocumentRenderOptions::RenderFlags flags = QPdfDocumentRenderOptions::RenderFlag::None;
  if (options.annotations) {
    flags |= QPdfDocumentRenderOptions::RenderFlag::Annotations;
  }
  if (!options.anti_alias) {
    flags |= QPdfDocumentRenderOptions::RenderFlag::TextAliased;
    flags |= QPdfDocumentRenderOptions::RenderFlag::ImageAliased;
    flags |= QPdfDocumentRenderOptions::RenderFlag::PathAliased;
  }
  render.setRenderFlags(flags);
  return render;
}

// Page size in device pixels at the requested resolution, clamped so one huge page cannot
// exhaust memory. Returns an invalid size when the page has no usable size.
QSize rendered_page_size(QSizeF page_points, int resolution_ppi, bool* clamped) {
  if (page_points.width() <= 0.0 || page_points.height() <= 0.0) {
    return {};
  }
  double width = page_points.width() / kPointsPerInch * resolution_ppi;
  double height = page_points.height() / kPointsPerInch * resolution_ppi;
  if (const double longest = std::max(width, height); longest > kMaxRenderedPagePixels) {
    const double fit = kMaxRenderedPagePixels / longest;
    width *= fit;
    height *= fit;
    if (clamped != nullptr) {
      *clamped = true;
    }
  }
  return {std::max(1, static_cast<int>(std::lround(width))), std::max(1, static_cast<int>(std::lround(height)))};
}

// Drops a uniform border of one color. Qt PDF exposes no crop box selection, so trimming
// to the content happens after rendering. The comparison keeps alpha, because PDFium
// renders onto a TRANSPARENT page: a vector PDF with no painted background imports with a
// transparent border, and a scanned page imports with its own white one. Both trim.
QImage trimmed_to_content(const QImage& page) {
  if (page.isNull()) {
    return page;
  }
  const QImage source = page.convertToFormat(QImage::Format_ARGB32);
  const QRgb border = source.pixel(0, 0);
  int left = source.width();
  int right = -1;
  int top = source.height();
  int bottom = -1;
  for (int y = 0; y < source.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    for (int x = 0; x < source.width(); ++x) {
      if (row[x] == border) {
        continue;
      }
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  if (right < left || bottom < top) {
    return page;  // a blank page: keep it whole rather than collapsing it to nothing
  }
  return page.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
}

QString error_message(QPdfDocument::Error error, const QString& file_name) {
  switch (error) {
    case QPdfDocument::Error::IncorrectPassword:
      return QObject::tr("%1 is password protected.").arg(file_name);
    case QPdfDocument::Error::FileNotFound:
      return QObject::tr("%1 could not be found.").arg(file_name);
    case QPdfDocument::Error::InvalidFileFormat:
      return QObject::tr("%1 is not a readable PDF file.").arg(file_name);
    case QPdfDocument::Error::UnsupportedSecurityScheme:
      return QObject::tr("%1 uses a security scheme Patchy cannot open.").arg(file_name);
    case QPdfDocument::Error::DataNotYetAvailable:
      return QObject::tr("%1 is still loading.").arg(file_name);
    case QPdfDocument::Error::Unknown:
    case QPdfDocument::Error::None:
      break;
  }
  return QObject::tr("%1 could not be opened.").arg(file_name);
}

// Opens the document, asking for a password (up to three tries) when it is encrypted.
// A null parent means the non-interactive path: a wrong or missing password just fails.
// The password that worked lands in *accepted_password so the editable importer can
// derive its own keys from the same secret.
QPdfDocument::Error open_pdf(QPdfDocument& document, const QString& path, const QString& password, QWidget* parent,
                             QString* accepted_password = nullptr) {
  if (!password.isEmpty()) {
    document.setPassword(password);
  }
  auto error = document.load(path);
  if (error == QPdfDocument::Error::None && accepted_password != nullptr) {
    *accepted_password = password;
  }
  if (error != QPdfDocument::Error::IncorrectPassword || parent == nullptr) {
    return error;
  }
  for (int attempt = 0; attempt < 3 && error == QPdfDocument::Error::IncorrectPassword; ++attempt) {
    bool accepted = false;
    const auto entered = QInputDialog::getText(parent, QObject::tr("Open PDF"),
                                               QObject::tr("Password for %1:").arg(QFileInfo(path).fileName()),
                                               QLineEdit::Password, QString(), &accepted);
    if (!accepted) {
      return QPdfDocument::Error::IncorrectPassword;
    }
    document.setPassword(entered);
    error = document.load(path);
    if (error == QPdfDocument::Error::None && accepted_password != nullptr) {
      *accepted_password = entered;
    }
  }
  return error;
}

std::string scanned_pages_notice(int count) {
  return QObject::tr("%n scanned page(s) had nothing to keep editable and were imported as flattened images.",
                     nullptr, count)
      .toStdString();
}

// One page in two steps, so an interactive import can overlap them: PDFium's render
// (about half a second for a 17 Mpx JPEG 2000 scan; single-threaded behind Qt PDF's
// global lock, so two pages can never render at once), then everything that only needs
// the pixels: the trim, and for separate documents the conversion and the pass-through
// capture, which can run while the NEXT page renders.
struct RenderedPage {
  QImage image;                      // kept only for the layers-in-one-document target
  std::optional<Document> document;  // separate documents: built where the pixels are hot
  std::shared_ptr<const PdfSourcePage> source;
};

struct RenderJob {
  int page{0};
  QSize size;
  QString title;
};

QImage render_page_image(QPdfDocument& pdf, const RenderJob& job, const QPdfDocumentRenderOptions& render) {
  const std::string profile_detail = "page=" + std::to_string(job.page + 1);
  const UiProfileScope profile_scope("pdf_import.render", profile_detail);
  return pdf.render(job.page, job.size, render);
}

// `reader_mutex` serializes the probe: a PageReader is one thread at a time, and two
// finishing steps can overlap when a render is quicker than the conversion before it.
RenderedPage finish_rendered_page(QImage image, const RenderJob& job, const PdfImportOptions& options,
                                  int resolution_ppi, const pdf::PageReader* reader, bool capture_source,
                                  std::mutex& reader_mutex) {
  const std::string profile_detail = "page=" + std::to_string(job.page + 1);
  RenderedPage result;
  result.image = std::move(image);
  bool trimmed = false;
  if (options.trim_to_bounding_box) {
    const UiProfileScope profile_scope("pdf_import.trim", profile_detail);
    const QSize before = result.image.size();
    result.image = trimmed_to_content(result.image);
    trimmed = result.image.size() != before;
  }
  // The page's own image can stand in for these pixels only if they ARE the page:
  // nothing trimmed away, and no annotation drawn over the image.
  if (capture_source && reader != nullptr && !trimmed) {
    const UiProfileScope profile_scope("pdf_import.probe", profile_detail);
    const std::lock_guard<std::mutex> lock(reader_mutex);
    const auto probe = reader->probe_page(job.page);
    if (!(options.annotations && probe.has_annotations)) {
      result.source = probe.source_page;
    }
  }
  if (options.separate_documents) {
    const UiProfileScope profile_scope("pdf_import.to_document", profile_detail);
    Document document = document_from_qimage(result.image, job.title.toStdString());
    document.print_settings().horizontal_ppi = resolution_ppi;
    document.print_settings().vertical_ppi = resolution_ppi;
    attach_pdf_source_page(document, result.source);
    result.document = std::move(document);
    result.image = QImage();  // 70 MB a page: never hold the frame and the document both
  }
  return result;
}

std::optional<PdfImportResult> render_pages(QPdfDocument& pdf, const PdfImportOptions& options,
                                            const QString& file_name, QString* error,
                                            const pdf::PageReader* reader) {
  std::vector<int> pages = options.pages;
  if (pages.empty()) {
    pages.push_back(0);
  }
  const int resolution_ppi = std::clamp(options.resolution_ppi, kMinResolutionPpi, kMaxResolutionPpi);
  const auto render = render_options(options);

  bool clamped = false;
  std::vector<RenderJob> jobs;
  std::vector<int> job_positions;  // 1-based position in the selection, for progress
  int selection_position = 0;
  for (const int page : pages) {
    ++selection_position;
    if (page < 0 || page >= pdf.pageCount()) {
      continue;
    }
    const QSize size = rendered_page_size(pdf.pagePointSize(page), resolution_ppi, &clamped);
    if (!size.isValid()) {
      continue;
    }
    jobs.push_back({page, size, QObject::tr("Page %1").arg(page + 1)});
    job_positions.push_back(selection_position);
  }
  // Original image data only makes sense for a document that is one page.
  const bool capture_source = options.separate_documents || jobs.size() == 1U;
  // A caller that reports progress is interactive: the work goes to workers and this
  // thread pumps events, so the progress dialog paints and Cancel answers mid-page. The
  // non-interactive load may itself be on a worker, where pumping would be wrong.
  const bool pump = static_cast<bool>(options.progress) && !kBackgroundWorkRunsInline &&
                    QCoreApplication::instance() != nullptr &&
                    QThread::currentThread() == QCoreApplication::instance()->thread();
  std::mutex reader_mutex;
  const auto start_render = [&](std::size_t index) {
    return launch_async([&pdf, &render, job = jobs[index]] { return render_page_image(pdf, job, render); });
  };
  const auto start_finish = [&](std::size_t index, QImage image) {
    return launch_async([&options, &reader_mutex, reader, resolution_ppi, capture_source, job = jobs[index],
                         image = std::move(image)]() mutable {
      return finish_rendered_page(std::move(image), job, options, resolution_ppi, reader, capture_source,
                                  reader_mutex);
    });
  };
  const auto pump_until_ready = [](const auto& future) {
    while (future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
      QApplication::processEvents(QEventLoop::AllEvents, 15);
    }
  };

  std::vector<QImage> frames;
  QStringList layer_names;
  std::vector<std::shared_ptr<const PdfSourcePage>> sources;
  PdfImportResult result;
  int page_count = 0;
  // Files a finished page where its target wants it, in page order.
  const auto take = [&](std::size_t index, RenderedPage rendered) {
    ++page_count;
    if (options.separate_documents) {
      // Photoshop's behavior: every page is its own document at its own size. The
      // caller opens one session per entry, page 1 first.
      if (page_count == 1) {
        result.document = std::move(*rendered.document);
        result.document_title = jobs[index].title;
      } else {
        result.extra_documents.push_back({std::move(*rendered.document), jobs[index].title});
      }
    } else {
      frames.push_back(std::move(rendered.image));
      layer_names.push_back(jobs[index].title);
      sources.push_back(std::move(rendered.source));
    }
  };

  bool stopped = false;
  std::optional<std::size_t> failed_index;
  std::future<QImage> ahead;  // the render of the page the loop reaches next
  std::deque<std::pair<std::size_t, std::future<RenderedPage>>> finishing;
  const auto take_oldest = [&] {
    pump_until_ready(finishing.front().second);
    const auto index = finishing.front().first;
    auto rendered = finishing.front().second.get();
    finishing.pop_front();
    take(index, std::move(rendered));
  };
  for (std::size_t index = 0; index < jobs.size(); ++index) {
    if (options.progress && !options.progress(job_positions[index], static_cast<int>(pages.size()))) {
      stopped = true;
      break;
    }
    if (!pump) {
      QImage image = render_page_image(pdf, jobs[index], render);
      if (image.isNull()) {
        failed_index = index;
        break;
      }
      take(index, finish_rendered_page(std::move(image), jobs[index], options, resolution_ppi, reader,
                                       capture_source, reader_mutex));
      continue;
    }
    if (!ahead.valid()) {
      ahead = start_render(index);
    }
    pump_until_ready(ahead);
    QImage image = ahead.get();
    if (image.isNull()) {
      failed_index = index;
      break;
    }
    if (index + 1U < jobs.size()) {
      ahead = start_render(index + 1U);  // PDFium moves on while this page is converted
    }
    finishing.emplace_back(index, start_finish(index, std::move(image)));
    // At most two pages of pixels in flight besides the one rendering.
    while (finishing.size() > 1U) {
      take_oldest();
    }
  }
  if (ahead.valid()) {
    ahead.wait();  // a read-ahead nobody will take (Cancel, a failed page): finish before `pdf` goes away
  }
  while (!finishing.empty()) {
    take_oldest();
  }
  if (failed_index.has_value()) {
    if (error != nullptr) {
      *error = QObject::tr("Page %1 of %2 could not be rendered.").arg(jobs[*failed_index].page + 1).arg(file_name);
    }
    return std::nullopt;
  }
  if (page_count == 0) {
    if (error != nullptr) {
      *error = QObject::tr("%1 has no pages Patchy could render.").arg(file_name);
    }
    return std::nullopt;
  }

  if (!options.separate_documents) {
    auto document = document_from_frames(std::move(frames), layer_names);
    if (!document.has_value()) {
      if (error != nullptr) {
        *error = QObject::tr("%1 could not be turned into a document.").arg(file_name);
      }
      return std::nullopt;
    }
    document->print_settings().horizontal_ppi = resolution_ppi;
    document->print_settings().vertical_ppi = resolution_ppi;
    if (sources.size() == 1U) {
      attach_pdf_source_page(*document, sources.front());
    }
    result.document = std::move(*document);
  }

  result.notices.push_back(QObject::tr("PDF content was rasterized at %1 ppi; text and vectors are pixels now.")
                               .arg(resolution_ppi)
                               .toStdString());
  if (page_count > 1 && options.separate_documents) {
    result.notices.push_back(
        QObject::tr("%1 pages opened as separate documents.").arg(page_count).toStdString());
  } else if (page_count > 1) {
    result.notices.push_back(
        QObject::tr("%1 pages imported as layers; only the first starts visible.").arg(page_count).toStdString());
  } else if (pdf.pageCount() > 1) {
    result.notices.push_back(QObject::tr("Only page 1 of %1 was imported.").arg(pdf.pageCount()).toStdString());
  }
  if (!options.annotations) {
    result.notices.push_back(QObject::tr("Annotations were not drawn.").toStdString());
  }
  if (options.trim_to_bounding_box) {
    result.notices.push_back(QObject::tr("Pages were trimmed to their content.").toStdString());
  }
  if (clamped) {
    result.notices.push_back(
        QObject::tr("A page was too large to render at %1 ppi and was scaled down.").arg(resolution_ppi).toStdString());
  }
  if (stopped) {
    result.notices.push_back(QObject::tr("Import stopped after %1 of %2 pages.")
                                 .arg(page_count)
                                 .arg(static_cast<int>(pages.size()))
                                 .toStdString());
  }
  return result;
}

}  // namespace

bool pdf_import_is_available() {
  return true;
}

std::optional<PdfImportResult> load_pdf_document(const QString& path, const PdfImportOptions& options,
                                                 const QString& password, QString* error) {
  const auto file_name = QFileInfo(path).fileName();
  QPdfDocument pdf;
  if (const auto status = open_pdf(pdf, path, password, nullptr); status != QPdfDocument::Error::None) {
    if (error != nullptr) {
      *error = error_message(status, file_name);
    }
    return std::nullopt;
  }
  // The reader only adds the original image data for a later PDF export; a file Patchy's
  // own parser cannot open still renders through PDFium.
  std::unique_ptr<pdf::PageReader> reader;
  if (QFile file(path); file.open(QIODevice::ReadOnly)) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<qint64>(0, file.size())));
    const qint64 read = file.read(reinterpret_cast<char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(std::max<qint64>(0, read)));
    try {
      reader = std::make_unique<pdf::PageReader>(std::move(bytes), password.toStdString());
    } catch (const std::exception&) {
      reader.reset();
    }
  }
  return render_pages(pdf, options, file_name, error, reader.get());
}

std::optional<PdfImportResult> run_pdf_import_dialog(QWidget* parent, const QString& path) {
  const auto file_name = QFileInfo(path).fileName();
  QPdfDocument pdf;
  QString accepted_password;
  if (const auto status = open_pdf(pdf, path, QString(), parent, &accepted_password);
      status != QPdfDocument::Error::None) {
    QMessageBox box(QMessageBox::Warning, QObject::tr("Open PDF"), error_message(status, file_name), QMessageBox::Ok,
                    parent);
    box.setObjectName(QStringLiteral("pdfOpenFailedMessageBox"));
    exec_dialog(box);
    return std::nullopt;
  }

  auto settings = app_settings();
  PdfImportOptions options;
  options.resolution_ppi = std::clamp(settings.value(settings_key("Resolution"), options.resolution_ppi).toInt(),
                                      kMinResolutionPpi, kMaxResolutionPpi);
  options.annotations = settings.value(settings_key("Annotations"), options.annotations).toBool();
  options.anti_alias = settings.value(settings_key("AntiAlias"), options.anti_alias).toBool();
  options.trim_to_bounding_box = settings.value(settings_key("TrimToContent"), options.trim_to_bounding_box).toBool();
  options.separate_documents =
      settings.value(settings_key("PagesTarget"), QStringLiteral("documents")).toString() != QStringLiteral("layers");

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("pdfImportDialog"));
  dialog.setWindowTitle(QObject::tr("Import PDF - %1").arg(file_name));
  dialog.resize(620, 460);
  auto* layout = new QVBoxLayout(&dialog);

  auto* body = new QHBoxLayout();
  auto* pages_list = new QListWidget(&dialog);
  pages_list->setObjectName(QStringLiteral("pdfImportPagesList"));
  pages_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  pages_list->setIconSize(QSize(kThumbnailHeight, kThumbnailHeight));
  // Thumbnails render at a fixed small size regardless of the chosen import resolution:
  // this is a page picker, not a preview of output quality. Every one is a PDFium page
  // render, so none happens before the dialog shows: each row starts with a blank icon
  // of the right shape (the text never shifts) and a zero timer fills them in one per
  // tick, rows on screen first, until the dialog closes.
  const auto thumbnail_size = [&pdf](int page) {
    const QSizeF page_points = pdf.pagePointSize(page);
    if (!(page_points.height() > 0.0) || !(page_points.width() > 0.0)) {
      return QSize();
    }
    const double aspect = page_points.width() / page_points.height();
    return aspect >= 1.0
               ? QSize(kThumbnailHeight, std::max(1, static_cast<int>(std::lround(kThumbnailHeight / aspect))))
               : QSize(std::max(1, static_cast<int>(std::lround(kThumbnailHeight * aspect))), kThumbnailHeight);
  };
  for (int page = 0; page < pdf.pageCount(); ++page) {
    auto* item = new QListWidgetItem(QObject::tr("Page %1").arg(page + 1), pages_list);
    if (const QSize size = thumbnail_size(page); size.isValid()) {
      QPixmap blank(size);
      blank.fill(Qt::transparent);
      item->setIcon(QIcon(blank));
      item->setData(kThumbnailPendingRole, true);
    }
  }
  auto* thumbnail_timer = new QTimer(&dialog);
  thumbnail_timer->setObjectName(QStringLiteral("pdfImportThumbnailTimer"));
  thumbnail_timer->setInterval(0);
  QObject::connect(thumbnail_timer, &QTimer::timeout, &dialog, [&pdf, pages_list, thumbnail_timer, thumbnail_size] {
    const QRect viewport = pages_list->viewport()->rect();
    QListWidgetItem* next = nullptr;
    for (int row = 0; row < pages_list->count(); ++row) {
      auto* item = pages_list->item(row);
      if (!item->data(kThumbnailPendingRole).toBool()) {
        continue;
      }
      if (next == nullptr) {
        next = item;  // nothing pending on screen: keep going in page order
      }
      if (pages_list->visualItemRect(item).intersects(viewport)) {
        next = item;
        break;
      }
    }
    if (next == nullptr) {
      thumbnail_timer->stop();
      return;
    }
    next->setData(kThumbnailPendingRole, false);
    const int page = pages_list->row(next);
    const UiProfileScope profile_scope("pdf_import.thumbnail");
    const QImage thumbnail = pdf.render(page, thumbnail_size(page));
    if (!thumbnail.isNull()) {
      next->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    }
  });
  // The import renders through the same QPdfDocument from a worker: no thumbnail may
  // start once the dialog is on its way out.
  QObject::connect(&dialog, &QDialog::finished, thumbnail_timer, &QTimer::stop);
  thumbnail_timer->start();
  if (pages_list->count() > 0) {
    pages_list->item(0)->setSelected(true);
    pages_list->setCurrentRow(0);
  }
  body->addWidget(pages_list, 1);

  auto* side = new QVBoxLayout();
  auto* select_all = new QPushButton(QObject::tr("Select All Pages"), &dialog);
  select_all->setObjectName(QStringLiteral("pdfImportSelectAllButton"));
  QObject::connect(select_all, &QPushButton::clicked, pages_list, &QListWidget::selectAll);
  side->addWidget(select_all);

  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);
  // Editable is the default: shapes stay shapes and text stays text, which is what
  // an image editor's import is FOR. Flatten remains for scanned pages and for
  // content the vector reader cannot model (it also serves as the automatic
  // fallback when editable import fails).
  auto* mode = new QComboBox(&dialog);
  mode->setObjectName(QStringLiteral("pdfImportModeCombo"));
  mode->addItem(QObject::tr("Editable shapes and text"), QStringLiteral("editable"));
  mode->addItem(QObject::tr("Flattened image per page"), QStringLiteral("flatten"));
  const auto stored_mode = settings.value(settings_key("Mode"), QStringLiteral("editable")).toString();
  mode->setCurrentIndex(std::max(0, mode->findData(stored_mode)));
  form->addRow(new QLabel(QObject::tr("Import as:"), &dialog), mode);
  // Separate documents is Photoshop's Import PDF behavior and the default; layers on
  // one canvas remains for flip-book style uses (it is what the image-sequence and
  // animated-GIF imports do too).
  auto* pages_target = new QComboBox(&dialog);
  pages_target->setObjectName(QStringLiteral("pdfImportPagesTargetCombo"));
  pages_target->addItem(QObject::tr("Separate documents"), QStringLiteral("documents"));
  pages_target->addItem(QObject::tr("Layers in one document"), QStringLiteral("layers"));
  pages_target->setCurrentIndex(
      std::max(0, pages_target->findData(options.separate_documents ? QStringLiteral("documents")
                                                                    : QStringLiteral("layers"))));
  form->addRow(new QLabel(QObject::tr("Pages become:"), &dialog), pages_target);
  auto* resolution = new QSpinBox(&dialog);
  resolution->setObjectName(QStringLiteral("pdfImportResolutionSpin"));
  resolution->setRange(kMinResolutionPpi, kMaxResolutionPpi);
  resolution->setSuffix(QObject::tr(" ppi"));
  resolution->setValue(options.resolution_ppi);
  configure_dialog_spinbox(resolution);
  form->addRow(new QLabel(QObject::tr("Resolution:"), &dialog), resolution);
  side->addLayout(form);

  auto* annotations = new QCheckBox(QObject::tr("Include annotations"), &dialog);
  annotations->setObjectName(QStringLiteral("pdfImportAnnotationsCheck"));
  annotations->setChecked(options.annotations);
  side->addWidget(annotations);

  auto* anti_alias = new QCheckBox(QObject::tr("Anti-alias"), &dialog);
  anti_alias->setObjectName(QStringLiteral("pdfImportAntiAliasCheck"));
  anti_alias->setChecked(options.anti_alias);
  side->addWidget(anti_alias);

  auto* trim = new QCheckBox(QObject::tr("Trim to content"), &dialog);
  trim->setObjectName(QStringLiteral("pdfImportTrimCheck"));
  trim->setChecked(options.trim_to_bounding_box);
  side->addWidget(trim);

  auto* size_label = new QLabel(&dialog);
  size_label->setObjectName(QStringLiteral("pdfImportSizeLabel"));
  size_label->setWordWrap(true);
  side->addWidget(size_label);
  side->addStretch(1);
  body->addLayout(side);
  layout->addLayout(body);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* import_button = buttons->addButton(QObject::tr("Import"), QDialogButtonBox::AcceptRole);
  import_button->setObjectName(QStringLiteral("pdfImportButton"));
  import_button->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  const auto selected_pages = [pages_list] {
    std::vector<int> pages;
    for (int row = 0; row < pages_list->count(); ++row) {
      if (pages_list->item(row)->isSelected()) {
        pages.push_back(row);
      }
    }
    return pages;
  };
  const auto sync_size_label = [&] {
    const auto pages = selected_pages();
    import_button->setEnabled(!pages.empty());
    if (pages.empty()) {
      size_label->setText(QObject::tr("Select at least one page."));
      return;
    }
    const QSize size = rendered_page_size(pdf.pagePointSize(pages.front()), resolution->value(), nullptr);
    const bool separate = pages_target->currentData().toString() == QStringLiteral("documents");
    QString text = (separate ? QObject::tr("%1 document(s), first page %2 x %3 px")
                             : QObject::tr("%1 layer(s), first page %2 x %3 px"))
                       .arg(pages.size())
                       .arg(size.width())
                       .arg(size.height());
    // Every page is held as RGBA pixels, which a long scanned PDF turns into gigabytes
    // (85 pages at 300 ppi is about 6 GB). Say so before the import, not after.
    double bytes = 0.0;
    for (const int page : pages) {
      const QSize page_size = rendered_page_size(pdf.pagePointSize(page), resolution->value(), nullptr);
      if (page_size.isValid()) {
        bytes += 4.0 * page_size.width() * page_size.height();
      }
    }
    const double gigabytes = bytes / (1024.0 * 1024.0 * 1024.0);
    if (gigabytes >= 0.5) {
      const qint64 ram_mb = total_physical_ram_mb();
      const double ram_gb = ram_mb > 0 ? static_cast<double>(ram_mb) / 1024.0 : 0.0;
      text += QLatin1Char('\n');
      text += ram_gb > 0.0 && gigabytes > ram_gb / 2.0
                  ? QObject::tr("About %1 GB of memory, more than half of this computer's %2 GB. Fewer pages "
                                "or a lower resolution will open faster.")
                        .arg(QLocale().toString(gigabytes, 'f', 1), QLocale().toString(ram_gb, 'f', 0))
                  : QObject::tr("About %1 GB of memory.").arg(QLocale().toString(gigabytes, 'f', 1));
    }
    size_label->setText(text);
  };
  QObject::connect(pages_list, &QListWidget::itemSelectionChanged, &dialog, sync_size_label);
  QObject::connect(resolution, &QSpinBox::valueChanged, &dialog, sync_size_label);
  QObject::connect(pages_target, &QComboBox::currentIndexChanged, &dialog, sync_size_label);
  // The render toggles only shape the raster path; graying them out in editable
  // mode says so without a second dialog layout.
  const auto sync_mode_controls = [mode, annotations, anti_alias, trim] {
    const bool flatten = mode->currentData().toString() == QStringLiteral("flatten");
    annotations->setEnabled(flatten);
    anti_alias->setEnabled(flatten);
    trim->setEnabled(flatten);
  };
  QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, sync_mode_controls);
  sync_mode_controls();
  sync_size_label();

  remember_dialog_position(dialog);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  options.pages = selected_pages();
  options.resolution_ppi = resolution->value();
  options.annotations = annotations->isChecked();
  options.anti_alias = anti_alias->isChecked();
  options.trim_to_bounding_box = trim->isChecked();
  options.separate_documents = pages_target->currentData().toString() == QStringLiteral("documents");
  const bool editable = mode->currentData().toString() == QStringLiteral("editable");
  settings.setValue(settings_key("Mode"), mode->currentData().toString());
  settings.setValue(settings_key("PagesTarget"), pages_target->currentData().toString());
  settings.setValue(settings_key("Resolution"), options.resolution_ppi);
  settings.setValue(settings_key("Annotations"), options.annotations);
  settings.setValue(settings_key("AntiAlias"), options.anti_alias);
  settings.setValue(settings_key("TrimToContent"), options.trim_to_bounding_box);

  // A multi-page import runs on the UI thread page by page (QPdfDocument renders here;
  // the vector reader runs on a worker while this pumps events), so the dialog is what
  // keeps the app visibly alive. Cancel keeps the pages already imported.
  const int selected_count = static_cast<int>(std::max<std::size_t>(1, options.pages.size()));
  QProgressDialog progress(QObject::tr("Importing page %1 of %2...").arg(1).arg(selected_count),
                           QObject::tr("Cancel"), 0, selected_count, parent);
  progress.setObjectName(QStringLiteral("pdfImportProgressDialog"));
  progress.setWindowTitle(QObject::tr("Import PDF - %1").arg(file_name));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  remember_dialog_position(progress);
  progress.setValue(0);
  options.progress = [&progress, selected_count](int position, int count) {
    progress.setLabelText(QObject::tr("Importing page %1 of %2...").arg(position).arg(count));
    progress.setValue(std::min(position - 1, selected_count));
    QApplication::processEvents(QEventLoop::AllEvents);
    return !progress.wasCanceled();
  };

  // One reader for the whole import: the editable pages, the scan probe, and the original
  // image data of flattened pages all come from it, so the file is parsed once instead
  // of once per page. A file Patchy's own parser cannot open still imports through
  // PDFium, just without any of those.
  std::unique_ptr<pdf::PageReader> reader;
  QString reader_failure;
  {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
      const auto read_started = std::chrono::steady_clock::now();
      std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<qint64>(0, file.size())));
      const qint64 read = file.read(reinterpret_cast<char*>(bytes.data()), static_cast<qint64>(bytes.size()));
      bytes.resize(static_cast<std::size_t>(std::max<qint64>(0, read)));
      log_ui_profile("pdf_import.read_file",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - read_started).count());
      try {
        const UiProfileScope profile_scope("pdf_import.open_reader");
        reader = std::make_unique<pdf::PageReader>(std::move(bytes), accepted_password.toStdString());
      } catch (const std::exception& exception) {
        reader_failure = QString::fromUtf8(exception.what());
        if (reader_failure == QStringLiteral("This PDF is password protected.")) {
          reader_failure = QObject::tr("This PDF is password protected.");
        }
      }
    } else {
      reader_failure = file.errorString();
    }
  }

  QString editable_failure;
  int scanned_pages = 0;
  if (editable && reader == nullptr) {
    editable_failure = reader_failure;
  } else if (editable) {
    // The vector reader builds one page per call. Separate-documents mode calls it
    // once per selected page; layers mode keeps page 1 only, because editable text
    // and shapes from several pages cannot honestly share one canvas.
    std::vector<int> editable_pages = options.pages.empty() ? std::vector<int>{0} : options.pages;
    if (!options.separate_documents && editable_pages.size() > 1) {
      editable_pages.resize(1);
    }
    // A page that is (mostly) an image in a codec the editable reader cannot decode is a
    // scan: there is nothing to keep editable, and importing it "editable" would drop
    // the image and keep only what little sits on top of it. Such pages flatten.
    std::vector<pdf::PageProbe> probes;
    probes.reserve(editable_pages.size());
    {
      const UiProfileScope profile_scope("pdf_import.probe_pages");
      for (const int page : editable_pages) {
        probes.push_back(reader->probe_page(page));
      }
    }
    const auto is_scan = [](const pdf::PageProbe& probe) {
      return probe.only_undecodable_images() || probe.undecodable_coverage >= 0.5;
    };
    scanned_pages = static_cast<int>(std::count_if(probes.begin(), probes.end(), is_scan));
    const bool all_scanned = scanned_pages == static_cast<int>(probes.size());

    PdfImportResult result;
    bool first_page_done = false;
    bool unmodelled = false;
    bool stopped = false;
    int position = 0;
    // Every page a scan: the flatten path below does them all, with read-ahead.
    for (std::size_t index = 0; !all_scanned && index < editable_pages.size(); ++index) {
      const int page = editable_pages[index];
      ++position;
      if (!options.progress(position, static_cast<int>(editable_pages.size()))) {
        stopped = true;
        break;
      }
      pdf::VectorReadOptions vector_options;
      vector_options.page = page;
      vector_options.pixels_per_point = options.resolution_ppi / 72.0;
      const auto title = QObject::tr("Page %1").arg(page + 1);
      std::optional<Document> page_document;
      const std::string profile_detail = "page=" + std::to_string(page + 1);
      const auto flatten_this_page = [&]() -> bool {
        PdfImportOptions page_options = options;
        page_options.pages = {page};
        // This page is already counted; the callback only says "interactive", which
        // sends the render to a worker so the progress dialog stays alive.
        page_options.progress = [](int, int) { return true; };
        const UiProfileScope profile_scope("pdf_import.raster_fallback", profile_detail);
        QString page_error;
        auto rendered = render_pages(pdf, page_options, file_name, &page_error, reader.get());
        if (!rendered.has_value()) {
          return false;
        }
        page_document = std::move(rendered->document);
        return true;
      };
      if (is_scan(probes[index])) {
        if (!flatten_this_page()) {
          break;
        }
      } else {
        try {
          const UiProfileScope profile_scope("pdf_import.vector_page", profile_detail);
          // The Qt-free reader is safe on a worker; pumping here keeps the progress
          // dialog painting and its Cancel button live during a slow page.
          auto future = launch_async([page_reader = reader.get(), vector_options] {
            return page_reader->read_page(vector_options);
          });
          while (future.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready) {
            QApplication::processEvents(QEventLoop::AllEvents, 15);
          }
          auto vectors = future.get();
          page_document = std::move(vectors.document);
          // A page that is one decodable image keeps its original bytes for export too;
          // the composite is only final after the Qt-side image pass, which stamps it.
          page_document->metadata().pdf_source_page = probes[index].source_page;
          unmodelled = unmodelled || vectors.has_unmodelled_content;
          for (auto& notice : vectors.notices) {
            if (std::find(result.notices.begin(), result.notices.end(), notice) == result.notices.end()) {
              result.notices.push_back(std::move(notice));
            }
          }
        } catch (const std::exception& exception) {
          editable_failure = QString::fromUtf8(exception.what());
          if (!options.separate_documents) {
            break;  // the whole import falls back to the raster path below
          }
          // One page the vector reader cannot model does not sink the others: that
          // page flattens (with the notice) and the rest stay editable.
          if (!flatten_this_page()) {
            break;
          }
          result.notices.push_back(QObject::tr("Editable import was not possible for page %1 (%2); it was "
                                               "flattened instead.")
                                       .arg(page + 1)
                                       .arg(editable_failure)
                                       .toStdString());
          editable_failure.clear();
        }
      }
      if (!first_page_done) {
        result.document = std::move(*page_document);
        result.document_title = options.separate_documents ? title : QString();
        first_page_done = true;
      } else {
        result.extra_documents.push_back({std::move(*page_document), title});
      }
    }
    if (first_page_done && editable_failure.isEmpty()) {
      if (scanned_pages > 0) {
        result.notices.push_back(scanned_pages_notice(scanned_pages));
      }
      if (!options.separate_documents && options.pages.size() > 1) {
        result.notices.push_back(QObject::tr("Editable import brings in one page; page %1 was imported.")
                                     .arg(editable_pages.front() + 1)
                                     .toStdString());
      } else if (options.separate_documents && editable_pages.size() > 1) {
        result.notices.push_back(QObject::tr("%1 pages opened as separate documents.")
                                     .arg(1 + result.extra_documents.size())
                                     .toStdString());
      }
      if (stopped) {
        result.notices.push_back(QObject::tr("Import stopped after %1 of %2 pages.")
                                     .arg(1 + result.extra_documents.size())
                                     .arg(editable_pages.size())
                                     .toStdString());
      }
      if (unmodelled) {
        result.notices.push_back(
            QObject::tr("Some artwork could not be kept editable; reimport with \"Flattened image per "
                        "page\" for an exact copy.")
                .toStdString());
      }
      return result;
    }
  }

  QString error;
  auto result = render_pages(pdf, options, file_name, &error, reader.get());
  if (!result.has_value()) {
    QMessageBox box(QMessageBox::Warning, QObject::tr("Open PDF"), error, QMessageBox::Ok, parent);
    box.setObjectName(QStringLiteral("pdfOpenFailedMessageBox"));
    exec_dialog(box);
    return std::nullopt;
  }
  if (!editable_failure.isEmpty()) {
    // The user asked for editable and got a flattened page instead: that swap must
    // never be silent.
    result->notices.insert(result->notices.begin(),
                           QObject::tr("Editable import was not possible (%1); the page was flattened instead.")
                               .arg(editable_failure)
                               .toStdString());
  } else if (scanned_pages > 0) {
    // The user asked for editable and got flattened pages: never silent, and once
    // for the whole file rather than once per page.
    result->notices.insert(result->notices.begin(), scanned_pages_notice(scanned_pages));
  }
  return result;
}

}  // namespace patchy::ui
