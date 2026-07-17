// Shape-tool vector mode tests: Shape-mode drags create shape layers (with
// combine ops extending them), Path mode populates the work path, Pixels mode
// keeps the legacy raster commit, and the mode rides new sessions.
#include "ui_test_support.hpp"

#include "core/document_path.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"
#include "ui/default_custom_shapes.hpp"
#include "ui/pattern_library.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

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
  SettingsValueRestorer work_path_tolerance_{QStringLiteral("paths/makeWorkPathTolerance")};
  SettingsValueRestorer simulate_pressure_{QStringLiteral("paths/strokeSimulatePressure")};
  SettingsValueRestorer fill_contents_{QStringLiteral("paths/fillContents")};
  SettingsValueRestorer fill_pattern_{QStringLiteral("paths/fillPatternId")};
  SettingsValueRestorer fill_opacity_{QStringLiteral("paths/fillOpacity")};
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
  // The action prompts for a name, prefilled with the generated fallback.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("defineCustomShapeDialog"));
    CHECK(dialog != nullptr);
    auto* name_edit = dialog->findChild<QLineEdit*>(QStringLiteral("defineCustomShapeNameEdit"));
    CHECK(name_edit != nullptr);
    CHECK(!name_edit->text().isEmpty());
    name_edit->setText(QStringLiteral("Test Heart Copy"));
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("editDefineCustomShapeAction"))->trigger();
  QApplication::processEvents();
  CHECK(combo->count() == entries_before + 1);
  CHECK(combo->currentText() == QStringLiteral("Test Heart Copy"));
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

  // Fill Path paints the foreground color into the active raster layer (the
  // dialog's default contents).
  canvas->set_primary_color(QColor(30, 160, 40));
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"))->setCurrentIndex(0);
    dialog->findChild<QSpinBox*>(QStringLiteral("fillPathOpacitySpin"))->setValue(100);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 260)), QColor(30, 160, 40), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 260)), Qt::white, 8));

  // Stroke Path replays the path through the BRUSH ENGINE (one undo entry).
  // The targeted path's outline overlay draws with any tool and would cover
  // the stroke's center line in a widget grab, so deselect (empty-space
  // click) before sampling: the overlay must disappear and reveal the paint.
  canvas->set_primary_color(QColor(200, 40, 160));
  canvas->set_brush_size(8);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  const auto undo_depth_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("strokePathDialog"));
    CHECK(dialog != nullptr);
    auto* simulate =
        dialog->findChild<QCheckBox*>(QStringLiteral("strokePathSimulatePressureCheck"));
    CHECK(simulate != nullptr);
    simulate->setChecked(false);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathStrokeAction"))->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_depth_before + 1);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(200, 260)), QColor(200, 40, 160), 12));

  // Make Selection converts the path (re-select the row first).
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
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
    // Stroke rows grey out while the stroke is disabled.
    CHECK(!stroke_check->isChecked());
    CHECK(!stroke_width->isEnabled());
    CHECK(!stroke_align->isEnabled());
    stroke_check->setChecked(true);
    CHECK(stroke_width->isEnabled());
    CHECK(stroke_align->isEnabled());
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

void ui_paths_panel_clipping_path_toggle() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two saved paths.
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(200, 200));
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(300, 100), QPoint(400, 200));

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  auto* clipping = window.findChild<QAction*>(QStringLiteral("pathClippingAction"));
  CHECK(paths_list != nullptr && clipping != nullptr);
  CHECK(paths_list->count() == 2);

  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(clipping->isEnabled());
  CHECK(!clipping->isChecked());
  clipping->trigger();
  QApplication::processEvents();
  CHECK(document.paths()[0].is_clipping_path());
  CHECK(paths_list->item(0)->font().underline());
  CHECK(!paths_list->item(1)->font().underline());

  // Exclusivity: designating the second path clears the first.
  paths_list->setCurrentRow(1);
  QApplication::processEvents();
  CHECK(!clipping->isChecked());
  clipping->trigger();
  QApplication::processEvents();
  CHECK(!document.paths()[0].is_clipping_path());
  CHECK(document.paths()[1].is_clipping_path());
  CHECK(clipping->isChecked());

  // Toggling again clears it entirely.
  clipping->trigger();
  QApplication::processEvents();
  CHECK(!document.paths()[1].is_clipping_path());
  CHECK(!paths_list->item(1)->font().underline());
}

void ui_path_edits_refresh_panel_thumbnails() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  const auto before = paths_list->item(0)->icon().pixmap(QSize(42, 30)).toImage();

  // A Direct Select anchor drag mutates the path canvas-side; the panel row
  // thumbnail must follow without any other UI action.
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(40, 60)));
  QApplication::processEvents();
  CHECK(paths_list->count() == 1);
  const auto after = paths_list->item(0)->icon().pixmap(QSize(42, 30)).toImage();
  CHECK(before != after);
}

