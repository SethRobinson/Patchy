// Free Transform Skew, Distort and Perspective (GitHub issue 225): Ctrl+drag a
// corner distorts, Edit > Transform picks a mode in which plain drags skew,
// distort or apply perspective, pixel layers commit through the quad's
// homography, type layers refuse a non-affine quad, and session Undo steps back
// over a distortion. docs/free-transform.md.
#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/smart_object_render.hpp"
#include "local_psd_fixtures.hpp"
#include "scripting_test_support.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDoubleSpinBox>
#include <QImage>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRegion>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolButton>
#include <optional>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::scripting_support::run_script;

const QColor kBoxColor(60, 130, 230);

// A position-locked white Background plus one opaque layer (blue unless `color`)
// at `bounds`, at 100% with the Move tool and snapping off.
void prepare_distort_scene(patchy::ui::MainWindow& window, QRect bounds, QColor color = kBoxColor) {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Box",
                      solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(patchy::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()});
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Distort"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
}

QPoint widget_at(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return canvas.widget_position_for_document_point(QPoint(x, y));
}

bool is_box(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return color_close(canvas_pixel(canvas, QPoint(x, y)), kBoxColor, 24);
}

bool is_white(patchy::ui::CanvasWidget& canvas, int x, int y) {
  return color_close(canvas_pixel(canvas, QPoint(x, y)), QColor(Qt::white), 24);
}

// Bounds of a resampled quad: the homography's floating-point corners can put
// an edge on either side of a whole pixel.
bool rect_near(const std::optional<QRect>& actual, QRect expected) {
  return actual.has_value() && std::abs(actual->left() - expected.left()) <= 1 &&
         std::abs(actual->top() - expected.top()) <= 1 && std::abs(actual->right() - expected.right()) <= 1 &&
         std::abs(actual->bottom() - expected.bottom()) <= 1;
}

QImage canvas_crop(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  QApplication::processEvents();
  return canvas.grab().toImage().copy(
      QRect(canvas.widget_position_for_document_point(document_rect.topLeft()), document_rect.size()));
}

// +1 or -1 for a strictly convex quad, 0 for a concave or self-crossing one.
int quad_winding(const std::array<QPointF, 4>& quad) {
  int winding = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto a = quad[i];
    const auto b = quad[(i + 1) % 4];
    const auto c = quad[(i + 2) % 4];
    const auto cross = (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
    const int sign = cross > 1e-6 ? 1 : (cross < -1e-6 ? -1 : 0);
    if (sign == 0 || (winding != 0 && sign != winding)) {
      return 0;
    }
    winding = sign;
  }
  return winding;
}

QRect quad_bounds(const std::array<QPointF, 4>& quad) {
  QPolygonF polygon;
  for (const auto& corner : quad) {
    polygon << corner;
  }
  return polygon.boundingRect().toAlignedRect();
}

bool point_near(QPointF actual, QPointF expected) {
  return std::abs(actual.x() - expected.x()) <= 0.51 && std::abs(actual.y() - expected.y()) <= 0.51;
}

// Pixels of the layer's `color` outside `keep` (sampled every other pixel,
// skipping the document's 2 px rim, where the canvas frame is drawn): smeared
// layer pixels. The color must differ from the session chrome's blue.
int painted_outside(patchy::ui::CanvasWidget& canvas, QRect keep, QColor color) {
  const QRect document_rect(0, 0, 400, 300);
  const auto image = canvas_crop(canvas, document_rect);
  int painted = 0;
  for (int y = 2; y < document_rect.height() - 2; y += 2) {
    for (int x = 2; x < document_rect.width() - 2; x += 2) {
      if (!keep.contains(x, y) && color_close(image.pixelColor(x, y), color, 40)) {
        if (painted == 0) {
          std::printf("painted outside %d,%d %d x %d at %d,%d\n", keep.x(), keep.y(), keep.width(), keep.height(), x,
                      y);
        }
        ++painted;
      }
    }
  }
  return painted;
}

void double_click_at(patchy::ui::CanvasWidget& canvas, QPoint widget_point) {
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
  send_mouse(canvas, QEvent::MouseButtonDblClick, widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// Ctrl+drag a corner moves that corner alone; the commit resamples the pixels
// onto the new quad.
void ui_free_transform_ctrl_drag_corner_distorts() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  const auto view = QRect(40, 30, 320, 220);
  QList<QImage> strips{canvas_crop(*canvas, view)};
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_distort_corner", strips);

  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 140)));
  // Inside the new quad near the moved corner, and below the old bottom edge.
  CHECK(is_box(*canvas, 295, 205));
  CHECK(is_box(*canvas, 265, 185));
  // The bottom edge now runs from (100, 180) to (300, 210): below it near the
  // left stays white, which a plain corner scale would have filled.
  CHECK(is_white(*canvas, 120, 195));
  // The other three corners stayed put.
  CHECK(is_box(*canvas, 102, 72));
  CHECK(is_box(*canvas, 257, 72));
  CHECK(is_box(*canvas, 102, 177));
}

// Edit > Transform > Perspective: dragging a corner sideways moves the corner
// across the same side the opposite way (a symmetric trapezoid).
void ui_free_transform_perspective_mode_mirrors_the_partner_corner() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editTransformPerspectiveAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 260, 70), widget_at(*canvas, 300, 70));
  const auto view = QRect(40, 30, 320, 220);
  QList<QImage> strips{canvas_crop(*canvas, view)};
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_perspective_mode", strips);

  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(60, 70, 240, 110)));
  CHECK(is_box(*canvas, 65, 72));
  CHECK(is_box(*canvas, 295, 72));
  // The bottom keeps its width: the slanted sides leave the lower outer area white.
  CHECK(is_white(*canvas, 70, 175));
  CHECK(is_white(*canvas, 290, 175));
  CHECK(is_box(*canvas, 102, 177));
  CHECK(is_box(*canvas, 257, 177));
}

// Edit > Transform > Skew: an edge slides along itself (the vertical part of the
// drag is dropped), turning the box into a parallelogram.
void ui_free_transform_skew_mode_slides_an_edge() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editTransformSkewAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 180, 70), widget_at(*canvas, 210, 74));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 190, 110)));
  CHECK(is_box(*canvas, 285, 72));
  CHECK(is_white(*canvas, 105, 72));
  CHECK(is_box(*canvas, 103, 177));
  CHECK(is_white(*canvas, 285, 177));
}

