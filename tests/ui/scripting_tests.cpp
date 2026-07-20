// JS scripting system coverage (docs/scripting.md): engine mutations ride ONE
// undo entry per run, wrappers re-resolve by id (stale wrappers throw instead
// of crashing), pixel access round-trips and honors the palette-mode snap,
// timers keep a run alive, the watchdog interrupts runaway loops, console
// output and error line numbers reach the sink, the CLI output-file contract
// holds, and the editor dialog / script canvas window render.

#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/palette.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include "ui/script_engine.hpp"

#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::show_window;

patchy::ui::ScriptEngineHost& start_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("test-script");
  (void)host.run_source(source, std::move(options));
  return host;
}

void wait_for_run_end(patchy::ui::ScriptEngineHost& host, int timeout_ms = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  // One extra turn so the coalesced refresh flush lands.
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
}

// Runs to completion; CHECKs that the run ended and reports whether it ended clean.
bool run_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = start_script(window, source);
  wait_for_run_end(host);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

bool backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

const patchy::Layer* layer_named(const patchy::Document& document, const char* name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

void ui_script_mutations_ride_single_undo_entry() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Scripted');
    layer.fill('#ff4000');
    layer.opacity = 40;
    layer.blendMode = 'multiply';
    layer.moveTo(5, 7);
    console.log('mutated');
  )JS")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = layer_named(document, "Scripted");
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 5);
  CHECK(layer->bounds().y == 7);
  CHECK(layer->opacity() > 0.39F && layer->opacity() < 0.41F);
  CHECK(layer->blend_mode() == patchy::BlendMode::Multiply);
  const auto* pixel = std::as_const(*layer).pixels().pixel(10, 10);
  CHECK(pixel[0] == 255 && pixel[1] == 64 && pixel[2] == 0 && pixel[3] == 255);
  // Five mutating calls, exactly one history entry; undo removes the whole run.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "Scripted") == nullptr);
}

void ui_script_stale_layer_wrapper_throws() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Doomed');
    layer.remove();
    var threw = false;
    try { var unused = layer.name; } catch (error) { threw = true; }
    console.log('stale-threw=' + threw);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("stale-threw=true")));
}

void ui_script_pixels_roundtrip_and_palette_snap() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Pixels');
    var w = 8, h = 4;
    var bytes = new Uint8Array(w * h * 4);
    for (var i = 0; i < w * h; i++) {
      bytes[i * 4] = 10; bytes[i * 4 + 1] = 200; bytes[i * 4 + 2] = 30; bytes[i * 4 + 3] = 255;
    }
    layer.setPixels({x: 3, y: 2, width: w, height: h, data: bytes.buffer});
    var back = layer.getPixels();
    var view = new Uint8Array(back.data);
    console.log('roundtrip=' + back.x + ',' + back.y + ',' + back.width + 'x' + back.height +
                ',' + view[1]);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("roundtrip=3,2,8x4,200")));

  // Palette mode on: script pixel writes snap like every tool write.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0, 0, 0}, {255, 0, 0}};
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.findLayer('Pixels');
    var bytes = new Uint8Array(4 * 1 * 4);
    for (var i = 0; i < 4; i++) {
      bytes[i * 4] = 250; bytes[i * 4 + 1] = 40; bytes[i * 4 + 2] = 40; bytes[i * 4 + 3] = 200;
    }
    layer.setPixels({x: 0, y: 0, width: 4, height: 1, data: bytes.buffer});
    var view = new Uint8Array(layer.getPixels().data);
    console.log('snapped=' + view[0] + ',' + view[1] + ',' + view[2] + ',' + view[3]);
  )JS")));
  // 250,40,40 snaps to the red entry; alpha 200 hardens to 255.
  CHECK(backlog_contains(window, QStringLiteral("snapped=255,0,0,255")));
  document.palette_editing().reset();
}

void ui_script_undo_disable_skips_history() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.undoEnabled = false;
    var doc = app.activeDocument;
    doc.addLayer('NoUndo').fill('#123456');
    console.log('undo-flag=' + app.undoEnabled);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("undo-flag=false")));
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "NoUndo") != nullptr);
  // No history entry, but the session still counts as modified work.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Re-enabling mid-run snapshots from that point on (and resets per run).
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    app.undoEnabled = false;
    doc.addLayer('StillNoUndo');
    app.undoEnabled = true;
    doc.addLayer('UndoAgain');
  )JS")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_timer_keeps_run_alive() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    setTimeout(function () {
      doc.addLayer('FromTimer');
      console.log('timer-done');
    }, 60);
  )JS"));
  CHECK(host.run_active());  // sync code finished, the timer keeps it alive
  wait_for_run_end(host);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("timer-done")));
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "FromTimer") != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_watchdog_interrupts_infinite_loop() {
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", QByteArray("300"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = start_script(window, QStringLiteral("while (true) {}"));
    wait_for_run_end(host);
    CHECK(!host.run_active());
    CHECK(host.last_run_had_error());
    CHECK(backlog_contains(window, QStringLiteral("time limit")));
  }
  qunsetenv("PATCHY_SCRIPT_TIMEOUT_MS");
}