void ui_shape_pattern_fill_uses_custom_library_pattern() {
  // Regression: choosing a CUSTOM library pattern (imported image, auto
  // generated id) in the Shape Appearance dialog rendered an empty fill.
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A shape layer to restyle.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());

  // A custom pattern exactly like the Pattern Manager's image import: the
  // pattern id is auto-generated by the library.
  patchy::PixelBuffer tile(8, 8, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      auto* px = tile.pixel(x, y);
      const bool first = ((x / 4) + (y / 4)) % 2 == 0;
      px[0] = first ? 30 : 220;
      px[1] = first ? 180 : 60;
      px[2] = first ? 60 : 200;
      px[3] = 255;
    }
  }
  // A poisoned document-store entry from an earlier failed attempt: same id,
  // EMPTY tile. Adoption must heal it rather than skip (the empty tile would
  // render the fill transparent forever).
  patchy::PatternResource poisoned;
  poisoned.name = "poisoned";
  const auto storage_id = window.pattern_library().add_pattern(
      QStringLiteral("Custom Import Test"), tile, QStringLiteral("Tests"), QString());
  CHECK(!storage_id.isEmpty());
  const auto* entry = window.pattern_library().find_entry(storage_id);
  CHECK(entry != nullptr);
  const auto pattern_id = entry->id;
  CHECK(!pattern_id.isEmpty());
  poisoned.id = pattern_id.toStdString();
  document.metadata().patterns.patterns.push_back(poisoned);

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    CHECK(dialog != nullptr);
    auto* type_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillKindCombo"));
    auto* pattern_combo = dialog->findChild<QComboBox*>(QStringLiteral("shapeFillPatternCombo"));
    CHECK(type_combo != nullptr && pattern_combo != nullptr);
    const auto pattern_type_index = type_combo->findText(QObject::tr("Pattern"));
    CHECK(pattern_type_index >= 0);
    type_combo->setCurrentIndex(pattern_type_index);
    const auto pattern_index = pattern_combo->findData(pattern_id);
    CHECK(pattern_index >= 0);
    pattern_combo->setCurrentIndex(pattern_index);
    QApplication::processEvents();
    dialog->accept();
  });
  patchy::ui::MainWindowTestAccess::edit_active_shape_appearance(window);
  QApplication::processEvents();

  const auto* layer = std::as_const(document).find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->vector_shape()->fill.kind == patchy::VectorFillKind::Pattern);
  CHECK(layer->vector_shape()->fill.pattern_id == pattern_id.toStdString());
  // The healthy tile healed the poisoned store entry and the raster shows the
  // checker colors inside the shape.
  const auto* adopted = document.metadata().patterns.find(pattern_id.toStdString());
  CHECK(adopted != nullptr);
  CHECK(!adopted->tile.empty());
  const auto inside = canvas_pixel(*canvas, QPoint(220, 200));
  const bool matches_checker = color_close(inside, QColor(30, 180, 60), 12) ||
                               color_close(inside, QColor(220, 60, 200), 12);
  CHECK(matches_checker);

  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_new_fill_layer_clips_to_targeted_path() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Path-mode rect drag creates AND targets the work path.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  CHECK(canvas->panel_path_targeted());

  canvas->set_primary_color(QColor(40, 90, 200));
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyColorDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("layerNewSolidColorFillAction"))->trigger();
  QApplication::processEvents();

  // Photoshop's "current path" rule: the new fill layer's shape path IS the
  // targeted path, so the fill clips to it.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* layer = std::as_const(document).find_layer(*active);
  CHECK(layer != nullptr);
  CHECK(patchy::layer_is_vector_shape(*layer));
  CHECK(layer->vector_shape()->path.subpaths.size() == 1);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(300, 260)), QColor(40, 90, 200), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), Qt::white, 8));
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

