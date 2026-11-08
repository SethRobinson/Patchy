// Commands that need a target layer warn when none is selected (desk card no-layer-warnings;
// docs/layer-panel-selection.md, "Deselected state"): a selection can exist with no layer
// selected (Type Mask, Select > Deselect Layers), and Stroke, Alt+Backspace and the rest used
// to return silently. Each one now shows "<command>: select a layer first" in the red status
// bar and leaves the document, its layers and its history alone.
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "test_harness.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QStatusBar>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::test::ui::require_canvas;
using patchy::test::ui::require_hotkey_action;
using patchy::test::ui::send_mouse;
using patchy::ui::CanvasWidget;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

const QString kSuffix = QStringLiteral(": select a layer first");

patchy::PixelBuffer solid(int width, int height, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = red;
      pixel[1] = green;
      pixel[2] = blue;
      pixel[3] = 255U;
    }
  }
  return pixels;
}

// Everything a command could change: the layer tree (ids, names, kinds, bounds, locks,
// blend modes) and every layer's pixels.
struct DocumentFingerprint {
  std::vector<std::string> rows;
  std::vector<std::vector<std::uint8_t>> pixels;
  bool operator==(const DocumentFingerprint&) const = default;
};

void fingerprint_layers(const std::vector<patchy::Layer>& layers, DocumentFingerprint& into) {
  for (const auto& layer : layers) {
    const auto bounds = layer.bounds();
    char row[256];
    std::snprintf(row, sizeof(row), "%llu|%d|%d|%d|%d|%d|%d|%d|%d", static_cast<unsigned long long>(layer.id()),
                  static_cast<int>(layer.kind()), bounds.x, bounds.y, bounds.width, bounds.height,
                  static_cast<int>(layer.blend_mode()), static_cast<int>(layer.clipped()),
                  static_cast<int>(layer.mask().has_value()));
    into.rows.push_back(std::string(row) + "|" + layer.name());
    const auto data = layer.pixels().data();
    into.pixels.emplace_back(data.begin(), data.end());
    fingerprint_layers(layer.children(), into);
  }
}

DocumentFingerprint fingerprint(const patchy::Document& document) {
  DocumentFingerprint result;
  fingerprint_layers(document.layers(), result);
  return result;
}

struct NoLayerFixture {
  patchy::test::ui::ShownWindowFixture shown;
  MainWindow& window{shown.window};
  CanvasWidget* canvas{nullptr};
  bool modal_opened{false};
  QTimer modal_guard;

  NoLayerFixture() {
    // Two layers, so no command can fall back to "the only layer" (select_only_layer_if_none_active).
    patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Bottom", solid(160, 120, 40, 120, 200));
    document.add_pixel_layer("Top", solid(160, 120, 220, 60, 30));
    window.add_document_session(std::move(document), QStringLiteral("No Layer"));
    QApplication::processEvents();
    canvas = require_canvas(window);
    canvas->set_zoom(1.0);
    // A selection (Type Mask makes one with no layer selected; a rectangle does too).
    const auto& doc = MainWindowTestAccess::document(window);
    patchy::PixelBuffer coverage(doc.width(), doc.height(), patchy::PixelFormat::gray8());
    for (int y = 20; y < 80; ++y) {
      for (int x = 30; x < 110; ++x) {
        *coverage.pixel(x, y) = 255U;
      }
    }
    canvas->apply_grayscale_to_selection(coverage);
    require_hotkey_action(window, QStringLiteral("select.deselect_layers"))->trigger();
    QApplication::processEvents();
    CHECK(!MainWindowTestAccess::document(window).active_layer_id().has_value());
    CHECK(canvas->has_selection());
    // A command that got past its layer check would open its dialog: close it and fail
    // instead of parking the run in a modal loop.
    QObject::connect(&modal_guard, &QTimer::timeout, [this] {
      if (auto* modal = QApplication::activeModalWidget(); modal != nullptr) {
        modal_opened = true;
        if (auto* dialog = qobject_cast<QDialog*>(modal); dialog != nullptr) {
          dialog->reject();
        } else {
          modal->close();
        }
      }
    });
    modal_guard.start(50);
  }

  QString message() { return window.statusBar()->currentMessage(); }
};

