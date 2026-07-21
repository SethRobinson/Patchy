// JS scripting system coverage (docs/scripting.md): engine mutations ride ONE
// undo entry per run, wrappers re-resolve by id (stale wrappers throw instead
// of crashing), pixel access round-trips and honors the palette-mode snap,
// timers keep a run alive, the watchdog interrupts runaway loops, console
// output and error line numbers reach the sink, the CLI output-file contract
// holds, the editor dialog / script canvas window render, and the @cli
// command-line example and scripting-guide viewer surfaces work.

#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/palette.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"

#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

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

void ui_script_get_pixels_reads_rgb_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  // Opaque opened photos (JPEG and friends) store 3-channel RGB layers;
  // getPixels hands scripts RGBA with alpha 255 and setPixels writes RGBA8.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::PixelBuffer rgb(document.width(), document.height(), patchy::PixelFormat::rgb8());
  const auto span = rgb.data();
  for (std::size_t i = 0; i < span.size(); i += 3) {
    span[i] = 200;
    span[i + 1] = 100;
    span[i + 2] = 50;
  }
  document.add_pixel_layer("RgbPhoto", std::move(rgb));
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.findLayer('RgbPhoto');
    var img = layer.getPixels();
    var view = new Uint8Array(img.data);
    console.log('rgb=' + img.width + 'x' + img.height + ',' +
                view[0] + ',' + view[1] + ',' + view[2] + ',' + view[3]);
    view[0] = 10;
    layer.setPixels(img);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("rgb=1024x768,200,100,50,255")));
  const auto* layer = layer_named(patchy::ui::MainWindowTestAccess::document(window), "RgbPhoto");
  CHECK(layer != nullptr);
  CHECK(layer->pixels().format().channels == 4);
  const auto* pixel = layer->pixels().pixel(0, 0);
  CHECK(pixel[0] == 10 && pixel[1] == 100 && pixel[2] == 50 && pixel[3] == 255);
}

void ui_script_fill_rect_partial_updates() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Sprite');
    // Empty layer + fillRect allocates a buffer covering exactly the rect.
    layer.fillRect(20, 30, 8, 4, '#ff0000');
    var b = layer.bounds;
    console.log('alloc=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height);
    // Partial overwrite, then a transparent clear of the same sub-rect.
    layer.fillRect(22, 30, 2, 2, '#00ff00');
    var v1 = new Uint8Array(layer.getPixels().data);
    layer.fillRect(22, 30, 2, 2, '#00000000');
    var v2 = new Uint8Array(layer.getPixels().data);
    // Buffer-local pixel (2,0) = document (22,30); (0,0) stays red throughout.
    console.log('painted=' + v1[2 * 4 + 1] + ' cleared=' + v2[2 * 4 + 3] +
                ' kept=' + v2[0] + ',' + v2[3]);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("alloc=20,30,8x4")));
  CHECK(backlog_contains(window, QStringLiteral("painted=255 cleared=0 kept=255,255")));
}

void ui_script_canvas_window_receives_space_key() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Keys'});
    win.onKeyDown = function (key) { console.log('key=' + key); };
    win.onFrame = function () {};
  )JS"));
  CHECK(host.run_active());
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
  CHECK(dialog != nullptr);
  auto* surface = dialog->findChild<QWidget*>(QStringLiteral("scriptCanvasSurface"));
  CHECK(surface != nullptr);
  // Space must reach the game window instead of being swallowed by the
  // application-level spacebar canvas pan filter.
  QTest::keyClick(surface, Qt::Key_Space);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  CHECK(backlog_contains(window, QStringLiteral("key=Space")));
  host.stop_active_run();
  wait_for_run_end(host);
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
    CHECK(backlog_contains(window, QStringLiteral("no activity")));
  }
  qunsetenv("PATCHY_SCRIPT_TIMEOUT_MS");
}

