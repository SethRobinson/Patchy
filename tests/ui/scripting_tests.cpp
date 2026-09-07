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
#include "formats/document_flatten.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include "ui/canvas_widget.hpp"
#include "ui/brush_automation.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/ai_control_paths.hpp"
#include "ui/ai_setup_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_manager.hpp"

#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "RgbPhoto", std::move(rgb)));
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

// Samples, from a timer that runs inside the busy pump's processEvents, whether
// the app-modal stop panel was ever on screen at the same time as a script
// canvas window. That overlap is the defect: Qt marks every window shown while
// an application-modal window is up as blocked, and a blocked window is skipped
// by the key-delivery path entirely. The desktop platforms lift the block when
// the modal hides, but the wasm plugin never does, so the game window paints
// and takes clicks yet never receives a keystroke again (docs/wasm.md).
class StopPanelOverlapWatch {
public:
  explicit StopPanelOverlapWatch(patchy::ui::MainWindow& window) : window_(window) {
    timer_ = new QTimer(&window);
    QObject::connect(timer_, &QTimer::timeout, &window, [this] { sample(); });
    timer_->start(5);
  }

  void sample() {
    auto* panel = window_.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
    const bool panel_up = panel != nullptr && panel->isVisible();
    if (panel_up) {
      saw_panel_ = true;
      if (window_.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog")) != nullptr) {
        overlapped_ = true;
      }
    }
  }

  void stop() { timer_->stop(); }
  [[nodiscard]] bool overlapped() const { return overlapped_; }
  [[nodiscard]] bool saw_panel() const { return saw_panel_; }

private:
  patchy::ui::MainWindow& window_;
  QTimer* timer_{nullptr};
  bool overlapped_{false};
  bool saw_panel_{false};
};

// A script that has already been working long enough to raise the stop panel
// must not create its canvas window underneath it.
void ui_script_canvas_window_dismisses_stop_panel() {
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    StopPanelOverlapWatch watch(window);
    // Busy long enough to raise the panel, open the window, then stay busy so
    // the sampler gets ticks in which the two could still overlap.
    auto& host = start_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('busy');
      var start = Date.now();
      while (Date.now() - start < 150) { layer.fillRect(0, 0, 2, 2, '#00ff00'); }
      var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Game'});
      win.onKeyDown = function (key) { console.log('key=' + key); };
      win.onFrame = function () {};
      start = Date.now();
      while (Date.now() - start < 150) { layer.fillRect(0, 0, 2, 2, '#00ff00'); }
    )JS"));
    watch.stop();
    CHECK(host.run_active());
    CHECK(watch.saw_panel());    // the burst really was slow enough to raise it
    CHECK(!watch.overlapped());  // ...and it was gone before the window existed
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
    CHECK(dialog != nullptr);
    CHECK(QApplication::activeModalWidget() == nullptr);
    // The window is usable: keys reach the script rather than being dropped.
    auto* surface = dialog->findChild<QWidget*>(QStringLiteral("scriptCanvasSurface"));
    CHECK(surface != nullptr);
    QTest::keyClick(surface, Qt::Key_Space);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    CHECK(backlog_contains(window, QStringLiteral("key=Space")));
    host.stop_active_run();
    wait_for_run_end(host);
  }
  qunsetenv("PATCHY_SCRIPT_BUSY_DELAY_MS");
}

// While a script owns an open canvas window, a slow frame callback must not
// raise the stop panel over it (same blocking hazard, plus a game behind an
// application-modal panel is unplayable on every platform).
void ui_script_canvas_window_suppresses_stop_panel() {
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = start_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('game');
      var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Game'});
      var frames = 0;
      win.onFrame = function () {
        var start = Date.now();
        while (Date.now() - start < 60) { layer.fillRect(0, 0, 2, 2, '#0000ff'); }
        console.log('frame=' + (++frames));
      };
    )JS"));
    CHECK(host.run_active());
    CHECK(window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog")) != nullptr);
    // Sample across several slow frames; each one crosses the zeroed threshold.
    StopPanelOverlapWatch watch(window);
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 600) {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    watch.stop();
    CHECK(backlog_contains(window, QStringLiteral("frame=3")));  // slow frames really ran
    CHECK(!watch.saw_panel());
    CHECK(QApplication::activeModalWidget() == nullptr);
    host.stop_active_run();
    wait_for_run_end(host);
  }
  qunsetenv("PATCHY_SCRIPT_BUSY_DELAY_MS");
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

void ui_script_text_layer_with_uncovered_script_does_not_crash() {
  // No registered face covers Thai in the offscreen suite (the registry rescue is off), so
  // Qt answers the per-writing-system probe with its glyph-box engine. The missing-font
  // check used to index that engine's empty family list and take the process down.
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addTextLayer('สวัสดี', {size: 24, x: 10, y: 40});
    var first = layer.bounds.height;
    layer.text = 'สวัสดี こんにちは';
    console.log('survived=' + (first > 0 && layer.bounds.height > 0 && layer.isText));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("survived=true")));
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
  auto& theme = patchy::ui::ThemeManager::instance();
  const auto old_preference = theme.preference();
  theme.set_preference(patchy::ui::ColorSchemePreference::Light, false);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  save_widget_artifact("script_editor_dialog_light", dialog);
  theme.set_preference(old_preference, false);
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
  // run its standalone branch (patchy.isMainScript() is false inside it), and
  // its own top-level OPTIONS block must not clobber the includer's OPTIONS
  // (the Breakout frozen-paddle bug: fancy-background's OPTIONS replaced
  // paddleSpeed/lives with undefined).
  CHECK(run_script(window, QStringLiteral(R"JS(
    var OPTIONS = { marker: 42 };
    console.log('main=' + patchy.isMainScript());
    include('Effects/fancy-background.js');
    console.log('have-fn=' + (typeof drawFancyBackground === 'function'));
    console.log('options-marker=' + OPTIONS.marker);
    var stray = app.activeDocument.findLayer('Fancy Background');
    console.log('ran-standalone=' + (stray !== undefined));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("main=true")));
  CHECK(backlog_contains(window, QStringLiteral("have-fn=true")));
  CHECK(backlog_contains(window, QStringLiteral("options-marker=42")));
  CHECK(backlog_contains(window, QStringLiteral("ran-standalone=false")));
}