// A type layer takes a skew (affine) but refuses a corner distortion: the drag
// does nothing and the status bar says to rasterize first.
void ui_free_transform_distort_refuses_type_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, widget_at(*canvas, 200, 150), widget_at(*canvas, 520, 400));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  editor->setPlainText(QStringLiteral("Distort me"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(120);
  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  if (!before.has_value()) {
    return;
  }

  require_action(window, "editTransformDistortAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto corner = before->topLeft() + QPoint(before->width(), before->height());
  drag(*canvas, canvas->widget_position_for_document_point(corner),
       canvas->widget_position_for_document_point(corner + QPoint(40, 30)));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Rasterize")));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == before);
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
}

// Session Undo takes back a distortion like any other drag.
void ui_free_transform_distort_is_a_session_step() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == bounds);
  CHECK(is_white(*canvas, 280, 200));
}

// A shape layer distorts through its path: the moved corner's anchor lands on
// the dragged point, the live rectangle annotation drops, and the re-rendered
// ink follows the quad.
void ui_free_transform_distort_shape_layer_maps_path() {
  patchy::ui::MainWindow window;
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Shape", patchy::PixelBuffer());
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = 100;
  params.top = 70;
  params.right = 260;
  params.bottom = 180;
  patchy::populate_live_shape_box_corners(params);
  patchy::VectorShapeContent content;
  content.path.subpaths = patchy::generate_live_shape_subpaths(params);
  content.origination.push_back(params);
  content.fill.kind = patchy::VectorFillKind::Solid;
  content.fill.color = patchy::RgbColor{60, 130, 230};
  layer.set_vector_shape(std::move(content));
  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(400, 300), nullptr);
  const auto id = layer.id();
  document.add_layer(std::move(layer));
  window.add_document_session(std::move(document), QStringLiteral("Shape"));
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_zoom(1.0);
  canvas->center_document_in_view();
  canvas->set_snap_enabled(false);
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  const auto* shape_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(id);
  CHECK(shape_layer != nullptr);
  const auto* shape = shape_layer != nullptr ? shape_layer->vector_shape() : nullptr;
  CHECK(shape != nullptr);
  if (shape == nullptr) {
    return;
  }
  CHECK(shape->origination.empty());
  bool moved_corner = false;
  bool kept_corner = false;
  for (const auto& subpath : shape->path.subpaths) {
    for (const auto& anchor : subpath.anchors) {
      moved_corner = moved_corner || (std::abs(anchor.anchor_x - 300.0) < 0.6 && std::abs(anchor.anchor_y - 210.0) < 0.6);
      kept_corner = kept_corner || (std::abs(anchor.anchor_x - 100.0) < 0.6 && std::abs(anchor.anchor_y - 70.0) < 0.6);
    }
  }
  CHECK(moved_corner);
  CHECK(kept_corner);
  CHECK(is_box(*canvas, 295, 205));
  CHECK(is_white(*canvas, 120, 195));
}

// A smart object distorts as a perspective placement: nonAffineTransform holds the
// corners, Trnf their bounding box (Photoshop's rule), the content re-renders onto
// the quad, and both quads survive a PSD round trip. The written file is the
// Photoshop acceptance artifact (scripts\dev\photoshop-open-check.ps1).
void ui_free_transform_distort_smart_object_writes_perspective_placement() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "layerConvertSmartObjectAction")->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto id = document.active_layer_id();
  CHECK(id.has_value());
  const auto* converted = id.has_value() ? document.find_layer(*id) : nullptr;
  CHECK(converted != nullptr && patchy::layer_is_smart_object(*converted));

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  save_image_artifact("ui_free_transform_distort_smart_object", canvas_crop(*canvas, QRect(40, 30, 320, 220)));

  const auto* layer = document.find_layer(*id);
  CHECK(layer != nullptr);
  const auto placement = patchy::smart_object_placement_from_layer(*layer);
  CHECK(placement.has_value() && placement->non_affine_transform.has_value());
  if (!placement.has_value() || !placement->non_affine_transform.has_value()) {
    return;
  }
  const std::array<double, 8> expected_quad{100.0, 70.0, 260.0, 70.0, 300.0, 210.0, 100.0, 180.0};
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs((*placement->non_affine_transform)[i] - expected_quad[i]) < 0.01);
  }
  const std::array<double, 8> expected_bounds{100.0, 70.0, 300.0, 70.0, 300.0, 210.0, 100.0, 210.0};
  for (std::size_t i = 0; i < 8U; ++i) {
    CHECK(std::abs(placement->transform[i] - expected_bounds[i]) < 0.01);
  }
  CHECK(patchy::smart_object_lock_reason(*layer).empty());
  CHECK(is_box(*canvas, 295, 205));
  CHECK(is_white(*canvas, 120, 195));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  ensure_artifact_dir();
  {
    std::ofstream out("test-artifacts/free_transform_perspective_smart_object.psd", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto reread = patchy::psd::DocumentIo::read(bytes);
  const patchy::Layer* reread_layer = nullptr;
  for (const auto& candidate : reread.layers()) {
    if (patchy::layer_is_smart_object(candidate)) {
      reread_layer = &candidate;
    }
  }
  CHECK(reread_layer != nullptr);
  if (reread_layer == nullptr) {
    return;
  }
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value() && reread_placement->non_affine_transform.has_value());
  if (reread_placement.has_value() && reread_placement->non_affine_transform.has_value()) {
    for (std::size_t i = 0; i < 8U; ++i) {
      CHECK(std::abs((*reread_placement->non_affine_transform)[i] - expected_quad[i]) < 0.01);
      CHECK(std::abs(reread_placement->transform[i] - expected_bounds[i]) < 0.01);
    }
  }
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
}

