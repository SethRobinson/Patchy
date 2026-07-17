// Shape-tool vector mode tests: Shape-mode drags create shape layers (with
// combine ops extending them), Path mode populates the work path, Pixels mode
// keeps the legacy raster commit, and the mode rides new sessions.
#include "ui_test_support.hpp"

#include "core/document_path.hpp"
#include "core/vector_shape.hpp"
#include "ui/default_custom_shapes.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QTimer>

#include <cmath>

using namespace patchy::test::ui;

namespace {

// The MainWindow constructor loads tools/* settings, so pin the vector keys
// to their defaults for deterministic runs on developer machines.
class VectorSettingsGuard {
public:
  VectorSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const auto* key :
         {"tools/vectorToolMode", "tools/vectorFillColor", "tools/vectorStrokeColor",
          "tools/vectorStrokeEnabled", "tools/vectorStrokeWidth", "tools/vectorLineWeight"}) {
      settings.remove(QString::fromLatin1(key));
    }
    settings.sync();
  }

private:
  SettingsValueRestorer mode_{QStringLiteral("tools/vectorToolMode")};
  SettingsValueRestorer fill_{QStringLiteral("tools/vectorFillColor")};
  SettingsValueRestorer stroke_color_{QStringLiteral("tools/vectorStrokeColor")};
  SettingsValueRestorer stroke_enabled_{QStringLiteral("tools/vectorStrokeEnabled")};
  SettingsValueRestorer stroke_width_{QStringLiteral("tools/vectorStrokeWidth")};
  SettingsValueRestorer line_weight_{QStringLiteral("tools/vectorLineWeight")};
  SettingsValueRestorer corner_radius_{QStringLiteral("tools/shapeCornerRadius")};
};

void shape_drag(patchy::ui::CanvasWidget& canvas, QPoint document_from, QPoint document_to) {
  drag(canvas, canvas.widget_position_for_document_point(document_from),
       canvas.widget_position_for_document_point(document_to));
  QApplication::processEvents();
}

void ui_shape_tool_creates_shape_layer_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  // A leaked corner radius would turn the drag into an 8-anchor rounded rect.
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Rectangle 1");
  CHECK(patchy::layer_is_vector_shape(*layer));
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->fill.kind == patchy::VectorFillKind::Solid);
  CHECK(!content->stroke.enabled);
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths[0].anchors.size() == 4);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Rectangle);
  CHECK(std::abs(content->origination[0].left - 120.0) < 0.5);
  CHECK(std::abs(content->origination[0].bottom - 260.0) < 0.5);
  CHECK(layer->bounds().width == 200);
  CHECK(layer->bounds().height == 120);
  // Default appearance: black fill baked into the pixel cache.
  const auto center = canvas_pixel(*canvas, QPoint(220, 200));
  CHECK(color_close(center, Qt::black, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
  const auto after_undo = canvas_pixel(*canvas, QPoint(220, 200));
  CHECK(color_close(after_undo, Qt::white, 8));
}

void ui_shape_tool_combine_extends_active_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(100, 100), QPoint(400, 300));
  const auto layers_after_first = document.layers().size();

  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(combine_combo != nullptr);
  combine_combo->setCurrentIndex(2);  // Subtract
  shape_drag(*canvas, QPoint(200, 160), QPoint(300, 240));

  CHECK(document.layers().size() == layers_after_first);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 2);
  CHECK(content->path.subpaths[1].op == patchy::PathCombineOp::Subtract);
  CHECK(content->path.subpaths[1].shape_group == 1);
  CHECK(content->origination.size() == 2);
  CHECK(patchy::layer_vector_block_dirty(*layer));
  // The subtracted interior shows the white background again.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 200)), Qt::white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 130)), Qt::black, 8));
}