void ui_pen_hover_shows_context_cursor_badges() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  canvas->setFocus();
  // Cursor state is driven by hover moves carrying the event's own modifiers
  // (the offscreen platform never clears the global keyboard state).
  const auto hover = [&](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    send_mouse(*canvas, QEvent::MouseMove,
               canvas->widget_position_for_document_point(document_point), Qt::NoButton,
               Qt::NoButton, modifiers);
    return canvas->cursor();
  };

  const auto plain = hover(QPoint(500, 400), Qt::NoModifier);
  CHECK(plain.shape() == Qt::BitmapCursor);
  CHECK(plain.hotSpot() == QPoint(10, 10));
  const auto plain_image = plain.pixmap().toImage();

  const auto add = hover(QPoint(200, 100), Qt::NoModifier);  // top-edge segment
  CHECK(add.shape() == Qt::BitmapCursor);
  const auto add_image = add.pixmap().toImage();
  CHECK(add_image != plain_image);

  const auto del = hover(QPoint(100, 100), Qt::NoModifier);  // anchor
  const auto delete_image = del.pixmap().toImage();
  CHECK(delete_image != plain_image);
  CHECK(delete_image != add_image);

  const auto convert = hover(QPoint(100, 100), Qt::AltModifier);  // Alt over anchor
  const auto convert_image = convert.pixmap().toImage();
  CHECK(convert_image != delete_image);
  CHECK(convert_image != plain_image);

  // Ctrl with a stationary pointer flips to the temporary Direct Select arrow
  // and back on release (the folded-modifier filter path).
  hover(QPoint(500, 400), Qt::NoModifier);
  send_key_press(*canvas, Qt::Key_Control, Qt::NoModifier);
  CHECK(canvas->cursor().shape() == Qt::ArrowCursor);
  send_key_release(*canvas, Qt::Key_Control, Qt::ControlModifier);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  // Mid-session, hovering the first anchor advertises the close action.
  pen_click(*canvas, QPoint(500, 300));
  pen_click(*canvas, QPoint(650, 300));
  pen_click(*canvas, QPoint(575, 420));
  CHECK(canvas->pen_session_active());
  const auto close = hover(QPoint(500, 300), Qt::NoModifier);
  const auto close_image = close.pixmap().toImage();
  CHECK(close_image != plain_image);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = make_rect_shape_layer(window, *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Ctrl+click on an anchor selects it - it must never delete (the plain-click
  // pen behavior) or start a session.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(100, 100)), Qt::ControlModifier);
  QApplication::processEvents();
  auto* layer = document.find_layer(layer_id);
  CHECK(layer->vector_shape()->path.subpaths[0].anchors.size() == 4);
  CHECK(canvas->path_edit_has_selection());
  CHECK(!canvas->pen_session_active());

  // Ctrl+drag moves the anchor with Direct Select semantics.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(60, 70)), Qt::ControlModifier);
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  const auto* content = layer->vector_shape();
  CHECK(content->path.subpaths[0].anchors.size() == 4);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_x - 60.0) < 1.0);
  CHECK(std::abs(content->path.subpaths[0].anchors[0].anchor_y - 70.0) < 1.0);
  CHECK(content->origination.empty());
  CHECK(!canvas->pen_session_active());

  // Releasing Ctrl leaves the Pen drawing as usual.
  pen_click(*canvas, QPoint(600, 80));
  CHECK(canvas->pen_session_active());
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();

  // The whole Ctrl-drag is one history entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(layer_id);
  CHECK(std::abs(layer->vector_shape()->path.subpaths[0].anchors[0].anchor_x - 100.0) < 0.5);
  CHECK(layer->vector_shape()->origination.size() == 1);
}

void ui_pen_ctrl_drag_moves_in_progress_session_anchor() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path

  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(300, 100));
  pen_click(*canvas, QPoint(200, 250));
  CHECK(canvas->pen_session_active());

  // Ctrl+drag the middle anchor of the in-progress path; the session survives
  // and no anchor is added.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 100)),
       canvas->widget_position_for_document_point(QPoint(340, 140)), Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->pen_session_active());

  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->pen_session_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);
  const auto& anchors = work->path().subpaths[0].anchors;
  CHECK(anchors.size() == 3);
  CHECK(std::abs(anchors[1].anchor_x - 340.0) < 1.5);
  CHECK(std::abs(anchors[1].anchor_y - 140.0) < 1.5);
  // The handles rode along with the anchor.
  CHECK(std::abs(anchors[1].in_x - 340.0) < 1.5);
  CHECK(std::abs(anchors[1].out_x - 340.0) < 1.5);
}

void ui_pen_session_start_shows_hint_message() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto hint = QStringLiteral(
      "Click to add points, drag for curves. Click the first point to close; "
      "Enter commits an open path; Esc cancels.");

  make_rect_shape_layer(window, *canvas);
  canvas->set_tool(patchy::ui::CanvasTool::Pen);
  // Editing the existing path (add anchor on the top edge) is not a session:
  // no hint appears.
  pen_click(*canvas, QPoint(200, 100));
  CHECK(!canvas->pen_session_active());
  CHECK(window.statusBar()->currentMessage() != hint);

  // The first anchor of a new path shows the hint once.
  pen_click(*canvas, QPoint(600, 400));
  CHECK(canvas->pen_session_active());
  CHECK(window.statusBar()->currentMessage() == hint);
  pen_click(*canvas, QPoint(700, 400));
  CHECK(window.statusBar()->currentMessage() == hint);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_vector_mode_combo_disables_pixels_for_vector_only_tools() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  auto* model = qobject_cast<QStandardItemModel*>(mode_combo->model());
  CHECK(model != nullptr);

  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  mode_combo->setCurrentIndex(2);  // Pixels, the app-wide persisted mode
  QApplication::processEvents();
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);

  // The Pen never rasterizes: Pixels greys out and the combo displays the
  // effective mode (Path) without rewriting the setting.
  require_action(window, "toolPenAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->currentIndex() == 1);
  CHECK(!model->item(2)->isEnabled());
  CHECK(canvas->vector_tool_mode() == patchy::ui::VectorToolMode::Pixels);

  // Pen commits land on the work path, matching the displayed mode.
  pen_click(*canvas, QPoint(100, 100));
  pen_click(*canvas, QPoint(300, 100));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 1);

  // A raster-capable shape tool shows the real setting again.
  require_action(window, "toolRectAction")->trigger();
  QApplication::processEvents();
  CHECK(mode_combo->currentIndex() == 2);
  CHECK(model->item(2)->isEnabled());
}