// Photoshop-authored perspective smart objects import editable. Their first
// Patchy render must match Photoshop's stored raster closely, and an
// open-and-save without edits keeps that raster byte for byte.
void ui_pinball_perspective_smart_objects_render_like_photoshop_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_retronight_poster_a3.psd");
  patchy::test::skip_unless_exists(path);
  const auto document = patchy::psd::DocumentIo::read_file(path);
  std::vector<const patchy::Layer*> perspective;
  const auto collect = [&perspective](auto&& self, const std::vector<patchy::Layer>& layers) -> void {
    for (const auto& layer : layers) {
      if (layer.kind() == patchy::LayerKind::Group) {
        self(self, layer.children());
        continue;
      }
      if (const auto placement = patchy::smart_object_placement_from_layer(layer);
          placement.has_value() && placement->non_affine_transform.has_value()) {
        perspective.push_back(&layer);
      }
    }
  };
  collect(collect, document.layers());
  CHECK(perspective.size() >= 2U);

  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  for (const auto* layer : perspective) {
    CHECK(patchy::smart_object_lock_reason(*layer).empty());
    const auto rendered =
        patchy::ui::render_smart_object_unfiltered_layer_preview(document, *layer,
                                                                  patchy::ui::CanvasWidget::TransformInterpolation::Bicubic);
    CHECK(rendered.has_value());
    if (!rendered.has_value()) {
      continue;
    }
    // Compare what a viewer sees: both rasters placed on the canvas and reduced 4x
    // (thin antialiased grid lines make a per-pixel test measure edge antialiasing,
    // not the picture). Geometry first: the render must cover where Photoshop's does.
    const auto stored_bounds = layer->bounds();
    const auto render_bounds = rendered->bounds;
    const QRect canvas(0, 0, document.width(), document.height());
    const auto stored_rect =
        QRect(stored_bounds.x, stored_bounds.y, stored_bounds.width, stored_bounds.height).intersected(canvas);
    const auto render_rect =
        QRect(render_bounds.x, render_bounds.y, render_bounds.width, render_bounds.height).intersected(canvas);
    CHECK(!stored_rect.isEmpty());
    CHECK(render_rect.contains(stored_rect.adjusted(2, 2, -2, -2)));
    const auto on_canvas = [&](const QImage& source, patchy::Rect bounds) {
      QImage canvas_image(document.width(), document.height(), QImage::Format_RGBA8888_Premultiplied);
      canvas_image.fill(Qt::transparent);
      QPainter painter(&canvas_image);
      painter.drawImage(QPoint(bounds.x, bounds.y), source);
      painter.end();
      return canvas_image.copy(stored_rect).scaled(stored_rect.size() / 4, Qt::IgnoreAspectRatio,
                                                    Qt::SmoothTransformation);
    };
    const auto photoshop = on_canvas(patchy::ui::qimage_from_pixel_buffer(layer->pixels()), stored_bounds);
    const auto patchy_render = on_canvas(patchy::ui::qimage_from_pixel_buffer(rendered->pixels), render_bounds);
    save_image_artifact("pinball_perspective_photoshop_" + std::to_string(layer->id()), photoshop);
    save_image_artifact("pinball_perspective_patchy_" + std::to_string(layer->id()), patchy_render);
    double total = 0.0;
    int worst = 0;
    std::int64_t samples = 0;
    // Premultiplied bytes, so fully transparent pixels compare equal whatever color
    // they would have had.
    for (int y = 0; y < photoshop.height(); ++y) {
      const auto* a = photoshop.constScanLine(y);
      const auto* b = patchy_render.constScanLine(y);
      for (int x = 0; x < photoshop.width() * 4; x += 4) {
        int delta = 0;
        for (int channel = 0; channel < 4; ++channel) {
          delta = std::max(delta, std::abs(static_cast<int>(a[x + channel]) - static_cast<int>(b[x + channel])));
        }
        total += delta;
        worst = std::max(worst, delta);
        ++samples;
      }
    }
    CHECK(samples > 0);
    const double mean = samples > 0 ? total / static_cast<double>(samples) : 255.0;
    std::printf("perspective SO '%s' (%d): 4x-reduced mean delta %.2f, worst %d over %lld pixels\n",
                layer->name().c_str(), static_cast<int>(layer->id()), mean, worst, static_cast<long long>(samples));
    CHECK(mean < 4.0);
    CHECK(worst < 128);

    // Open and save without edits keeps Photoshop's raster.
    const patchy::Layer* saved = nullptr;
    const auto find = [&saved, id = layer->id()](auto&& self, const std::vector<patchy::Layer>& layers) -> void {
      for (const auto& candidate : layers) {
        if (candidate.id() == id) {
          saved = &candidate;
        }
        self(self, candidate.children());
      }
    };
    find(find, reread.layers());
    CHECK(saved != nullptr);
    if (saved != nullptr) {
      CHECK(saved->bounds() == layer->bounds());
      CHECK(saved->pixels().byte_size() == layer->pixels().byte_size());
      CHECK(std::equal(saved->pixels().data().begin(), saved->pixels().data().end(), layer->pixels().data().begin()));
    }
  }
}

// layer.transform: a quad distorts like Ctrl+drag (the same commit), a matrix maps
// the box, and the whole script is one undo step.
void ui_script_layer_transform_quad_and_matrix() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.activeLayer.transform({ quad: [[100, 70], [260, 70], [300, 210], [100, 180]] });
  )JS")));
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 140)));
  CHECK(is_box(*canvas, 295, 205));
  CHECK(is_white(*canvas, 120, 195));
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == bounds);

  // A matrix: scale by 0.5 about the origin, then move by (20, 10), with nearest neighbor.
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.activeLayer.transform({ matrix: [0.5, 0, 0, 0.5, 20, 10], interpolation: "nearest" });
  )JS")));
  QApplication::processEvents();
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(70, 45, 80, 55)));
  CHECK(is_box(*canvas, 100, 70));
  CHECK(is_white(*canvas, 200, 150));
}