void ui_shape_tool_path_mode_populates_work_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Path);

  shape_drag(*canvas, QPoint(150, 150), QPoint(350, 280));
  CHECK(document.layers().size() == initial_layers);
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  CHECK(work->path().subpaths[0].anchors.size() == 4);
  CHECK(work->dirty());

  // A second drag extends the same work path in a new shape group.
  shape_drag(*canvas, QPoint(400, 150), QPoint(500, 250));
  CHECK(document.work_path()->path().subpaths.size() == 2);
  CHECK(document.work_path()->path().subpaths[1].shape_group == 1);

  // The persisted mode rides new document sessions.
  patchy::Document extra(320, 240, patchy::PixelFormat::rgb8());
  extra.add_pixel_layer("Base", solid_pixels(320, 240, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  window.add_document_session(std::move(extra), QStringLiteral("Second"));
  QApplication::processEvents();
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Path);
}

void ui_shape_tool_pixels_mode_keeps_raster_commit() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(2);  // Pixels
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);
  canvas->set_primary_color(Qt::black);
  canvas->set_fill_shapes(true);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);

  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(document.layers().size() == initial_layers);
  CHECK(document.work_path() == nullptr);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  CHECK(!patchy::layer_is_vector_shape(*document.find_layer(*active)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::black, 8));
}

void ui_line_shape_layer_uses_weight_and_stroke_settings() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Line);
  auto* weight_spin = window.findChild<QSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  CHECK(weight_spin != nullptr);
  weight_spin->setValue(10);

  shape_drag(*canvas, QPoint(100, 200), QPoint(300, 200));
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Line 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].kind == patchy::LiveShapeKind::Line);
  CHECK(std::abs(content->origination[0].line_weight - 10.0) < 1e-9);
  // A 10 px weight centered on y=200 rasterizes a band 195..205.
  CHECK(layer->bounds().height == 10);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 200)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 212)), Qt::white, 8));
}

void pen_click(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  drag(canvas, widget_point, widget_point);
  QApplication::processEvents();
}

void ui_pen_tool_click_and_close_creates_shape_layer() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Shape);
  pen_click(*canvas, QPoint(100, 100));
  CHECK(canvas->pen_session_active());
  pen_click(*canvas, QPoint(300, 100));
  pen_click(*canvas, QPoint(200, 250));
  // Clicking the first anchor closes and commits the shape.
  pen_click(*canvas, QPoint(100, 100));
  CHECK(!canvas->pen_session_active());

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Shape 1");
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 1);
  CHECK(content->path.subpaths[0].closed);
  CHECK(content->path.subpaths[0].anchors.size() == 3);
  CHECK(content->origination.empty());
  // Default black fill inside the triangle; white outside.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 140)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 240)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_pen_tool_path_mode_keys_and_handles() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  const auto initial_layers = document.layers().size();
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path

  // Click, click, drag-for-smooth-handles, Backspace pops it, Enter commits.
  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(260, 120));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 240)),
       canvas->widget_position_for_document_point(QPoint(260, 280)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Backspace);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  CHECK(!work->path().subpaths[0].closed);
  CHECK(work->path().subpaths[0].anchors.size() == 2);
  CHECK(document.layers().size() == initial_layers);  // no shape layer in Path mode

  // A drag places a smooth anchor with mirrored handles.
  pen_click(*canvas, QPoint(400, 100));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(500, 200)),
       canvas->widget_position_for_document_point(QPoint(540, 240)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(document.work_path()->path().subpaths.size() == 2);
  const auto& smooth_subpath = document.work_path()->path().subpaths[1];
  CHECK(smooth_subpath.anchors.size() == 2);
  CHECK(smooth_subpath.anchors[1].smooth);
  CHECK(std::abs(smooth_subpath.anchors[1].out_x - 540.0) < 1.5);
  CHECK(std::abs(smooth_subpath.anchors[1].in_x - 460.0) < 1.5);

  // Escape cancels a session without touching the work path.
  pen_click(*canvas, QPoint(600, 100));
  pen_click(*canvas, QPoint(700, 100));
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  CHECK(document.work_path()->path().subpaths.size() == 2);
}

// Creates a 200x120 rectangle shape layer at (100,100)-(300,220) and returns
// its layer id (Shape mode, zero corner radius).
patchy::LayerId make_rect_shape_layer(patchy::ui::MainWindow& window,
                                      patchy::ui::CanvasWidget& canvas) {
  canvas.set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(canvas, QPoint(100, 100), QPoint(300, 220));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  return *active;
}