// The watchdog measures INACTIVITY, not runtime: a script that keeps making
// API calls outlives any number of windows (a contact sheet may run for
// hours), while total silence still dies (the test above).
void ui_script_watchdog_allows_busy_scripts() {
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", QByteArray("300"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    // ~1.2 s of wall-clock work (four windows deep) with a console ping every
    // ~50 ms; each ping feeds the watchdog, so the run must complete.
    CHECK(run_script(window, QStringLiteral(R"JS(
      var start = Date.now();
      var pings = 0;
      while (Date.now() - start < 1200) {
        var t = Date.now();
        while (Date.now() - t < 50) {}
        console.log('ping ' + (++pings));
      }
      console.log('busy-done');
    )JS")));
    CHECK(backlog_contains(window, QStringLiteral("busy-done")));
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

// addTextLayer's size is document pixels: the committed raster must not depend
// on the canvas zoom (the July 2026 dialog-showcase overlap bug: the script
// path set a point-sized font on the zoom-scaled inline editor).
void ui_script_text_size_is_zoom_independent() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var a = doc.addTextLayer('Zoom probe', {size: 24, x: 10, y: 40});
    app.runCommand('view.zoom_out');
    app.runCommand('view.zoom_out');
    var b = doc.addTextLayer('Zoom probe', {size: 24, x: 10, y: 200});
    console.log('a=' + a.bounds.width + 'x' + a.bounds.height +
                ' b=' + b.bounds.width + 'x' + b.bounds.height);
    // The editor font rounds to whole editor pixels, so fractional zooms may
    // drift the committed size by a pixel or two; the bug this guards against
    // committed ~2x bigger, so a proportional tolerance is enough.
    function close(u, v) {
      return Math.abs(u - v) <= Math.max(3, Math.round(0.12 * Math.max(u, v)));
    }
    console.log('match=' + (close(a.bounds.width, b.bounds.width) &&
                            close(a.bounds.height, b.bounds.height)));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("match=true")));
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

void ui_script_editor_status_shows_running_and_ready() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  auto* stop_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorStopButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorStatusLabel"));
  CHECK(code != nullptr && run_button != nullptr && stop_button != nullptr && status != nullptr);
  CHECK(status->text() == QStringLiteral("Ready"));
  CHECK(!stop_button->isEnabled());
  // A long timer keeps the run alive after the synchronous phase, so the
  // running state is observable; the stop-sign button ends it.
  code->setPlainText(QStringLiteral("setTimeout(function () {}, 60000);"));
  run_button->click();
  CHECK(window.script_engine_host().run_active());
  CHECK(status->text().startsWith(QStringLiteral("Running")));
  CHECK(stop_button->isEnabled());
  CHECK(!run_button->isEnabled());
  save_widget_artifact("script_editor_running_status", dialog);
  stop_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(status->text() == QStringLiteral("Ready"));
  CHECK(!stop_button->isEnabled());
  CHECK(run_button->isEnabled());
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
  QMenu* games_menu = nullptr;
  for (auto* action : menu->actions()) {
    if (action->objectName() == QStringLiteral("fileScriptEditorAction")) {
      editor_entry = true;
    }
    if (action->menu() != nullptr && action->text() == QStringLiteral("Games")) {
      games_menu = action->menu();
    }
  }
  CHECK(editor_entry);
  // The bundled scripts are staged next to the test binary by CMake; folders
  // (Games/Demos/Effects/Utilities) become submenus. Entries show their @name
  // display name and carry an icon (the sidecar PNG or the generic fallback).
  CHECK(games_menu != nullptr);
  bool pong_entry = false;
  for (const auto* action : games_menu->actions()) {
    if (action->text() == QStringLiteral("Pong")) {
      pong_entry = true;
      CHECK(!action->icon().isNull());
    }
  }
  menu->close();
  CHECK(pong_entry);
}

// Redirects QStandardPaths writable locations (the user scripts folder) into
// the per-user qttest sandbox so shadow-override saves never touch the real
// profile; exception-safe so a failing CHECK cannot leave test mode on.
struct StandardPathsTestMode {
  StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(true); }
  ~StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(false); }
};

QTreeWidgetItem* find_child_item(QTreeWidgetItem* parent, const QString& text) {
  for (int i = 0; i < parent->childCount(); ++i) {
    if (parent->child(i)->text(0) == text) {
      return parent->child(i);
    }
  }
  return nullptr;
}

void ui_script_editor_tree_shadow_override() {
  const StandardPathsTestMode test_paths;
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* save_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorSaveButton"));
  auto* refresh_button =
      dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRefreshButton"));
  CHECK(tree != nullptr && code != nullptr && save_button != nullptr && refresh_button != nullptr);

  // The Bundled root is expanded by default and mirrors the folder structure.
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  CHECK(bundled_root->isExpanded());
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  // Scripts show their "@name" display name, sidecar icon, and @window flag.
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  CHECK(!breakout_item->icon(0).isNull());
  constexpr int kFileNameRole = Qt::UserRole + 3;
  constexpr int kWindowRole = Qt::UserRole + 5;
  CHECK(breakout_item->data(0, kFileNameRole).toString() == QStringLiteral("breakout.js"));
  CHECK(breakout_item->data(0, kWindowRole).toBool());
  save_widget_artifact("script_manager_tree", *tree);

  // Folder rows (roots included) carry their on-disk path for the context
  // menu's Show in Folder.
  constexpr int kFolderPathRole = Qt::UserRole + 2;
  CHECK(bundled_root->data(0, kFolderPathRole).toString() ==
        patchy::ui::MainWindow::bundled_scripts_directory());
  CHECK(games_folder->data(0, kFolderPathRole).toString().endsWith(QStringLiteral("/Games")));

  // Selecting a script loads it (single click); the itemActivated emit also
  // exercises the explicit activation path, which stays for switching away
  // from a dirty editor. Emitted directly rather than synthesizing a
  // key/click: the gesture that raises it is platform-styled (Return does not
  // fire it on the mac offscreen platform).
  tree->setCurrentItem(breakout_item);
  QMetaObject::invokeMethod(tree, "itemActivated", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item), Q_ARG(int, 0));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));

  // Save writes the user-folder shadow copy, and the tree keeps the entry in
  // place, now carrying the bundled-original path (what the delegate renders
  // as the amber "modified" tag).
  save_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto override_path = QDir(patchy::ui::MainWindow::user_scripts_directory())
                                 .absoluteFilePath(QStringLiteral("Games/breakout.js"));
  CHECK(QFile::exists(override_path));
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  constexpr int kPathRole = Qt::UserRole;
  constexpr int kBundledPathRole = Qt::UserRole + 1;
  auto* override_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(override_item != nullptr);
  CHECK(override_item->data(0, kPathRole).toString() == override_path);
  CHECK(!override_item->data(0, kBundledPathRole).toString().isEmpty());
  // The override does NOT show under My Scripts (it replaces the bundled row).
  QTreeWidgetItem* user_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("My Scripts")) {
      user_root = tree->topLevelItem(i);
    }
  }
  CHECK(user_root != nullptr);
  CHECK(find_child_item(user_root, QStringLiteral("Games")) == nullptr);

  // Removing the copy (what Revert to Bundled does) restores the plain entry.
  CHECK(QFile::remove(override_path));
  refresh_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* restored_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(restored_item != nullptr);
  CHECK(restored_item->data(0, kBundledPathRole).toString().isEmpty());
  dialog.close();
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
}