void ui_layer_context_menu_offers_shape_appearance_for_shape_layers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  make_rect_shape_layer(window, *canvas);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() > 0);

  // Open the context menu on the active (shape) layer and record its actions.
  const auto collect_menu_actions = [&](QStringList& names) {
    bool saw_menu = false;
    int poll_attempts = 0;
    QTimer poller;
    QObject::connect(&poller, &QTimer::timeout, [&] {
      if (++poll_attempts > 500) {
        poller.stop();
        return;
      }
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") &&
            menu->isVisible()) {
          saw_menu = true;
          for (auto* action : menu->actions()) {
            names << action->objectName();
          }
          menu->close();
          poller.stop();
          return;
        }
      }
    });
    poller.start(10);
    QMetaObject::invokeMethod(
        &window,
        [&window, layer_list] {
          patchy::ui::MainWindowTestAccess::show_layer_context_menu(
              window, layer_list->visualItemRect(layer_list->item(0)).center());
        },
        Qt::QueuedConnection);
    QApplication::processEvents();
    for (int i = 0; i < 200 && !saw_menu && poll_attempts <= 500; ++i) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    poller.stop();
    return saw_menu;
  };

  QStringList shape_layer_actions;
  CHECK(collect_menu_actions(shape_layer_actions));
  // Edit Layer Styles... stays first; the appearance editor rides second.
  CHECK(shape_layer_actions.indexOf(QStringLiteral("layerContextEditShapeAppearanceAction")) == 1);

  // A plain pixel layer (after undoing the shape) offers no appearance entry.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  QStringList pixel_layer_actions;
  CHECK(collect_menu_actions(pixel_layer_actions));
  CHECK(!pixel_layer_actions.contains(QStringLiteral("layerContextEditShapeAppearanceAction")));
}

void ui_paths_panel_actions_follow_row_selection() {
  // A real mouse click updates the list's CURRENT item before committing the
  // selection, so the panel must refresh its action states on selectionChanged
  // too; clicking a row used to leave Fill/Stroke/Make Selection/Delete stuck
  // disabled (and the post-open blanket enable left them wrongly enabled).
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  auto* list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  auto* new_action = window.findChild<QAction*>(QStringLiteral("pathNewAction"));
  auto* fill = window.findChild<QAction*>(QStringLiteral("pathFillAction"));
  auto* stroke = window.findChild<QAction*>(QStringLiteral("pathStrokeAction"));
  auto* make_selection = window.findChild<QAction*>(QStringLiteral("pathMakeSelectionAction"));
  auto* delete_action = window.findChild<QAction*>(QStringLiteral("pathDeleteAction"));
  CHECK(list != nullptr && new_action != nullptr && fill != nullptr && stroke != nullptr &&
        make_selection != nullptr && delete_action != nullptr);
  CHECK(list->count() == 1);  // the work path

  // Drawing auto-targets the work path row (Photoshop highlights it as soon
  // as you draw), so the row commands start enabled.
  CHECK(canvas->active_document_path().has_value());
  CHECK(new_action->isEnabled());
  CHECK(fill->isEnabled());
  CHECK(stroke->isEnabled());
  CHECK(make_selection->isEnabled());
  CHECK(delete_action->isEnabled());

  // Clicking the empty area below the rows deselects and disables them.
  send_mouse(*list->viewport(), QEvent::MouseButtonPress,
             QPoint(list->viewport()->width() / 2, list->viewport()->height() - 4), Qt::LeftButton,
             Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->active_document_path().has_value());
  CHECK(new_action->isEnabled());
  CHECK(!fill->isEnabled());
  CHECK(!stroke->isEnabled());
  CHECK(!make_selection->isEnabled());
  CHECK(!delete_action->isEnabled());

  // A real mouse click on the row (press + release) re-enables the row
  // commands: the panel must refresh action states on selectionChanged, not
  // just currentItemChanged (the click updates the current item first).
  const auto row_rect = list->visualItemRect(list->item(0));
  send_mouse(*list->viewport(), QEvent::MouseButtonPress, row_rect.center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*list->viewport(), QEvent::MouseButtonRelease, row_rect.center(), Qt::LeftButton,
             Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_document_path().has_value());
  CHECK(fill->isEnabled());
  CHECK(stroke->isEnabled());
  CHECK(make_selection->isEnabled());
  CHECK(delete_action->isEnabled());
}

