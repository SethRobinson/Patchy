// Shape-tool vector mode tests: Shape-mode drags create shape layers (with
// combine ops extending them), Path mode populates the work path, Pixels mode
// keeps the legacy raster commit, and the mode rides new sessions.
#include "ui_test_support.hpp"

#include "core/document_path.hpp"
#include "core/vector_shape.hpp"

#include <QComboBox>
#include <QSpinBox>

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
  };
}