// A single click (tree selection) loads the clicked script into the editor;
// once the editor holds unsaved edits, selection changes leave them alone (no
// load, no prompt) - only activation switches, behind the discard prompt.
void ui_script_manager_single_click_loads_and_preserves_edits() {
  const StandardPathsTestMode test_paths;
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* file_label = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorFileLabel"));
  CHECK(tree != nullptr && code != nullptr && file_label != nullptr);
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  auto* utilities_folder = find_child_item(bundled_root, QStringLiteral("Utilities"));
  CHECK(utilities_folder != nullptr);
  auto* batch_export_item = find_child_item(utilities_folder, QStringLiteral("Batch Export"));
  CHECK(batch_export_item != nullptr);

  // Selection alone (what a single click or an arrow-key step raises) loads
  // the script; no itemActivated needed.
  tree->setCurrentItem(breakout_item);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));
  CHECK(file_label->text() == QStringLiteral("breakout.js"));

  // Unsaved edits pin the editor: selecting another script neither loads it
  // nor prompts (the discard prompt lives on the activation path only).
  code->textCursor().insertText(QStringLiteral("// dirty-edit-marker\n"));
  CHECK(code->document()->isModified());
  tree->setCurrentItem(batch_export_item);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("dirty-edit-marker")));
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));
  CHECK(file_label->text() == QStringLiteral("breakout.js *"));
  dialog.close();
}

// The New button (the folder context menu's New Script entry calls the same
// path) seeds the editor with the starter template - header directives plus a
// hello console.log - left unmodified so browsing away never prompts. The
// template must actually run: Run executes it against the startup document.
void ui_script_manager_new_button_inserts_template() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* new_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorNewButton"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  auto* file_label = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorFileLabel"));
  CHECK(code != nullptr && console_pane != nullptr && new_button != nullptr &&
        run_button != nullptr && file_label != nullptr);
  new_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("// @name ")));
  CHECK(code->toPlainText().contains(QStringLiteral("// @description ")));
  CHECK(code->toPlainText().contains(QStringLiteral("// @author ")));
  CHECK(code->toPlainText().contains(QStringLiteral("console.log")));
  CHECK(!code->document()->isModified());
  CHECK(file_label->text() == QStringLiteral("untitled.js"));
  run_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("Hi!")));
  CHECK(console_pane->toPlainText().contains(QStringLiteral("1024x768")));
  dialog.close();
}

void ui_script_include_bundled_root_and_is_main() {
  patchy::ui::MainWindow window;
  show_window(window);
  // No script path (editor-style run): the include resolves through the
  // bundled scripts root. The included file defines its function but must not
  // run its standalone branch (patchy.isMainScript() is false inside it).
  CHECK(run_script(window, QStringLiteral(R"JS(
    console.log('main=' + patchy.isMainScript());
    include('Effects/fancy-background.js');
    console.log('have-fn=' + (typeof drawFancyBackground === 'function'));
    var stray = app.activeDocument.findLayer('Fancy Background');
    console.log('ran-standalone=' + (stray !== undefined));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("main=true")));
  CHECK(backlog_contains(window, QStringLiteral("have-fn=true")));
  CHECK(backlog_contains(window, QStringLiteral("ran-standalone=false")));
}

void ui_script_fancy_background_runs_standalone() {
  patchy::ui::MainWindow window;
  show_window(window);
  // Small document first so the pixel loop stays fast.
  CHECK(run_script(window, QStringLiteral("app.newDocument(96, 64);")));
  auto& host = window.script_engine_host();
  const auto path = QDir(patchy::ui::MainWindow::bundled_scripts_directory())
                        .absoluteFilePath(QStringLiteral("Effects/fancy-background.js"));
  // Unattended (the forwarded-CLI contract): showOptions answers with its
  // defaults instead of blocking on the options dialog.
  CHECK(host.run_file(path, {}, /*unattended=*/true));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "Fancy Background") !=
        nullptr);
}

void ui_script_dialog_pickers_listfiles_args_cli_defaults() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  for (const auto* name : {"a.png", "b.png", "c.txt"}) {
    QFile file(temp.filePath(QString::fromLatin1(name)));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("x");
  }
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cli-defaults");
  options.args = QStringList{QStringLiteral("name=World"), QStringLiteral("flag")};
  const auto source = QStringLiteral(R"JS(
    var r = patchy.ui.showDialog({title: 'T', fields: [
      {key: 'scale', type: 'number', value: 42, min: 0, max: 100},
      {key: 'on', type: 'checkbox', value: true},
      {key: 'mode', type: 'choice', value: 'jpg', choices: ['png', 'jpg']},
      {key: 'label', type: 'text', value: 'hi'}]});
    console.log('dlg=' + r.scale + ',' + r.on + ',' + r.mode + ',' + r.label);
    console.log('folder=[' + app.chooseFolder('pick') + ']');
    var files = patchy.io.listFiles('%1', '*.png');
    console.log('files=' + files.join(','));
    console.log('args=' + patchy.args.name + ',' + ('flag' in patchy.args));
  )JS")
                          .arg(QDir(temp.path()).absolutePath());
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("dlg=42,true,jpg,hi")));
  CHECK(backlog_contains(window, QStringLiteral("folder=[]")));
  CHECK(backlog_contains(window, QStringLiteral("files=a.png,b.png")));
  CHECK(backlog_contains(window, QStringLiteral("args=World,true")));
}