// True when a pixel near the document point reads as the path-overlay accent
// (116, 192, 255) in a canvas grab: distinctly blue-leaning against the
// white/black canvas content these tests use.
bool accent_overlay_near(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto image = canvas.grab().toImage();
  const auto center = canvas.widget_position_for_document_point(document_point);
  for (int dy = -3; dy <= 3; ++dy) {
    for (int dx = -3; dx <= 3; ++dx) {
      const QPoint probe(center.x() + dx, center.y() + dy);
      if (!image.rect().contains(probe)) {
        continue;
      }
      const auto color = image.pixelColor(probe);
      if (color.blue() >= 200 && color.blue() - color.red() >= 60 &&
          color.green() > color.red()) {
        return true;
      }
    }
  }
  return false;
}

void ui_paths_panel_target_shows_overlay_with_any_tool() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // A Path-mode drag creates the work path AND targets its row (Photoshop
  // highlights Work Path as soon as you draw).
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  CHECK(mode_combo != nullptr);
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));
  CHECK(canvas->active_document_path().has_value());
  CHECK(canvas->panel_path_targeted());

  // The outline stays visible with ANY tool (the Photoshop target-path rule).
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));  // left edge midpoint

  // Clicking empty panel space hides it.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));

  // Re-selecting the row shows it again; Escape under a path tool (with no
  // anchor selection to clear first) dismisses the panel targeting. A path
  // tool still displays its edit-target fallback (it would edit the work
  // path), so switch to Move to see the outline actually gone.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
  canvas->set_tool(patchy::ui::CanvasTool::DirectSelect);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(paths_list->selectedItems().isEmpty());
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));

  // View > Show Target Path (Ctrl+Shift+H) hides the overlay without
  // touching the targeting, and re-shows it on toggle-back.
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
  auto* toggle = window.findChild<QAction*>(QStringLiteral("viewToggleTargetPathAction"));
  CHECK(toggle != nullptr);
  toggle->trigger();
  QApplication::processEvents();
  CHECK(!accent_overlay_near(*canvas, QPoint(100, 160)));
  CHECK(canvas->panel_path_targeted());
  toggle->trigger();
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(100, 160)));
}

void ui_shape_layer_auto_targets_path_row_until_dismissed() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A Shape-mode drag creates a shape layer; its transient row auto-targets
  // so the outline shows on canvas with any tool.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  CHECK(patchy::layer_is_vector_shape(*document.find_layer(*active)));
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);  // the transient layer row
  CHECK(!paths_list->selectedItems().isEmpty());
  CHECK(canvas->panel_path_targeted());
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  QApplication::processEvents();
  CHECK(accent_overlay_near(*canvas, QPoint(120, 200)));  // shape left edge

  // Dismissing hides it, and a panel refresh must NOT resurrect it while the
  // same layer stays active.
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress,
             QPoint(paths_list->viewport()->width() / 2, paths_list->viewport()->height() - 4),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(!canvas->panel_path_targeted());
  CHECK(paths_list->selectedItems().isEmpty());

  // An explicit row click re-shows it (and clears the dismissal).
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  QApplication::processEvents();
  CHECK(canvas->panel_path_targeted());
  CHECK(!paths_list->selectedItems().isEmpty());
}

void ui_shape_layer_row_shows_vector_badge() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(120, 140), QPoint(320, 260));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* shape_item = require_layer_item(*layer_list, QStringLiteral("Rectangle 1"));
  auto* shape_row = layer_list->itemWidget(shape_item);
  CHECK(shape_row != nullptr);
  auto* badge = shape_row->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton"));
  CHECK(badge != nullptr);
  CHECK(!badge->icon().isNull());

  // Plain raster layers carry no vector badge.
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* background_row = layer_list->itemWidget(background_item);
  CHECK(background_row != nullptr);
  CHECK(background_row->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton")) ==
        nullptr);

  // Clicking the badge opens the Shape Appearance dialog. The badge defers
  // its open by one timer tick, so queue the reject AFTER the click: the
  // open fires first and blocks in the dialog loop, then the reject timer
  // fires inside that loop.
  bool dialog_seen = false;
  badge->click();
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog"));
    dialog_seen = dialog != nullptr;
    if (dialog != nullptr) {
      dialog->reject();
    }
  });
  QApplication::processEvents();
  CHECK(dialog_seen);
}

void ui_paths_panel_ctrl_click_loads_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(200, 200), QPoint(400, 320));
  CHECK(!canvas->has_selection());

  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 1);
  const auto row_rect = paths_list->visualItemRect(paths_list->item(0));
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonPress, row_rect.center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*paths_list->viewport(), QEvent::MouseButtonRelease, row_rect.center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(std::abs(selection->x() - 200) <= 1);
  CHECK(std::abs(selection->y() - 200) <= 1);
  CHECK(std::abs(selection->width() - 200) <= 2);
  CHECK(std::abs(selection->height() - 120) <= 2);

  // Ctrl+Enter on the canvas loads the targeted row too (the PS staple).
  canvas->clear_selection();
  CHECK(!canvas->has_selection());
  paths_list->setCurrentRow(0);
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->has_selection());
}

