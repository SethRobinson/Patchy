// Trace Image to Shapes coverage (docs/image-trace.md): the Layer-menu
// command drives the dialog into a group of shape layers above a hidden
// source (one undo entry), the dialog renders its preview, and the
// layer.traceToShapes scripting call builds the same group.

#include "core/document.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_engine.hpp"

#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_until;
using patchy::test::ui::require_canvas;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::show_window;

constexpr int kCanvasWidth = 1024;
constexpr int kCanvasHeight = 768;

// Paints the active layer: white everywhere, a red square, and a blue ring.
patchy::LayerId paint_trace_source(patchy::ui::MainWindow& window) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  auto* layer = document.find_layer(*active);
  CHECK(layer != nullptr);
  patchy::PixelBuffer pixels(kCanvasWidth, kCanvasHeight, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kCanvasHeight; ++y) {
    for (std::int32_t x = 0; x < kCanvasWidth; ++x) {
      auto* px = pixels.pixel(x, y);
      std::uint8_t r = 255;
      std::uint8_t g = 255;
      std::uint8_t b = 255;
      if (x >= 100 && x < 300 && y >= 100 && y < 300) {
        r = 220;
        g = 30;
        b = 30;
      }
      const double dx = x - 600.0;
      const double dy = y - 400.0;
      const double d2 = dx * dx + dy * dy;
      if (d2 <= 150.0 * 150.0 && d2 >= 80.0 * 80.0) {
        r = 30;
        g = 40;
        b = 220;
      }
      px[0] = r;
      px[1] = g;
      px[2] = b;
      px[3] = 255;
    }
  }
  patchy::ui::set_layer_pixels_with_bounds(*layer, std::move(pixels),
                                           patchy::Rect::from_size(kCanvasWidth, kCanvasHeight));
  require_canvas(window)->document_changed();
  QApplication::processEvents();
  return *active;
}

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, const QString& name) {
  for (const auto& layer : layers) {
    if (QString::fromStdString(layer.name()) == name) {
      return &layer;
    }
    if (const auto* nested = find_layer_named(layer.children(), name); nested != nullptr) {
      return nested;
    }
  }
  return nullptr;
}

bool group_holds_shape_layers(const patchy::Layer& group, std::size_t expected) {
  if (group.kind() != patchy::LayerKind::Group || group.children().size() != expected) {
    return false;
  }
  for (const auto& child : group.children()) {
    if (!patchy::layer_is_vector_shape(child) || child.vector_shape() == nullptr ||
        child.vector_shape()->path.subpaths.empty() ||
        child.vector_shape()->fill.kind != patchy::VectorFillKind::Solid) {
      return false;
    }
  }
  return true;
}

