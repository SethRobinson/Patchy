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
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QStatusBar>
#include <QTextEdit>
#include <optional>
#include <algorithm>
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

const QColor kBoxColor(60, 130, 230);

// A position-locked white Background plus one opaque blue layer at `bounds`, at
// 100% with the Move tool and snapping off.
void prepare_distort_scene(patchy::ui::MainWindow& window, QRect bounds) {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  auto& background = document.add_pixel_layer(
      "Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer layer(document.allocate_layer_id(), "Box",
                      solid_pixels(bounds.width(), bounds.height(), patchy::PixelFormat::rgba8(), kBoxColor));
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

// The three entries live under Edit > Transform with permanent hotkey ids, no
// default keys, and NoRole (macOS menu merging).
void ui_free_transform_mode_menu_entries() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("editTransformMenu"));
  CHECK(menu != nullptr);
  CHECK(menu != nullptr && menu->menuAction()->menuRole() == QAction::NoRole);
  for (const auto& [object_name, hotkey_id] :
       std::vector<std::pair<const char*, const char*>>{{"editTransformSkewAction", "edit.transform_skew"},
                                                        {"editTransformDistortAction", "edit.transform_distort"},
                                                        {"editTransformPerspectiveAction", "edit.transform_perspective"}}) {
    auto* action = require_action(window, object_name);
    CHECK(action->menuRole() == QAction::NoRole);
    CHECK(action->shortcut().isEmpty());
    CHECK(require_hotkey_action(window, QLatin1String(hotkey_id)) == action);
    CHECK(menu != nullptr && menu->actions().contains(action));
  }
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
      {"ui_free_transform_distort_smart_object_writes_perspective_placement",
       ui_free_transform_distort_smart_object_writes_perspective_placement},
      {"ui_pinball_perspective_smart_objects_render_like_photoshop_if_available",
       ui_pinball_perspective_smart_objects_render_like_photoshop_if_available},
      {"ui_free_transform_distort_shape_layer_maps_path", ui_free_transform_distort_shape_layer_maps_path},
  };
}