void ui_paths_panel_duplicate_and_reorder() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Two saved paths via New Path + Path-mode drags (each drag lands in the
  // targeted saved path).
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(100, 100), QPoint(200, 200));
  window.findChild<QAction*>(QStringLiteral("pathNewAction"))->trigger();
  QApplication::processEvents();
  shape_drag(*canvas, QPoint(300, 100), QPoint(400, 200));
  CHECK(document.paths().size() == 2);

  // Duplicate Path copies the selected row under "<name> copy" and targets it.
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  CHECK(paths_list->count() == 2);
  paths_list->setCurrentRow(0);  // "Path 1"
  QApplication::processEvents();
  window.findChild<QAction*>(QStringLiteral("pathDuplicateAction"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 3);
  CHECK(document.paths().back().name() == "Path 1 copy");
  CHECK(document.paths().back().path().subpaths.size() == 1);
  CHECK(canvas->active_document_path().has_value());
  CHECK(*canvas->active_document_path() == document.paths().back().id());

  // Undo removes the copy.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths().size() == 2);

  // Drag-reorder (via the model, like the channel panel test): move "Path 2"
  // above "Path 1" and check the document order followed.
  CHECK(paths_list->model()->moveRow(QModelIndex(), 1, QModelIndex(), 0));
  QApplication::processEvents();
  QApplication::processEvents();  // the reorder commit is deferred one tick
  CHECK(document.paths().size() == 2);
  CHECK(document.paths()[0].name() == "Path 2");
  CHECK(document.paths()[1].name() == "Path 1");

  // Undo restores the original order.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(document.paths()[0].name() == "Path 1");
  CHECK(document.paths()[1].name() == "Path 2");
}

void ui_fill_path_supports_patterns() {
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

  // Install a deterministic two-color checker pattern (the test settings
  // sandbox leaves the library unpopulated, so bundled entries are not
  // guaranteed here).
  patchy::PixelBuffer tile(8, 8, patchy::PixelFormat::rgba8());
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      auto* px = tile.pixel(x, y);
      const bool first = ((x / 4) + (y / 4)) % 2 == 0;
      px[0] = first ? 10 : 240;
      px[1] = first ? 200 : 40;
      px[2] = first ? 30 : 220;
      px[3] = 255;
    }
  }
  const auto pattern_id = QStringLiteral("patchy-test-fill-path-pattern");
  if (const auto* stale = window.pattern_library().find_entry_by_pattern_id(pattern_id);
      stale != nullptr) {
    CHECK(window.pattern_library().remove_pattern(stale->storage_id));
  }
  const auto storage_id = window.pattern_library().add_pattern(
      QStringLiteral("Fill Path Test"), tile, QStringLiteral("Tests"), pattern_id);
  CHECK(!storage_id.isEmpty());

  // A deliberately absurd foreground proves the pattern pixels were used.
  canvas->set_primary_color(QColor(255, 0, 255));
  bool pattern_selected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("fillPathDialog"));
    CHECK(dialog != nullptr);
    auto* contents = dialog->findChild<QComboBox*>(QStringLiteral("fillPathContentsCombo"));
    auto* pattern = dialog->findChild<QComboBox*>(QStringLiteral("fillPathPatternCombo"));
    CHECK(contents != nullptr && pattern != nullptr);
    contents->setCurrentIndex(2);  // Pattern
    CHECK(pattern->isEnabled());
    const auto index = pattern->findData(pattern_id);
    pattern_selected = index >= 0;
    if (pattern_selected) {
      pattern->setCurrentIndex(index);
    }
    dialog->findChild<QSpinBox*>(QStringLiteral("fillPathOpacitySpin"))->setValue(100);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFillAction"))->trigger();
  QApplication::processEvents();
  CHECK(pattern_selected);

  // The active Paint Layer gained checker pixels inside the path (never the
  // magenta foreground); outside stays untouched.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* paint_layer = std::as_const(document).find_layer(*active);
  CHECK(paint_layer != nullptr);
  const auto& pixels = paint_layer->pixels();
  CHECK(pixels.format().channels == 4);
  const auto* inside = pixels.pixel(300, 260);
  CHECK(inside[3] == 255U);
  const bool matches_checker =
      (inside[0] == 10U && inside[1] == 200U && inside[2] == 30U) ||
      (inside[0] == 240U && inside[1] == 40U && inside[2] == 220U);
  CHECK(matches_checker);
  const auto* outside = pixels.pixel(150, 260);
  CHECK(outside[3] == 0U);

  CHECK(window.pattern_library().remove_pattern(storage_id));
}

