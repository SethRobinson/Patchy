#include "import_print_resolution_test_support.hpp"

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "test_harness.hpp"

#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QPainter>
#include <QString>
#include <QTimer>
#include <Qt>

#include <cmath>
#include <utility>

namespace patchy::test::ui::import_print_resolution_support {

#if defined(PATCHY_HAVE_QT_PDF)
QByteArray read_file_bytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
QImage over_white(const QImage& image) {
  QImage result(image.size(), QImage::Format_RGB32);
  result.fill(Qt::white);
  QPainter painter(&result);
  painter.drawImage(0, 0, image);
  painter.end();
  return result;
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
double mean_rgb_delta_over_white(const QImage& a, const QImage& b) {
  const QImage flat_a = over_white(a);
  const QImage flat_b = over_white(b);
  CHECK(flat_a.size() == flat_b.size());
  double total = 0.0;
  for (int y = 0; y < flat_a.height(); ++y) {
    for (int x = 0; x < flat_a.width(); ++x) {
      const auto ca = flat_a.pixelColor(x, y);
      const auto cb = flat_b.pixelColor(x, y);
      total += std::abs(ca.red() - cb.red()) + std::abs(ca.green() - cb.green()) + std::abs(ca.blue() - cb.blue());
    }
  }
  return total / (static_cast<double>(flat_a.width()) * flat_a.height() * 3.0);
}
#endif

#if defined(PATCHY_HAVE_QT_PDF)
patchy::Layer solid_rect_shape_layer(patchy::Document& document, const char* name, int left, int top, int right,
                                     int bottom, patchy::RgbColor fill, double stroke_width) {
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = left;
  params.top = top;
  params.right = right;
  params.bottom = bottom;
  params.index = 0;
  patchy::populate_live_shape_box_corners(params);
  patchy::VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination = {params};
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = fill;
  if (stroke_width > 0.0) {
    content.stroke.enabled = true;
    content.stroke.width = stroke_width;
    content.stroke.content.kind = patchy::VectorFillKind::Solid;
    content.stroke.content.color = {20, 20, 20};
  }
  patchy::Layer layer(document.allocate_layer_id(), name, patchy::LayerKind::Pixel);
  layer.metadata()[patchy::kLayerMetadataVectorShape] = std::string("1");
  patchy::mark_layer_vector_block_dirty(layer);
  layer.set_vector_shape(std::move(content));
  patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                     &document.metadata().patterns);
  return layer;
}

void drive_modal_dialog(const std::shared_ptr<std::function<bool()>>& step, int attempts) {
  QTimer::singleShot(25, [step, attempts] {
    if (step == nullptr || !static_cast<bool>(*step)) {
      return;
    }
    if ((*step)()) {
      return;
    }
    if (attempts > 0) {
      drive_modal_dialog(step, attempts - 1);
    }
  });
}

QByteArray pdf_from_objects(const std::vector<std::string>& objects) {
  std::string pdf = "%PDF-1.7\n";
  std::vector<std::size_t> offsets;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const std::size_t xref_offset = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toStdString();
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) + "/Root 1 0 R>>\nstartxref\n" +
         std::to_string(xref_offset) + "\n%%EOF\n";
  return QByteArray::fromStdString(pdf);
}

#endif

}  // namespace patchy::test::ui::import_print_resolution_support