void ui_direct_select_drags_anchor_with_single_undo() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  // Drag the top-left anchor from (100,100) to (60,70).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(60, 70)));
  QApplication::processEvents();

  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 60.0) < 1.0);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 70.0) < 1.0);
  // Editing the rectangle's anchors drops its live-shape annotation.
  CHECK(content->origination.empty());
  CHECK(patchy::layer_vector_block_dirty(*layer));
  CHECK(canvas->path_edit_has_selection());

  // The whole drag is one history entry: a single undo restores everything.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto* restored = layer->vector_shape();
  CHECK(std::abs(restored->path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);
  CHECK(restored->origination.size() == 1);
}

void ui_path_select_drags_whole_shape_group() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  // Press on the top-left anchor: PathSelect grabs the whole group; drag by
  // (50, 30).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  QApplication::processEvents();

  auto* layer = document.find_layer(layer_id);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  const auto& anchors = content->path.subpaths[0].anchors;
  CHECK(anchors.size() == 4);
  CHECK(std::abs(anchors[0].anchor_x - 150.0) < 1.0);
  CHECK(std::abs(anchors[2].anchor_x - 350.0) < 1.0);
  CHECK(std::abs(anchors[2].anchor_y - 250.0) < 1.0);
  CHECK(layer->bounds().x == 150);
  CHECK(layer->bounds().y == 130);
}

void ui_pen_adds_deletes_and_converts_anchors() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Click the middle of the top edge: adds an anchor (no new session).
  pen_click(*canvas, QPoint(200, 100));
  CHECK(!canvas->pen_session_active());
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 5);
  CHECK(layer->vector_shape()->origination.empty());
  // The insertion t comes from 24-step sampling, so the anchor lands within
  // half a sample step of the click.
  const auto inserted_x = layer->vector_shape()->path.subpaths[0].anchors[1].anchor_x;
  CHECK(std::abs(inserted_x - 200.0) < 12.0);

  // Clicking an anchor deletes it.
  const auto inserted_y = layer->vector_shape()->path.subpaths[0].anchors[1].anchor_y;
  pen_click(*canvas, QPoint(static_cast<int>(inserted_x), static_cast<int>(inserted_y)));
  CHECK(!canvas->pen_session_active());
  layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);

  // Alt+click converts a corner anchor to smooth (handles appear).
  const auto corner = canvas->widget_position_for_document_point(QPoint(300, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, corner, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto& converted = layer->vector_shape()->path.subpaths[0].anchors[1];
  CHECK(converted.smooth);
  CHECK(std::abs(converted.out_x - converted.anchor_x) > 1.0);
}

void ui_path_select_combine_op_edit_applies() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  // Add an inner rectangle with Subtract (combine index 2).
  auto* combine_combo = window.findChild<QComboBox*>(QStringLiteral("vectorCombineCombo"));
  CHECK(combine_combo != nullptr);
  combine_combo->setCurrentIndex(2);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  shape_drag(*canvas, QPoint(160, 140), QPoint(240, 190));
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths.size() == 2);
  CHECK(layer->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Subtract);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::white, 8));

  // Select the inner shape with PathSelect and flip it to Add via the combo.
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  const auto inner_anchor = canvas->widget_position_for_document_point(QPoint(160, 140));
  drag(*canvas, inner_anchor, inner_anchor);
  QApplication::processEvents();
  CHECK(canvas->path_edit_has_selection());
  combine_combo->setCurrentIndex(1);  // Add
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[1].op == patchy::PathCombineOp::Add);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), Qt::black, 8));
}