void ui_stroke_path_simulate_pressure_tapers() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // Pin the pen-input mapping the taper rides (the user's preferences could
  // disable pressure->size).
  auto pen_settings = canvas->pen_input_settings();
  pen_settings.pressure_size = true;
  pen_settings.pressure_size_min_percent = 20;
  pen_settings.pressure_opacity = false;
  canvas->set_pen_input_settings(pen_settings);

  // An open horizontal line as the work path (document API: no implied chord).
  patchy::PathAnchor start;
  start.anchor_x = start.in_x = start.out_x = 150.0;
  start.anchor_y = start.in_y = start.out_y = 300.0;
  auto end = start;
  end.anchor_x = end.in_x = end.out_x = 450.0;
  patchy::PathSubpath line;
  line.closed = false;
  line.anchors = {start, end};
  patchy::VectorPath path;
  path.subpaths.push_back(line);
  patchy::DocumentPath work(document.allocate_path_id(), "Work Path",
                            patchy::DocumentPathKind::Work, std::move(path));
  work.mark_dirty();
  document.add_path(std::move(work));
  patchy::ui::MainWindowTestAccess::refresh_paths_panel(window);
  auto* paths_list = window.findChild<QListWidget*>(QStringLiteral("pathsList"));
  CHECK(paths_list != nullptr);
  paths_list->setCurrentRow(0);
  QApplication::processEvents();

  canvas->set_primary_color(QColor(20, 20, 20));
  canvas->set_brush_size(20);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("strokePathDialog"));
    CHECK(dialog != nullptr);
    dialog->findChild<QCheckBox*>(QStringLiteral("strokePathSimulatePressureCheck"))
        ->setChecked(true);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathStrokeAction"))->trigger();
  QApplication::processEvents();

  // The taper: thin near the ends, full brush width in the middle. The
  // startup document paints onto the transparent "Paint Layer" (the active
  // layer), so measure painted alpha there, not on the Background.
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* paint_layer = std::as_const(document).find_layer(*active);
  CHECK(paint_layer != nullptr);
  const auto& pixels = paint_layer->pixels();
  CHECK(pixels.format().channels == 4);
  const auto thickness_at = [&pixels](int x) {
    int count = 0;
    for (int y = 250; y <= 350; ++y) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++count;
      }
    }
    return count;
  };
  const int start_thickness = thickness_at(160);
  const int mid_thickness = thickness_at(300);
  CHECK(mid_thickness >= 15);
  CHECK(start_thickness >= 1);
  CHECK(start_thickness <= mid_thickness / 2);
}

void ui_path_free_transform_moves_scales_and_undoes() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A rect work path, auto-targeted by the drag.
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  auto* mode_combo = window.findChild<QComboBox*>(QStringLiteral("vectorModeCombo"));
  mode_combo->setCurrentIndex(1);  // Path
  auto* radius_spin = window.findChild<QSpinBox*>(QStringLiteral("shapeCornerRadiusSpin"));
  radius_spin->setValue(0);
  shape_drag(*canvas, QPoint(100, 100), QPoint(300, 220));

  // Ctrl+T under a path tool starts the PATH session, not the layer one.
  canvas->set_tool(patchy::ui::CanvasTool::PathSelect);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  CHECK(!canvas->free_transform_active());

  // Arrows nudge the pending box; Enter commits ONE undo entry.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down, Qt::ShiftModifier);  // +10
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  const auto* work = document.work_path();
  CHECK(work != nullptr);
  const auto bounds_of_work = [&document] {
    std::array<double, 4> bounds{1e9, 1e9, -1e9, -1e9};
    for (const auto& anchor : document.work_path()->path().subpaths[0].anchors) {
      bounds[0] = std::min(bounds[0], anchor.anchor_x);
      bounds[1] = std::min(bounds[1], anchor.anchor_y);
      bounds[2] = std::max(bounds[2], anchor.anchor_x);
      bounds[3] = std::max(bounds[3], anchor.anchor_y);
    }
    return bounds;
  };
  auto bounds = bounds_of_work();
  CHECK(std::abs(bounds[0] - 102.0) < 1e-6);
  CHECK(std::abs(bounds[1] - 110.0) < 1e-6);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[0] - 100.0) < 1e-6);

  // A corner-handle drag scales; the bottom-right anchor follows the handle.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->path_transform_active());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(300, 220)),
       canvas->widget_position_for_document_point(QPoint(400, 280)));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[2] - 400.0) < 1.5);
  CHECK(std::abs(bounds[3] - 280.0) < 1.5);

  // Escape cancels a pending transform without touching the path.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->path_transform_active());
  bounds = bounds_of_work();
  CHECK(std::abs(bounds[2] - 400.0) < 1.5);
}

