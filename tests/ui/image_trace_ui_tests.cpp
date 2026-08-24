// Trace Image to Shapes coverage (docs/image-trace.md): the Layer-menu
// command drives the dialog into a group of shape layers above a hidden
// source (one undo entry), the dialog renders its preview, and the
// layer.traceToShapes scripting call builds the same group.

#include "core/document.hpp"
#include "core/vector_shape.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_trace_dialog.hpp"
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
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_until;
using patchy::test::ui::require_action;
using patchy::test::ui::require_canvas;
using patchy::test::ui::require_hotkey_action;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::show_window;
using patchy::test::ui::solid_pixels;

constexpr const char* kUserPresetsKey = "imageTrace/userPresets";

// Selects the document-space rectangle (hard edged) on the active canvas.
void select_document_rect(patchy::ui::CanvasWidget& canvas, int width, int height, QRect rect) {
  patchy::PixelBuffer selection(width, height, patchy::PixelFormat::gray8());
  selection.clear(0U);
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      selection.pixel(x, y)[0] = 255U;
    }
  }
  canvas.replace_selection_from_grayscale(selection, QStringLiteral("Selection"));
  QApplication::processEvents();
  CHECK(canvas.has_selection());
}

// Runs `act` on the top-level dialog named `object_name` once it opens
// (armed right before the call that opens it: zero-delay timers fire in the
// FIRST nested loop that runs). A miss is recorded in `failures` instead of
// thrown: a throw across a modal exec loop aborts the suite, and the record
// is checked once the outer dialog has closed.
template <typename Act>
void when_dialog_opens(QStringList& failures, const char* object_name, Act act) {
  QTimer::singleShot(0, [&failures, object_name, act] {
    auto* dialog = find_top_level_dialog(QLatin1String(object_name));
    if (dialog == nullptr) {
      failures << QStringLiteral("dialog not found: %1").arg(QLatin1String(object_name));
      return;
    }
    act(*dialog);
  });
}

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
      auto* spinner = dialog->findChild<QWidget*>(QStringLiteral("imageTraceBusySpinner"));
      auto* warning = dialog->findChild<QLabel*>(QStringLiteral("imageTraceSizeWarningLabel"));
      auto* selection_note = dialog->findChild<QLabel*>(QStringLiteral("imageTraceSelectionNoteLabel"));
      auto* colors_slider = dialog->findChild<QSlider*>(QStringLiteral("imageTraceColorsSlider"));
      CHECK(spinner != nullptr && warning != nullptr && selection_note != nullptr && colors_slider != nullptr);
      CHECK(!selection_note->isVisible());  // no selection in this test
      mode->setCurrentIndex(mode->findData(0));  // Color
      colors->setValue(4);
      // The slider mirrors the spin box both ways.
      CHECK(colors_slider->value() == 4);
      colors_slider->setValue(6);
      CHECK(colors->value() == 6);
      colors->setValue(4);
      noise->setValue(4);
      method->setCurrentIndex(method->findData(0));  // Abutting
      // Hand-edited settings show as the Custom preset.
      CHECK(preset->currentIndex() == 0);
      // The debounced preview lands with the layer/anchor summary; the busy
      // spinner shows while the worker runs and hides with the result.
      bool saw_busy = spinner->isVisible();
      saw_preview = process_events_until(
          [&] {
            saw_busy = saw_busy || spinner->isVisible();
            return info->text().contains(QStringLiteral("shape layer"));
          },
          15000);
      CHECK(saw_busy);
      CHECK(!spinner->isVisible());
      CHECK(!warning->isVisible());  // three shapes, a few dozen anchors
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
  // The frontmost traced shape is active (not the group), so the pen and
  // path tools can edit the trace at once.
  CHECK(!group->children().empty());
  CHECK(document.active_layer_id() == group->children().back().id());
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