void ui_script_run_command_triggers_actions() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    console.log('ids=' + (app.commandIds().indexOf('select.all') >= 0));
    console.log('unknown=' + app.runCommand('no.such.command'));
    console.log('ran=' + app.runCommand('select.all'));
    console.log('sel=' + doc.selection.exists);
    app.runCommand('select.deselect');
    console.log('sel-after=' + doc.selection.exists);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("ids=true")));
  CHECK(backlog_contains(window, QStringLiteral("unknown=false")));
  CHECK(backlog_contains(window, QStringLiteral("ran=true")));
  CHECK(backlog_contains(window, QStringLiteral("sel=true")));
  CHECK(backlog_contains(window, QStringLiteral("sel-after=false")));
}

}  // namespace

// Set Icon from Current Window, end to end minus the literal right-click: the
// handler captures the active document's composite and writes the 64x64
// user-folder icon PNG that then overrides the bundled one.
void ui_script_manager_set_icon_from_document() {
  const StandardPathsTestMode test_paths;
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
  patchy::ui::MainWindow window;
  show_window(window);  // opens the historical startup document
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* console = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  CHECK(tree != nullptr && console != nullptr);
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  QMetaObject::invokeMethod(&dialog, "set_script_icon_from_window", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto icon_path = QDir(patchy::ui::MainWindow::user_scripts_directory())
                             .absoluteFilePath(QStringLiteral("Games/breakout.png"));
  CHECK(QFile::exists(icon_path));
  const QImage written(icon_path);
  CHECK(written.width() == 128 && written.height() == 128);
  CHECK(console->toPlainText().contains(QStringLiteral("Saved icon to")));
  // The refreshed tree resolves the user icon for the (unmodified) bundled
  // script.
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  CHECK(breakout_item->data(0, Qt::UserRole + 1).toString().isEmpty());  // still not "modified"
  dialog.close();
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
}

// The script browser model: @name/@window header parsing, sidecar icon
// resolution with the user-over-bundled override, display-name sorting, and
// the Set Icon write helpers (script_folders.hpp).
void ui_script_metadata_icons_and_write_target() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const QDir root(temp.path());
  CHECK(root.mkpath(QStringLiteral("bundled/Games")));
  CHECK(root.mkpath(QStringLiteral("user/Games")));
  const auto bundled_root = root.absoluteFilePath(QStringLiteral("bundled"));
  const auto user_root = root.absoluteFilePath(QStringLiteral("user"));
  auto write_file = [](const QString& path, const QByteArray& content) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content);
  };
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/zap.js")),
             "// @name Aardvark Attack\n"
             "// @description Aardvarks attack the\n"
             "// @description whole canvas.\n"
             "// @author Seth A. Robinson\n"
             "// @window\nvar x = 1;\n");
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/alpha.js")),
             "var y = 2;\n");
  QImage red(32, 16, QImage::Format_RGBA8888);
  red.fill(Qt::red);

  // Header parsing: @name/@description (repeated lines join)/@author/@window;
  // directives stop at the first non-comment line.
  const auto meta =
      patchy::ui::read_script_metadata(root.absoluteFilePath(QStringLiteral("bundled/Games/zap.js")));
  CHECK(meta.name == QStringLiteral("Aardvark Attack"));
  CHECK(meta.description == QStringLiteral("Aardvarks attack the whole canvas."));
  CHECK(meta.author == QStringLiteral("Seth A. Robinson"));
  CHECK(meta.opens_window);
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/late.js")),
             "var z = 3;\n// @name Too Late\n// @window\n");
  const auto late =
      patchy::ui::read_script_metadata(root.absoluteFilePath(QStringLiteral("bundled/Games/late.js")));
  CHECK(late.name.isEmpty());
  CHECK(!late.opens_window);

  // Scan: display-name fallback, display-name sort ("Aardvark Attack" sorts
  // before the fallback-named "alpha" despite zap.js > alpha.js), and the
  // bundled sidecar icon.
  write_file(root.absoluteFilePath(QStringLiteral("bundled/Games/alpha.png")), "not-a-real-png");
  auto scan = patchy::ui::scan_scripts(bundled_root, user_root);
  CHECK(scan.bundled.size() == 1);
  const auto& games = scan.bundled[0].children;
  CHECK(games.size() == 3);
  CHECK(games[0].display_name == QStringLiteral("Aardvark Attack"));
  CHECK(games[0].opens_window);
  CHECK(games[0].icon_path.isEmpty());
  CHECK(games[1].display_name == QStringLiteral("alpha"));
  CHECK(!games[1].opens_window);
  CHECK(games[1].icon_path ==
        QDir(bundled_root).absoluteFilePath(QStringLiteral("Games/alpha.png")));

  // A user icon at the same relative path overrides the bundled one WITHOUT
  // the .js being overridden (what Set Icon writes).
  const auto target = patchy::ui::script_icon_write_target(
      user_root, QStringLiteral("Games/alpha.js"));
  CHECK(target == QDir(user_root).absoluteFilePath(QStringLiteral("Games/alpha.png")));
  CHECK(patchy::ui::write_script_icon(red, target));
  const QImage written(target);
  CHECK(written.width() == 128 && written.height() == 128);
  scan = patchy::ui::scan_scripts(bundled_root, user_root);
  const auto& games_after = scan.bundled[0].children;
  CHECK(games_after[1].display_name == QStringLiteral("alpha"));
  CHECK(!games_after[1].is_override);
  CHECK(games_after[1].icon_path == target);
  // The unreadable bundled "png" falls back to the generic painted icon; the
  // real user icon is used as-is.
  CHECK(!patchy::ui::script_entry_icon(games_after[1]).isNull());
}