void ui_make_work_path_from_selection_traces_selection() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  // A donut selection: outer rect with a rectangular hole.
  patchy::PixelBuffer coverage(document.width(), document.height(),
                               patchy::PixelFormat::gray8());
  for (int y = 0; y < coverage.height(); ++y) {
    for (int x = 0; x < coverage.width(); ++x) {
      const bool outer = x >= 100 && x < 300 && y >= 100 && y < 260;
      const bool hole = x >= 160 && x < 240 && y >= 140 && y < 220;
      *coverage.pixel(x, y) = outer && !hole ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(coverage, QStringLiteral("test selection"));
  CHECK(canvas->has_selection());

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("makeWorkPathDialog"));
    CHECK(dialog != nullptr);
    dialog->accept();
  });
  window.findChild<QAction*>(QStringLiteral("pathFromSelectionAction"))->trigger();
  QApplication::processEvents();

  const auto* work = document.work_path();
  CHECK(work != nullptr);
  CHECK(work->path().subpaths.size() == 2);
  const auto& outer_subpath = work->path().subpaths[0];
  const auto& hole_subpath = work->path().subpaths[1];
  CHECK(outer_subpath.op == patchy::PathCombineOp::Add);
  CHECK(hole_subpath.op == patchy::PathCombineOp::Subtract);
  CHECK(outer_subpath.anchors.size() == 4);
  CHECK(hole_subpath.anchors.size() == 4);
  const auto bounds_of = [](const patchy::PathSubpath& subpath) {
    double min_x = 1e9;
    double min_y = 1e9;
    double max_x = -1e9;
    double max_y = -1e9;
    for (const auto& anchor : subpath.anchors) {
      min_x = std::min(min_x, anchor.anchor_x);
      min_y = std::min(min_y, anchor.anchor_y);
      max_x = std::max(max_x, anchor.anchor_x);
      max_y = std::max(max_y, anchor.anchor_y);
    }
    return std::array<double, 4>{min_x, min_y, max_x, max_y};
  };
  const auto outer_bounds = bounds_of(outer_subpath);
  CHECK(std::abs(outer_bounds[0] - 100.0) <= 0.5);
  CHECK(std::abs(outer_bounds[1] - 100.0) <= 0.5);
  CHECK(std::abs(outer_bounds[2] - 300.0) <= 0.5);
  CHECK(std::abs(outer_bounds[3] - 260.0) <= 0.5);
  const auto hole_bounds = bounds_of(hole_subpath);
  CHECK(std::abs(hole_bounds[0] - 160.0) <= 0.5);
  CHECK(std::abs(hole_bounds[1] - 140.0) <= 0.5);
  CHECK(std::abs(hole_bounds[2] - 240.0) <= 0.5);
  CHECK(std::abs(hole_bounds[3] - 220.0) <= 0.5);

  // The work path row is targeted (its outline shows immediately).
  CHECK(canvas->active_document_path().has_value());
  CHECK(*canvas->active_document_path() == work->id());
  CHECK(canvas->panel_path_targeted());
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
      {"ui_pen_hover_shows_context_cursor_badges", ui_pen_hover_shows_context_cursor_badges},
      {"ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws",
       ui_pen_ctrl_drag_direct_selects_committed_anchor_then_draws},
      {"ui_pen_ctrl_drag_moves_in_progress_session_anchor",
       ui_pen_ctrl_drag_moves_in_progress_session_anchor},
      {"ui_pen_session_start_shows_hint_message", ui_pen_session_start_shows_hint_message},
      {"ui_vector_mode_combo_disables_pixels_for_vector_only_tools",
       ui_vector_mode_combo_disables_pixels_for_vector_only_tools},
      {"ui_layer_context_menu_offers_shape_appearance_for_shape_layers",
       ui_layer_context_menu_offers_shape_appearance_for_shape_layers},
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
      {"ui_new_fill_layer_clips_to_targeted_path", ui_new_fill_layer_clips_to_targeted_path},
      {"ui_shape_pattern_fill_uses_custom_library_pattern",
       ui_shape_pattern_fill_uses_custom_library_pattern},
      {"ui_path_edits_refresh_panel_thumbnails", ui_path_edits_refresh_panel_thumbnails},
      {"ui_paths_panel_clipping_path_toggle", ui_paths_panel_clipping_path_toggle},
      {"ui_new_gradient_fill_layer_spans_canvas", ui_new_gradient_fill_layer_spans_canvas},
      {"ui_paths_panel_actions_follow_row_selection", ui_paths_panel_actions_follow_row_selection},
      {"ui_paths_panel_target_shows_overlay_with_any_tool",
       ui_paths_panel_target_shows_overlay_with_any_tool},
      {"ui_shape_layer_auto_targets_path_row_until_dismissed",
       ui_shape_layer_auto_targets_path_row_until_dismissed},
      {"ui_shape_layer_row_shows_vector_badge", ui_shape_layer_row_shows_vector_badge},
      {"ui_paths_panel_ctrl_click_loads_selection", ui_paths_panel_ctrl_click_loads_selection},
      {"ui_paths_panel_duplicate_and_reorder", ui_paths_panel_duplicate_and_reorder},
      {"ui_path_free_transform_moves_scales_and_undoes",
       ui_path_free_transform_moves_scales_and_undoes},
      {"ui_stroke_path_simulate_pressure_tapers", ui_stroke_path_simulate_pressure_tapers},
      {"ui_fill_path_supports_patterns", ui_fill_path_supports_patterns},
      {"ui_make_work_path_from_selection_traces_selection",
       ui_make_work_path_from_selection_traces_selection},
  };
}