// The tone synth's WAV container: canonical 44-byte RIFF/WAVE header for
// 44100 Hz 16-bit mono PCM, sample count from the duration, amplitude scaling
// from the volume, and clamped extremes still produce a bounded valid file.
void ui_sound_build_tone_wav_shape() {
  const auto u16 = [](const QByteArray& wav, int offset) {
    return static_cast<int>(static_cast<unsigned char>(wav[offset])) |
           (static_cast<int>(static_cast<unsigned char>(wav[offset + 1])) << 8);
  };
  const auto u32 = [&u16](const QByteArray& wav, int offset) {
    return static_cast<quint32>(u16(wav, offset)) |
           (static_cast<quint32>(u16(wav, offset + 2)) << 16);
  };
  const auto peak = [](const QByteArray& wav) {
    int max = 0;
    for (int i = 44; i + 1 < wav.size(); i += 2) {
      const auto sample = static_cast<qint16>(
          static_cast<unsigned char>(wav[i]) |
          (static_cast<unsigned char>(wav[i + 1]) << 8));
      max = std::max(max, std::abs(static_cast<int>(sample)));
    }
    return max;
  };

  const auto wav = patchy::ui::build_tone_wav(440.0, 100, 0.5, patchy::ui::ToneWave::Sine);
  CHECK(wav.startsWith("RIFF"));
  CHECK(wav.mid(8, 4) == QByteArrayLiteral("WAVE"));
  CHECK(wav.mid(12, 4) == QByteArrayLiteral("fmt "));
  CHECK(u16(wav, 20) == 1);       // PCM
  CHECK(u16(wav, 22) == 1);       // mono
  CHECK(u32(wav, 24) == 44100u);  // sample rate
  CHECK(u16(wav, 34) == 16);      // bits per sample
  CHECK(wav.mid(36, 4) == QByteArrayLiteral("data"));
  CHECK(u32(wav, 40) == 44100u / 10 * 2);  // 100 ms of 16-bit samples
  CHECK(wav.size() == 44 + 44100 / 10 * 2);

  const auto loud = patchy::ui::build_tone_wav(440.0, 100, 1.0, patchy::ui::ToneWave::Sine);
  CHECK(peak(loud) > peak(wav) * 3 / 2);  // volume scales amplitude
  const auto silent = patchy::ui::build_tone_wav(440.0, 100, 0.0, patchy::ui::ToneWave::Sine);
  CHECK(peak(silent) == 0);
  const auto square = patchy::ui::build_tone_wav(440.0, 100, 0.5, patchy::ui::ToneWave::Square);
  CHECK(square.mid(44) != wav.mid(44));

  const auto clamped =
      patchy::ui::build_tone_wav(999999.0, 999999, 9.0, patchy::ui::ToneWave::Square);
  CHECK(clamped.size() == 44 + 44100 * 4 * 2);  // duration caps at 4 s
}

// patchy.ui.playTone/playSound through the script API: PATCHY_NO_SOUND keeps
// the suite silent while the full path (clamps, include-style resolution, WAV
// validation, error text naming the file) still runs.
void ui_script_play_tone_and_sound_offscreen() {
  qputenv("PATCHY_NO_SOUND", "1");
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto wav_path = temp.filePath(QStringLiteral("blip.wav"));
  {
    QFile file(wav_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(patchy::ui::build_tone_wav(880.0, 30, 0.4, patchy::ui::ToneWave::Square));
  }
  const auto not_wav_path = temp.filePath(QStringLiteral("not-a-wav.wav"));
  {
    QFile file(not_wav_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("plain text");
  }
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.playTone(880);
    patchy.ui.playTone(60, 40, 0.2, "square");
    patchy.ui.playTone(-500, 999999, 42);  // clamped, still fine
    patchy.ui.playSound('%1');
    var missing = '';
    try { patchy.ui.playSound('no-such-file.wav'); } catch (e) { missing = String(e); }
    var invalid = '';
    try { patchy.ui.playSound('%2'); } catch (e) { invalid = String(e); }
    console.log('missing-error=' + missing);
    console.log('invalid-error=' + invalid);
    console.log('sound-ok');
  )JS")
                             .arg(QString(wav_path).replace(QLatin1Char('\\'), QLatin1Char('/')))
                             .arg(QString(not_wav_path)
                                      .replace(QLatin1Char('\\'), QLatin1Char('/')))));
  CHECK(backlog_contains(window, QStringLiteral("sound-ok")));
  CHECK(backlog_contains(window, QStringLiteral("missing-error=Error: playSound")));
  CHECK(backlog_contains(window, QStringLiteral("no-such-file.wav")));
  CHECK(backlog_contains(window, QStringLiteral("not a .wav file")));
  qunsetenv("PATCHY_NO_SOUND");
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
    const char* pending_layer_name = "";
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
          // The script's first mutation pumps events before its undo snapshot
          // is recorded, so the panel can appear (and be clicked) with nothing
          // to undo yet; the confirm samples the snapshot state when it opens.
          // Wait until the script's layer landed before stopping the run.
          if (layer_named(patchy::ui::MainWindowTestAccess::document(window),
                          pending_layer_name) == nullptr) {
            continue;
          }
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
    pending_layer_name = "stop-cancel-layer";
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
    pending_layer_name = "stop-undo-layer";
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
    // Slow mode creates several groups before the Stop confirmation. Its Undo
    // option must restore all retained steps of this run, not just the last one.
    panel_clicked = false;
    confirm_answered = false;
    pending_layer_name = "stop-slow-layer";
    const auto before_slow = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
    driver->start(30);
    (void)host.run_source(QStringLiteral(R"JS(
      patchy.ui.slowMode=true;
      app.activeDocument.addLayer('slow-earlier-layer').fill('#335577');
    )JS") + busy_script.arg(QStringLiteral("stop-slow-layer")), {});
    wait_for_run_end(host);
    driver->stop();
    CHECK(panel_clicked && confirm_answered && host.last_run_had_error());
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == before_slow);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "slow-earlier-layer") == nullptr);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "stop-slow-layer") == nullptr);
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
  CHECK(guide_text.contains(QStringLiteral("--headless")));
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

// Help > Set up AI Control opens the paste-into-your-assistant dialog. The
// release layout has the connector, the assembled skill, and its setup.md next
// to the test binary, so the text must carry all three real paths and no NOT
// FOUND marker, and Copy must put exactly that text on the clipboard.
void ui_ai_setup_dialog_opens_from_help() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto paths = patchy::ui::resolve_ai_control_paths();
  CHECK(!paths.connector_path.isEmpty());
  CHECK(!paths.skill_directory.isEmpty());
  CHECK(!paths.setup_document_path.isEmpty());
  CHECK(QFileInfo::exists(paths.connector_path));
  CHECK(QFileInfo::exists(paths.skill_directory + QStringLiteral("/SKILL.md")));

  auto* action = patchy::test::ui::require_action(window, "helpAiSetupAction");
  CHECK(action->isVisible());
  bool saw_dialog = false;
  QString blurb;
  QString status;
  QGuiApplication::clipboard()->clear();
  auto* dismisser = new QTimer(&window);
  QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("aiSetupDialog"));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    if (auto* text = dialog->findChild<QPlainTextEdit*>(QStringLiteral("aiSetupBlurbText"))) {
      blurb = text->toPlainText();
      CHECK(text->isReadOnly());
    }
    if (auto* label = dialog->findChild<QLabel*>(QStringLiteral("aiSetupStatusLabel"))) {
      status = label->text();
    }
    auto* copy = dialog->findChild<QPushButton*>(QStringLiteral("aiSetupCopyButton"));
    CHECK(copy != nullptr);
    if (copy != nullptr) {
      copy->click();
    }
    auto* open_skill =
        dialog->findChild<QPushButton*>(QStringLiteral("aiSetupOpenSkillFolderButton"));
    CHECK(open_skill != nullptr && open_skill->isEnabled());
    if (!saw_dialog) {
      save_widget_artifact("ai_setup_dialog", *dialog);
    }
    saw_dialog = true;
    dialog->close();
  });
  dismisser->start(30);
  action->trigger();
  dismisser->stop();
  CHECK(saw_dialog);

  CHECK(blurb.contains(QDir::toNativeSeparators(paths.connector_path)));
  CHECK(blurb.contains(QDir::toNativeSeparators(paths.skill_directory)));
  CHECK(blurb.contains(QDir::toNativeSeparators(paths.setup_document_path)));
  CHECK(blurb.contains(QString::fromLatin1(patchy::ui::kAiControlSetupUrl)));
  CHECK(blurb.contains(QStringLiteral("\"get_info\"")));
  CHECK(blurb.contains(QStringLiteral("named \"patchy\"")));
  CHECK(!blurb.contains(QStringLiteral("NOT FOUND (expected")));
  CHECK(!blurb.contains(QChar(0x2014)));
  CHECK(QGuiApplication::clipboard()->text() == blurb);
  CHECK(status.contains(QDir::toNativeSeparators(paths.connector_path).toHtmlEscaped()));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("copied")));

  // Reopening reuses the one hidden instance; no second nested loop.
  action->trigger();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto dialogs = window.findChildren<QDialog*>(QStringLiteral("aiSetupDialog"));
  CHECK(dialogs.size() == 1);
  CHECK(dialogs[0]->isVisible());
  dialogs[0]->close();
}