void ui_trace_image_to_shapes_creates_group_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto source_id = paint_trace_source(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto source_name = QString::fromStdString(document.find_layer(source_id)->name());
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool saw_preview = false;
  const std::function<void(int)> drive_dialog = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          drive_dialog(attempts - 1);
        }
        return;
      }
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("imageTraceModeCombo"));
      auto* colors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceColorsSpin"));
      auto* noise = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceNoiseSpin"));
      auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageTraceMethodCombo"));
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("imageTracePresetCombo"));
      auto* info = dialog->findChild<QLabel*>(QStringLiteral("imageTracePreviewInfo"));
      auto* preview = dialog->findChild<QWidget*>(QStringLiteral("imageTracePreview"));
      CHECK(mode != nullptr && colors != nullptr && noise != nullptr && method != nullptr && preset != nullptr &&
            info != nullptr && preview != nullptr);
      mode->setCurrentIndex(mode->findData(0));  // Color
      colors->setValue(4);
      noise->setValue(4);
      method->setCurrentIndex(method->findData(0));  // Abutting
      // Hand-edited settings show as the Custom preset.
      CHECK(preset->currentIndex() == 0);
      // The debounced preview lands with the layer/anchor summary.
      saw_preview = process_events_until(
          [info] { return info->text().contains(QStringLiteral("shape layer")); }, 15000);
      save_widget_artifact("ui_image_trace_dialog", *dialog);
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("imageTraceButtons"));
      CHECK(buttons != nullptr);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
  };
  drive_dialog(5);
  auto* action = window.findChild<QAction*>(QStringLiteral("layerTraceImageAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();
  CHECK(saw_preview);

  // One group above the (now hidden) source, holding one solid shape layer
  // per color: white, red, blue.
  const auto* group = find_layer_named(document.layers(), QStringLiteral("Traced %1").arg(source_name));
  CHECK(group != nullptr);
  CHECK(group_holds_shape_layers(*group, 3));
  CHECK(find_layer_named(group->children(), QStringLiteral("#DC1E1E")) != nullptr);
  CHECK(find_layer_named(group->children(), QStringLiteral("#1E28DC")) != nullptr);
  CHECK(find_layer_named(group->children(), QStringLiteral("#FFFFFF")) != nullptr);
  const auto* source = document.find_layer(source_id);
  CHECK(source != nullptr);
  CHECK(!source->visible());
  CHECK(document.active_layer_id() == group->id());
  // The blue ring keeps its hole (an Abutting Subtract group).
  const auto* blue = find_layer_named(group->children(), QStringLiteral("#1E28DC"));
  bool has_hole = false;
  for (const auto& subpath : blue->vector_shape()->path.subpaths) {
    has_hole = has_hole || subpath.op == patchy::PathCombineOp::Subtract;
  }
  CHECK(has_hole);
  // Exactly one history entry; undo restores the source and removes the group.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  auto& restored = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(find_layer_named(restored.layers(), QStringLiteral("Traced %1").arg(source_name)) == nullptr);
  const auto* restored_source = restored.find_layer(source_id);
  CHECK(restored_source != nullptr);
  CHECK(restored_source->visible());
}

void ui_script_trace_to_shapes_returns_group() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto source_id = paint_trace_source(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("trace-test");
  (void)host.run_source(QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.activeLayer;
    var group = layer.traceToShapes({mode: 'color', colors: 4, noise: 4, method: 'overlapping'});
    console.log('group:' + group.isGroup + ':' + group.children.length + ':' + layer.visible);
    var names = [];
    for (var i = 0; i < group.children.length; ++i) { names.push(group.children[i].name); }
    console.log('names:' + names.sort().join(','));
    try {
      layer.traceToShapes({bogus: 1});
      console.log('no-throw');
    } catch (error) {
      console.log('threw:' + error.message);
    }
  )JS"),
                         std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  bool saw_group = false;
  bool saw_names = false;
  bool saw_throw = false;
  for (const auto& line : host.message_backlog()) {
    saw_group = saw_group || line.contains(QStringLiteral("group:true:4:false"));
    saw_names = saw_names || line.contains(QStringLiteral("names:#1E28DC,#DC1E1E,#FFFFFF,#FFFFFF"));
    saw_throw = saw_throw || line.contains(QStringLiteral("threw:"));
  }
  CHECK(saw_group);
  CHECK(saw_names);
  CHECK(saw_throw);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* source = document.find_layer(source_id);
  CHECK(source != nullptr && !source->visible());
  const auto* group = find_layer_named(document.layers(), QStringLiteral("Traced %1").arg(QString::fromStdString(source->name())));
  CHECK(group != nullptr);
  // Overlapping: the white background (depth 0), the red square and the blue
  // ring (depth 1), and the white inside the ring (depth 2) are four stacked
  // layers painted without holes, so no Subtract group exists anywhere.
  CHECK(group_holds_shape_layers(*group, 4));
  for (const auto& child : group->children()) {
    for (const auto& subpath : child.vector_shape()->path.subpaths) {
      CHECK(subpath.op == patchy::PathCombineOp::Add);
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> image_trace_ui_tests() {
  return {
      {"ui_trace_image_to_shapes_creates_group_and_undoes", ui_trace_image_to_shapes_creates_group_and_undoes},
      {"ui_script_trace_to_shapes_returns_group", ui_script_trace_to_shapes_returns_group},
  };
}