void ui_vector_mask_from_current_path_masks_layer() {
  VectorSettingsGuard settings_guard;
  patchy::Document base(400, 300, patchy::PixelFormat::rgb8());
  base.add_pixel_layer("Red", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(200, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(base), QStringLiteral("Vector Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Build a work path over the left half, then convert it to a vector mask.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(0, 0), QPoint(200, 300));
  CHECK(document.work_path() != nullptr);

  auto* action = window.findChild<QAction*>(QStringLiteral("layerVectorMaskCurrentPathAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  const auto active = document.active_layer_id();
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->vector_mask() != nullptr);
  CHECK(layer->vector_mask()->path.subpaths.size() == 1);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::VectorMask);
  // Left half stays red, right half is masked to the checkerboard/white.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 150)), QColor(200, 30, 30), 8));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));

  // The row grew a vector-mask thumbnail.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Red"));
  auto* row = layer_list->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerVectorMaskThumbnail")) != nullptr);

  // The pen extends the mask path while the vector-mask target is active.
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  pen_click(*canvas, QPoint(250, 50));
  pen_click(*canvas, QPoint(380, 50));
  pen_click(*canvas, QPoint(320, 250));
  pen_click(*canvas, QPoint(250, 50));  // close
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(layer->vector_mask()->path.subpaths.size() == 2);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(315, 100)), QColor(200, 30, 30), 8));
}

void ui_vector_mask_shift_click_disable_and_rasterize() {
  VectorSettingsGuard settings_guard;
  patchy::Document base(400, 300, patchy::PixelFormat::rgb8());
  base.add_pixel_layer("Red", solid_pixels(400, 300, patchy::PixelFormat::rgb8(), QColor(200, 30, 30)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(base), QStringLiteral("Vector Mask 2"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(0, 0), QPoint(200, 300));
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskCurrentPathAction"))->trigger();
  QApplication::processEvents();
  const auto active = document.active_layer_id();

  // Shift-click the vector-mask thumbnail disables the mask (full red again).
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  click_layer_row_thumbnail(*layer_list, QStringLiteral("Red"),
                            QStringLiteral("layerVectorMaskThumbnail"), Qt::ShiftModifier);
  QApplication::processEvents();
  auto* layer = document.find_layer(*active);
  CHECK(layer->vector_mask() != nullptr);
  CHECK(layer->vector_mask()->disabled);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));
  click_layer_row_thumbnail(*layer_list, QStringLiteral("Red"),
                            QStringLiteral("layerVectorMaskThumbnail"), Qt::ShiftModifier);
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(!layer->vector_mask()->disabled);

  // Rasterize converts the coverage into the raster layer mask.
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskRasterizeAction"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(*active);
  CHECK(layer->vector_mask() == nullptr);
  CHECK(layer->mask().has_value());
  CHECK(*layer->mask()->pixels.pixel(100, 150) == 255);
  CHECK(*layer->mask()->pixels.pixel(300, 150) == 0);
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(300, 150)), QColor(200, 30, 30), 8));

  // Delete the raster mask path: Vector Mask > Delete now errors politely
  // (no vector mask) without crashing.
  window.findChild<QAction*>(QStringLiteral("layerVectorMaskDeleteAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.find_layer(*active)->mask().has_value());
}

QDialog* find_top_level_dialog(const QString& object_name);

void ui_free_transform_scales_shape_layer_crisply() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  auto* free_transform_action = window.findChild<QAction*>(QStringLiteral("editFreeTransformAction"));
  CHECK(free_transform_action != nullptr);
  free_transform_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  // Drag the bottom-right handle outward: (300,220) -> (500,340).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 220)),
       canvas->widget_position_for_document_point(QPoint(500, 340)));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());

  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  const auto& anchors = content->path.subpaths[0].anchors;
  CHECK(std::abs(anchors[2].anchor_x - 500.0) < 2.0);
  CHECK(std::abs(anchors[2].anchor_y - 340.0) < 2.0);
  // A pure axis-aligned scale keeps the live-rect annotation, scaled.
  CHECK(content->origination.size() == 1);
  CHECK(std::abs(content->origination[0].right - 500.0) < 2.0);
  CHECK(patchy::layer_vector_block_dirty(*layer));
  // The re-raster is crisp: bounds match the scaled path, and the pixels come
  // from the rasterizer (fill reaches exactly to the new edges).
  CHECK(std::abs(layer->bounds().x - 100) <= 1);
  CHECK(std::abs(layer->bounds().x + layer->bounds().width - 500) <= 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(490, 330)), Qt::black, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(510, 330)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->vector_shape()->path.subpaths[0].anchors[2].anchor_x - 300.0) < 0.5);
}