// The blurb builder handles a build without the kit (source tree, old zip) and
// the Flatpak sandbox, where the connector is reached through `flatpak run`.
void ui_ai_setup_blurb_reports_missing_and_flatpak_forms() {
  const auto url = QString::fromLatin1(patchy::ui::kAiControlSetupUrl);

  const auto missing = patchy::ui::ai_setup_blurb_text(patchy::ui::AiControlPaths{});
  CHECK(missing.contains(QStringLiteral("Command: NOT FOUND (expected")));
  CHECK(missing.contains(QStringLiteral("   NOT FOUND (expected")));
  CHECK(missing.contains(QStringLiteral("not installed; use the online copy")));
  CHECK(missing.contains(url));
  CHECK(!missing.contains(QChar(0x2014)));

  patchy::ui::AiControlPaths flatpak;
  flatpak.flatpak = true;
  const auto sandboxed = patchy::ui::ai_setup_blurb_text(flatpak);
  CHECK(sandboxed.contains(QStringLiteral("flatpak run --command=patchy-mcp com.rtsoft.patchy")));
  CHECK(sandboxed.contains(QStringLiteral("/app/share/patchy/ai/patchy-control")));
  CHECK(sandboxed.contains(QStringLiteral("flatpak run --command=cp com.rtsoft.patchy /app/share/patchy/ai/patchy-control/SKILL.md")));
  CHECK(sandboxed.contains(url));
  CHECK(!sandboxed.contains(QStringLiteral("NOT FOUND (expected")));
  CHECK(!sandboxed.contains(QChar(0x2014)));

  // A resolved layout quotes native paths so folders with spaces survive a paste.
  patchy::ui::AiControlPaths resolved;
  resolved.connector_path = QStringLiteral("/opt/My Apps/patchy-mcp");
  resolved.skill_directory = QStringLiteral("/opt/My Apps/ai/patchy-control");
  resolved.setup_document_path = resolved.skill_directory + QStringLiteral("/references/setup.md");
  const auto text = patchy::ui::ai_setup_blurb_text(resolved);
  CHECK(text.contains(QLatin1Char('"') + QDir::toNativeSeparators(resolved.connector_path) +
                      QLatin1Char('"')));
  CHECK(text.contains(QLatin1Char('"') + QDir::toNativeSeparators(resolved.skill_directory) +
                      QLatin1Char('"')));
  CHECK(!text.contains(QStringLiteral("NOT FOUND (expected")));

  CHECK(text.contains(QStringLiteral("use no connector arguments")));
  CHECK(text.contains(QStringLiteral("Reuse an existing matching Patchy connection")));
  CHECK(text.contains(QStringLiteral("without reinstalling")));
}

void ui_ai_setup_examples_keep_installation_prompt_stable() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto resolved = patchy::ui::resolve_ai_control_paths();
  const auto text = patchy::ui::ai_setup_blurb_text(resolved);
  patchy::ui::AiSetupDialog dialog(resolved, &window);
  dialog.show();
  QApplication::processEvents();
  CHECK(!dialog.findChild<QComboBox*>(QStringLiteral("aiSetupModeComboBox")));
  auto* examples = dialog.findChild<QComboBox*>(QStringLiteral("aiSetupExamplesComboBox"));
  auto* example_text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiSetupExampleText"));
  auto* example_copy = dialog.findChild<QPushButton*>(QStringLiteral("aiSetupExampleCopyButton"));
  auto* setup_text = dialog.findChild<QPlainTextEdit*>(QStringLiteral("aiSetupBlurbText"));
  auto* copy = dialog.findChild<QPushButton*>(QStringLiteral("aiSetupCopyButton"));
  CHECK(examples && example_text && example_copy && setup_text && copy);
  CHECK(examples->count() >= 5);
  CHECK(example_text->isReadOnly());
  CHECK(QFontMetrics(setup_text->font()).height() > QFontMetrics(window.font()).height());
  CHECK(example_text->font() == setup_text->font());
  QStringList prompts;
  for (int i = 0; i < examples->count(); ++i) {
    examples->setCurrentIndex(i);
    const auto prompt = example_text->toPlainText();
    CHECK(!prompt.isEmpty() && !prompts.contains(prompt));
    prompts.append(prompt);
    example_copy->click();
    CHECK(QGuiApplication::clipboard()->text() == prompt);
    CHECK(dialog.blurb_text() == text);
    copy->click();
    CHECK(QGuiApplication::clipboard()->text() == text);
  }
  const auto all = prompts.join(QLatin1Char('\n'));
  CHECK(all.contains(QStringLiteral("document I have open")));
  CHECK(all.contains(QStringLiteral("visible Patchy window")));
  CHECK(all.contains(QStringLiteral("background, without opening a window")));
  examples->setCurrentIndex(2);
  QTest::qWait(1250);
  CHECK(copy->text() == QStringLiteral("Copy Setup Prompt"));
  CHECK(example_copy->text() == QStringLiteral("Copy Example Prompt"));
  save_widget_artifact("ai_setup_examples", dialog);
  auto& theme = patchy::ui::ThemeManager::instance();
  auto& language = patchy::ui::LocalizationManager::instance();
  const auto old_theme = theme.preference();
  const auto old_language = language.current_language();
  const auto restore = qScopeGuard([&] {
    theme.set_preference(old_theme, false);
    (void)language.set_language(old_language, false);
  });
  theme.set_preference(patchy::ui::ColorSchemePreference::Light, false);
  QApplication::processEvents();
  save_widget_artifact("ai_setup_examples_light", dialog);
  dialog.close();
  CHECK(language.set_language(QStringLiteral("ja"), false));
  patchy::ui::AiSetupDialog japanese(resolved, &window);
  japanese.show();
  QApplication::processEvents();
  auto* japanese_copy = japanese.findChild<QPushButton*>(QStringLiteral("aiSetupExampleCopyButton"));
  CHECK(japanese_copy && japanese_copy->text() != QStringLiteral("Copy Example Prompt"));
  save_widget_artifact("ai_setup_examples_ja", japanese);
  japanese.close();
  CHECK(dialog.blurb_text() == text);
}