// Refusals throw: both keys, neither, a malformed quad, an unknown interpolation, a
// type layer under a non-parallelogram quad, and a pending interactive transform.
void ui_script_layer_transform_refusals() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.transform({});")));
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.activeLayer.transform({ matrix: [1, 0, 0, 1, 0, 0], quad: [[0, 0], [1, 0], [1, 1], [0, 1]] });")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.transform({ quad: [[0, 0], [1, 0]] });")));
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.activeLayer.transform({ matrix: [1, 0, 0, 1, 0, 0], interpolation: \"smooth\" });")));
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.activeLayer.transform({ matrix: [1, 0, 0, 1, 0, 0], extra: 1 });")));
  CHECK(canvas->active_layer_document_rect() == bounds);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.transform({ matrix: [1, 0, 0, 1, 5, 5] });")));
  CHECK(canvas->free_transform_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Type: a skew (parallelogram) works, perspective throws and leaves the layer alone.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var text = doc.addTextLayer("Skew me", { size: 40, x: 60, y: 200 });
    var b = text.bounds;
    text.transform({ matrix: [1, 0, 0.25, 1, 0, 0] });
  )JS")));
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* text_layer = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
  CHECK(text_layer != nullptr && patchy::layer_is_text(*text_layer));
  const auto text_bounds = text_layer != nullptr ? std::optional<patchy::Rect>(text_layer->bounds()) : std::nullopt;
  CHECK(!run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.activeLayer;
    var b = layer.bounds;
    layer.transform({ quad: [[b.x, b.y], [b.x + b.width + 40, b.y], [b.x + b.width, b.y + b.height],
                             [b.x, b.y + b.height]] });
  )JS")));
  const auto* text_after = document.active_layer_id().has_value() ? document.find_layer(*document.active_layer_id()) : nullptr;
  CHECK(text_after != nullptr && text_bounds.has_value() && text_after->bounds() == *text_bounds);
}

// GitHub issue 225 (Seth: "skew a corner around with vigor"): a corner dragged
// across the box would fold the quad, and its homography threw pixels far
// outside the box and the repaint, leaving a smear until the commit. Every frame
// of a wild Ctrl+drag through and past the opposite corner keeps a convex quad,
// repaints the previous frame's box, and paints nothing outside the box.
void ui_free_transform_wild_corner_drag_stays_convex_and_clean() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  // Red, so the session's blue handles and rotate stem never count as smear.
  const QColor box(220, 40, 40);
  prepare_distort_scene(window, bounds, box);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // Antialiased quad edges.
  constexpr int kChromeMargin = 2;
  const std::vector<QPoint> path = {{260, 180}, {200, 120}, {110, 80}, {60, 40},  {40, 150},
                                    {150, 250}, {330, 60},  {120, 175}, {300, 220}};
  PaintRegionRecorder recorder(canvas);
  canvas->installEventFilter(&recorder);
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_at(*canvas, path.front().x(), path.front().y()),
             Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  QApplication::processEvents();
  int folded_frames = 0;
  int unrepainted_frames = 0;
  int smeared_frames = 0;
  QList<QImage> strips;
  const auto view = QRect(20, 20, 360, 260);
  for (std::size_t segment = 1; segment < path.size(); ++segment) {
    for (int step = 1; step <= 6; ++step) {
      const auto from = path[segment - 1];
      const auto to = path[segment];
      const QPoint point(from.x() + (to.x() - from.x()) * step / 6, from.y() + (to.y() - from.y()) * step / 6);
      const auto before = canvas->free_transform_quad();
      recorder.reset();
      send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, point.x(), point.y()), Qt::NoButton, Qt::LeftButton,
                 Qt::ControlModifier);
      QApplication::processEvents();
      const auto after = canvas->free_transform_quad();
      if (quad_winding(after) == 0) {
        ++folded_frames;
      }
      if (before != after) {
        const auto old_box = quad_bounds(before).adjusted(1, 1, -1, -1);
        const QRect old_widget(canvas->widget_position_for_document_point(old_box.topLeft()),
                               canvas->widget_position_for_document_point(old_box.bottomRight()));
        if (!QRegion(old_widget).subtracted(recorder.region()).isEmpty()) {
          ++unrepainted_frames;
        }
      }
      if (painted_outside(*canvas, quad_bounds(after).adjusted(-kChromeMargin, -kChromeMargin, kChromeMargin,
                                                               kChromeMargin),
                          box) != 0) {
        ++smeared_frames;
      }
      if (step == 6 && (segment == 3 || segment == 6)) {
        strips.push_back(canvas_crop(*canvas, view));
      }
    }
  }
  canvas->removeEventFilter(&recorder);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_at(*canvas, path.back().x(), path.back().y()),
             Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  std::printf("wild corner drag: %d folded, %d unrepainted, %d smeared frames\n", folded_frames, unrepainted_frames,
              smeared_frames);
  CHECK(folded_frames == 0);
  CHECK(unrepainted_frames == 0);
  CHECK(smeared_frames == 0);

  // The last frame is a valid quad again: the corner follows the pointer.
  const auto final_quad = canvas->free_transform_quad();
  CHECK(point_near(final_quad[0], QPointF(100, 70)));
  CHECK(point_near(final_quad[1], QPointF(260, 70)));
  CHECK(point_near(final_quad[2], QPointF(300, 220)));
  CHECK(point_near(final_quad[3], QPointF(100, 180)));
  strips.push_back(canvas_crop(*canvas, view));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_wild_corner_drag", strips);
  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 150)));
  CHECK(painted_outside(*canvas, QRect(100, 70, 200, 150).adjusted(-2, -2, 2, 2), box) == 0);
}

// Holding the skew, distort or perspective modifiers over a handle shows that
// gesture's cursor before the press (GitHub issue 225), and releasing them
// brings the resize cursor back.
void ui_free_transform_modifier_keys_show_gesture_cursor() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto cursor_image = [&] { return canvas->cursor().pixmap().toImage(); };

  // Bottom-right corner: Ctrl distorts, Ctrl+Alt+Shift applies perspective.
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 260, 180), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_key_press(*canvas, Qt::Key_Control);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  const auto distort = cursor_image();
  send_key_press(*canvas, Qt::Key_Alt, Qt::ControlModifier);
  send_key_press(*canvas, Qt::Key_Shift, Qt::ControlModifier | Qt::AltModifier);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  const auto perspective = cursor_image();
  CHECK(!distort.isNull() && !perspective.isNull() && distort != perspective);
  send_key_release(*canvas, Qt::Key_Shift, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
  send_key_release(*canvas, Qt::Key_Alt, Qt::ControlModifier | Qt::AltModifier);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  // Right side: Ctrl moves the side (distort), Ctrl+Shift slides it (skew).
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 260, 125), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  send_key_press(*canvas, Qt::Key_Control);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(cursor_image() == distort);
  send_key_press(*canvas, Qt::Key_Shift, Qt::ControlModifier);
  QApplication::processEvents();
  const auto skew = cursor_image();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(!skew.isNull() && skew != distort && skew != perspective);
  // A hover with the keys still held keeps the hint.
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 260, 126), Qt::NoButton, Qt::NoButton,
             Qt::ControlModifier | Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(cursor_image() == skew);
  send_key_release(*canvas, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  save_image_artifact("ui_free_transform_gesture_cursors", [&] {
    QImage strip(3 * 40, 40, QImage::Format_ARGB32_Premultiplied);
    strip.fill(QColor(128, 128, 128));
    QPainter painter(&strip);
    int x = 4;
    for (const auto& image : {skew, distort, perspective}) {
      painter.drawImage(x, 4, image);
      x += 40;
    }
    painter.end();
    return strip;
  }());
}