void ui_image_trace_large_result_thresholds() {
  CHECK(!patchy::ui::image_trace_result_is_large(1999, 19999));
  CHECK(patchy::ui::image_trace_result_is_large(2000, 0));
  CHECK(patchy::ui::image_trace_result_is_large(0, 20000));
  CHECK(!patchy::ui::image_trace_result_is_large(0, 0));
}

void ui_image_trace_user_presets_round_trip() {
  patchy::ui::ImageTraceUserPreset first;
  first.name = QStringLiteral("Poster");
  first.options.mode = patchy::ImageTraceOptions::Mode::Grayscale;
  first.options.colors = 5;
  first.options.paths = 70;
  first.options.corners = 40;
  first.options.noise = 12;
  first.options.smoothing = 3;
  first.options.max_anchors = 2500;
  first.options.method = patchy::ImageTraceOptions::Method::Overlapping;
  first.options.snap_curves_to_lines = true;
  first.options.ignore_white = true;
  patchy::ui::ImageTraceUserPreset second;
  second.name = QStringLiteral("Ink");
  second.options.mode = patchy::ImageTraceOptions::Mode::BlackAndWhite;
  second.options.threshold = 90;
  const auto json = patchy::ui::serialize_image_trace_user_presets({first, second});
  const auto restored = patchy::ui::deserialize_image_trace_user_presets(json);
  CHECK(restored.size() == 2);
  CHECK(restored[0].name == first.name && restored[0].options == first.options);
  CHECK(restored[1].name == second.name && restored[1].options == second.options);

  // Malformed elements are skipped one at a time; the neighbors survive.
  const auto partial = patchy::ui::deserialize_image_trace_user_presets(
      QByteArray("[{\"name\":\"Good\",\"colors\":9}, 7, {\"colors\":3}, {\"name\":\"good\",\"colors\":2}]"));
  CHECK(partial.size() == 1);
  CHECK(partial[0].name == QStringLiteral("Good") && partial[0].options.colors == 9);
  // Presets saved before the smoothing/budget options load with the defaults,
  // and hand-edited out-of-range values clamp.
  CHECK(partial[0].options.smoothing == 0 && partial[0].options.max_anchors == 0);
  const auto clamped = patchy::ui::deserialize_image_trace_user_presets(
      QByteArray("[{\"name\":\"Wild\",\"smoothing\":99,\"maxAnchors\":-5}]"));
  CHECK(clamped.size() == 1);
  CHECK(clamped[0].options.smoothing == patchy::ImageTraceOptions::kMaxSmoothing);
  CHECK(clamped[0].options.max_anchors == 0);
  CHECK(patchy::ui::deserialize_image_trace_user_presets(QByteArray("not json")).empty());

  auto settings = patchy::ui::app_settings();
  settings.remove(QLatin1String(kUserPresetsKey));
  patchy::ui::save_image_trace_user_presets({first});
  CHECK(patchy::ui::app_settings().contains(QLatin1String(kUserPresetsKey)));
  const auto loaded = patchy::ui::load_image_trace_user_presets();
  CHECK(loaded.size() == 1 && loaded[0].name == first.name && loaded[0].options == first.options);
  patchy::ui::save_image_trace_user_presets({});
  CHECK(!patchy::ui::app_settings().contains(QLatin1String(kUserPresetsKey)));
}