void ui_polygon_tool_creates_polygons_and_stars() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_sides(QStringLiteral("tools/polygonSides"));
  SettingsValueRestorer saved_inset(QStringLiteral("tools/polygonStarInset"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Polygon);
  auto* sides = window.findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"));
  auto* inset = window.findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"));
  CHECK(sides != nullptr);
  CHECK(inset != nullptr);
  sides->setValue(6);
  inset->setValue(0);

  // Center-out drag: center (300,300), first vertex at the cursor (300,200).
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 300)),
       canvas->widget_position_for_document_point(QPoint(300, 200)));
  QApplication::processEvents();
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Polygon 1");
  const auto& hexagon = layer->vector_shape()->path.subpaths[0];
  CHECK(hexagon.anchors.size() == 6);
  CHECK(std::abs(hexagon.anchors[0].anchor_x - 300.0) < 1.0);
  CHECK(std::abs(hexagon.anchors[0].anchor_y - 200.0) < 1.0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 300)), Qt::black, 8));

  // Star inset doubles the point count.
  inset->setValue(50);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(600, 300)),
       canvas->widget_position_for_document_point(QPoint(600, 220)));
  QApplication::processEvents();
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer->name() == "Polygon 2");
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 12);
}

void ui_custom_shape_stamps_and_defines() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_shape(QStringLiteral("tools/customShapeId"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::CustomShape);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("customShapeCombo"));
  CHECK(combo != nullptr);
  CHECK(combo->count() >= 17);  // the code-generated builtins
  const auto heart_index = combo->findData(QStringLiteral("shape.builtin.heart"));
  CHECK(heart_index >= 0);
  combo->setCurrentIndex(heart_index);
  QApplication::processEvents();
  CHECK(canvas->custom_shape_path() != nullptr);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 300)));
  QApplication::processEvents();
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Custom Shape 1");
  CHECK(layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 6);
  // The heart lobes cover the upper half's center.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 180)), Qt::black, 8));

  // Define Custom Shape from the active shape layer's path, then stamp it.
  auto& library = patchy::ui::MainWindowTestAccess::custom_shape_library(window);
  QStringList user_shapes_before;
  for (const auto& entry : library.entries()) {
    user_shapes_before.append(entry.storage_id);
  }
  const auto entries_before = combo->count();
  window.findChild<QAction*>(QStringLiteral("editDefineCustomShapeAction"))->trigger();
  QApplication::processEvents();
  CHECK(combo->count() == entries_before + 1);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(500, 100)),
       canvas->widget_position_for_document_point(QPoint(600, 200)));
  QApplication::processEvents();
  layer = document.find_layer(*document.active_layer_id());
  CHECK(layer->name() == "Custom Shape 2");
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 6);

  // Remove the shape the test defined so runs never accumulate user entries.
  QStringList added;
  for (const auto& entry : library.entries()) {
    if (!user_shapes_before.contains(entry.storage_id)) {
      added.append(entry.storage_id);
    }
  }
  for (const auto& storage_id : added) {
    CHECK(library.remove_shape(storage_id));
  }
}