// Ctrl+Alt+drag mirrors the gesture about the center (GitHub issue 225): a side
// moves with the opposite side going the other way, a corner with the
// diagonally opposite corner. A double-click inside the distorted box applies.
void ui_free_transform_ctrl_alt_drag_is_symmetric() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 180, 70), widget_at(*canvas, 210, 70), Qt::ControlModifier | Qt::AltModifier);
  auto quad = canvas->free_transform_quad();
  CHECK(point_near(quad[0], QPointF(130, 70)));
  CHECK(point_near(quad[1], QPointF(290, 70)));
  CHECK(point_near(quad[2], QPointF(230, 180)));
  CHECK(point_near(quad[3], QPointF(70, 180)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 290, 200), Qt::ControlModifier | Qt::AltModifier);
  quad = canvas->free_transform_quad();
  CHECK(point_near(quad[0], QPointF(70, 50)));
  CHECK(point_near(quad[1], QPointF(260, 70)));
  CHECK(point_near(quad[2], QPointF(290, 200)));
  CHECK(point_near(quad[3], QPointF(100, 180)));
  const auto view = QRect(40, 30, 320, 220);
  QList<QImage> strips{canvas_crop(*canvas, view)};

  // Off the box a double-click keeps the session; inside it applies.
  double_click_at(*canvas, widget_at(*canvas, 350, 260));
  CHECK(canvas->free_transform_active());
  double_click_at(*canvas, widget_at(*canvas, 180, 125));
  CHECK(!canvas->free_transform_active());
  strips.push_back(canvas_crop(*canvas, view));
  save_stacked_artifact("ui_free_transform_ctrl_alt_symmetric", strips);
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(70, 50, 220, 150)));
  CHECK(is_box(*canvas, 75, 55));
  CHECK(is_box(*canvas, 285, 195));
  CHECK(is_white(*canvas, 280, 75));
}

// layer.transform refuses a quad whose corners cross or fold (it has no sensible
// projective map) and leaves the layer alone.
void ui_script_layer_transform_refuses_folded_quad() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.activeLayer.transform({ quad: [[100, 70], [260, 70], [100, 180], [260, 180]] });")));
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.activeLayer.transform({ quad: [[100, 70], [260, 70], [120, 90], [100, 180]] });")));
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == bounds);
}

// Scale, Rotate, Skew, Distort, Perspective and Warp live under Edit > Transform
// with permanent hotkey ids, no default keys, and NoRole (macOS menu merging); Free
// Transform stays on the Edit menu itself with Ctrl+T (GitHub issues 225 and 299).
void ui_free_transform_mode_menu_entries() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("editTransformMenu"));
  CHECK(menu != nullptr);
  CHECK(menu != nullptr && menu->menuAction()->menuRole() == QAction::NoRole);
  auto* edit_menu = menu != nullptr ? qobject_cast<QMenu*>(menu->parent()) : nullptr;
  CHECK(edit_menu != nullptr);
  for (const auto& [object_name, hotkey_id] :
       std::vector<std::pair<const char*, const char*>>{{"editTransformScaleAction", "edit.transform_scale"},
                                                        {"editTransformRotateAction", "edit.transform_rotate"},
                                                        {"editTransformSkewAction", "edit.transform_skew"},
                                                        {"editTransformDistortAction", "edit.transform_distort"},
                                                        {"editTransformPerspectiveAction", "edit.transform_perspective"},
                                                        {"editWarpTransformAction", "edit.warp_transform"}}) {
    auto* action = require_action(window, object_name);
    CHECK(action->menuRole() == QAction::NoRole);
    CHECK(action->shortcut().isEmpty());
    CHECK(require_hotkey_action(window, QLatin1String(hotkey_id)) == action);
    CHECK(menu != nullptr && menu->actions().contains(action));
    CHECK(edit_menu != nullptr && !edit_menu->actions().contains(action));
  }
  auto* free_transform = require_action(window, "editFreeTransformAction");
  CHECK(require_hotkey_action(window, QStringLiteral("edit.free_transform")) == free_transform);
  CHECK(free_transform->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_T));
  CHECK(edit_menu != nullptr && edit_menu->actions().contains(free_transform));
}

