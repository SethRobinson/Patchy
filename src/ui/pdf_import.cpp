#include "ui/pdf_import.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_sequence_dialog.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
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
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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
    case QPdfDocument::Error::None:
      break;
  }
  return QObject::tr("%1 could not be opened.").arg(file_name);
}

// Opens the document, asking for a password (up to three tries) when it is encrypted.
// A null parent means the non-interactive path: a wrong or missing password just fails.
QPdfDocument::Error open_pdf(QPdfDocument& document, const QString& path, const QString& password, QWidget* parent) {
  if (!password.isEmpty()) {
    document.setPassword(password);
  }
  auto error = document.load(path);
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
  }
  return error;
}

std::optional<PdfImportResult> render_pages(QPdfDocument& pdf, const PdfImportOptions& options,
                                            const QString& file_name, QString* error) {
  std::vector<int> pages = options.pages;
  if (pages.empty()) {
    pages.push_back(0);
  }
  const int resolution_ppi = std::clamp(options.resolution_ppi, kMinResolutionPpi, kMaxResolutionPpi);
  const auto render = render_options(options);

  std::vector<QImage> frames;
  QStringList layer_names;
  frames.reserve(pages.size());
  layer_names.reserve(static_cast<qsizetype>(pages.size()));
  bool clamped = false;
  for (const int page : pages) {
    if (page < 0 || page >= pdf.pageCount()) {
      continue;
    }
    const QSize size = rendered_page_size(pdf.pagePointSize(page), resolution_ppi, &clamped);
    if (!size.isValid()) {
      continue;
    }
    QImage image = pdf.render(page, size, render);
    if (image.isNull()) {
      if (error != nullptr) {
        *error = QObject::tr("Page %1 of %2 could not be rendered.").arg(page + 1).arg(file_name);
      }
      return std::nullopt;
    }
    if (options.trim_to_bounding_box) {
      image = trimmed_to_content(image);
    }
    frames.push_back(std::move(image));
    layer_names.push_back(QObject::tr("Page %1").arg(page + 1));
  }
  if (frames.empty()) {
    if (error != nullptr) {
      *error = QObject::tr("%1 has no pages Patchy could render.").arg(file_name);
    }
    return std::nullopt;
  }

  const int page_count = static_cast<int>(frames.size());
  auto document = document_from_frames(std::move(frames), layer_names);
  if (!document.has_value()) {
    if (error != nullptr) {
      *error = QObject::tr("%1 could not be turned into a document.").arg(file_name);
    }
    return std::nullopt;
  }
  document->print_settings().horizontal_ppi = resolution_ppi;
  document->print_settings().vertical_ppi = resolution_ppi;

  PdfImportResult result{std::move(*document), {}};
  result.notices.push_back(QObject::tr("PDF content was rasterized at %1 ppi; text and vectors are pixels now.")
                               .arg(resolution_ppi)
                               .toStdString());
  if (page_count > 1) {
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
  return render_pages(pdf, options, file_name, error);
}

std::optional<PdfImportResult> run_pdf_import_dialog(QWidget* parent, const QString& path) {
  const auto file_name = QFileInfo(path).fileName();
  QPdfDocument pdf;
  if (const auto status = open_pdf(pdf, path, QString(), parent); status != QPdfDocument::Error::None) {
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
  // this is a page picker, not a preview of output quality.
  for (int page = 0; page < pdf.pageCount(); ++page) {
    auto* item = new QListWidgetItem(QObject::tr("Page %1").arg(page + 1), pages_list);
    const QSizeF page_points = pdf.pagePointSize(page);
    if (page_points.height() > 0.0) {
      const int width =
          std::max(1, static_cast<int>(std::lround(kThumbnailHeight * page_points.width() / page_points.height())));
      const QImage thumbnail = pdf.render(page, QSize(width, kThumbnailHeight));
      if (!thumbnail.isNull()) {
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
      }
    }
  }
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
    size_label->setText(QObject::tr("%1 page(s), first page %2 x %3 px")
                            .arg(pages.size())
                            .arg(size.width())
                            .arg(size.height()));
  };
  QObject::connect(pages_list, &QListWidget::itemSelectionChanged, &dialog, sync_size_label);
  QObject::connect(resolution, &QSpinBox::valueChanged, &dialog, sync_size_label);
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
  settings.setValue(settings_key("Resolution"), options.resolution_ppi);
  settings.setValue(settings_key("Annotations"), options.annotations);
  settings.setValue(settings_key("AntiAlias"), options.anti_alias);
  settings.setValue(settings_key("TrimToContent"), options.trim_to_bounding_box);

  QString error;
  auto result = render_pages(pdf, options, file_name, &error);
  if (!result.has_value()) {
    QMessageBox box(QMessageBox::Warning, QObject::tr("Open PDF"), error, QMessageBox::Ok, parent);
    box.setObjectName(QStringLiteral("pdfOpenFailedMessageBox"));
    exec_dialog(box);
    return std::nullopt;
  }
  return result;
}

}  // namespace patchy::ui