void ui_custom_shape_builtin_geometry_refreshes() {
  // Builtin shape geometry is code-authoritative: a sidecar materialized by
  // an older build (simulated by tampering the stored path into a triangle)
  // is rewritten to the current code geometry by restore_default_shapes(),
  // while a user rename survives and refreshes never count as adds.
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto storage = directory.filePath(QStringLiteral("shapes"));
  const auto heart_id = QStringLiteral("shape.builtin.heart");
  const patchy::ui::BuiltinCustomShape* builtin_heart = nullptr;
  for (const auto& builtin : patchy::ui::builtin_custom_shapes()) {
    if (heart_id == QLatin1String(builtin.id)) {
      builtin_heart = &builtin;
    }
  }
  CHECK(builtin_heart != nullptr);

  QString sidecar_file;
  {
    patchy::ui::CustomShapeLibrary library(storage);
    CHECK(library.restore_default_shapes() ==
          static_cast<int>(patchy::ui::builtin_custom_shapes().size()));
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    // rename_shape re-sorts the entry vector, so `heart` dangles afterwards;
    // capture the storage id first.
    const auto heart_storage_id = heart->storage_id;
    CHECK(library.rename_shape(heart_storage_id, QStringLiteral("My Heart")));
    sidecar_file = storage + QStringLiteral("/") + heart_storage_id + QStringLiteral(".json");
  }

  patchy::VectorPath triangle;
  patchy::PathSubpath triangle_subpath;
  for (const auto& [x, y] : {std::pair{0.5, 0.0}, {1.0, 1.0}, {0.0, 1.0}}) {
    patchy::PathAnchor anchor;
    anchor.anchor_x = anchor.in_x = anchor.out_x = x;
    anchor.anchor_y = anchor.in_y = anchor.out_y = y;
    triangle_subpath.anchors.push_back(anchor);
  }
  triangle.subpaths.push_back(triangle_subpath);
  {
    QFile file(sidecar_file);
    CHECK(file.open(QIODevice::ReadOnly));
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    object.insert(QStringLiteral("path"),
                  QString::fromStdString(patchy::serialize_vector_path(triangle)));
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
  }

  {
    patchy::ui::CustomShapeLibrary library(storage);
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == triangle);  // the tampered sidecar loaded as written
    CHECK(library.restore_default_shapes() == 0);
    heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == builtin_heart->path);
    CHECK(heart->name == QStringLiteral("My Heart"));
  }
  {
    // The refresh reached the sidecar, not just the in-memory entry.
    patchy::ui::CustomShapeLibrary library(storage);
    const auto* heart = library.find_entry_by_shape_id(heart_id);
    CHECK(heart != nullptr);
    CHECK(heart->path == builtin_heart->path);
    CHECK(heart->name == QStringLiteral("My Heart"));
  }
}

void ui_line_arrowheads_extend_the_shape() {
  VectorSettingsGuard settings_guard;
  SettingsValueRestorer saved_start(QStringLiteral("tools/lineArrowStart"));
  SettingsValueRestorer saved_end(QStringLiteral("tools/lineArrowEnd"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Line);
  auto* weight_spin = window.findChild<QSpinBox*>(QStringLiteral("vectorLineWeightSpin"));
  CHECK(weight_spin != nullptr);
  weight_spin->setValue(6);
  auto* arrow_end = window.findChild<QCheckBox*>(QStringLiteral("lineArrowEndCheck"));
  CHECK(arrow_end != nullptr);
  arrow_end->setChecked(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 200)),
       canvas->widget_position_for_document_point(QPoint(400, 200)));
  QApplication::processEvents();
  auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  const auto* content = layer->vector_shape();
  CHECK(content != nullptr);
  CHECK(content->path.subpaths.size() == 2);  // body quad + arrowhead
  CHECK(content->origination.size() == 1);
  CHECK(content->origination[0].arrow_end);
  // The head is wider than the 6 px body: bounds reach ~15 px from the axis.
  CHECK(layer->bounds().height > 20);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 200)), Qt::black, 8));
}

void ui_paths_panel_lists_saves_and_targets_paths() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Path-mode drag creates the work path; the panel lists it.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  auto* paths_dock = window.findChild<QDockWidget*>(QStringLiteral("pathsDock"));
  CHECK(paths_dock != nullptr);
  paths_dock->raise();
  QApplication::processEvents();
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  CHECK(paths_list->item(0)->text() == QStringLiteral("Work Path"));
  CHECK(paths_list->item(0)->font().italic());

  // Double-click saves the work path under a generated name. (Emitted via the
  // signal: the tabified dock has no reliable item geometry offscreen.)
  QMetaObject::invokeMethod(paths_list, "itemDoubleClicked", Qt::DirectConnection,
                            Q_ARG(QListWidgetItem*, paths_list->item(0)));
  QApplication::processEvents();
  CHECK(document.work_path() == nullptr);
  CHECK(document.paths().size() == 1);
  CHECK(document.paths().front().name() == "Path 1");
  CHECK(paths_list->count() == 1);
  CHECK(!paths_list->item(0)->font().italic());

  // New Path creates an empty saved path and targets it: the next Path-mode
  // drag lands there instead of a fresh work path.
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 2);
  CHECK(canvas->active_document_path().has_value());
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  shape_drag(*canvas, QPoint(400, 100), QPoint(500, 200));
  const auto* path2 = document.find_path(*canvas->active_document_path());
  CHECK(path2 != nullptr);
  CHECK(path2->path().subpaths.size() == 1);
  CHECK(document.work_path() == nullptr);

  // Clicking empty panel space deselects (back to the work-path routing).
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
}