void ui_trace_image_dialog_saves_and_deletes_user_preset() {
  patchy::ui::app_settings().remove(QLatin1String(kUserPresetsKey));
  patchy::ui::MainWindow window;
  show_window(window);
  paint_trace_source(window);

  bool drove = false;
  QStringList failures;
  const auto expect = [&failures](bool condition, const char* what) {
    if (!condition) {
      failures << QLatin1String(what);
    }
  };
  const std::function<void(int)> drive_dialog = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          drive_dialog(attempts - 1);
        }
        return;
      }
      drove = true;
      auto* colors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceColorsSpin"));
      auto* noise = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceNoiseSpin"));
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("imageTracePresetCombo"));
      auto* save = dialog->findChild<QToolButton*>(QStringLiteral("imageTraceSavePresetButton"));
      auto* remove = dialog->findChild<QToolButton*>(QStringLiteral("imageTraceDeletePresetButton"));
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("imageTraceButtons"));
      if (colors == nullptr || noise == nullptr || preset == nullptr || save == nullptr || remove == nullptr ||
          buttons == nullptr) {
        failures << QStringLiteral("dialog controls missing");
        dialog->reject();
        return;
      }
      colors->setValue(4);
      noise->setValue(4);
      expect(preset->currentIndex() == 0, "hand-edited settings show Custom");
      expect(!remove->isEnabled(), "Delete disabled for Custom");

      // Save... prompts for a name; the new row is selected and deletable.
      when_dialog_opens(failures, "imageTraceSavePresetNameDialog", [&expect](QDialog& prompt) {
        auto* edit = prompt.findChild<QLineEdit*>();
        expect(edit != nullptr, "name prompt has a line edit");
        if (edit != nullptr) {
          edit->setText(QStringLiteral("Test Preset"));
        }
        prompt.accept();
      });
      save->click();
      QApplication::processEvents();
      expect(preset->currentText() == QStringLiteral("Test Preset"), "saved preset selected");
      expect(remove->isEnabled(), "Delete enabled for the user preset");
      expect(patchy::ui::app_settings().value(QLatin1String(kUserPresetsKey)).toByteArray().contains("Test Preset"),
             "preset persisted");

      // Editing drops back to Custom; re-picking the row restores the values.
      noise->setValue(5);
      expect(preset->currentIndex() == 0, "edit returns to Custom");
      expect(!remove->isEnabled(), "Delete disabled again");
      const auto row = preset->findText(QStringLiteral("Test Preset"));
      expect(row > 0, "user preset row exists");
      if (row > 0) {
        preset->setCurrentIndex(row);
        emit preset->activated(row);
        QApplication::processEvents();
      }
      expect(noise->value() == 4, "re-picking restores noise");
      expect(remove->isEnabled(), "Delete enabled after re-pick");

      // A built-in name is refused (the message box opens after the prompt
      // closes, so its finder is armed from inside the prompt's callback).
      when_dialog_opens(failures, "imageTraceSavePresetNameDialog", [&failures](QDialog& prompt) {
        if (auto* edit = prompt.findChild<QLineEdit*>(); edit != nullptr) {
          edit->setText(QStringLiteral("3 Colors"));
        }
        when_dialog_opens(failures, "imageTracePresetNameMessageBox", [](QDialog& box) { box.reject(); });
        prompt.accept();
      });
      save->click();
      QApplication::processEvents();
      expect(patchy::ui::load_image_trace_user_presets().size() == 1, "built-in name refused");

      // Delete asks, then removes the row and the setting.
      when_dialog_opens(failures, "imageTraceDeletePresetMessageBox", [&expect](QDialog& box) {
        auto* message = qobject_cast<QMessageBox*>(&box);
        expect(message != nullptr, "delete confirmation is a message box");
        if (message != nullptr) {
          message->button(QMessageBox::Yes)->click();
        } else {
          box.reject();
        }
      });
      remove->click();
      QApplication::processEvents();
      expect(preset->findText(QStringLiteral("Test Preset")) < 0, "deleted row gone");
      expect(preset->currentIndex() == 0, "Custom after delete");
      expect(!patchy::ui::app_settings().contains(QLatin1String(kUserPresetsKey)), "setting removed");
      buttons->button(QDialogButtonBox::Cancel)->click();
    });
  };
  drive_dialog(5);
  require_action(window, "layerTraceImageAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  for (const auto& failure : failures) {
    std::fprintf(stderr, "  preset dialog: %s\n", qPrintable(failure));
  }
  CHECK(failures.isEmpty());
  patchy::ui::app_settings().remove(QLatin1String(kUserPresetsKey));
}