// The Move bar's type buttons start a session in that type, and the session bar's
// group switches type, checks the open one (Warp included), and goes back to plain
// Free Transform on a click of the checked one (GitHub issue 299).
void ui_free_transform_type_buttons_switch_modes() {
  using Mode = patchy::ui::CanvasWidget::TransformDistortMode;
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  const auto session_button = [&window](const char* name) {
    auto* button = window.findChild<QPushButton*>(QLatin1String(name));
    CHECK(button != nullptr);
    return button;
  };
  const char* const session_names[] = {"transformSkewModeButton", "transformDistortModeButton",
                                       "transformPerspectiveModeButton", "transformWarpModeButton"};
  // `expected` null: plain Free Transform, nothing checked.
  const auto only_checked = [&](const char* expected) {
    bool ok = true;
    for (const char* name : session_names) {
      auto* button = session_button(name);
      ok = ok && button != nullptr && button->isVisible() &&
           button->isChecked() == (expected != nullptr && QLatin1String(name) == QLatin1String(expected));
    }
    return ok;
  };

  auto* move_skew = window.findChild<QToolButton*>(QStringLiteral("moveTransformSkewButton"));
  CHECK(move_skew != nullptr && move_skew->isVisible());
  for (const char* name : {"moveTransformDistortButton", "moveTransformPerspectiveButton", "moveWarpButton"}) {
    auto* button = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(button != nullptr && button->isVisible() && !button->toolTip().isEmpty());
  }
  if (move_skew == nullptr) {
    return;
  }
  auto* options_content = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(options_content != nullptr);
  if (options_content != nullptr) {
    save_widget_artifact("issue299_move_bar", *options_content);
  }
  if (auto* menu = window.findChild<QMenu*>(QStringLiteral("editTransformMenu")); menu != nullptr) {
    menu->popup(window.mapToGlobal(QPoint(120, 80)));
    process_events_for(80);
    save_widget_artifact("issue299_edit_transform_menu", *menu);
    menu->hide();
    QApplication::processEvents();
  }
  const auto options_height = options_content != nullptr ? options_content->height() : 0;
  move_skew->click();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_distort_mode() == Mode::Skew);
  CHECK(only_checked("transformSkewModeButton"));
  // The session row, type buttons included, keeps one line in the suite's 1180 px
  // window, so starting a transform never grows the bar and moves the canvas.
  auto* cancel = window.findChild<QPushButton*>(QStringLiteral("freeTransformCancelButton"));
  auto* session_skew = session_button("transformSkewModeButton");
  CHECK(cancel != nullptr && session_skew != nullptr &&
        std::abs(session_skew->geometry().center().y() - cancel->geometry().center().y()) <= 3);
  CHECK(options_content != nullptr && options_content->height() == options_height);
  if (options_content != nullptr) {
    save_widget_artifact("issue299_session_bar", *options_content);
  }

  for (const auto& [name, mode] : std::vector<std::pair<const char*, Mode>>{
           {"transformDistortModeButton", Mode::Distort},
           {"transformPerspectiveModeButton", Mode::Perspective}}) {
    if (auto* button = session_button(name); button != nullptr) {
      button->click();
    }
    QApplication::processEvents();
    CHECK(canvas->free_transform_active());
    CHECK(canvas->free_transform_distort_mode() == mode);
    CHECK(only_checked(name));
  }
  // A click on the checked type goes back to plain Free Transform.
  if (auto* button = session_button("transformPerspectiveModeButton"); button != nullptr) {
    button->click();
  }
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_distort_mode() == Mode::None);
  CHECK(only_checked(nullptr));

  if (auto* warp = session_button("transformWarpModeButton"); warp != nullptr) {
    warp->click();
  }
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  CHECK(only_checked("transformWarpModeButton"));
  // From Warp, a type button goes back to Free Transform in that type.
  if (auto* distort = session_button("transformDistortModeButton"); distort != nullptr) {
    distort->click();
  }
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());
  CHECK(canvas->free_transform_active());
  CHECK(canvas->free_transform_distort_mode() == Mode::Distort);
  CHECK(only_checked("transformDistortModeButton"));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), bounds));
}

// Rotate mode turns a corner drag into a rotation about the reference point (the
// box keeps its size); Scale mode drops the rotate reach outside the box, so a drag
// there does nothing, while a side drag still scales (GitHub issue 299).
void ui_free_transform_scale_and_rotate_modes() {
  using Mode = patchy::ui::CanvasWidget::TransformDistortMode;
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);

  require_action(window, "editTransformRotateAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_distort_mode() == Mode::Rotate);
  // The bottom-right corner swings a quarter turn about the center (180, 125).
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 125, 205));
  auto state = canvas->transform_controls_state();
  CHECK(state.has_value() && std::abs(state->rotation_degrees) > 45.0);
  CHECK(state.has_value() && std::abs(state->scale_x_percent - 100.0) < 0.01 &&
        std::abs(state->scale_y_percent - 100.0) < 0.01);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  require_action(window, "editTransformScaleAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_distort_mode() == Mode::Scale);
  // Just outside the top-left corner is rotate reach in plain Free Transform.
  drag(*canvas, widget_at(*canvas, 86, 56), widget_at(*canvas, 60, 140));
  state = canvas->transform_controls_state();
  CHECK(state.has_value() && std::abs(state->rotation_degrees) < 0.01);
  CHECK(state.has_value() && std::abs(state->scale_x_percent - 100.0) < 0.01);
  drag(*canvas, widget_at(*canvas, 260, 125), widget_at(*canvas, 300, 125));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 110)));
}