void ui_script_console_and_error_line_numbers() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(!run_script(window, QStringLiteral("console.log('hello console');\nthrow new Error('boom');")));
  CHECK(backlog_contains(window, QStringLiteral("hello console")));
  CHECK(backlog_contains(window, QStringLiteral("boom")));
  CHECK(backlog_contains(window, QStringLiteral("test-script:2")));
}

void ui_script_filters_and_text_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Filtered');
    layer.fill('#ff0000');
    layer.applyFilter('patchy.filters.invert');
    var view = new Uint8Array(layer.getPixels().data);
    console.log('inverted=' + view[0] + ',' + view[1] + ',' + view[2]);
    var text = doc.addTextLayer('Scripted Text', {size: 24, x: 40, y: 80});
    console.log('text=' + text.isText + ',' + text.text);
    var badMode = false;
    try { text.blendMode = 'no-such-mode'; } catch (error) { badMode = true; }
    console.log('bad-mode-threw=' + badMode);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("inverted=0,255,255")));
  CHECK(backlog_contains(window, QStringLiteral("text=true,Scripted Text")));
  CHECK(backlog_contains(window, QStringLiteral("bad-mode-threw=true")));
}

void ui_script_run_command_writes_output_file() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto script_path = temp.filePath(QStringLiteral("cli-test.js"));
  {
    QFile file(script_path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("console.log('from cli script');\nconsole.warn('careful');\n");
  }
  const auto output_path = temp.filePath(QStringLiteral("out.txt"));
  window.run_script_command(script_path, output_path);
  auto& host = window.script_engine_host();
  wait_for_run_end(host);
  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(output_path) && timer.elapsed() < 5000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QFile output(output_path);
  CHECK(output.open(QIODevice::ReadOnly | QIODevice::Text));
  const auto text = QString::fromUtf8(output.readAll());
  CHECK(text.contains(QStringLiteral("from cli script")));
  CHECK(text.contains(QStringLiteral("[warn] careful")));
  CHECK(text.endsWith(QStringLiteral("[done]\n")));
}

void ui_script_editor_dialog_runs_and_shows_console() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  CHECK(code != nullptr && console_pane != nullptr && run_button != nullptr);
  code->setPlainText(QStringLiteral("console.log('hello from editor');"));
  run_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("hello from editor")));
  save_widget_artifact("script_editor_dialog", dialog);
  dialog.close();
}

void ui_script_canvas_window_renders_frames() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var win = patchy.ui.createCanvas({width: 200, height: 140, title: 'Test Window'});
    win.onFrame = function () {
      var g = win.graphics;
      g.clear('#204060');
      g.fillRect(20, 20, 80, 50, '#ffcc00');
      g.circle(150, 90, 30, '#40ff40', true);
      g.text(16, 120, 'scripted', '#ffffff', 12);
    };
  )JS"));
  CHECK(host.run_active());
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
  CHECK(dialog != nullptr);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 400) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  const QImage frame = dialog->grab().toImage();
  CHECK(frame.pixelColor(60, 40) == QColor(0xff, 0xcc, 0x00));
  CHECK(frame.pixelColor(150, 90) == QColor(0x40, 0xff, 0x40));
  save_widget_artifact("script_canvas_window", *dialog);
  host.stop_active_run();
  wait_for_run_end(host);
  CHECK(!host.run_active());
}

void ui_scripts_menu_lists_bundled_scripts() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("fileScriptsMenu"));
  CHECK(menu != nullptr);
  // aboutToShow triggers the folder rescan.
  menu->popup(QPoint(0, 0));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  bool editor_entry = false;
  bool pong_entry = false;
  for (const auto* action : menu->actions()) {
    if (action->objectName() == QStringLiteral("fileScriptEditorAction")) {
      editor_entry = true;
    }
    if (action->text() == QStringLiteral("pong")) {
      pong_entry = true;
    }
  }
  menu->close();
  CHECK(editor_entry);
  // The bundled scripts are staged next to the test binary by CMake.
  CHECK(pong_entry);
}

}  // namespace

std::vector<patchy::test::TestCase> scripting_tests() {
  return {
      {"ui_script_mutations_ride_single_undo_entry", ui_script_mutations_ride_single_undo_entry},
      {"ui_script_stale_layer_wrapper_throws", ui_script_stale_layer_wrapper_throws},
      {"ui_script_pixels_roundtrip_and_palette_snap", ui_script_pixels_roundtrip_and_palette_snap},
      {"ui_script_undo_disable_skips_history", ui_script_undo_disable_skips_history},
      {"ui_script_timer_keeps_run_alive", ui_script_timer_keeps_run_alive},
      {"ui_script_watchdog_interrupts_infinite_loop", ui_script_watchdog_interrupts_infinite_loop},
      {"ui_script_console_and_error_line_numbers", ui_script_console_and_error_line_numbers},
      {"ui_script_filters_and_text_layers", ui_script_filters_and_text_layers},
      {"ui_script_run_command_writes_output_file", ui_script_run_command_writes_output_file},
      {"ui_script_editor_dialog_runs_and_shows_console", ui_script_editor_dialog_runs_and_shows_console},
      {"ui_script_canvas_window_renders_frames", ui_script_canvas_window_renders_frames},
      {"ui_scripts_menu_lists_bundled_scripts", ui_scripts_menu_lists_bundled_scripts},
  };
}