// showOptions: --script-arg values override the field defaults (coerced by
// type; a bare flag token turns a checkbox on) and an unattended run answers
// without any dialog - app.alert logs instead of showing, the forwarded-CLI
// contract.
void ui_script_show_options_unattended_merges_args() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("options-test");
  options.unattended = true;
  options.args = QStringList{QStringLiteral("scale=7.5"), QStringLiteral("on=false"),
                             QStringLiteral("mode=jpg"), QStringLiteral("out=C:/tmp/x"),
                             QStringLiteral("flag")};
  const auto source = QStringLiteral(R"JS(
    var r = patchy.ui.showOptions({title: 'T', description: 'unused when unattended',
      fields: [
        {key: 'scale', type: 'number', value: 42, min: 0, max: 100},
        {key: 'on', type: 'checkbox', value: true},
        {key: 'flag', type: 'checkbox', value: false},
        {key: 'mode', type: 'choice', value: 'png', choices: ['png', 'jpg']},
        {key: 'out', type: 'folder', value: ''},
        {key: 'label', type: 'text', value: 'hi'}]});
    console.log('opt=' + r.scale + ',' + r.on + ',' + r.flag + ',' + r.mode + ',' +
                r.out + ',' + r.label);
    app.alert('quiet');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("opt=7.5,false,true,jpg,C:/tmp/x,hi")));
  CHECK(backlog_contains(window, QStringLiteral("[alert] quiet")));
}

// The GUI path of showOptions: the dialog carries the description label and a
// folder row with its Browse button, args still pre-fill the fields, and OK
// returns the values.
void ui_script_show_options_dialog_description_and_folder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  bool accepted = false;
  bool saw_description = false;
  bool saw_browse = false;
  // Repeating dismisser (the import-notices pattern): accept the form dialog
  // once it appears inside the script's nested exec loop.
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window,
                   [&accepted, &saw_description, &saw_browse] {
                     for (auto* widget : QApplication::topLevelWidgets()) {
                       auto* dialog = qobject_cast<QDialog*>(widget);
                       if (dialog != nullptr && dialog->isVisible() &&
                           dialog->objectName() == QStringLiteral("scriptFormDialog")) {
                         saw_description =
                             dialog->findChild<QLabel*>(QStringLiteral("scriptFormDescription")) !=
                             nullptr;
                         saw_browse = dialog->findChild<QPushButton*>(QStringLiteral(
                                          "scriptFormField_outBrowse")) != nullptr;
                         if (!accepted) {
                           save_widget_artifact("script_options_dialog", *dialog);
                         }
                         accepted = true;
                         dialog->accept();
                       }
                     }
                   });
  dismisser->start(50);
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("options-gui");
  options.args = QStringList{QStringLiteral("out=D:/pictures")};
  (void)host.run_source(QStringLiteral(R"JS(
    var r = patchy.ui.showOptions({title: 'T', description: 'Pick things.', fields: [
      {key: 'out', type: 'folder', value: ''},
      {key: 'n', type: 'number', value: 3, min: 0, max: 9}]});
    console.log('gui=' + (r ? r.out + ',' + r.n : 'null'));
  )JS"),
                         std::move(options));
  wait_for_run_end(host);
  dismisser->stop();
  dismisser->deleteLater();
  CHECK(accepted);
  CHECK(saw_description);
  CHECK(saw_browse);
  CHECK(backlog_contains(window, QStringLiteral("gui=D:/pictures,3")));
}

// A synchronous burst blocking the GUI past the (env-shortened) threshold
// shows the canvas processing overlay automatically and drops it when the
// burst ends; script timers defer instead of re-entering the mid-evaluation
// engine while the pump processes events.
void ui_script_busy_overlay_and_timer_guard() {
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = window.findChild<patchy::ui::CanvasWidget*>();
    CHECK(canvas != nullptr);
    const auto overlays_before = canvas->render_cache_diagnostics().processing_overlays_shown;
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('busy');
      var ticks = 0;
      setTimeout(function () { ticks++; console.log('tick=' + ticks); }, 0);
      var start = Date.now();
      while (Date.now() - start < 200) {
        layer.fillRect(0, 0, 2, 2, '#00ff00');
      }
      console.log('sync-done ticks=' + ticks);
    )JS")));
    // The 0ms timer must NOT have fired inside the synchronous burst (the
    // pump processes events mid-evaluation; the guard defers it)...
    CHECK(backlog_contains(window, QStringLiteral("sync-done ticks=0")));
    // ...but it still fires right afterwards.
    CHECK(backlog_contains(window, QStringLiteral("tick=1")));
    CHECK(canvas->render_cache_diagnostics().processing_overlays_shown > overlays_before);
    CHECK(!canvas->processing_overlay_visible());
  }
  qunsetenv("PATCHY_SCRIPT_BUSY_DELAY_MS");
}