// GitHub issue 353: the options bar's W and H follow the pending shape, not only
// the box. Distort and Perspective change them (the mean extent of opposite
// sides along the box's axes), Skew keeps them, session Undo restores them, a
// typed W scales the distorted shape to that value, and rotate and nudge keep
// Angle, X and Y current. The bar must always match transform_controls_state.
void ui_free_transform_options_bar_follows_distortion() {
  patchy::ui::MainWindow window;
  window.resize(1400, 900);
  const QRect bounds(100, 70, 160, 110);
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  auto* x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* w_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  auto* h_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleYSpin"));
  auto* angle_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformRotationSpin"));
  auto* link = window.findChild<QPushButton*>(QStringLiteral("freeTransformLinkScaleButton"));
  auto* options = window.findChild<QWidget*>(QStringLiteral("OptionsContent"));
  CHECK(x_spin != nullptr && w_spin != nullptr && h_spin != nullptr && angle_spin != nullptr && link != nullptr &&
        options != nullptr);
  if (x_spin == nullptr || w_spin == nullptr || h_spin == nullptr || angle_spin == nullptr || link == nullptr ||
      options == nullptr) {
    return;
  }
  const auto approx = [](double actual, double expected) { return std::abs(actual - expected) < 0.02; };
  // The fields show what the canvas reports, to the spin boxes' two decimals.
  const auto bar_matches_state = [&] {
    const auto state = canvas->transform_controls_state();
    return state.has_value() && approx(w_spin->value(), state->scale_x_percent) &&
           approx(h_spin->value(), state->scale_y_percent) && approx(angle_spin->value(), state->rotation_degrees) &&
           approx(x_spin->value(), state->reference_position.x());
  };

  // Distort: the top-right corner moves in by 40 and down by 30.
  require_action(window, "editTransformDistortAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  drag(*canvas, widget_at(*canvas, 260, 70), widget_at(*canvas, 220, 100));
  QApplication::processEvents();
  std::printf("distort: W %.2f H %.2f\n", w_spin->value(), h_spin->value());
  {
    QList<QImage> strips{options->grab().toImage().copy(QRect(0, 0, std::min(options->width(), 1000), options->height())),
                         canvas_crop(*canvas, QRect(40, 30, 320, 220))};
    save_stacked_artifact("ui_free_transform_options_bar_follows_distortion", strips);
  }
  CHECK(approx(w_spin->value(), 87.5));       // (120 + 160) / 2 over 160
  CHECK(approx(h_spin->value(), 86.36));      // (110 + 80) / 2 over 110
  CHECK(bar_matches_state());

  // A typed W scales the distorted shape to it (linked H off).
  link->setChecked(false);
  QApplication::processEvents();
  w_spin->setValue(43.75);
  QApplication::processEvents();
  const auto quad = canvas->free_transform_quad();
  const auto mean_width = ((quad[1].x() - quad[0].x()) + (quad[2].x() - quad[3].x())) / 2.0;
  std::printf("typed W: mean width %.3f, W %.2f H %.2f\n", mean_width, w_spin->value(), h_spin->value());
  CHECK(std::abs(mean_width - 70.0) < 0.05);
  CHECK(approx(w_spin->value(), 43.75));
  CHECK(approx(h_spin->value(), 86.36));
  CHECK(bar_matches_state());

  // Session Undo steps back through the typed W and the distortion.
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(approx(w_spin->value(), 87.5));
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(approx(w_spin->value(), 100.0) && approx(h_spin->value(), 100.0));
  CHECK(bar_matches_state());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  // Perspective: the top edge widens to 240, the bottom keeps 160.
  require_action(window, "editTransformPerspectiveAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 70), widget_at(*canvas, 300, 70));
  QApplication::processEvents();
  std::printf("perspective: W %.2f H %.2f\n", w_spin->value(), h_spin->value());
  CHECK(approx(w_spin->value(), 125.0) && approx(h_spin->value(), 100.0));
  CHECK(bar_matches_state());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Skew: sides slide along themselves; W and H stay.
  require_action(window, "editTransformSkewAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 180, 70), widget_at(*canvas, 210, 74));
  QApplication::processEvents();
  CHECK(approx(w_spin->value(), 100.0) && approx(h_spin->value(), 100.0));
  CHECK(bar_matches_state());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // Ctrl+drag a corner in plain Free Transform distorts too.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 180), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(approx(w_spin->value(), 112.5));  // (160 + 200) / 2 over 160
  CHECK(bar_matches_state());
  // Rotate and nudge keep Angle and X current.
  const auto x_before = x_spin->value();
  send_key(*canvas, Qt::Key_Right);
  QApplication::processEvents();
  CHECK(approx(x_spin->value(), x_before + 1.0));
  CHECK(bar_matches_state());
  angle_spin->setValue(30.0);
  QApplication::processEvents();
  CHECK(approx(w_spin->value(), 112.5));
  CHECK(bar_matches_state());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  require_action(window, "editTransformRotateAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 125, 205));
  QApplication::processEvents();
  CHECK(std::abs(angle_spin->value()) > 45.0);
  CHECK(approx(w_spin->value(), 100.0));
  CHECK(bar_matches_state());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

// The Move tool's box with Transform Controls on (GitHub issue 351): Ctrl on a
// handle takes Free Transform's Ctrl gestures, starting the session the grab opens.
void prepare_move_box_scene(patchy::ui::MainWindow& window, QRect bounds) {
  prepare_distort_scene(window, bounds);
  auto* canvas = require_canvas(window);
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();
}

// The four corners still form the axis-aligned box from `top_left`.
bool quad_is_upright_box_from(const std::array<QPointF, 4>& quad, QPointF top_left) {
  return point_near(quad[0], top_left) && std::abs(quad[1].y() - quad[0].y()) <= 0.51 &&
         std::abs(quad[3].x() - quad[0].x()) <= 0.51 && std::abs(quad[2].x() - quad[1].x()) <= 0.51 &&
         std::abs(quad[2].y() - quad[3].y()) <= 0.51;
}

// Ctrl+drag a Move-box corner moves that corner alone, as one session step; the
// cursor over the corner shows the distort gesture while Ctrl is held, and Ctrl
// inside the box still selects layers instead of opening a session.
void ui_move_box_ctrl_drag_corner_distorts() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_move_box_scene(window, bounds);
  auto* canvas = require_canvas(window);

  drag(*canvas, widget_at(*canvas, 180, 125), widget_at(*canvas, 182, 126), Qt::ControlModifier);
  CHECK(!canvas->free_transform_active());

  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 260, 180), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  send_key_press(*canvas, Qt::Key_Control);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);

  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  CHECK(canvas->free_transform_active());
  auto quad = canvas->free_transform_quad();
  CHECK(point_near(quad[0], QPointF(100, 70)));
  CHECK(point_near(quad[1], QPointF(260, 70)));
  CHECK(point_near(quad[2], QPointF(300, 210)));
  CHECK(point_near(quad[3], QPointF(100, 180)));
  save_image_artifact("ui_move_box_ctrl_distort", canvas_crop(*canvas, QRect(60, 40, 280, 200)));

  // One drag, one step: Undo goes back to the plain box with the session open.
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(quad_is_upright_box_from(canvas->free_transform_quad(), QPointF(100, 70)));
  CHECK(point_near(canvas->free_transform_quad()[2], QPointF(260, 180)));

  drag(*canvas, widget_at(*canvas, 260, 180), widget_at(*canvas, 300, 210), Qt::ControlModifier);
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(rect_near(canvas->active_layer_document_rect(), QRect(100, 70, 200, 140)));
  CHECK(is_box(*canvas, 285, 195));  // clear of the passive box's corner handle
  CHECK(is_white(*canvas, 120, 195));
  CHECK(is_box(*canvas, 257, 72));
}

// Ctrl+drag a Move-box side moves the side (both its corners), Ctrl+Shift slides
// it along itself (a skew), as in a Free Transform session.
void ui_move_box_ctrl_drag_side_distorts_and_skews() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_move_box_scene(window, bounds);
  auto* canvas = require_canvas(window);

  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 260, 125), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  send_key_press(*canvas, Qt::Key_Control);
  QApplication::processEvents();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  QApplication::processEvents();

  drag(*canvas, widget_at(*canvas, 260, 125), widget_at(*canvas, 290, 145), Qt::ControlModifier);
  CHECK(canvas->free_transform_active());
  auto quad = canvas->free_transform_quad();
  CHECK(point_near(quad[0], QPointF(100, 70)));
  CHECK(point_near(quad[1], QPointF(290, 90)));
  CHECK(point_near(quad[2], QPointF(290, 200)));
  CHECK(point_near(quad[3], QPointF(100, 180)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  drag(*canvas, widget_at(*canvas, 260, 125), widget_at(*canvas, 290, 145), Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(canvas->free_transform_active());
  quad = canvas->free_transform_quad();
  CHECK(point_near(quad[1], QPointF(260, 90)));
  CHECK(point_near(quad[2], QPointF(260, 200)));
  CHECK(point_near(quad[0], QPointF(100, 70)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

// Ctrl pressed or released during a Move-box corner drag switches between distort
// and the plain resize from the same press, with no new press (the reporter's
// video), and the whole drag stays one session step.
void ui_move_box_ctrl_toggles_distort_mid_drag() {
  patchy::ui::MainWindow window;
  const QRect bounds(100, 70, 160, 110);
  prepare_move_box_scene(window, bounds);
  auto* canvas = require_canvas(window);

  send_mouse(*canvas, QEvent::MouseButtonPress, widget_at(*canvas, 260, 180), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 300, 210), Qt::NoButton, Qt::LeftButton,
             Qt::ControlModifier);
  CHECK(canvas->free_transform_active());
  auto quad = canvas->free_transform_quad();
  CHECK(point_near(quad[1], QPointF(260, 70)));
  CHECK(point_near(quad[2], QPointF(300, 210)));

  // Ctrl up with the pointer still: the box is a plain resize from the press.
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  quad = canvas->free_transform_quad();
  CHECK(quad_is_upright_box_from(quad, QPointF(100, 70)));
  CHECK(quad[2].x() > 280.0 && quad[2].y() > 190.0);
  send_mouse(*canvas, QEvent::MouseMove, widget_at(*canvas, 320, 220), Qt::NoButton, Qt::LeftButton);
  quad = canvas->free_transform_quad();
  CHECK(quad_is_upright_box_from(quad, QPointF(100, 70)));
  CHECK(quad[2].x() > 300.0);

  // Ctrl down again: back to moving that corner alone.
  send_key_press(*canvas, Qt::Key_Control);
  quad = canvas->free_transform_quad();
  CHECK(point_near(quad[0], QPointF(100, 70)));
  CHECK(point_near(quad[1], QPointF(260, 70)));
  CHECK(point_near(quad[2], QPointF(320, 220)));
  CHECK(point_near(quad[3], QPointF(100, 180)));
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_at(*canvas, 320, 220), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(point_near(canvas->free_transform_quad()[2], QPointF(320, 220)));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  quad = canvas->free_transform_quad();
  CHECK(quad_is_upright_box_from(quad, QPointF(100, 70)));
  CHECK(point_near(quad[2], QPointF(260, 180)));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->active_layer_document_rect() == bounds);
}

}  // namespace

std::vector<patchy::test::TestCase> free_transform_distort_tests() {
  return {
      {"ui_free_transform_ctrl_drag_corner_distorts", ui_free_transform_ctrl_drag_corner_distorts},
      {"ui_free_transform_perspective_mode_mirrors_the_partner_corner",
       ui_free_transform_perspective_mode_mirrors_the_partner_corner},
      {"ui_free_transform_skew_mode_slides_an_edge", ui_free_transform_skew_mode_slides_an_edge},
      {"ui_free_transform_distort_refuses_type_layer", ui_free_transform_distort_refuses_type_layer},
      {"ui_free_transform_distort_is_a_session_step", ui_free_transform_distort_is_a_session_step},
      {"ui_free_transform_mode_menu_entries", ui_free_transform_mode_menu_entries},
      {"ui_free_transform_type_buttons_switch_modes", ui_free_transform_type_buttons_switch_modes},
      {"ui_free_transform_scale_and_rotate_modes", ui_free_transform_scale_and_rotate_modes},
      {"ui_free_transform_options_bar_follows_distortion", ui_free_transform_options_bar_follows_distortion},
      {"ui_script_layer_transform_quad_and_matrix", ui_script_layer_transform_quad_and_matrix},
      {"ui_script_layer_transform_refusals", ui_script_layer_transform_refusals},
      {"ui_free_transform_distort_smart_object_writes_perspective_placement",
       ui_free_transform_distort_smart_object_writes_perspective_placement},
      {"ui_pinball_perspective_smart_objects_render_like_photoshop_if_available",
       ui_pinball_perspective_smart_objects_render_like_photoshop_if_available},
      {"ui_free_transform_distort_shape_layer_maps_path", ui_free_transform_distort_shape_layer_maps_path},
      {"ui_free_transform_wild_corner_drag_stays_convex_and_clean",
       ui_free_transform_wild_corner_drag_stays_convex_and_clean},
      {"ui_free_transform_modifier_keys_show_gesture_cursor", ui_free_transform_modifier_keys_show_gesture_cursor},
      {"ui_free_transform_ctrl_alt_drag_is_symmetric", ui_free_transform_ctrl_alt_drag_is_symmetric},
      {"ui_script_layer_transform_refuses_folded_quad", ui_script_layer_transform_refuses_folded_quad},
      {"ui_move_box_ctrl_drag_corner_distorts", ui_move_box_ctrl_drag_corner_distorts},
      {"ui_move_box_ctrl_drag_side_distorts_and_skews", ui_move_box_ctrl_drag_side_distorts_and_skews},
      {"ui_move_box_ctrl_toggles_distort_mid_drag", ui_move_box_ctrl_toggles_distort_mid_drag},
  };
}