// patchy.ui.zoom / fitOnScreen: the documented view controls (percent, active
// document, status-bar clamping), including the connector sessions that refuse
// app.runCommand('view.fit_on_screen').
void ui_script_ui_view_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.setWindowSize(1000, 700);
    patchy.ui.zoom = 50;
    console.log('half=' + patchy.ui.zoom);
    patchy.ui.zoom = 100000;
    console.log('max=' + patchy.ui.zoom);
    patchy.ui.zoom = 0.001;
    console.log('min=' + patchy.ui.zoom);
    patchy.ui.fitOnScreen();
    var fit = patchy.ui.zoom;
    var doc = app.activeDocument;
    console.log('fits=' + (fit > 0 && doc.width * fit / 100 <= 1000 && doc.height * fit / 100 <= 700));
    patchy.ui.zoom = 200;
    console.log('after=' + patchy.ui.zoom);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("half=50")));
  CHECK(backlog_contains(window, QStringLiteral("max=12800")));
  CHECK(backlog_contains(window, QStringLiteral("min=5")));
  CHECK(backlog_contains(window, QStringLiteral("fits=true")));
  CHECK(backlog_contains(window, QStringLiteral("after=200")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = NaN;")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = -5;")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.zoom = 0;")));
  // A failed assignment leaves the view alone.
  CHECK(run_script(window, QStringLiteral("console.log('kept=' + patchy.ui.zoom);")));
  CHECK(backlog_contains(window, QStringLiteral("kept=200")));

  patchy::ui::MainWindow connector;
  show_window(connector);
  connector.set_cli_automation_mode(true);
  connector.script_engine_host().set_connector_mode(true);
  CHECK(run_script(connector, QStringLiteral(R"JS(
    app.documents.forEach(function (d) { d.close(); });
    console.log('empty=' + patchy.ui.zoom);
  )JS")));
  CHECK(backlog_contains(connector, QStringLiteral("empty=0")));
  CHECK(!run_script(connector, QStringLiteral("patchy.ui.zoom = 100;")));
  CHECK(!run_script(connector, QStringLiteral("patchy.ui.fitOnScreen();")));
  CHECK(run_script(connector, QStringLiteral(R"JS(
    var doc = app.newDocument(64, 64);
    patchy.ui.zoom = 200;
    console.log('connector=' + patchy.ui.zoom);
    patchy.ui.fitOnScreen();
    console.log('connectorFit=' + (patchy.ui.zoom > 200));
  )JS")));
  CHECK(backlog_contains(connector, QStringLiteral("connector=200")));
  CHECK(backlog_contains(connector, QStringLiteral("connectorFit=true")));
  CHECK(!run_script(connector, QStringLiteral("app.runCommand('view.fit_on_screen');")));
}

void ui_script_ui_staging_apis() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto capture_path = QStringLiteral("test-artifacts/ui_script_capture_window.png");
  QFile::remove(capture_path);
  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.setWindowSize(1200, 800);
    patchy.ui.setSidePanelWidth(420);
    patchy.ui.setStatusMessage('Staged by script');
    if (!patchy.ui.captureWindow('test-artifacts/ui_script_capture_window.png')) {
      throw new Error('captureWindow returned false');
    }
  )JS")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Staged by script"));
  CHECK(window.width() == 1200);
  CHECK(window.height() == 800);
  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  CHECK(layers_dock != nullptr);
  // 420 stays above the measured stack minimum, which tracks the localized
  // blend/opacity row and would clamp a smaller request.
  CHECK(layers_dock->width() == 420);
  const QImage captured(capture_path);
  CHECK(!captured.isNull());
  CHECK(captured.width() == 1200);
  CHECK(captured.height() == 800);
  // An empty path throws instead of writing nowhere.
  CHECK(!run_script(window, QStringLiteral("patchy.ui.captureWindow('');")));
}

void ui_script_active_layer_setter_reveals_row() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Base", patchy::PixelBuffer{}));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested", patchy::PixelBuffer{}));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Reveal"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  // Collapse the folder through its disclosure button (the user path).
  auto* folder_item = patchy::test::ui::require_layer_item(*layer_list, QStringLiteral("Folder"));
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(disclosure->isChecked());
  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(patchy::test::ui::find_layer_item(*layer_list, QStringLiteral("Nested")) == nullptr);

  // The activeLayer setter reveals the row like a user's click would: the
  // collapsed ancestor expands and the row becomes the current selection.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.activeLayer = doc.findLayer('Nested');
  )JS")));
  auto* nested_item = patchy::test::ui::find_layer_item(*layer_list, QStringLiteral("Nested"));
  CHECK(nested_item != nullptr);
  CHECK(layer_list->currentItem() == nested_item);
  const auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* nested_layer = std::as_const(active_document).layers().back().children().empty()
                                 ? nullptr
                                 : &std::as_const(active_document).layers().back().children().front();
  CHECK(nested_layer != nullptr);
  CHECK(active_document.active_layer_id() == nested_layer->id());
}

void ui_script_io_round_trips_unicode_path() {
  // The patchy.io probes plus saveAs/open on a Unicode, special-character path. The
  // directory comes in through --script-arg style args; the file name is built in the
  // script with JS escapes so the test is independent of the arg parser.
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QLatin1Char('/') +
                   QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                     static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) +
                   QStringLiteral("/script-io");
  QDir(dir).removeRecursively();
  CHECK(!QDir(dir).exists());

  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("unicode-io");
  options.args = QStringList{QStringLiteral("dir=") + dir};
  const auto source = QStringLiteral(R"JS(
    var dir = patchy.args.dir;
    var name = 'せす café 🎨 #1 50% %20 &and \'q\' [x] ! ; = @ (v2).psd';
    var sub = dir + '/サブ sub';
    console.log('mkdir=' + patchy.io.makeDir(sub));
    var p = sub + '/' + name;
    console.log('before=' + patchy.io.fileExists(p) + ',' + patchy.io.fileSize(p) + ',' + patchy.io.deleteFile(p));
    console.log('saved=' + app.activeDocument.saveAs(p));
    console.log('after=' + patchy.io.fileExists(p) + ',' + (patchy.io.fileSize(p) > 0));
    console.log('listed=' + patchy.io.listFiles(sub, '*.psd').join('|'));
    var reopened = app.open(p);
    console.log('path_ok=' + (reopened.path.slice(-name.length) === name));
    console.log('deleted=' + patchy.io.deleteFile(p) + ',' + patchy.io.fileExists(p) + ',' + patchy.io.fileSize(p));
    console.log('dir_delete=' + patchy.io.deleteFile(sub));
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("mkdir=true")));
  CHECK(backlog_contains(window, QStringLiteral("before=false,-1,false")));
  CHECK(backlog_contains(window, QStringLiteral("saved=true")));
  CHECK(backlog_contains(window, QStringLiteral("after=true,true")));
  CHECK(backlog_contains(window, QStringLiteral("listed=せす café")));
  CHECK(backlog_contains(window, QStringLiteral("path_ok=true")));
  CHECK(backlog_contains(window, QStringLiteral("deleted=true,false,-1")));
  CHECK(backlog_contains(window, QStringLiteral("dir_delete=false")));
  CHECK(QDir(dir + QStringLiteral("/サブ sub")).exists());
}