void ui_paths_panel_fill_stroke_and_make_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  paths_list->setCurrentRow(0);
  QApplication::processEvents();

  // Fill Path paints the foreground color into the active raster layer.
  canvas->set_primary_color(QColor(30, 160, 40));
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 260)), QColor(30, 160, 40), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 260)), Qt::white, 8));

  // Stroke Path draws a brush-sized band along the outline.
  canvas->set_primary_color(QColor(200, 40, 160));
  canvas->set_brush_size(8);
  window.findChild<QAction*>(QStringLiteral("pathStrokeAction"))->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 260)), QColor(200, 40, 160), 12));

  // Make Selection converts the path (accept the dialog unchanged).
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("makeSelectionDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathMakeSelectionAction"))->trigger();
  QApplication::processEvents();
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(std::abs(selection->x() - 200) <= 1);
  CHECK(std::abs(selection->width() - 200) <= 2);

  // Delete Path removes it and clears the panel targeting.
  window.findChild<QAction*>(QStringLiteral("pathDeleteAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().empty());
  CHECK(paths_list->count() == 0);
}

QDialog* find_top_level_dialog(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == object_name) {
      return qobject_cast<QDialog*>(widget);
    }
  }
  return nullptr;
}

void ui_shape_appearance_dialog_commits_and_cancels() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  CHECK(radius_spin != nullptr);
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  // Commit: enable a 6 px centered stroke and check the live preview fired.
  bool saw_live_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_check = dialog->findChild<QCheckBox*>(QStringLiteral("shapeStrokeCheck"));
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    auto* stroke_align = dialog->findChild<QComboBox*>(QStringLiteral("shapeStrokeAlignCombo"));
    CHECK(stroke_check != nullptr);
    CHECK(stroke_width != nullptr);
    CHECK(stroke_align != nullptr);
    stroke_check->setChecked(true);
    stroke_width->setValue(6.0);
    stroke_align->setCurrentIndex(1);  // Center
    QApplication::processEvents();
    // Preview applies to the layer immediately: the stroke color defaults to
    // black over the black fill, so check the model rather than pixels.
    const auto* preview_layer = document.find_layer(*layer_id);
    saw_live_preview = preview_layer != nullptr && preview_layer->vector_shape() != nullptr &&
                       preview_layer->vector_shape()->stroke.enabled;
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  CHECK(saw_live_preview);
  auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape()->stroke.enabled);
  CHECK(std::abs(layer->vector_shape()->stroke.width - 6.0) < 1e-9);
  CHECK(layer->vector_shape()->stroke.alignment == patchy::VectorStrokeAlignment::Center);
  CHECK(patchy::layer_vector_block_dirty(*layer));

  // Cancel: change the width, reject, and expect no change to the model.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeStrokeWidthSpin"));
    CHECK(stroke_width != nullptr);
    stroke_width->setValue(20.0);
    QApplication::processEvents();
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();
  layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->vector_shape()->stroke.width - 6.0) < 1e-9);
}

void ui_new_solid_fill_layer_uses_selection_mask() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto initial_layers = document.layers().size();

  canvas->set_primary_color(QColor(200, 40, 40));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(300, 220)));
  QApplication::processEvents();
  const auto selection_rect = canvas->selected_document_rect();
  CHECK(selection_rect.has_value());

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  auto* action = window.findChild<QAction*>(QStringLiteral("layerNewSolidColorFillAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  CHECK(document.layers().size() == initial_layers + 1);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Color Fill 1");
  CHECK(patchy::layer_is_vector_shape(*layer));
  CHECK(layer->vector_shape()->path.empty());
  CHECK(layer->vector_shape()->fill.color == (patchy::RgbColor{200, 40, 40}));
  CHECK(layer->mask().has_value());
  CHECK(layer->mask()->bounds.x == selection_rect->x());
  CHECK(layer->mask()->bounds.width == selection_rect->width());
  CHECK(layer->mask()->default_color == 0);
  // Inside the selection the fill shows; outside the mask hides it.
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 160)), QColor(200, 40, 40), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(50, 50)), Qt::white, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.layers().size() == initial_layers);
}