void ui_layer_context_menu_offers_trace_image_to_shapes() {
  patchy::ui::MainWindow window;
  show_window(window);
  paint_trace_source(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() > 0);

  bool saw_menu = false;
  QStringList action_names;
  int poll_attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++poll_attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->objectName() == QStringLiteral("layerContextMenu") && menu->isVisible()) {
        saw_menu = true;
        for (auto* action : menu->actions()) {
          action_names << action->objectName();
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
  CHECK(saw_menu);
  const auto trace_index = action_names.indexOf(QStringLiteral("layerTraceImageAction"));
  const auto rasterize_index = action_names.indexOf(QStringLiteral("layerRasterizeAction"));
  CHECK(trace_index >= 0);
  CHECK(rasterize_index >= 0 && trace_index > rasterize_index);

  // The command also has a default shortcut now.
  auto* action = require_hotkey_action(window, QStringLiteral("layer.trace_image_to_shapes"));
  CHECK(action != nullptr);
  CHECK(action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_T));
}

void ui_pixels_limited_to_selection_zeroes_alpha_outside() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(100, 80, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Layer", solid_pixels(100, 80, patchy::PixelFormat::rgba8(), QColor(10, 20, 30, 255)));
  window.add_document_session(std::move(built), QStringLiteral("Limited"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* layer = std::as_const(document).find_layer(*active);
  CHECK(layer != nullptr);

  // Hard rectangle plus a half-covered band: coverage below 50% is outside.
  patchy::PixelBuffer selection(100, 80, patchy::PixelFormat::gray8());
  selection.clear(0U);
  for (int y = 10; y <= 40; ++y) {
    for (int x = 10; x <= 40; ++x) {
      selection.pixel(x, y)[0] = 255U;
    }
    for (int x = 50; x <= 60; ++x) {
      selection.pixel(x, y)[0] = 100U;
    }
  }
  canvas->replace_selection_from_grayscale(selection, QStringLiteral("Selection"));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  const auto limited = patchy::ui::pixels_limited_to_selection(*canvas, layer->pixels(), layer->bounds());
  CHECK(limited.width() == 100 && limited.height() == 80);
  CHECK(limited.format().channels == 4);
  CHECK(limited.pixel(20, 20)[3] == 255 && limited.pixel(20, 20)[0] == 10);
  CHECK(limited.pixel(5, 5)[3] == 0);
  CHECK(limited.pixel(55, 20)[3] == 0);
  CHECK(limited.pixel(70, 70)[3] == 0);
}

void ui_trace_image_to_shapes_limits_to_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto source_id = paint_trace_source(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto source_name = QString::fromStdString(document.find_layer(source_id)->name());
  // The selection holds the red square and some white; the blue ring is outside.
  select_document_rect(*require_canvas(window), kCanvasWidth, kCanvasHeight, QRect(50, 50, 300, 300));

  bool saw_note = false;
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
      auto* info = dialog->findChild<QLabel*>(QStringLiteral("imageTracePreviewInfo"));
      auto* note = dialog->findChild<QLabel*>(QStringLiteral("imageTraceSelectionNoteLabel"));
      CHECK(mode != nullptr && colors != nullptr && noise != nullptr && method != nullptr && info != nullptr &&
            note != nullptr);
      saw_note = note->isVisible();
      mode->setCurrentIndex(mode->findData(0));
      colors->setValue(4);
      noise->setValue(4);
      method->setCurrentIndex(method->findData(0));
      CHECK(process_events_until([info] { return info->text().contains(QStringLiteral("shape layer")); }, 15000));
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("imageTraceButtons"));
      CHECK(buttons != nullptr);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
  };
  drive_dialog(5);
  require_action(window, "layerTraceImageAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_note);

  const auto* group = find_layer_named(document.layers(), QStringLiteral("Traced %1").arg(source_name));
  CHECK(group != nullptr);
  CHECK(group_holds_shape_layers(*group, 2));
  CHECK(find_layer_named(group->children(), QStringLiteral("#DC1E1E")) != nullptr);
  CHECK(find_layer_named(group->children(), QStringLiteral("#FFFFFF")) != nullptr);
  CHECK(find_layer_named(group->children(), QStringLiteral("#1E28DC")) == nullptr);
}

void ui_script_trace_to_shapes_uses_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  paint_trace_source(window);
  select_document_rect(*require_canvas(window), kCanvasWidth, kCanvasHeight, QRect(50, 50, 300, 300));
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("trace-selection-test");
  (void)host.run_source(QStringLiteral(R"JS(
    var group = app.activeDocument.activeLayer.traceToShapes({mode: 'color', colors: 4, noise: 4});
    var names = [];
    for (var i = 0; i < group.children.length; ++i) { names.push(group.children[i].name); }
    console.log('names:' + names.sort().join(','));
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
  bool saw_names = false;
  for (const auto& line : host.message_backlog()) {
    saw_names = saw_names || line.contains(QStringLiteral("names:#DC1E1E,#FFFFFF"));
    CHECK(!line.contains(QStringLiteral("#1E28DC")));
  }
  CHECK(saw_names);
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
    var group = layer.traceToShapes({mode: 'color', colors: 4, noise: 4, method: 'overlapping',
                                     smoothing: 0, maxAnchors: 0});
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

void ui_image_trace_dialog_offers_smoothing_and_anchor_budget() {
  patchy::ui::MainWindow window;
  show_window(window);
  paint_trace_source(window);

  bool drove = false;
  const std::function<void(int)> drive_dialog = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          drive_dialog(attempts - 1);
        }
        return;
      }
      drove = true;
      auto* colors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceColorsSpin"));
      auto* smoothing = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceSmoothingSpin"));
      auto* smoothing_slider = dialog->findChild<QSlider*>(QStringLiteral("imageTraceSmoothingSlider"));
      auto* max_anchors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceMaxAnchorsSpin"));
      auto* max_anchors_slider = dialog->findChild<QSlider*>(QStringLiteral("imageTraceMaxAnchorsSlider"));
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("imageTraceModeCombo"));
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("imageTracePresetCombo"));
      CHECK(colors != nullptr && smoothing != nullptr && smoothing_slider != nullptr && max_anchors != nullptr &&
            max_anchors_slider != nullptr && mode != nullptr && preset != nullptr);
      // The color range follows the raised core cap.
      CHECK(colors->maximum() == 256);
      CHECK(colors->maximum() == patchy::ImageTraceOptions::kMaxColors);
      CHECK(smoothing->minimum() == 0 && smoothing->maximum() == patchy::ImageTraceOptions::kMaxSmoothing);
      CHECK(smoothing->suffix() == QStringLiteral(" px"));
      CHECK(max_anchors->minimum() == 0 && max_anchors->maximum() == 20000);
      CHECK(!max_anchors->specialValueText().isEmpty());  // 0 renders as Off
      // Two-way slider/spin mirroring for both new rows.
      smoothing->setValue(3);
      CHECK(smoothing_slider->value() == 3);
      smoothing_slider->setValue(5);
      CHECK(smoothing->value() == 5);
      max_anchors->setValue(4000);
      CHECK(max_anchors_slider->value() == 4000);
      // Hand-edited settings show as the Custom preset.
      CHECK(preset->currentIndex() == 0);
      // Both rows stay enabled in every mode.
      for (const int mode_value : {0, 1, 2}) {
        mode->setCurrentIndex(mode->findData(mode_value));
        CHECK(smoothing->parentWidget()->isEnabled());
        CHECK(max_anchors->parentWidget()->isEnabled());
      }
      // The Photo (Maximum) preset writes the full palette and light denoise.
      const auto photo_maximum = preset->findText(QStringLiteral("Photo (Maximum)"));
      CHECK(photo_maximum > 0);
      preset->setCurrentIndex(photo_maximum);
      emit preset->activated(photo_maximum);
      CHECK(colors->value() == 256);
      CHECK(smoothing->value() == 2);
      CHECK(max_anchors->value() == 0);
      save_widget_artifact("ui_image_trace_dialog_budget", *dialog);
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("imageTraceButtons"));
      CHECK(buttons != nullptr);
      buttons->button(QDialogButtonBox::Cancel)->click();
    });
  };
  drive_dialog(5);
  auto* action = window.findChild<QAction*>(QStringLiteral("layerTraceImageAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents();
  CHECK(drove);
}

void ui_image_trace_new_options_persist_in_settings() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imageTrace/smoothing"));
    settings.remove(QStringLiteral("imageTrace/maxAnchors"));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto source_id = paint_trace_source(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  bool drove = false;
  const std::function<void(int)> drive_dialog = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          drive_dialog(attempts - 1);
        }
        return;
      }
      drove = true;
      auto* info = dialog->findChild<QLabel*>(QStringLiteral("imageTracePreviewInfo"));
      auto* smoothing = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceSmoothingSpin"));
      auto* max_anchors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceMaxAnchorsSpin"));
      CHECK(info != nullptr && smoothing != nullptr && max_anchors != nullptr);
      smoothing->setValue(2);
      max_anchors->setValue(5000);
      (void)process_events_until([&] { return info->text().contains(QStringLiteral("shape layer")); }, 15000);
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
  CHECK(drove);
  // The exact key spellings are persisted identifiers.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imageTrace/smoothing")).toInt() == 2);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imageTrace/maxAnchors")).toInt() == 5000);

  // Reopening seeds the dialog from the saved values.
  document.set_active_layer(source_id);
  bool restored = false;
  const std::function<void(int)> reopen = [&](int attempts) {
    QTimer::singleShot(0, [&, attempts] {
      auto* dialog = find_top_level_dialog(QStringLiteral("imageTraceDialog"));
      if (dialog == nullptr) {
        if (attempts > 0) {
          reopen(attempts - 1);
        }
        return;
      }
      auto* smoothing = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceSmoothingSpin"));
      auto* max_anchors = dialog->findChild<QSpinBox*>(QStringLiteral("imageTraceMaxAnchorsSpin"));
      restored = smoothing != nullptr && smoothing->value() == 2 && max_anchors != nullptr &&
                 max_anchors->value() == 5000;
      auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("imageTraceButtons"));
      if (buttons != nullptr) {
        buttons->button(QDialogButtonBox::Cancel)->click();
      }
    });
  };
  reopen(5);
  action->trigger();
  QApplication::processEvents();
  CHECK(restored);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("imageTrace/smoothing"));
  settings.remove(QStringLiteral("imageTrace/maxAnchors"));
}

}  // namespace