void ui_script_automation_strokes_match_native_and_undo() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("var d=app.newDocument(64,64); d.addLayer('Ink').fill('#e0d8c8'); d.selection.selectRect(10,10,40,40);")));
  auto& host = window.script_engine_host();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_zoom(4.0);
  for (const int size : {1, 2, 14}) {
    for (const int flow : {100, 25}) {
      for (const bool pressure : {false, true}) {
        for (const bool erase : {false, true}) {
          const auto points = pressure
              ? QStringLiteral("[{x:12,y:20,pressure:0.1},{x:35,y:40,pressure:1},{x:50,y:18,pressure:0.3}]")
              : QStringLiteral("[{x:12,y:20},{x:35,y:40},{x:50,y:18}]");
          const auto source = QStringLiteral(
              "app.activeDocument.activeLayer.drawStrokes([{size:%1,flow:%2,opacity:65,softness:40,color:'#245b93',seed:9,tool:'%3',points:%4}]);")
              .arg(size).arg(flow).arg(erase ? "eraser" : "brush", points);
          const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
          CHECK(run_script(window, source));
          CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
          const auto scripted = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
          CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
          canvas->set_tool(erase ? patchy::ui::CanvasTool::Eraser : patchy::ui::CanvasTool::Brush);
          canvas->set_primary_color(QColor("#245b93"));
          canvas->set_brush_size(size);
          canvas->set_brush_opacity(65);
          canvas->set_brush_softness(40);
          canvas->set_brush_flow(flow);
          canvas->set_brush_build_up(false);
          canvas->set_brush_tip(nullptr, {});
          canvas->set_brush_dynamics({});
          canvas->set_brush_smoothing(0);
          canvas->set_pen_input_settings({});
          canvas->set_brush_dynamics_test_seed(9);
          if (pressure) {
            using patchy::test::ui::send_tablet;
            const auto point = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x,y)); };
            send_tablet(*canvas, QEvent::TabletPress, point(12,20), 0.1);
            send_tablet(*canvas, QEvent::TabletMove, point(35,40), 1.0, Qt::NoButton);
            send_tablet(*canvas, QEvent::TabletMove, point(50,18), 0.3, Qt::NoButton);
            send_tablet(*canvas, QEvent::TabletRelease, point(50,18), 0.3, Qt::LeftButton, Qt::NoButton);
          } else {
            patchy::test::ui::drag_document_path(*canvas, {{12,20},{35,40},{50,18}}, 1);
          }
          const auto native = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
          if (!std::equal(scripted.data().begin(), scripted.data().end(), native.data().begin(), native.data().end())) {
            throw std::runtime_error("stroke parity: size=" + std::to_string(size) + " flow=" + std::to_string(flow) +
                                     " pressure=" + std::to_string(pressure) + " erase=" + std::to_string(erase));
          }
          patchy::ui::MainWindowTestAccess::undo(window);
        }
      }
    }
  }
}

void ui_script_automation_preview_ids_and_errors() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  auto& host = window.script_engine_host();
  host.set_connector_mode(true);
  const auto dir = QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                    static_cast<qsizetype>(patchy::test::kUnicodeDirName.size()));
  QDir().mkpath(QStringLiteral("test-artifacts/") + dir + "/preview");
  const auto base = QStringLiteral("test-artifacts/") + dir + "/preview/";
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("automation-preview");
  options.args = {QStringLiteral("base=") + base};
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    var d=app.newDocument(16,16), l=d.addLayer('Ink');
    l.fillRect(4,4,4,4,'#ff0000');
    if(!d.saveAs(patchy.args.base+'doc.psd')) throw new Error('save');
    var before=d.path, changed=d.modified;
    var preview=d.renderPreview(patchy.args.base+'preview.png',
        {rect:{x:4,y:4,width:4,height:4},maxWidth:32,maxHeight:32,nearestNeighbor:true});
    if(d.path!==before || d.modified!==changed) throw new Error('preview changed save state');
    patchy.setResult({documentId:d.id,layerId:l.id,preview:preview});
  )JS"), std::move(options)));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  const auto result = host.last_result().toObject();
  const QImage png(base + "preview.png");
  CHECK(png.size() == QSize(32,32));
  CHECK(png.pixelColor(5,5) == QColor("#ff0000"));
  const auto id = result["documentId"].toString();
  const auto layer_id = result["layerId"].toString();
  CHECK(run_script(window, QStringLiteral("var l=app.getDocument('%1').getLayer('%2'); l.fill('#00ff00');").arg(id,layer_id)));
  QJsonObject metadata;
  const auto preview = host.render_preview(id.toLongLong(), {}, &metadata);
  CHECK(preview.pixelColor(5,5) == QColor("#00ff00"));
  CHECK(!run_script(window, QStringLiteral("app.getDocument('999999');")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.getLayer('999999');")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.drawStrokes([{points:[{x:2,y:2}],size:0}]);")));
  CHECK(!run_script(window, QStringLiteral("app.runCommand('file.open');")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.createCanvas();")));
  CHECK(run_script(window, QStringLiteral("app.getDocument('%1').close();").arg(id)));
  CHECK(!run_script(window, QStringLiteral("app.getDocument('%1');").arg(id)));
}

void ui_script_automation_pressure_seed_selection_palette() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("var d=app.newDocument(64,64); d.addLayer('Ink'); d.selection.selectRect(10,10,40,40);")));
  auto& host = window.script_engine_host();
  const auto script = QStringLiteral(R"JS(
    app.activeDocument.activeLayer.drawStrokes([
      {size:18,flow:25,opacity:60,softness:50,color:'#ef3322',seed:17,sizeJitter:0.3,scatter:0.4,
       points:[{x:3,y:20,pressure:0.1},{x:24,y:20,pressure:1},{x:58,y:20,pressure:0.3}]},
      {tool:'eraser',size:3,points:[{x:20,y:10},{x:20,y:35}]}
    ]);
  )JS");
  CHECK(run_script(window, script));
  const auto first = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  CHECK(run_script(window, script));
  const auto second = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(first.data().begin(), first.data().end(), second.data().begin(), second.data().end()));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_zoom(4.0);
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor("#ef3322"));
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(60);
  canvas->set_brush_flow(25);
  canvas->set_brush_softness(50);
  canvas->set_brush_build_up(false);
  canvas->set_brush_tip(nullptr, {});
  patchy::BrushDynamics dynamics;
  dynamics.seed = 17;
  dynamics.size_jitter = 0.3;
  dynamics.scatter = 0.4;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(17);
  canvas->set_brush_smoothing(0);
  canvas->set_pen_input_settings({});
  using patchy::test::ui::send_tablet;
  const auto point = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x,y)); };
  send_tablet(*canvas, QEvent::TabletPress, point(3,20), 0.1);
  send_tablet(*canvas, QEvent::TabletMove, point(24,20), 1.0, Qt::NoButton);
  send_tablet(*canvas, QEvent::TabletMove, point(58,20), 0.3, Qt::NoButton);
  send_tablet(*canvas, QEvent::TabletRelease, point(58,20), 0.3, Qt::LeftButton, Qt::NoButton);
  canvas->set_tool(patchy::ui::CanvasTool::Eraser);
  canvas->set_brush_size(3);
  canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100);
  canvas->set_brush_softness(0);
  patchy::test::ui::drag_document_path(*canvas, {{20,10},{20,35}}, 1);
  const auto native = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(first.data().begin(), first.data().end(), native.data().begin(), native.data().end()));
  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  auto* doc = host.session_document(host.active_session_id());
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0,0,0},{255,0,0}};
  editing.palette_revision = 1;
  doc->palette_editing() = editing;
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.drawStrokes([{size:1,color:'#fb2311',points:[{x:0,y:25},{x:60,y:25}]}]);")));
  const auto* layer = std::as_const(*doc).find_layer(*doc->active_layer_id());
  CHECK(layer != nullptr);
  const auto bounds = layer->bounds();
  // Native storage may include transparent pixels beyond the selection.
  // Assert painted coverage rather than the allocation rectangle.
  for (int y = 0; y < layer->pixels().height(); ++y) {
    for (int x = 0; x < layer->pixels().width(); ++x) {
      if (!QRect(10,10,40,40).contains(x + bounds.x, y + bounds.y)) {
        CHECK(layer->pixels().pixel(x,y)[3] == 0);
      }
    }
  }
  const auto* p = layer->pixels().pixel(20 - bounds.x,25 - bounds.y);
  CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
  const auto scripted_palette = patchy::flatten_document_rgba8(std::as_const(*doc));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor("#fb2311"));
  canvas->set_brush_size(1);
  canvas->set_brush_dynamics({});
  canvas->set_brush_dynamics_test_seed(0);
  patchy::test::ui::drag_document_path(*canvas, {{0,25},{60,25}}, 1);
  const auto native_palette = patchy::flatten_document_rgba8(std::as_const(*doc));
  CHECK(std::equal(scripted_palette.data().begin(), scripted_palette.data().end(),
                   native_palette.data().begin(), native_palette.data().end()));
}