void ui_new_gradient_fill_layer_spans_canvas() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_primary_color(Qt::black);
  canvas->set_secondary_color(Qt::white);
  // The gradient flow opens the appearance dialog right after creating the
  // layer; accept it unchanged.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  auto* action = window.findChild<QAction*>(QStringLiteral("layerNewGradientFillAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();

  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(layer->name() == "Gradient Fill 1");
  CHECK(layer->vector_shape()->fill.kind == patchy::VectorFillKind::Gradient);
  // Photoshop's 90-degree linear gradient points up: stop 0 (foreground
  // black) fills the bottom, the background white the top.
  const auto top = canvas_pixel(*canvas, QPoint(512, 8));
  const auto bottom = canvas_pixel(*canvas, QPoint(512, 760));
  CHECK(bottom.red() + 60 < top.red());
}

}  // namespace

std::vector<patchy::test::TestCase> vector_shape_tool_tests() {
  return {
      {"ui_shape_tool_creates_shape_layer_and_undoes", ui_shape_tool_creates_shape_layer_and_undoes},
      {"ui_shape_tool_combine_extends_active_shape_layer",
       ui_shape_tool_combine_extends_active_shape_layer},
      {"ui_shape_tool_path_mode_populates_work_path", ui_shape_tool_path_mode_populates_work_path},
      {"ui_shape_tool_pixels_mode_keeps_raster_commit", ui_shape_tool_pixels_mode_keeps_raster_commit},
      {"ui_line_shape_layer_uses_weight_and_stroke_settings",
       ui_line_shape_layer_uses_weight_and_stroke_settings},
      {"ui_pen_tool_click_and_close_creates_shape_layer",
       ui_pen_tool_click_and_close_creates_shape_layer},
      {"ui_pen_tool_path_mode_keys_and_handles", ui_pen_tool_path_mode_keys_and_handles},
      {"ui_direct_select_drags_anchor_with_single_undo",
       ui_direct_select_drags_anchor_with_single_undo},
      {"ui_path_select_drags_whole_shape_group", ui_path_select_drags_whole_shape_group},
      {"ui_pen_adds_deletes_and_converts_anchors", ui_pen_adds_deletes_and_converts_anchors},
      {"ui_path_select_combine_op_edit_applies", ui_path_select_combine_op_edit_applies},
      {"ui_vector_mask_from_current_path_masks_layer", ui_vector_mask_from_current_path_masks_layer},
      {"ui_vector_mask_shift_click_disable_and_rasterize",
       ui_vector_mask_shift_click_disable_and_rasterize},
      {"ui_paths_panel_lists_saves_and_targets_paths", ui_paths_panel_lists_saves_and_targets_paths},
      {"ui_paths_panel_fill_stroke_and_make_selection",
       ui_paths_panel_fill_stroke_and_make_selection},
      {"ui_free_transform_scales_shape_layer_crisply",
       ui_free_transform_scales_shape_layer_crisply},
      {"ui_polygon_tool_creates_polygons_and_stars", ui_polygon_tool_creates_polygons_and_stars},
      {"ui_custom_shape_stamps_and_defines", ui_custom_shape_stamps_and_defines},
      {"ui_custom_shape_builtin_geometry_refreshes", ui_custom_shape_builtin_geometry_refreshes},
      {"ui_line_arrowheads_extend_the_shape", ui_line_arrowheads_extend_the_shape},
      {"ui_shape_appearance_dialog_commits_and_cancels",
       ui_shape_appearance_dialog_commits_and_cancels},
      {"ui_new_solid_fill_layer_uses_selection_mask", ui_new_solid_fill_layer_uses_selection_mask},
      {"ui_new_gradient_fill_layer_spans_canvas", ui_new_gradient_fill_layer_spans_canvas},
  };
}