std::vector<patchy::test::TestCase> image_trace_ui_tests() {
  return {
      {"ui_trace_image_to_shapes_creates_group_and_undoes", ui_trace_image_to_shapes_creates_group_and_undoes},
      {"ui_script_trace_to_shapes_returns_group", ui_script_trace_to_shapes_returns_group},
      {"ui_image_trace_large_result_thresholds", ui_image_trace_large_result_thresholds},
      {"ui_image_trace_user_presets_round_trip", ui_image_trace_user_presets_round_trip},
      {"ui_trace_image_dialog_saves_and_deletes_user_preset",
       ui_trace_image_dialog_saves_and_deletes_user_preset},
      {"ui_image_trace_dialog_offers_smoothing_and_anchor_budget",
       ui_image_trace_dialog_offers_smoothing_and_anchor_budget},
      {"ui_image_trace_new_options_persist_in_settings", ui_image_trace_new_options_persist_in_settings},
      {"ui_layer_context_menu_offers_trace_image_to_shapes",
       ui_layer_context_menu_offers_trace_image_to_shapes},
      {"ui_pixels_limited_to_selection_zeroes_alpha_outside",
       ui_pixels_limited_to_selection_zeroes_alpha_outside},
      {"ui_trace_image_to_shapes_limits_to_selection", ui_trace_image_to_shapes_limits_to_selection},
      {"ui_script_trace_to_shapes_uses_selection", ui_script_trace_to_shapes_uses_selection},
  };
}