void ui_script_unattended_normalizes_forms_and_guards_commands() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("unattended-regressions");
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    function check(ok, message) { if (!ok) throw new Error(message); }
    var doc = app.activeDocument;
    doc.addLayer('Keep me').fill('#aabbcc');
    check(!app.runCommand('edit.undo'), 'undo must not alter the running transaction');
    check(!app.runCommand('edit.redo'), 'redo must not alter the running transaction');
    check(!app.runCommand('file.quit'), 'quit must not tear down the engine');
    app.runCommand('file.close');
    check(app.activeDocument.id === doc.id, 'modified document must stay open');
    var r = patchy.ui.showDialog({fields:[
      {key:'mode', type:'choice', choices:['first','second'], value:1},
      {key:'color', type:'color', value:'RED'},
      {key:'number', type:'number', min:2, max:5, value:99},
      {key:'text', type:'text'}, {key:'checked', type:'checkbox'}]});
    check(r.mode === 'second', 'choice index must normalize to text');
    check(r.color === '#ff0000', 'color must normalize');
    check(r.number === 5 && r.text === '' && r.checked === false, 'defaults and limits');
    var rejected = false;
    try { patchy.ui.showDialog({fields:[{type:'text'}]}); } catch(e) { rejected = true; }
    check(rejected, 'missing field key must throw');
    rejected = false;
    try { app.open('this-file-does-not-exist.psd'); } catch(e) { rejected = true; }
    check(rejected, 'failed open must throw without prompting');
    console.log('unattended-complete');
  )JS"), std::move(options)));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("unattended-complete")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_geometry_rgb_fill_and_empty_text_regressions() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::PixelBuffer rgb(4, 3, patchy::PixelFormat::rgb8());
  std::fill(rgb.data().begin(), rgb.data().end(), std::uint8_t{90});
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "RgbPhoto", std::move(rgb)));
  CHECK(run_script(window, QStringLiteral(R"JS(
    function check(ok, message) { if (!ok) throw new Error(message); }
    var doc = app.activeDocument, layer = doc.findLayer('RgbPhoto');
    layer.fillRect(1, 1, 1, 1, '#ff0000');
    var p = new Uint8Array(layer.getPixels().data);
    check(p[0] === 90 && p[3] === 255 && p[20] === 255 && p[21] === 0, 'RGB promotion');
    layer.fill('#00ff00');
    p = new Uint8Array(layer.getPixels().data);
    check(p[0] === 0 && p[1] === 255 && p[3] === 255, 'RGB fill');
    var rejected = false;
    try { layer.moveTo(2147483648, 0); } catch(e) { rejected = true; }
    check(rejected && layer.x === 0, 'move overflow must throw without mutation');
    doc.selection.selectRect(5000, 5000, 20, 20);
    check(!doc.selection.exists, 'outside selection must be empty');
    doc.selection.selectRect(-3, -5, 10, 10);
    var b = doc.selection.bounds;
    check(b.x === 0 && b.y === 0 && b.width === 7 && b.height === 5, 'selection must clip');
    doc.selection.deselect();
    var text = doc.addTextLayer('Clear me', {x:20,y:20,size:24});
    text.text = '';
    check(text.text === '', 'empty text must commit');
    var pixels = new Uint8Array(text.getPixels().data);
    for (var i=3;i<pixels.length;i+=4) check(pixels[i] === 0, 'empty text has no ink');
  )JS")));
}

void ui_script_advanced_brush_native_parity() {
  using namespace patchy::ui;
  MainWindow window; show_window(window); window.set_cli_automation_mode(true);
  CHECK(run_script(window, "var d=app.newDocument(96,96);d.activeLayer.fill('#b9c7d8');"));
  auto& host=window.script_engine_host(); auto* canvas=MainWindowTestAccess::canvas(window);
  canvas->set_zoom(4.0);
  auto& library=window.brush_automation_library();
  const QStringList settings{
    R"({"dynamics":{"wetEdges":true}})",
    R"({"dynamics":{"textureEnabled":true,"textureStyle":"canvas","textureDepth":0.7}})",
    R"({"dynamics":{"dualBrushEnabled":true,"dualBrushSize":0.4,"dualBrushSpacing":0.7}})",
    R"({"dynamics":{"colorDynamicsEnabled":true,"foregroundBackgroundJitter":0.6,"hueJitter":0.1}})",
    R"({"dynamics":{"sizeJitter":0.3,"angleJitter":0.3,"roundnessJitter":0.5,"flipXJitter":true}})",
    R"({"dynamics":{"scatter":0.4,"scatterBothAxes":true,"count":3,"countJitter":0.4}})",
    R"({"dynamics":{"opacityJitter":0.3,"flowJitter":0.4,"flowControl":"fade","flowFadeSteps":40}})",
    R"({"tool":"mixer","mixer":{"wet":65,"load":40,"mix":75,"sampleAllLayers":true}})",
    R"({"tool":"mixer","mixer":{"wet":0,"load":10,"mix":0}})",
    R"({"smoothing":{"amount":12,"pulledString":true,"catchUp":false,"catchUpOnEnd":true}})"
  };
  for (const auto& settings_json:settings) {
    auto config=QJsonDocument::fromJson(settings_json.toUtf8()).object();
    config["size"]=19;config["flow"]=35;config["color"]="#813c22";config["seed"]=21;
    const auto serialized=QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    const auto before=BrushAutomationLibrary::settings(canvas->current_script_brush());
    CHECK(run_script(window,"var s="+serialized+";s.points=[{x:12,y:20},{x:35,y:66},{x:78,y:36}];app.activeDocument.activeLayer.drawStrokes([s]);"));
    CHECK(before==BrushAutomationLibrary::settings(canvas->current_script_brush()));
    const auto scripted=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(run_script(window,"app.activeDocument.undo();"));
    canvas->apply_script_brush(library.resolve(config));canvas->set_brush_dynamics_test_seed(21);
    patchy::test::ui::drag_document_path(*canvas,{{12,20},{35,66},{78,36}},1);
    const auto native=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    if (!std::equal(scripted.data().begin(),scripted.data().end(),native.data().begin(),native.data().end())) {
      QImage(scripted.data().data(),96,96,96*4,QImage::Format_RGBA8888).save("test-artifacts/advanced-scripted.png");
      QImage(native.data().data(),96,96,96*4,QImage::Format_RGBA8888).save("test-artifacts/advanced-native.png");
      throw std::runtime_error("advanced brush parity: "+settings_json.toStdString());
    }
    MainWindowTestAccess::undo(window);
  }
}
void ui_script_advanced_brush_timing_validation_and_restore() {
  using namespace patchy::ui;
  MainWindow window; show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).addLayer('Timed');"));
  auto& host=window.script_engine_host();const auto id=host.active_session_id();
  const auto stroke=QStringLiteral(R"JS(
    var s={size:24,color:'#934321',flow:15,airbrush:true,
      smoothing:{amount:15,catchUp:true},dynamics:{opacityControl:'off',sizeControl:'penPressure'},
      points:[{x:20,y:40,pressure:.2,timeMs:0},{x:65,y:40,pressure:1,timeMs:100},{x:65,y:40,pressure:1,timeMs:500}]};
    app.activeDocument.activeLayer.drawStrokes([s]);
  )JS");
  const auto before=host.automation_fingerprint();
  const auto depth=MainWindowTestAccess::active_session_undo_depth(window);
  const QStringList bad{
    "{airbrush:true,points:[{x:2,y:2}]}",
    "{points:[{x:2,y:2,timeMs:1}]}",
    "{points:[{x:2,y:2,timeMs:0},{x:4,y:4}]}",
    "{points:[{x:2,y:2,xTilt:4}]}",
    "{dynamics:{wetEdges:1},points:[{x:2,y:2}]}",
    "{dynamics:{sizeJitter:NaN},points:[{x:2,y:2}]}",
    "{tool:'mixer',dynamics:{wetEdges:true},points:[{x:2,y:2}]}",
    "{tipId:'missing',points:[{x:2,y:2}]}",
    "{sizeJitter:.2,dynamics:{sizeJitter:.4},points:[{x:2,y:2}]}"
  };
  for(const auto& s:bad) {
    CHECK(!run_script(window,"app.activeDocument.activeLayer.drawStrokes([{size:12,points:[{x:8,y:8}]} ,"+s+"]);"));
    CHECK(host.automation_fingerprint()==before);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window)==depth);
  }
  CHECK(run_script(window,stroke));
  const auto first=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(run_script(window,"app.activeDocument.undo();patchy.ui.present(60);"+stroke+"patchy.ui.present(60);"));
  const auto second=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(std::equal(first.data().begin(),first.data().end(),second.data().begin(),second.data().end()));
  CHECK(run_script(window,"app.activeDocument.undo();patchy.ui.slowMode=true;"+stroke));
  const auto slow=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(std::equal(first.data().begin(),first.data().end(),slow.data().begin(),slow.data().end()));
  CHECK(run_script(window,"patchy.ui.slowMode=false;"));
  CHECK(run_script(window,"app.activeDocument.undo();app.activeDocument.redo();"));
  CHECK(run_script(window,R"JS(
    var b=patchy.brushes.resolve({dynamics:{sizeControl:'penPressure',opacityControl:'off'}}).settings;
    if(b.dynamics.sizeControl!=='penPressure'||b.dynamics.opacityControl!=='off')throw Error('independent pressure');
    patchy.setResult(patchy.brushes.getCurrent());
  )JS"));
}
void ui_script_advanced_brush_presets_snapshots_and_refresh() {
  using namespace patchy::ui;
  QTemporaryDir dir(QDir::currentPath()+"/test-artifacts/brush-library-XXXXXX");CHECK(dir.isValid());
  BrushTipLibrary tips(dir.path()+"/tips");
  BrushAutomationLibrary first(tips,nullptr,dir.path()+"/presets");
  BrushTipLibrary other_tips(dir.path()+"/tips");
  BrushAutomationLibrary other(other_tips,nullptr,dir.path()+"/presets");
  QImage mask(12,8,QImage::Format_Grayscale8);mask.fill(0);
  for(int y=2;y<6;++y)for(int x=1;x<11;++x)mask.scanLine(y)[x]=255;
  const auto tip=tips.add_tip(QStringLiteral("\u6bdb brush"),mask,.4);CHECK(!tip.isEmpty());first.refresh();
  const auto s=first.resolve(QJsonObject{{"tipId",tip},{"size",23},{"dynamics",QJsonObject{{"wetEdges",true}}}});
  const auto id=first.save(QStringLiteral("\u6cb9 Oil"),s,false);
  other.refresh();CHECK(other.preset(id)["name"]==QStringLiteral("\u6cb9 Oil"));
  CHECK(tips.remove_tip(tip));first.refresh();
  const auto saved=first.resolve(QJsonObject{{"presetId",id}});CHECK(saved.tip != nullptr);CHECK(saved.dynamics.wet_edges);
  CHECK(saved.tip->mask==s.tip->mask);CHECK(saved.spacing==s.spacing);
  const auto revision=first.revision();first.refresh();CHECK(first.revision()==revision);
  CHECK(first.save("Renamed",saved,false,id)==id);other.refresh();CHECK(other.preset(id)["name"]=="Renamed");
  first.remove(id);other.refresh();bool stale=false;
  try{(void)other.resolve(QJsonObject{{"presetId",id}});}catch(const std::exception&){stale=true;}CHECK(stale);
  QFile blocked(dir.path()+"/blocked");CHECK(blocked.open(QIODevice::WriteOnly));blocked.close();
  BrushAutomationLibrary unwritable(tips,nullptr,blocked.fileName());
  const auto unchanged=unwritable.revision();bool failed=false;
  try{(void)unwritable.save("Cannot save",s,false);}catch(const std::exception&){failed=true;}
  CHECK(failed);CHECK(unwritable.revision()==unchanged);CHECK(unwritable.presets().size()==first.presets().size());
}
void ui_script_advanced_brush_pen_pose_and_dab_cancellation() {
  using namespace patchy::ui;
  MainWindow window;show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).activeLayer.fill('#c6bdab');"));
  auto& host=window.script_engine_host();auto* canvas=MainWindowTestAccess::canvas(window);
  auto& library=window.brush_automation_library();canvas->set_zoom(4);
  const QStringList configs{
    R"({"dynamics":{"angleControl":"penRotation","roundnessControl":"penTilt","minimumRoundness":0.2,"opacityControl":"stylusWheel"}})",
    R"({"dynamics":{"sizeControl":"penPressure","opacityControl":"off"}})",
    R"({"pen":{"pressureSize":false,"pressureOpacity":true,"tiltShape":true}})",
    R"({"tool":"eraser","pen":{"pressureSize":true,"pressureOpacity":false}})"
  };
  for(const auto& config:configs) {
    auto settings=QJsonDocument::fromJson(config.toUtf8()).object();settings["size"]=23;settings["roundness"]=45;
    settings["color"]="#713828";settings["flow"]=60;
    const auto encoded=QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
    CHECK(run_script(window,"var s="+encoded+R"JS(;s.points=[
      {x:12,y:20,pressure:.3,xTilt:10,yTilt:15,rotation:20,tangentialPressure:-.5},
      {x:40,y:63,pressure:.9,xTilt:35,yTilt:25,rotation:90,tangentialPressure:.3},
      {x:79,y:34,pressure:.6,xTilt:55,yTilt:5,rotation:150,tangentialPressure:.8}];
      app.activeDocument.activeLayer.drawStrokes([s]);)JS"));
    const auto scripted=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(run_script(window,"app.activeDocument.undo();"));canvas->apply_script_brush(library.resolve(settings));
    canvas->set_brush_dynamics_test_seed(0);
    const auto tablet=[&](QEvent::Type type,QPoint p,double pressure,float tilt_x,float tilt_y,double rotation,float wheel) {
      patchy::test::ui::send_tablet(*canvas,type,canvas->widget_position_for_document_point(p),pressure,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton,
        Qt::NoModifier,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure|QInputDevice::Capability::XTilt|
        QInputDevice::Capability::YTilt|QInputDevice::Capability::Rotation|QInputDevice::Capability::TangentialPressure,
        tilt_x,tilt_y,rotation,wheel);
    };
    tablet(QEvent::TabletPress,{12,20},.3,10,15,20,-.5F);
    tablet(QEvent::TabletMove,{40,63},.9,35,25,90,.3F);
    tablet(QEvent::TabletMove,{79,34},.6,55,5,150,.8F);
    tablet(QEvent::TabletRelease,{79,34},.6,55,5,150,.8F);
    const auto native=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(std::equal(scripted.data().begin(),scripted.data().end(),native.data().begin(),native.data().end()));
    MainWindowTestAccess::undo(window);
  }
  // One very long segment must service cancellation inside its native dab loop.
  const auto before=BrushAutomationLibrary::settings(canvas->current_script_brush());
  auto stroke=library.resolve(QJsonObject{{"size",12},{"spacing",.01},{"dynamics",QJsonObject{{"textureEnabled",true}}}});
  ScriptStrokePoint start;start.position={5,5};
  ScriptStrokePoint end;end.position={99999,5};
  stroke.points={start,end};int callbacks=0;
  const auto dirty=canvas->paint_script_stroke(stroke,[&](const QRect&){return ++callbacks>=3;});
  CHECK(!dirty.isEmpty());CHECK(callbacks>=3);CHECK(callbacks<10);
  CHECK(before==BrushAutomationLibrary::settings(canvas->current_script_brush()));
}
void ui_script_advanced_brush_creation_preview_and_psd() {
  using namespace patchy::ui;
  MainWindow window;show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).activeLayer.fill('#dbc8a7');"));
  auto& host=window.script_engine_host();auto& tips=window.brush_tip_library();auto& library=window.brush_automation_library();
  QString tip_id,preset_id;
  const auto cleanup=qScopeGuard([&]{if(!tip_id.isEmpty())tips.remove_tip(tip_id);if(!preset_id.isEmpty()){try{library.remove(preset_id);}catch(const std::exception&){}}});
  CHECK(run_script(window,R"JS(
    var bytes=new Uint8Array(12*8);for(var y=1;y<7;y++)for(var x=1;x<11;x++)bytes[y*12+x]=(x%3)?255:80;
    var tip=patchy.brushes.createTip('Test brush',{width:12,height:8,data:bytes.buffer});
    var preset=patchy.brushes.savePreset('Test oil',{tipId:tip.id,size:17,dynamics:{textureEnabled:true,textureDepth:.4}});
    patchy.setResult({tip:tip.id,preset:preset.id});
  )JS"));
  tip_id=host.last_result().toObject()["tip"].toString();preset_id=host.last_result().toObject()["preset"].toString();CHECK(!tip_id.isEmpty());CHECK(!preset_id.isEmpty());
  auto* combo=window.findChild<QComboBox*>("brushPresetCombo");CHECK(combo);CHECK(combo->findData(preset_id)>=0);
  const auto before=host.automation_fingerprint();
  const auto preview=QStringLiteral("test-artifacts/\u6bdb-swatch.png");
  CHECK(run_script(window,"patchy.brushes.renderPreview('"+preview+"',{presetId:'"+preset_id+"'});"));
  CHECK(QFileInfo::exists(preview));CHECK(host.automation_fingerprint()==before);
  CHECK(run_script(window,"var l=app.activeDocument.addLayer('Fur');l.drawStrokes([{presetId:'"+preset_id+"',color:'#9a4b2b',points:[{x:10,y:20},{x:45,y:60},{x:80,y:30}]}]);"));
  const auto saved=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(run_script(window,"if(!app.activeDocument.saveAs('test-artifacts/advanced-brush.psd'))throw Error('save');app.activeDocument.close();app.open('test-artifacts/advanced-brush.psd');"));
  const auto reopened=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(saved.data().begin(),saved.data().end(),reopened.data().begin(),reopened.data().end()));
  combo->setCurrentIndex(combo->findData(preset_id));
  CHECK(MainWindowTestAccess::canvas(window)->has_brush_tip());
  for (int repeat=0;repeat<2;++repeat) {
    bool opened=false;
    QTimer::singleShot(0,&window,[&] {
      if(auto* dialog=window.findChild<QInputDialog*>()) {opened=true;dialog->reject();}
    });
    combo->setCurrentIndex(combo->findData("__saveBrush"));
    CHECK(opened);CHECK(combo->currentData().toString()==preset_id);
  }
  save_widget_artifact("advanced_brush_presets",window);
}