// The hover card (driven directly; real hover timing needs a live pointer):
// shows beside a script row with the metadata details painted in.
void ui_script_manager_hover_card_shows_details() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  CHECK(tree != nullptr);
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  // Breakout ships @description and @author, so the card has real content.
  CHECK(!breakout_item->data(0, Qt::UserRole + 6).toString().isEmpty());
  CHECK(breakout_item->data(0, Qt::UserRole + 7).toString() ==
        QStringLiteral("Seth A. Robinson"));
  QMetaObject::invokeMethod(&dialog, "show_script_hover_card", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item));
  auto* card = dialog.findChild<QWidget*>(QStringLiteral("scriptHoverCard"));
  CHECK(card != nullptr);
  CHECK(card->isVisible());
  CHECK(card->height() > 120);  // icon + name + author + description + footer
  save_widget_artifact("script_hover_card", *card);
  dialog.close();
}

// The running-script stop panel: appears once a burst crosses the
// (env-zeroed) threshold; Stop opens the NON-BLOCKING confirm (the script
// keeps working underneath) - Cancel just dismisses it, Stop Script with the
// undo box checked interrupts the run and rolls the document back to its
// pre-script state.
void ui_script_stop_panel_confirm_and_undo() {
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = window.script_engine_host();

    // Driven from a repeating timer: the pump's processEvents runs it while
    // the script blocks the UI thread. One panel click and one confirm answer
    // per run.
    bool click_panel = false;
    bool confirm_with_undo = false;
    bool panel_clicked = false;
    bool confirm_answered = false;
    bool artifact_saved = false;
    auto* driver = new QTimer(&window);
    QObject::connect(driver, &QTimer::timeout, &window, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible()) {
          continue;
        }
        if (dialog->objectName() == QStringLiteral("scriptStopConfirmDialog") &&
            !confirm_answered) {
          confirm_answered = true;
          if (confirm_with_undo) {
            auto* undo_box =
                dialog->findChild<QCheckBox*>(QStringLiteral("scriptStopUndoCheckBox"));
            CHECK(undo_box != nullptr && undo_box->isVisible());
            undo_box->setChecked(true);
            dialog->findChild<QPushButton*>(QStringLiteral("scriptStopConfirmButton"))->click();
          } else {
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
          }
        } else if (dialog->objectName() == QStringLiteral("scriptStopPanel") && click_panel &&
                   !panel_clicked) {
          panel_clicked = true;
          if (!artifact_saved) {
            artifact_saved = true;
            save_widget_artifact("script_stop_panel", *dialog);
          }
          dialog->findChild<QPushButton*>(QStringLiteral("scriptStopPanelButton"))->click();
        }
      }
    });
    driver->start(30);

    const auto busy_script = QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('%1');
      var start = Date.now();
      while (Date.now() - start < 700) {
        layer.fillRect(0, 0, 2, 2, '#ff0000');
      }
      console.log('survived');
    )JS");

    // Pass 1: Cancel at the confirm - the script must finish normally.
    click_panel = true;
    CHECK(run_script(window, busy_script.arg(QStringLiteral("stop-cancel-layer"))));
    CHECK(panel_clicked);
    CHECK(confirm_answered);
    CHECK(backlog_contains(window, QStringLiteral("survived")));
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-cancel-layer") != nullptr);

    // Pass 2: Stop Script with undo checked - the run dies stopped and the
    // layer it added is rolled back (pass 1's layer stays, proving only the
    // stopped run's snapshot was undone).
    panel_clicked = false;
    confirm_answered = false;
    confirm_with_undo = true;
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("stoppable");
    (void)host.run_source(busy_script.arg(QStringLiteral("stop-undo-layer")), std::move(options));
    wait_for_run_end(host);
    driver->stop();
    CHECK(panel_clicked);
    CHECK(confirm_answered);
    CHECK(host.last_run_had_error());
    CHECK(backlog_contains(window, QStringLiteral("Script stopped.")));
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-undo-layer") == nullptr);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-cancel-layer") != nullptr);
    auto* panel = window.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
    CHECK(panel != nullptr && !panel->isVisible());
  }
  qunsetenv("PATCHY_SCRIPT_BUSY_DELAY_MS");
}

// The busy overlay and stop panel step aside while the script shows its own
// modal (alert here): ModalWatchdogPause ends the indicator on entry.
void ui_script_busy_panel_yields_to_script_dialogs() {
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool alert_seen = false;
    bool panel_visible_during_alert = false;
    auto* dismisser = new QTimer(&window);
    QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box != nullptr && box->isVisible() &&
            box->objectName() == QStringLiteral("scriptAlertMessageBox")) {
          alert_seen = true;
          auto* panel = window.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
          panel_visible_during_alert =
              panel_visible_during_alert || (panel != nullptr && panel->isVisible());
          box->accept();
        }
      }
    });
    dismisser->start(30);
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('yield-test');
      var start = Date.now();
      while (Date.now() - start < 150) {
        layer.fillRect(0, 0, 2, 2, '#00ff00');
      }
      app.alert('paused at a dialog');
      console.log('after-alert');
    )JS")));
    dismisser->stop();
    CHECK(alert_seen);
    CHECK(!panel_visible_during_alert);
    CHECK(backlog_contains(window, QStringLiteral("after-alert")));
  }
  qunsetenv("PATCHY_SCRIPT_BUSY_DELAY_MS");
}