// Every command in the list is enabled with no layer selected (a hotkey fires it) and
// refuses with the shared message, changing nothing.
void ui_no_layer_commands_warn_and_change_nothing() {
  NoLayerFixture fixture;
  auto& window = fixture.window;
  const auto& document = MainWindowTestAccess::document(window);
  const auto before = fingerprint(document);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  struct Command {
    const char* id;
    const char* name;
  };
  const std::vector<Command> commands{
      {"edit.stroke_selection", "Stroke Selection"},
      {"layer.fill", "Fill"},             // Alt+Backspace
      {"layer.fill_background", "Fill"},  // Ctrl+Backspace
      {"edit.fill", "Fill"},              // Shift+Backspace dialog
      {"layer.clear", "Clear"},           // Delete
      {"layer.via_copy", "Layer via Copy"},
      {"layer.via_cut", "Layer via Cut"},
      {"layer.duplicate", "Duplicate Layer"},
      {"layer.delete", "Delete Layer"},
      {"layer.flip_horizontal", "Flip Horizontal"},
      {"layer.flip_vertical", "Flip Vertical"},
      {"layer.bring_to_front", "Arrange"},
      {"layer.send_to_back", "Arrange"},
      {"layer.toggle_clipping_mask", "Create Clipping Mask"},
      {"layer.lock_transparent_pixels", "Lock Transparent Pixels"},
      {"image.levels", "Levels"},
      {"image.curves", "Curves"},
      {"image.auto_all", "Auto All"},
      {"edit.remove_object", "Remove Object"},
  };
  for (const auto& command : commands) {
    auto* action = require_hotkey_action(window, QString::fromLatin1(command.id));
    if (action == nullptr) {
      continue;
    }
    window.statusBar()->clearMessage();
    if (!action->isEnabled()) {
      std::fprintf(stderr, "[INFO] %s is disabled with no layer selected\n", command.id);
      continue;
    }
    action->trigger();
    QApplication::processEvents();
    const auto expected = QString::fromLatin1(command.name) + kSuffix;
    if (fixture.message() != expected) {
      std::fprintf(stderr, "[FAIL-DETAIL] %s: status \"%s\", expected \"%s\"\n", command.id,
                   fixture.message().toUtf8().constData(), expected.toUtf8().constData());
    }
    CHECK(fixture.message() == expected);
    CHECK(!fixture.modal_opened);
    CHECK(!document.active_layer_id().has_value());
    CHECK(fingerprint(document) == before);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  }
  CHECK(fixture.canvas->has_selection());
}

// A painting tool's press on the canvas names the tool.
void ui_no_layer_brush_press_names_the_tool() {
  NoLayerFixture fixture;
  auto& window = fixture.window;
  auto& canvas = *fixture.canvas;
  const auto& document = MainWindowTestAccess::document(window);
  const auto before = fingerprint(document);
  const auto undo_before = MainWindowTestAccess::active_session_undo_depth(window);
  require_hotkey_action(window, QStringLiteral("tools.brush"))->trigger();
  QApplication::processEvents();
  window.statusBar()->clearMessage();
  const auto at = canvas.widget_position_for_document_point(QPoint(60, 50));
  send_mouse(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(canvas, QEvent::MouseMove, at + QPoint(12, 6), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, at + QPoint(12, 6), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(fixture.message() == QStringLiteral("Brush") + kSuffix);
  CHECK(fingerprint(document) == before);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
}

// Review capture for the desk card: Stroke with a selection and no layer selected.
void ui_no_layer_warning_review_capture() {
  NoLayerFixture fixture;
  auto& window = fixture.window;
  window.resize(1100, 720);
  QApplication::processEvents();
  require_hotkey_action(window, QStringLiteral("edit.stroke_selection"))->trigger();
  QApplication::processEvents();
  CHECK(fixture.message() == QStringLiteral("Stroke Selection") + kSuffix);
  patchy::test::ui::save_widget_artifact("ui_no_layer_warning_stroke", window);
}

}  // namespace

std::vector<patchy::test::TestCase> no_layer_warning_tests() {
  return {
      {"ui_no_layer_commands_warn_and_change_nothing", ui_no_layer_commands_warn_and_change_nothing},
      {"ui_no_layer_brush_press_names_the_tool", ui_no_layer_brush_press_names_the_tool},
      {"ui_no_layer_warning_review_capture", ui_no_layer_warning_review_capture},
  };
}