std::vector<patchy::test::TestCase> scripting_tests() {
  return {
      {"ui_script_advanced_brush_pen_pose_and_dab_cancellation",ui_script_advanced_brush_pen_pose_and_dab_cancellation},
      {"ui_script_advanced_brush_native_parity",ui_script_advanced_brush_native_parity},
      {"ui_script_advanced_brush_timing_validation_and_restore",ui_script_advanced_brush_timing_validation_and_restore},
      {"ui_script_advanced_brush_presets_snapshots_and_refresh",ui_script_advanced_brush_presets_snapshots_and_refresh},
      {"ui_script_advanced_brush_creation_preview_and_psd",ui_script_advanced_brush_creation_preview_and_psd},
      {"ui_script_automation_strokes_match_native_and_undo", ui_script_automation_strokes_match_native_and_undo},
      {"ui_script_automation_preview_ids_and_errors", ui_script_automation_preview_ids_and_errors},
      {"ui_script_automation_pressure_seed_selection_palette", ui_script_automation_pressure_seed_selection_palette},
      {"ui_script_mutations_ride_single_undo_entry", ui_script_mutations_ride_single_undo_entry},
      {"ui_script_stale_layer_wrapper_throws", ui_script_stale_layer_wrapper_throws},
      {"ui_script_pixels_roundtrip_and_palette_snap", ui_script_pixels_roundtrip_and_palette_snap},
      {"ui_script_get_pixels_reads_rgb_layers", ui_script_get_pixels_reads_rgb_layers},
      {"ui_script_fill_rect_partial_updates", ui_script_fill_rect_partial_updates},
      {"ui_script_canvas_window_receives_space_key", ui_script_canvas_window_receives_space_key},
      {"ui_script_canvas_window_dismisses_stop_panel",
       ui_script_canvas_window_dismisses_stop_panel},
      {"ui_script_canvas_window_suppresses_stop_panel",
       ui_script_canvas_window_suppresses_stop_panel},
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
      {"ui_script_text_layer_with_uncovered_script_does_not_crash",
       ui_script_text_layer_with_uncovered_script_does_not_crash},
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
      {"ui_sound_build_tone_wav_shape", ui_sound_build_tone_wav_shape},
      {"ui_script_play_tone_and_sound_offscreen", ui_script_play_tone_and_sound_offscreen},
      {"ui_script_fancy_background_runs_standalone", ui_script_fancy_background_runs_standalone},
      {"ui_script_dialog_pickers_listfiles_args_cli_defaults",
       ui_script_dialog_pickers_listfiles_args_cli_defaults},
      {"ui_script_run_command_triggers_actions", ui_script_run_command_triggers_actions},
      {"ui_script_cli_directive_and_example_command",
       ui_script_cli_directive_and_example_command},
      {"ui_script_manager_cli_example_dialog", ui_script_manager_cli_example_dialog},
      {"ui_script_scripting_guide_opens_from_help", ui_script_scripting_guide_opens_from_help},
      {"ui_ai_setup_dialog_opens_from_help", ui_ai_setup_dialog_opens_from_help},
      {"ui_ai_setup_blurb_reports_missing_and_flatpak_forms",
       ui_ai_setup_blurb_reports_missing_and_flatpak_forms},
      {"ui_ai_setup_examples_keep_installation_prompt_stable", ui_ai_setup_examples_keep_installation_prompt_stable},
      {"ui_script_ui_view_zoom", ui_script_ui_view_zoom},
      {"ui_script_ui_staging_apis", ui_script_ui_staging_apis},
      {"ui_script_active_layer_setter_reveals_row", ui_script_active_layer_setter_reveals_row},
      {"ui_script_io_round_trips_unicode_path", ui_script_io_round_trips_unicode_path},
      {"ui_script_unattended_normalizes_forms_and_guards_commands", ui_script_unattended_normalizes_forms_and_guards_commands},
      {"ui_script_geometry_rgb_fill_and_empty_text_regressions", ui_script_geometry_rgb_fill_and_empty_text_regressions},
  };
}
