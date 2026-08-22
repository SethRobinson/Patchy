#include "ui/pdf_export.hpp"

#include "ui/image_document_io.hpp"
#include "ui/print_internal.hpp"

#include <QImage>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRect>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace patchy::ui {
namespace {

constexpr double kPointsPerInch = 72.0;
// PDF 1.7 (ISO 32000-1, Annex C) caps a page at 14400 units, i.e. 200 inches. A very
// large document at a low PPI would exceed that, so the page shrinks to the cap and the
// image simply prints smaller; the pixels are untouched either way.
constexpr double kMaxPagePoints = 14400.0;

}  // namespace

void write_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options) {
  const QImage image = flat_export_qimage(document, true);
  if (image.isNull()) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }

  const double horizontal_ppi = print_detail::document_horizontal_ppi(document);
  const double vertical_ppi = print_detail::document_vertical_ppi(document);
  double page_width_points = image.width() / horizontal_ppi * kPointsPerInch;
  double page_height_points = image.height() / vertical_ppi * kPointsPerInch;
  if (const double longest = std::max(page_width_points, page_height_points); longest > kMaxPagePoints) {
    const double fit = kMaxPagePoints / longest;
    page_width_points *= fit;
    page_height_points *= fit;
  }
  // QPageSize defaults to FuzzyMatch, which would snap a nearly-Letter page to Letter and
  // change the document's physical size. Exact sizes only.
  const QPageSize page_size(QSizeF(std::max(page_width_points, 1.0), std::max(page_height_points, 1.0)),
                            QPageSize::Point, QString(), QPageSize::ExactMatch);

  QPdfWriter writer(path);
  writer.setCreator(QStringLiteral("Patchy"));
  writer.setPageSize(page_size);
  writer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0));
  // The device resolution only sets the painter's logical grid; keeping it at the
  // document's own PPI makes that grid one unit per document pixel.
  writer.setResolution(std::clamp(static_cast<int>(std::lround(horizontal_ppi)), 72, 2400));

  QPainter painter;
  if (!painter.begin(&writer)) {
    throw std::runtime_error("The PDF file could not be opened for writing.");
  }
  // LosslessImageRendering is what stops QPdfEnginePrivate::addImage from re-encoding the
  // composite as JPEG quality 94; without it every PDF export would be lossy.
  painter.setRenderHint(QPainter::LosslessImageRendering, options.lossless);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(painter.viewport(), image);
  painter.end();
}

}  // namespace patchy::ui
