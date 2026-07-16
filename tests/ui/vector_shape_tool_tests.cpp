// Shape-tool vector mode tests: Shape-mode drags create shape layers (with
// combine ops extending them), Path mode populates the work path, Pixels mode
// keeps the legacy raster commit, and the mode rides new sessions.
#include "ui_test_support.hpp"

#include "core/document_path.hpp"
#include "core/vector_shape.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
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
      {"ui_shape_appearance_dialog_commits_and_cancels",
       ui_shape_appearance_dialog_commits_and_cancels},
      {"ui_new_solid_fill_layer_uses_selection_mask", ui_new_solid_fill_layer_uses_selection_mask},
      {"ui_new_gradient_fill_layer_spans_canvas", ui_new_gradient_fill_layer_spans_canvas},
  };
}