// The @cli header directive and the command builder behind the Script
// Manager's C:\ button: tokens append verbatim (repeated lines join), the
// fallback covers scripts with no metadata at all, and paths stay quoted.
void ui_script_cli_directive_and_example_command() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const QDir root(temp.path());
  auto write_file = [](const QString& path, const QByteArray& content) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content);
  };
  const auto args_path = root.absoluteFilePath(QStringLiteral("args.js"));
  write_file(args_path,
             "// @name Args Demo\n"
             "// @cli --script-arg folder=C:\\photos\n"
             "// @cli --script-arg out=C:\\photos\\web example.png\n"
             "var x = 1;\n");
  const auto meta = patchy::ui::read_script_metadata(args_path);
  CHECK(meta.cli_example ==
        QStringLiteral(
            "--script-arg folder=C:\\photos --script-arg out=C:\\photos\\web example.png"));

  const auto command = patchy::ui::script_cli_example_command(
      QStringLiteral("C:/Program Files/Patchy/patchy.exe"), args_path, meta);
  CHECK(command.startsWith(QLatin1Char('"')));
  CHECK(command.contains(QStringLiteral("patchy.exe\" --run-script \"")));
  CHECK(command.contains(QStringLiteral("--script-arg folder=C:\\photos")));
  CHECK(command.endsWith(QStringLiteral("example.png")));

  // A spaced exe path forces quotes, and the shells then genuinely differ:
  // the PowerShell flavor puts the call operator in front.
  const auto powershell = patchy::ui::script_cli_example_command(
      QStringLiteral("C:/Program Files/Patchy/patchy.exe"), args_path, meta,
      patchy::ui::CliShell::PowerShell);
  CHECK(powershell == QStringLiteral("& ") + command);

  // A plain exe path stays unquoted - the one form Command Prompt,
  // PowerShell, and batch files all run as pasted - so both flavors match.
  const auto universal = patchy::ui::script_cli_example_command(
      QStringLiteral("D:/tools/patchy.exe"), args_path, meta);
  CHECK(!universal.startsWith(QLatin1Char('"')));
  CHECK(universal.contains(QStringLiteral("patchy.exe --run-script \"")));
  CHECK(universal ==
        patchy::ui::script_cli_example_command(QStringLiteral("D:/tools/patchy.exe"), args_path,
                                               meta, patchy::ui::CliShell::PowerShell));

  // No @cli: active-document scripts get the example.png placeholder, @window
  // scripts the bare command, and empty metadata never throws.
  const patchy::ui::ScriptMetadata plain;
  const auto fallback = patchy::ui::script_cli_example_command(
      QStringLiteral("patchy.exe"), root.absoluteFilePath(QStringLiteral("plain.js")), plain);
  CHECK(fallback.endsWith(QStringLiteral(" example.png")));
  patchy::ui::ScriptMetadata windowed;
  windowed.opens_window = true;
  const auto bare = patchy::ui::script_cli_example_command(
      QStringLiteral("patchy.exe"), root.absoluteFilePath(QStringLiteral("game.js")), windowed);
  CHECK(bare.endsWith(QStringLiteral(".js\"")));

  // The scan carries the directive to the tree/menu consumers.
  const auto entries = patchy::ui::scan_script_folder(root.absolutePath());
  CHECK(entries.size() == 1);
  CHECK(entries[0].cli_example == meta.cli_example);
}

// The C:\ toolbar button: disabled until a script is selected, then pops the
// example dialog whose command comes from the script's @cli directive, and
// Copy puts the exact command on the clipboard.
void ui_script_manager_cli_example_dialog() {
  const StandardPathsTestMode test_paths;
  QDir(patchy::ui::MainWindow::user_scripts_directory()).removeRecursively();
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* cli_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorCliButton"));
  CHECK(tree != nullptr && cli_button != nullptr);
  CHECK(!cli_button->isEnabled());

  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* utilities = find_child_item(bundled_root, QStringLiteral("Utilities"));
  CHECK(utilities != nullptr);
  auto* batch_export = find_child_item(utilities, QStringLiteral("Batch Export"));
  CHECK(batch_export != nullptr);
  tree->setCurrentItem(batch_export);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(cli_button->isEnabled());

  cli_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* example = dialog.findChild<QDialog*>(QStringLiteral("scriptEditorCliDialog"));
  CHECK(example != nullptr);
  CHECK(example->isVisible());
  auto* command_box =
      example->findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCliCommand"));
  CHECK(command_box != nullptr);
  const auto command = command_box->toPlainText();
  CHECK(command.contains(QStringLiteral("--run-script")));
  CHECK(command.contains(QStringLiteral("batch-export.js")));
  CHECK(command.contains(QStringLiteral("--script-output result.txt")));
  CHECK(command.contains(QStringLiteral("--script-arg folder=C:\\photos")));
  // The test binary's path has no spaces, so one universal line serves every
  // shell: unquoted exe token, no separate PowerShell box.
  CHECK(!command.startsWith(QLatin1Char('"')));
  CHECK(example->findChild<QPlainTextEdit*>(
            QStringLiteral("scriptEditorCliCommandPowerShell")) == nullptr);
  save_widget_artifact("script_cli_example_dialog", *example);
  auto* copy_button =
      example->findChild<QPushButton*>(QStringLiteral("scriptEditorCliCopyButton"));
  CHECK(copy_button != nullptr);
  copy_button->click();
  CHECK(QGuiApplication::clipboard()->text() == command);
  example->close();
}

// Help opens the bundled scripting guide in the markdown viewer (the same
// file README links), and the Help > Scripting Guide menu action reuses the
// one instance. The first open parks in run_non_modal_dialog's nested loop,
// so a timer inspects and closes it from inside.
void ui_script_scripting_guide_opens_from_help() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto guide_path = patchy::ui::MainWindow::bundled_scripts_directory() +
                          QStringLiteral("/scripting-guide.md");
  CHECK(QFile::exists(guide_path));

  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* help_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorHelpButton"));
  CHECK(help_button != nullptr);
  bool saw_guide = false;
  QString guide_text;
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
    auto* viewer = window.findChild<QDialog*>(QStringLiteral("markdownViewerDialog"));
    if (viewer == nullptr || !viewer->isVisible()) {
      return;
    }
    if (auto* browser =
            viewer->findChild<QTextBrowser*>(QStringLiteral("markdownViewerBrowser"))) {
      guide_text = browser->document()->toPlainText();
    }
    if (!saw_guide) {
      save_widget_artifact("scripting_guide_viewer", *viewer);
    }
    saw_guide = true;
    viewer->close();
  });
  dismisser->start(30);
  help_button->click();
  dismisser->stop();
  CHECK(saw_guide);
  CHECK(guide_text.contains(QStringLiteral("Patchy Scripting Guide")));
  CHECK(guide_text.contains(QStringLiteral("--run-script")));
  CHECK(guide_text.contains(QStringLiteral("@cli")));

  // Reopening (here via the Help menu action) reuses the single hidden
  // instance and returns immediately - no second nested loop.
  auto* action = window.findChild<QAction*>(QStringLiteral("helpScriptingGuideAction"));
  CHECK(action != nullptr);
  action->trigger();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto viewers = window.findChildren<QDialog*>(QStringLiteral("markdownViewerDialog"));
  CHECK(viewers.size() == 1);
  CHECK(viewers[0]->isVisible());
  viewers[0]->close();
}

std::vector<patchy::test::TestCase> scripting_tests() {
  return {
      {"ui_script_mutations_ride_single_undo_entry", ui_script_mutations_ride_single_undo_entry},
      {"ui_script_stale_layer_wrapper_throws", ui_script_stale_layer_wrapper_throws},
      {"ui_script_pixels_roundtrip_and_palette_snap", ui_script_pixels_roundtrip_and_palette_snap},
      {"ui_script_get_pixels_reads_rgb_layers", ui_script_get_pixels_reads_rgb_layers},
      {"ui_script_fill_rect_partial_updates", ui_script_fill_rect_partial_updates},
      {"ui_script_canvas_window_receives_space_key", ui_script_canvas_window_receives_space_key},
      {"ui_script_undo_disable_skips_history", ui_script_undo_disable_skips_history},
      {"ui_script_timer_keeps_run_alive", ui_script_timer_keeps_run_alive},
      {"ui_script_watchdog_interrupts_infinite_loop", ui_script_watchdog_interrupts_infinite_loop},
      {"ui_script_watchdog_allows_busy_scripts", ui_script_watchdog_allows_busy_scripts},
      {"ui_script_stop_panel_confirm_and_undo", ui_script_stop_panel_confirm_and_undo},
      {"ui_script_busy_panel_yields_to_script_dialogs",
       ui_script_busy_panel_yields_to_script_dialogs},
      {"ui_script_console_and_error_line_numbers", ui_script_console_and_error_line_numbers},
      {"ui_script_filters_and_text_layers", ui_script_filters_and_text_layers},
      {"ui_script_text_size_is_zoom_independent", ui_script_text_size_is_zoom_independent},
      {"ui_script_run_command_writes_output_file", ui_script_run_command_writes_output_file},
      {"ui_script_editor_dialog_runs_and_shows_console", ui_script_editor_dialog_runs_and_shows_console},
      {"ui_script_editor_status_shows_running_and_ready", ui_script_editor_status_shows_running_and_ready},
      {"ui_script_canvas_window_renders_frames", ui_script_canvas_window_renders_frames},
      {"ui_scripts_menu_lists_bundled_scripts", ui_scripts_menu_lists_bundled_scripts},
      {"ui_script_editor_tree_shadow_override", ui_script_editor_tree_shadow_override},
      {"ui_script_manager_single_click_loads_and_preserves_edits",
       ui_script_manager_single_click_loads_and_preserves_edits},
      {"ui_script_manager_new_button_inserts_template",
       ui_script_manager_new_button_inserts_template},
      {"ui_script_metadata_icons_and_write_target", ui_script_metadata_icons_and_write_target},
      {"ui_script_manager_set_icon_from_document", ui_script_manager_set_icon_from_document},
      {"ui_script_show_options_unattended_merges_args",
       ui_script_show_options_unattended_merges_args},
      {"ui_script_show_options_dialog_description_and_folder",
       ui_script_show_options_dialog_description_and_folder},
      {"ui_script_busy_overlay_and_timer_guard", ui_script_busy_overlay_and_timer_guard},
      {"ui_script_manager_hover_card_shows_details", ui_script_manager_hover_card_shows_details},
      {"ui_script_include_bundled_root_and_is_main", ui_script_include_bundled_root_and_is_main},
      {"ui_script_fancy_background_runs_standalone", ui_script_fancy_background_runs_standalone},
      {"ui_script_dialog_pickers_listfiles_args_cli_defaults",
       ui_script_dialog_pickers_listfiles_args_cli_defaults},
      {"ui_script_run_command_triggers_actions", ui_script_run_command_triggers_actions},
      {"ui_script_cli_directive_and_example_command",
       ui_script_cli_directive_and_example_command},
      {"ui_script_manager_cli_example_dialog", ui_script_manager_cli_example_dialog},
      {"ui_script_scripting_guide_opens_from_help", ui_script_scripting_guide_opens_from_help},
  };
}
