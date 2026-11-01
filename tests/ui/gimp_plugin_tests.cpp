// GIMP 3 plug-ins in the application (docs/gimp-plugins.md): the run box states
// offscreen, and, when GIMP 3 is installed, Patchy's own test plug-in
// (test-fixtures/gimp-plugins/patchy-test) found through patchy.gimpPlugins and run
// through the real helper: tiles in and out limited to the selection, the context and
// message calls, a clear refusal for a GIMP function Patchy lacks, 16 and 32-bit
// layers, and undo. Tests that need GIMP skip without it.

#include "core/pixel_depth.hpp"
#include "ui/app_settings.hpp"
#include "ui/command_search.hpp"
#include "filters/filter_engine.hpp"
#include "plugins/gimp/gimp_install.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/gimp_plugin_catalog.hpp"
#include "ui/gimp_plugin_run_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui/script_engine.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFile>
#include <QImage>
#include <QElapsedTimer>
#include <QEvent>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QScopeGuard>
#include <QString>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace patchy::test::ui;

bool run_script(patchy::ui::MainWindow& window, const QString& source, int timeout_ms = 120000) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("gimp-plugin-test");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

// Prints the script's messages, so a failed run says why.
bool run_script_verbose(patchy::ui::MainWindow& window, const QString& source) {
  const bool ok = run_script(window, source);
  if (!ok) {
    for (const auto& line : window.script_engine_host().message_backlog()) {
      std::fprintf(stderr, "  script: %s\n", qPrintable(line));
    }
  }
  return ok;
}

bool backlog_contains(patchy::ui::MainWindow& window, const QString& needle) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(needle)) {
      return true;
    }
  }
  return false;
}

void dump_backlog(patchy::ui::MainWindow& window) {
  for (const auto& line : window.script_engine_host().message_backlog()) {
    std::fprintf(stderr, "  script: %s\n", qPrintable(line));
  }
}

void ui_gimp_plugin_run_dialog_states() {
  patchy::ui::GimpPluginRunDialog box(QStringLiteral("Blinds"));
  CHECK(box.state() == patchy::ui::GimpPluginRunDialog::State::Starting);
  box.set_plugin_window(QRect(100, 100, 400, 300));
  CHECK(box.state() == patchy::ui::GimpPluginRunDialog::State::WaitingForUser);
  auto* message = box.findChild<QLabel*>(QStringLiteral("gimpPluginRunMessage"));
  CHECK(message != nullptr && message->text().contains(QStringLiteral("Blinds")));
  // Progress while the dialog is up is the plug-in's own preview, not the result.
  box.set_progress(0.5, QStringLiteral("Previewing"));
  CHECK(box.state() == patchy::ui::GimpPluginRunDialog::State::WaitingForUser);
  box.set_plugin_window(QRect());
  box.set_progress(0.25, QStringLiteral("Adding blinds"));
  CHECK(box.state() == patchy::ui::GimpPluginRunDialog::State::Applying);
  CHECK(message != nullptr && message->text().contains(QStringLiteral("Adding blinds")));
  CHECK(!box.stop_requested());
  auto* stop = box.findChild<QPushButton*>(QStringLiteral("gimpPluginStopButton"));
  CHECK(stop != nullptr);
  if (stop != nullptr) {
    stop->click();
  }
  CHECK(box.stop_requested());
}

// Points the scan at the test plug-in only (PATCHY_GIMP_SCAN_ONLY_ADDED keeps GIMP's own
// plug-ins out, so the test does not query dozens of programs) and skips without GIMP 3.
void use_test_plugin_folder(patchy::ui::MainWindow& window) {
  qputenv("PATCHY_GIMP_SCAN_ONLY_ADDED", "1");
  const auto folder = QDir::cleanPath(QStringLiteral(PATCHY_SOURCE_DIR "/test-fixtures/gimp-plugins"));
  CHECK(run_script(window, QStringLiteral("patchy.gimpPlugins.folders = ['%1'];\n"
                                          "var install = patchy.gimpPlugins.install();\n"
                                          "console.log('gimp found ' + install.found + ' ' + (install.version || '') + ' ' + install.problem);\n"
                                          "install.failedFiles.forEach(function (f) { console.log('gimp test plug-in failed: ' + f.error); });\n"
                                          "var ids = patchy.gimpPlugins.list().map(function (p) { return p.id; });\n"
                                          "console.log('gimp test plug-in ' + (ids.indexOf('gimp.patchy-test-invert-bands') >= 0));\n")
                               .arg(folder)));
  if (!backlog_contains(window, QStringLiteral("gimp found true"))) {
    qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
    patchy::test::skip("GIMP 3 is not installed");
  }
  if (!backlog_contains(window, QStringLiteral("gimp test plug-in true"))) {
    // GIMP's own Python may not start here (macOS lets only GIMP start it).
    qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
    patchy::test::skip("the Python test plug-in cannot run with this GIMP install");
  }
}

void ui_gimp_plugin_script_api_lists_and_runs_test_plugin_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  CHECK(run_script(window, QStringLiteral(
                               "var list = patchy.gimpPlugins.list();\n"
                               "for (var i = 0; i < list.length; ++i) {\n"
                               "  var p = list[i];\n"
                               "  console.log('proc ' + p.id + '|' + p.name + '|' + p.menu + '|' + p.supported + '|' + p.params.map(function(a) { return a.name; }).join(','));\n"
                               "}\n")));
  CHECK(backlog_contains(window, QStringLiteral("proc gimp.patchy-test-invert-bands|Invert Bands...|Patchy Test|true|band")));
  CHECK(backlog_contains(window, QStringLiteral("proc gimp.patchy-test-context|Paint Context|Patchy Test|true|")));
  if (!backlog_contains(window, QStringLiteral("proc gimp.patchy-test-invert-bands"))) {
    dump_backlog(window);
    return;
  }

  // Invert Bands over a selection: rows 30..129 of columns 20..169 alternate in bands of
  // 10, everything outside the selection stays. One undo takes it all back.
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.newDocument(300, 200, {name: 'gimp test'});\n"
      "var layer = doc.activeLayer;\n"
      "layer.fillRect(0, 0, 300, 200, '#204060');\n"
      "doc.selection.selectRect(20, 30, 150, 100);\n")));
  // The plug-in in a script of its own, so undo takes back exactly its run.
  CHECK(run_script_verbose(window, QStringLiteral(
      "var layer = app.activeDocument.activeLayer;\n"
      "layer.applyGimpPlugin('gimp.patchy-test-invert-bands', {args: {band: 10}});\n"
      "var px = layer.getPixels();\n"
      "var d = new Uint8Array(px.data);\n"
      "function at(x, y) { var i = ((y - px.y) * px.width + (x - px.x)) * 4; return d[i] + ',' + d[i + 1] + ',' + d[i + 2]; }\n"
      "console.log('band inside ' + at(25, 32) + ' next ' + at(25, 42) + ' outside ' + at(10, 32) + ' below ' + at(25, 135));\n")));
  // The run is one undo step (a script may undo only before its own edits).
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.activeDocument;\n"
      "doc.undo();\n"
      "var px = doc.activeLayer.getPixels(); var d = new Uint8Array(px.data);\n"
      "var i = ((32 - px.y) * px.width + (25 - px.x)) * 4;\n"
      "console.log('after undo ' + d[i] + ',' + d[i + 1] + ',' + d[i + 2]);\n")));
  CHECK(backlog_contains(window, QStringLiteral("band inside 223,191,159 next 32,64,96 outside 32,64,96 below 32,64,96")));
  CHECK(backlog_contains(window, QStringLiteral("after undo 32,64,96")));

  // Paint Context: image, layer and context calls; the foreground color fills the
  // selection, and the plug-in's message reaches Patchy.
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.activeDocument;\n"
      "doc.selection.selectRect(200, 150, 40, 30);\n"
      "doc.activeLayer.applyGimpPlugin('gimp.patchy-test-context');\n"
      "var px = doc.activeLayer.getPixels(); var d = new Uint8Array(px.data);\n"
      "function at(x, y) { var i = ((y - px.y) * px.width + (x - px.x)) * 4; return d[i] + ',' + d[i + 1] + ',' + d[i + 2]; }\n"
      "console.log('context ' + at(210, 160) + ' outside ' + at(195, 160));\n")));
  CHECK(backlog_contains(window, QStringLiteral("context 0,0,0 outside 32,64,96")));

  // A procedure that needs a GIMP function Patchy lacks fails clearly and changes nothing.
  CHECK(!run_script(window, QStringLiteral(
      "app.activeDocument.selection.selectAll();\n"
      "app.activeDocument.activeLayer.applyGimpPlugin('gimp.patchy-test-unsupported');\n")));
  CHECK(backlog_contains(window, QStringLiteral("gimp-layer-new")));

  // Unknown arguments and unknown ids are refused before anything runs.
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyGimpPlugin('gimp.patchy-test-invert-bands', {args: {nope: 1}});")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyGimpPlugin('gimp.patchy-test-invert-bands', {args: {band: 0}});")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyGimpPlugin('gimp.no-such-procedure');")));
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

void ui_gimp_plugin_runs_on_deep_layers_if_available() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  // 16-bit integer and 32-bit float layers travel at their own depth: the plug-in asks
  // for 8-bit pixels and GIMP's own conversion in the plug-in does the rest.
  for (const int bits : {16, 32}) {
    CHECK(run_script_verbose(window, QStringLiteral(
        "var doc = app.newDocument(64, 48, {name: 'deep'});\n"
        "doc.activeLayer.fillRect(0, 0, 64, 48, '#ffffff');\n"
        "doc.convertBitDepth(%1);\n"
        "doc.activeLayer.applyGimpPlugin('gimp.patchy-test-invert-bands', {args: {band: 4}});\n"
        "var px = doc.activeLayer.getPixels(); var d = new Uint8Array(px.data);\n"
        "console.log('deep %1 ' + doc.bitDepth + ' ' + d[0] + ' ' + d[(5 * 64) * 4]);\n")
                                 .arg(bits)));
    CHECK(backlog_contains(window, QStringLiteral("deep %1 %1 0 255").arg(bits)));
  }
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

#ifdef Q_OS_WIN
// The plug-in's own dialog, interactive: it opens, its preview draws from Patchy's tiles,
// OK applies the result, Cancel leaves the layer as it was. Needs the interactive desktop
// (the GTK dialog is a real window); the image of the dialog goes to test-artifacts.
void ui_gimp_plugin_dialog_ok_and_cancel_if_available() {
  wchar_t station[64] = {};
  DWORD needed = 0;
  if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &needed) == FALSE ||
      _wcsicmp(station, L"WinSta0") != 0) {
    patchy::test::skip("no interactive desktop for the plug-in's dialog");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  ensure_artifact_dir();
  const auto shot = QDir::current().filePath(QStringLiteral("test-artifacts/gimp-plugin-dialog.png"));
  QFile::remove(shot);
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.newDocument(240, 160, {name: 'dialog'});\n"
      "doc.activeLayer.fillRect(0, 0, 240, 160, '#204060');\n")));
  CHECK(run_script_verbose(window, QStringLiteral(
      "var layer = app.activeDocument.activeLayer;\n"
      "layer.applyGimpPlugin('gimp.patchy-test-invert-bands', {dialog: true, captureDialog: '%1', answerDialog: 'ok'});\n"
      "var px = layer.getPixels(); var d = new Uint8Array(px.data);\n"
      "console.log('dialog ok ' + d[0] + ',' + d[1] + ',' + d[2]);\n")
                                   .arg(shot)));
  // The plug-in's own default band is 8 rows: the first band is inverted.
  CHECK(backlog_contains(window, QStringLiteral("dialog ok 223,191,159")));
  CHECK(QImage(shot).width() > 100);
  CHECK(!run_script(window, QStringLiteral(
      "var layer = app.activeDocument.activeLayer;\n"
      "try { layer.applyGimpPlugin('gimp.patchy-test-invert-bands', {dialog: true, answerDialog: 'cancel'}); }\n"
      "finally { var px = layer.getPixels(); var d = new Uint8Array(px.data); console.log('dialog cancel ' + d[0]); }\n")));
  CHECK(backlog_contains(window, QStringLiteral("was cancelled")));
  CHECK(backlog_contains(window, QStringLiteral("dialog cancel 223")));
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

// GitHub issue 567 follow-up: a plug-in started from its menu entry ran inside the
// menu's mouse handling, so a menu deleted during the run (a rescan rebuilds them) left
// Qt's popup code returning into a freed window. The run now starts after the click has
// been handled; the menu is deleted mid-run here, on a 3-channel layer (a JPEG, no alpha)
// through the plug-in's own dialog, answered OK. Before the fix this test crashed.
void ui_gimp_plugin_menu_run_survives_menu_deletion_if_available() {
  wchar_t station[64] = {};
  DWORD needed = 0;
  if (GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &needed) == FALSE ||
      _wcsicmp(station, L"WinSta0") != 0) {
    patchy::test::skip("no interactive desktop for the plug-in's dialog");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  ensure_artifact_dir();
  const auto jpeg = QDir::current().filePath(QStringLiteral("test-artifacts/gimp-menu-run.jpg"));
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.newDocument(240, 160, {name: 'menu run'});\n"
      "doc.activeLayer.fillRect(0, 0, 240, 160, '#204060');\n"
      "doc.saveAs('%1'); doc.close();\n"
      "app.open('%1');\n").arg(jpeg)));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  if (!layer_id.has_value()) return;
  CHECK(std::as_const(document).find_layer(*layer_id)->pixels().format().channels == 3);
  QAction* invert = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
    if (action->data().toString() == QStringLiteral("gimp.patchy-test-invert-bands")) invert = action;
  }
  CHECK(invert != nullptr);
  if (invert == nullptr) return;
  QMenu* menu = nullptr;
  for (auto* object : invert->associatedObjects()) {
    if (auto* m = qobject_cast<QMenu*>(object)) menu = m;
  }
  CHECK(menu != nullptr);
  if (menu == nullptr) return;
  patchy::ui::set_stored_gimp_plugin_own_dialog(QStringLiteral("patchy-test-invert-bands"), true);
  const auto reset = qScopeGuard([] {
    patchy::ui::set_stored_gimp_plugin_own_dialog(QStringLiteral("patchy-test-invert-bands"), true);  // the default
  });
  patchy::ui::MainWindowTestAccess::set_gimp_plugin_dialog_answer(window, 1);
  // As a person does it: Plugins > GIMP 3 Plug-ins opened, then its Patchy Test submenu.
  // The parent menu closes last with the pointer outside it, which makes Qt replay the
  // click and touch the submenu's window after the click has been handled.
  auto* gimp_menu = window.findChild<QMenu*>(QStringLiteral("gimpPluginsMenu"));
  CHECK(gimp_menu != nullptr);
  if (gimp_menu == nullptr) return;
  gimp_menu->popup(window.mapToGlobal(QPoint(100, 100)));
  CHECK(QTest::qWaitForWindowExposed(gimp_menu));
  gimp_menu->setActiveAction(menu->menuAction());
  QTest::keyClick(gimp_menu, Qt::Key_Right);
  CHECK(QTest::qWaitForWindowExposed(menu));
  QPointer<QMenu> doomed(menu);
  QTimer::singleShot(700, &window, [&window, doomed] {
    // What a rescan finishing now does, with the deferred deletion carried out at once.
    patchy::ui::MainWindowTestAccess::rebuild_gimp_plugins_menu(window);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(doomed == nullptr);
  });
  QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(invert).center());
  // The click returned before the plug-in ran; the menu goes away while it runs (above).
  const auto first_pixel = [&document, &layer_id] {
    const auto* layer = std::as_const(document).find_layer(*layer_id);
    return layer == nullptr ? -1 : static_cast<int>(layer->pixels().row(0)[0]);
  };
  CHECK(first_pixel() < 100);
  CHECK(process_events_until([&] { return first_pixel() > 200; }, 60000));
  QTest::qWait(300);
  CHECK(window.findChild<QDialog*>(QStringLiteral("gimpPluginErrorMessageBox")) == nullptr);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}
#endif

// Preferences > Plug-ins lists the GIMP install, the added folder and what each procedure
// can do, with the reason for the ones that cannot run.
void ui_gimp_plugins_preferences_page_lists_test_plugin_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  bool saw_page = false;
  QTimer::singleShot(0, [&] {
    QDialog* preferences = nullptr;
    process_events_until([&] {
      preferences = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
      return preferences != nullptr && preferences->isVisible();
    });
    CHECK(preferences != nullptr);
    if (preferences == nullptr) {
      return;
    }
    if (auto* pages = preferences->findChild<QListWidget*>(QStringLiteral("preferencesPageList")); pages != nullptr) {
      for (int row = 0; row < pages->count(); ++row) {
        if (pages->item(row)->text() == QStringLiteral("Plug-ins")) {
          pages->setCurrentRow(row);
        }
      }
      QApplication::processEvents();
    }
    auto* found = preferences->findChild<QListWidget*>(QStringLiteral("preferencesGimpPluginsFoundList"));
    auto* install = preferences->findChild<QLabel*>(QStringLiteral("preferencesGimpInstallLabel"));
    auto* folders = preferences->findChild<QListWidget*>(QStringLiteral("preferencesGimpPluginFoldersList"));
    CHECK(found != nullptr && install != nullptr && folders != nullptr);
    if (found != nullptr && install != nullptr && folders != nullptr) {
      saw_page = true;
      CHECK(install->text().startsWith(QStringLiteral("GIMP 3.")));
      CHECK(folders->count() == 1);
      bool lists_invert = false;
      for (int row = 0; row < found->count(); ++row) {
        lists_invert = lists_invert || found->item(row)->text().contains(QStringLiteral("Invert Bands"));
      }
      CHECK(lists_invert);
      if (auto* group = preferences->findChild<QWidget*>(QStringLiteral("preferencesGimpPluginsGroup")); group != nullptr) {
        if (auto* scroll = qobject_cast<QScrollArea*>(group->parentWidget() != nullptr
                                                          ? group->parentWidget()->parentWidget()->parentWidget()
                                                          : nullptr);
            scroll != nullptr) {
          scroll->ensureWidgetVisible(group);
        }
        QApplication::processEvents();
        (void)capture_widget_artifact("ui_gimp_plugins_preferences_group", *group);
      }
      // The list alone shows no local folder.
      (void)capture_widget_artifact("ui_gimp_plugins_preferences_list", *found);
    }
    preferences->reject();
  });
  patchy::ui::MainWindowTestAccess::show_preferences(window);
  QApplication::processEvents();
  CHECK(saw_page);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

// Runs `body` inside the event loop of a dialog that is about to open (the dialog's own
// loop must be running, so this never waits for it from outside).
void when_dialog_visible(QObject* context, const QString& object_name, std::function<void(QDialog*)> body) {
  auto poll = std::make_shared<std::function<void()>>();
  *poll = [context, object_name, body = std::move(body), poll] {
    auto* dialog = find_top_level_dialog(object_name);
    if (dialog != nullptr && dialog->isVisible()) {
      body(dialog);
      *poll = nullptr;
      return;
    }
    QTimer::singleShot(20, context, *poll);
  };
  QTimer::singleShot(0, context, *poll);
}

// Patchy's settings dialog for a plug-in whose settings are all among its arguments
// (Invert Bands' band height): the canvas shows the result as soon as the dialog opens,
// a change re-runs it (a run still going for the old value is ended), Cancel puts the
// layer back, and OK reuses the finished preview as one undo step without another run.
void ui_gimp_plugin_settings_dialog_previews_on_canvas_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  patchy::ui::set_stored_gimp_plugin_own_dialog(QStringLiteral("patchy-test-invert-bands"), false);
  patchy::ui::set_stored_gimp_plugin_preview(QStringLiteral("patchy-test-invert-bands"), true);
  patchy::ui::set_stored_gimp_plugin_values(QStringLiteral("patchy-test-invert-bands"), QString());
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.newDocument(240, 160, {name: 'settings'});\n"
      "doc.activeLayer.fillRect(0, 0, 240, 160, '#204060');\n")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  if (!layer_id.has_value()) return;
  const auto red_at = [&document, &layer_id](int y) {
    const auto* layer = std::as_const(document).find_layer(*layer_id);
    return layer == nullptr ? -1 : static_cast<int>(layer->pixels().row(y)[0]);
  };
  QAction* invert = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
    if (action->data().toString() == QStringLiteral("gimp.patchy-test-invert-bands")) invert = action;
  }
  CHECK(invert != nullptr);
  if (invert == nullptr) return;

  // Cancel: the preview (default band 8) shows, then the layer is back.
  bool cancel_checked = false;
  when_dialog_visible(&window, QStringLiteral("gimpPluginSettingsDialog"), [&](QDialog* dialog) {
    CHECK(dialog->findChild<QSpinBox*>(QStringLiteral("gimpArgBandSpin")) != nullptr);
    CHECK(process_events_until([&] { return red_at(0) == 223; }, 30000));
    CHECK(red_at(8) == 32);
    dialog->reject();
    cancel_checked = true;
  });
  invert->trigger();
  CHECK(process_events_until([&] { return cancel_checked; }, 60000));
  CHECK(process_events_until([&] { return find_top_level_dialog(QStringLiteral("gimpPluginSettingsDialog")) == nullptr; }, 5000));
  CHECK(red_at(0) == 32);

  // A change re-runs the preview; a run for the old value is ended when the value moves on.
  bool ok_checked = false;
  int started_before_ok = 0;
  when_dialog_visible(&window, QStringLiteral("gimpPluginSettingsDialog"), [&](QDialog* dialog) {
    auto* spin = dialog->findChild<QSpinBox*>(QStringLiteral("gimpArgBandSpin"));
    CHECK(spin != nullptr);
    if (spin == nullptr) {
      dialog->reject();
      ok_checked = true;
      return;
    }
    CHECK(process_events_until([&] { return red_at(0) == 223; }, 30000));
    const int aborted = patchy::ui::MainWindowTestAccess::gimp_preview_runs_aborted(window);
    const int started = patchy::ui::MainWindowTestAccess::gimp_preview_runs_started(window);
    spin->setValue(16);
    CHECK(process_events_until([&] {
      return patchy::ui::MainWindowTestAccess::gimp_preview_runs_started(window) > started;
    }, 10000));
    spin->setValue(4);  // the band-16 run is still going: it is ended
    CHECK(patchy::ui::MainWindowTestAccess::gimp_preview_runs_aborted(window) > aborted);
    // Band 4: rows 4..7 keep their color, rows 8..11 are inverted.
    CHECK(process_events_until([&] {
      auto* status = dialog->findChild<QLabel*>(QStringLiteral("gimpPluginPreviewStatus"));
      return red_at(0) == 223 && red_at(4) == 32 && red_at(8) == 223 && status != nullptr && status->text().isEmpty();
    }, 30000));
    started_before_ok = patchy::ui::MainWindowTestAccess::gimp_preview_runs_started(window);
    auto* ok = dialog->findChild<QPushButton*>(QStringLiteral("gimpPluginOkButton"));
    CHECK(ok != nullptr);
    if (ok != nullptr) ok->click();
    ok_checked = true;
  });
  invert->trigger();
  CHECK(process_events_until([&] { return ok_checked; }, 90000));
  CHECK(process_events_until([&] { return find_top_level_dialog(QStringLiteral("gimpPluginSettingsDialog")) == nullptr; }, 10000));
  // OK used the finished preview: no further run.
  CHECK(patchy::ui::MainWindowTestAccess::gimp_preview_runs_started(window) == started_before_ok);
  CHECK(red_at(0) == 223 && red_at(4) == 32 && red_at(8) == 223);
  CHECK(run_script_verbose(window, QStringLiteral(
      "var doc = app.activeDocument; doc.undo();\n"
      "var d = new Uint8Array(doc.activeLayer.getPixels().data); console.log('settings undo ' + d[0]);\n")));
  CHECK(backlog_contains(window, QStringLiteral("settings undo 32")));
  // The last values are remembered for the next time.
  CHECK(patchy::ui::stored_gimp_plugin_values(QStringLiteral("patchy-test-invert-bands")).contains(QStringLiteral("\"band\":4")));
  patchy::ui::set_stored_gimp_plugin_values(QStringLiteral("patchy-test-invert-bands"), QString());
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

// Development sweep (PATCHY_GIMP_SWEEP=1, docs/gimp-plugins.md "Results"): every GIMP
// procedure Patchy's settings dialog holds, from its menu entry on a layer without alpha
// (an opened JPEG) and on one with alpha: the preview settles, OK applies. One SWEEP line
// each on stderr. Skips unless asked for: it starts every installed plug-in.
void ui_gimp_plugin_settings_dialog_sweep_if_requested() {
  if (!qEnvironmentVariableIsSet("PATCHY_GIMP_SWEEP")) {
    patchy::test::skip("set PATCHY_GIMP_SWEEP=1 to run every installed plug-in");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
  CHECK(run_script(window, QStringLiteral(
      "patchy.gimpPlugins.folders = [];\npatchy.gimpPlugins.rescan();\n"
      "patchy.gimpPlugins.list().forEach(function (p) { if (p.supported && !p.hidden) console.log('sweepproc ' + p.id); });\n")));
  QStringList ids;
  for (const auto& line : window.script_engine_host().message_backlog()) {
    const auto at = line.indexOf(QStringLiteral("sweepproc "));
    if (at >= 0) ids << line.mid(at + 10).trimmed();
  }
  const auto only = qEnvironmentVariable("PATCHY_GIMP_SWEEP_ONLY");
  ensure_artifact_dir();
  const auto jpeg = QDir::current().filePath(QStringLiteral("test-artifacts/gimp-sweep.jpg"));
  for (const auto& id : ids) {
    if (!only.isEmpty() && !only.split(QLatin1Char(',')).contains(id.mid(5))) continue;
    QAction* action = nullptr;
    for (auto* a : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
      if (a->data().toString() == id) action = a;
    }
    if (action == nullptr) continue;
    for (const bool alpha : {false, true}) {
      CHECK(run_script(window, QStringLiteral(
          "while (app.documents.length > 0) app.documents[0].close();\n"
          "var doc = app.newDocument(320, 240, {name: 'sweep'});\n"
          "for (var y = 0; y < 240; y += 16) for (var x = 0; x < 320; x += 16)\n"
          "  doc.activeLayer.fillRect(x, y, 16, 16, '#' + [(x * 255 / 320) | 0, (y * 255 / 240) | 0, (x ^ y) & 255]\n"
          "    .map(function (v) { return ('0' + v.toString(16)).slice(-2); }).join(''));\n"
          "%1\n"
          "app.activeDocument.selection.selectRect(40, 30, 200, 150);\n")
                                  .arg(alpha ? QString() : QStringLiteral("doc.saveAs('%1'); doc.close(); app.open('%1');").arg(jpeg))));
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      const auto layer_id = document.active_layer_id();
      if (!layer_id.has_value()) continue;
      const auto before = std::as_const(document).find_layer(*layer_id)->pixels();
      QString outcome = QStringLiteral("own-dialog");
      QString detail;
      QElapsedTimer timer;
      timer.start();
      bool patchy_dialog = false;
      // Patchy's dialog: the preview settles, then OK. Plug-ins that open their own dialog
      // get it answered with OK.
      auto alive = std::make_shared<bool>(true);
      auto poll = std::make_shared<std::function<void()>>();
      *poll = [&, alive, poll] {
        if (!*alive) {
          *poll = nullptr;
          return;
        }
        auto* dialog = find_top_level_dialog(QStringLiteral("gimpPluginSettingsDialog"));
        if (dialog == nullptr || !dialog->isVisible()) {
          QTimer::singleShot(20, &window, *poll);
          return;
        }
        *poll = nullptr;
        patchy_dialog = true;
        auto* status = dialog->findChild<QLabel*>(QStringLiteral("gimpPluginPreviewStatus"));
        auto* error = dialog->findChild<QLabel*>(QStringLiteral("gimpPluginPreviewError"));
        (void)process_events_until([&] {
          return (status != nullptr && status->text().isEmpty()) || (error != nullptr && error->isVisible());
        }, 120000);
        outcome = error != nullptr && error->isVisible() ? QStringLiteral("preview-failed") : QStringLiteral("previewed");
        detail = error != nullptr ? error->text().left(160).replace(QLatin1Char('\n'), QLatin1Char(' ')) : QString();
        if (auto* ok = dialog->findChild<QPushButton*>(QStringLiteral("gimpPluginOkButton")); ok != nullptr) ok->click();
        (void)process_events_until([&] { return !dialog->isVisible(); }, 120000);
        if (dialog->isVisible()) dialog->reject();
      };
      QTimer::singleShot(0, &window, *poll);
      // An error box after OK (a modal message) is noted and closed.
      QTimer watcher;
      watcher.setInterval(100);
      QObject::connect(&watcher, &QTimer::timeout, &window, [&] {
        if (auto* box = find_top_level_dialog(QStringLiteral("gimpPluginErrorMessageBox")); box != nullptr && box->isVisible()) {
          if (auto* label = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label")); label != nullptr) {
            detail = label->text().left(160).replace(QLatin1Char('\n'), QLatin1Char(' '));
          }
          outcome = QStringLiteral("failed");
          box->reject();
        }
      });
      watcher.start();
      patchy::ui::MainWindowTestAccess::set_gimp_plugin_dialog_answer(window, 1);
      action->trigger();
      (void)process_events_until([&] { return patchy_dialog || patchy::ui::MainWindowTestAccess::gimp_plugin_active(window); }, 5000);
      (void)process_events_until([&] { return !patchy::ui::MainWindowTestAccess::gimp_plugin_active(window); }, 300000);
      QTest::qWait(400);
      watcher.stop();
      *alive = false;
      patchy::ui::MainWindowTestAccess::set_gimp_plugin_dialog_answer(window, 0);
      if (!patchy_dialog && outcome == QLatin1String("own-dialog")) {
        outcome = QStringLiteral("own-dialog-ok");
      }
      const auto* layer = std::as_const(document).find_layer(*layer_id);
      const bool changed = layer != nullptr && layer->pixels().data().size() == before.data().size() &&
                           std::memcmp(layer->pixels().data().data(), before.data().data(), before.data().size()) != 0;
      const bool format_changed = layer != nullptr && layer->pixels().format().channels != before.format().channels;
      std::fprintf(stderr, "SWEEP %s %s %s changed=%d %lld ms %s\n", qPrintable(id.mid(5)), alpha ? "alpha" : "no-alpha",
                   qPrintable(outcome), (changed || format_changed) ? 1 : 0, static_cast<long long>(timer.elapsed()),
                   qPrintable(detail));
    }
  }
}

// Development captures for the docs and the card (PATCHY_GIMP_CAPTURE=<folder>): the
// CC0 sample photo with Patchy's settings dialog open for each procedure in
// PATCHY_GIMP_CAPTURE_ONLY (default Blinds and Van Gogh), once the canvas shows the full
// preview: <procedure>-window.png and <procedure>-dialog.png.
void ui_gimp_plugin_settings_dialog_capture_if_requested() {
  const auto folder = qEnvironmentVariable("PATCHY_GIMP_CAPTURE");
  if (folder.isEmpty()) {
    patchy::test::skip("set PATCHY_GIMP_CAPTURE=<folder> to capture the settings dialog");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1400, 900);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
  CHECK(run_script(window, QStringLiteral("patchy.gimpPlugins.folders = [];\npatchy.gimpPlugins.rescan();\n")));
  const auto photo = QDir::cleanPath(QStringLiteral(PATCHY_SOURCE_DIR "/test-fixtures/readme/havana_vintage_car_cc0.jpg"));
  const auto only = qEnvironmentVariable("PATCHY_GIMP_CAPTURE_ONLY", QStringLiteral("plug-in-blinds,plug-in-lic"));
  for (const auto& procedure : only.split(QLatin1Char(','))) {
    CHECK(run_script(window, QStringLiteral("while (app.documents.length > 0) app.documents[0].close();\napp.open('%1');\n")
                                 .arg(photo)));
    QAction* action = nullptr;
    for (auto* a : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
      if (a->data().toString() == QStringLiteral("gimp.") + procedure) action = a;
    }
    if (action == nullptr) continue;
    bool done = false;
    when_dialog_visible(&window, QStringLiteral("gimpPluginSettingsDialog"), [&](QDialog* dialog) {
      auto* status = dialog->findChild<QLabel*>(QStringLiteral("gimpPluginPreviewStatus"));
      (void)process_events_until([&] { return status != nullptr && status->text().isEmpty(); }, 120000);
      QTest::qWait(500);
      window.grab().save(QDir(folder).filePath(procedure + QStringLiteral("-window.png")));
      dialog->grab().save(QDir(folder).filePath(procedure + QStringLiteral("-dialog.png")));
      dialog->reject();
      done = true;
    });
    action->trigger();
    CHECK(process_events_until([&] { return done; }, 180000));
  }
  // The menus with a refused entry: Distorts (Pagecurl) and Render (Spyrogimp, Flame).
  for (const auto& id : {QStringLiteral("gimp.plug-in-pagecurl"), QStringLiteral("gimp.plug-in-flame")}) {
    for (auto* a : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
      if (a->data().toString() != id || a->associatedObjects().isEmpty()) continue;
      if (auto* menu = qobject_cast<QMenu*>(a->associatedObjects().front()); menu != nullptr) {
        emit window.findChild<QMenu*>(QStringLiteral("gimpPluginsMenu"))->aboutToShow();
        menu->popup(window.mapToGlobal(QPoint(300, 120)));
        CHECK(QTest::qWaitForWindowExposed(menu));
        QTest::qWait(300);
        menu->grab().save(QDir(folder).filePath(QStringLiteral("menu-") + id.mid(5) + QStringLiteral(".png")));
        menu->hide();
      }
    }
  }
}

// Plugins > GIMP 3 Plug-ins lists every image command a plug-in registers: the ones
// Patchy cannot run are disabled with "(Unsupported)" in their text and the reason as
// tooltip and status tip, and Search Commands lists them too. Runnable ones say what the
// document lacks (no image, a group layer) and turn on when the menu opens again with that
// fixed. File formats and helpers stay hidden.
void ui_gimp_plugins_menu_lists_refused_entries_with_reasons_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
  CHECK(run_script(window, QStringLiteral(
      "patchy.gimpPlugins.folders = [];\npatchy.gimpPlugins.rescan();\n"
      "var ids = patchy.gimpPlugins.list().map(function (p) { return p.id; });\n"
      "console.log('gimp bundled ' + patchy.gimpPlugins.install().found + ' ' + (ids.indexOf('gimp.plug-in-pagecurl') >= 0) + ' ' +\n"
      "            (ids.indexOf('gimp.plug-in-blinds') >= 0));\n")));
  if (!backlog_contains(window, QStringLiteral("gimp bundled true true true"))) {
    patchy::test::skip("GIMP 3 with its bundled plug-ins is not installed");
  }
  // The entry in the menu now (a rebuild's old entries may still await deletion).
  const auto action_for = [&window](const QString& id) -> QAction* {
    for (auto* a : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
      if (a->data().toString() == id && !a->associatedObjects().isEmpty()) return a;
    }
    return nullptr;
  };
  auto* menu = window.findChild<QMenu*>(QStringLiteral("gimpPluginsMenu"));
  CHECK(menu != nullptr);
  if (menu == nullptr) return;
  CHECK(menu->toolTipsVisible());

  auto* pagecurl = action_for(QStringLiteral("gimp.plug-in-pagecurl"));
  CHECK(pagecurl != nullptr);
  if (pagecurl != nullptr) {
    CHECK(!pagecurl->isEnabled());
    CHECK(pagecurl->text().endsWith(QStringLiteral("... (Unsupported)")));
    // Refused entries share one submenu; the category menus hold what runs.
    bool in_unsupported_menu = false;
    for (auto* object : pagecurl->associatedObjects()) {
      if (auto* m = qobject_cast<QMenu*>(object)) {
        in_unsupported_menu = in_unsupported_menu || m->objectName() == QLatin1String("gimpPluginsUnsupportedMenu");
      }
    }
    CHECK(in_unsupported_menu);
    CHECK(pagecurl->toolTip().contains(QStringLiteral("new layer")));
    CHECK(pagecurl->statusTip() == pagecurl->toolTip());
  }
  if (auto* film = action_for(QStringLiteral("gimp.plug-in-film")); film != nullptr) {
    CHECK(!film->isEnabled());
    CHECK(film->text().endsWith(QStringLiteral(" (Unsupported)")));
    CHECK(film->toolTip().contains(QStringLiteral("several open images")));
  }
  // File formats and helpers are not commands (File > Print is one, and is listed).
  for (auto* a : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
    const auto id = a->data().toString();
    CHECK(!id.startsWith(QStringLiteral("gimp.file-png")) && !id.startsWith(QStringLiteral("gimp.file-jpeg")));
  }
  // Search Commands finds the disabled entry, its text saying why.
  bool searched = false;
  for (const auto& entry : patchy::ui::collect_menu_commands(window.menuBar())) {
    if (entry.action == pagecurl) {
      searched = entry.name.contains(QStringLiteral("(Unsupported)"));
    }
  }
  CHECK(searched);

  // State: without an image Blinds says so; with one it runs; a group layer is not a
  // pixel layer.
  auto* blinds = action_for(QStringLiteral("gimp.plug-in-blinds"));
  CHECK(blinds != nullptr);
  if (blinds == nullptr) return;
  CHECK(run_script(window, QStringLiteral("while (app.documents.length > 0) app.documents[0].close();\n")));
  emit menu->aboutToShow();
  CHECK(!blinds->isEnabled());
  CHECK(blinds->text().endsWith(QStringLiteral("(Needs an open image)")));
  CHECK(run_script_verbose(window, QStringLiteral("var doc = app.newDocument(64, 48, {name: 'menu state'});\n"
                                                  "doc.activeLayer.fillRect(0, 0, 64, 48, '#336699');\n")));
  emit menu->aboutToShow();
  CHECK(blinds->isEnabled());
  CHECK(blinds->text() == QStringLiteral("Blinds..."));
  CHECK(run_script_verbose(window, QStringLiteral("app.activeDocument.addGroup('Group');\n")));
  emit menu->aboutToShow();
  CHECK(!blinds->isEnabled());
  CHECK(blinds->text().endsWith(QStringLiteral("(Needs a pixel layer)")));
  CHECK(!blinds->toolTip().isEmpty());
}

// A scan that ends while a popup is open (the user waiting in the Plugins menu) must not
// leave "Scanning GIMP plug-ins..." behind: the menu is rebuilt once the popups close, or
// when the GIMP submenu opens. It used to retry once after a second and then give up, so
// the note stayed until a Rescan.
void ui_gimp_plugins_menu_scan_finishing_under_a_popup_updates_the_menu() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("gimpPluginsMenu"));
  CHECK(menu != nullptr);
  if (menu == nullptr) return;
  const auto has_note = [menu](const char* name) {
    for (auto* a : menu->actions()) {
      if (a->objectName() == QLatin1String(name)) return true;
    }
    return false;
  };
  const auto settle = [](int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
  };
  const auto finish = [&window](const QString& problem) {
    patchy::ui::GimpPluginScanResult result;
    result.problem = problem;
    patchy::ui::MainWindowTestAccess::finish_gimp_plugin_scan(window, std::move(result));
  };
  QMenu popup;
  popup.addAction(QStringLiteral("Something"));

  // The popups close a while after the scan ends: the retry rebuilds then.
  patchy::ui::MainWindowTestAccess::begin_gimp_plugin_scan(window);
  CHECK(has_note("gimpPluginsScanningNote"));
  popup.popup(window.mapToGlobal(QPoint(100, 100)));
  CHECK(QApplication::activePopupWidget() != nullptr);
  finish(QStringLiteral("First scan done"));
  CHECK(has_note("gimpPluginsScanningNote"));  // nothing is deleted under an open popup
  settle(1300);
  CHECK(has_note("gimpPluginsScanningNote"));
  popup.hide();
  CHECK(QApplication::activePopupWidget() == nullptr);
  settle(700);
  CHECK(!has_note("gimpPluginsScanningNote"));
  CHECK(has_note("gimpPluginsProblemNote"));

  // The GIMP submenu opens while a popup is still up: it rebuilds as it opens.
  patchy::ui::MainWindowTestAccess::begin_gimp_plugin_scan(window);
  popup.popup(window.mapToGlobal(QPoint(100, 100)));
  finish(QStringLiteral("Second scan done"));
  CHECK(has_note("gimpPluginsScanningNote"));
  emit menu->aboutToShow();
  CHECK(!has_note("gimpPluginsScanningNote"));
  bool second = false;
  for (auto* a : menu->actions()) {
    second = second || a->text() == QLatin1String("Second scan done");
  }
  CHECK(second);
  popup.hide();
  settle(700);
  CHECK(has_note("gimpPluginsProblemNote"));
}

// GEGL filters from the user's GIMP 3 (docs/gegl-filters.md): the scan through
// patchy-gegl-host maps GEGL's settings (ranges, pixel units, choices), offers the GPL-3
// operations (the helper's --gpl3), leaves out what is not a one-layer image filter, fills
// Plugins > GEGL Filters, and a run lands on the layer through apply_filter as one undo
// step. Skips without GIMP's GEGL.
void ui_gegl_filters_list_and_apply_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::ensure_gegl_filters_scanned(window);
  const auto registry =
      patchy::ui::MainWindowTestAccess::gegl_filter_registry(window, QStringLiteral("gegl.gaussian-blur"));
  if (registry == nullptr || registry->find("gegl.gaussian-blur") == nullptr) {
    patchy::test::skip("GIMP 3 with its GEGL is not installed");
  }
  const auto* blur = registry->find("gegl.gaussian-blur");
  const auto parameter = [](const patchy::FilterDefinition& filter, std::string_view key)
      -> const patchy::FilterParameterDefinition* {
    for (const auto& p : filter.catalog.parameters) {
      if (p.key == key) return &p;
    }
    return nullptr;
  };
  const auto* std_dev = parameter(*blur, "std-dev-x");
  CHECK(std_dev != nullptr);
  if (std_dev != nullptr) {
    CHECK(std_dev->kind == patchy::FilterParameterKind::Double);
    CHECK(std_dev->spatial_scale == patchy::FilterSpatialScale::Pixels);
    CHECK(std_dev->practical_maximum.has_value() && *std_dev->practical_maximum > 1.0);
    CHECK(std::get<double>(std_dev->default_value) == 1.5);
  }
  const auto* abyss = parameter(*blur, "abyss-policy");
  CHECK(abyss != nullptr && abyss->kind == patchy::FilterParameterKind::Option && !abyss->options.empty());
  CHECK(registry->find("gegl.cartoon") != nullptr);  // GPL-3, offered through --gpl3
  CHECK(registry->find("gegl.red-eye-removal") == nullptr);
  CHECK(registry->find("gegl.crop") == nullptr);       // geometry, not a filter
  CHECK(registry->find("gegl.over") == nullptr);       // two inputs

  // Straight through the registry: a run changes a two-color buffer at its edge.
  patchy::PixelBuffer pixels(16, 8, patchy::PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4});
  auto bytes = pixels.data();
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      auto* px = bytes.data() + static_cast<std::size_t>(y) * pixels.stride_bytes() + static_cast<std::size_t>(x) * 4U;
      px[0] = x < 8 ? 255 : 0;
      px[1] = 0;
      px[2] = x < 8 ? 0 : 255;
      px[3] = 255;
    }
  }
  const auto before = pixels;
  auto invocation = registry->default_invocation("gegl.gaussian-blur");
  invocation.parameters["std-dev-x"] = 3.0;
  registry->apply(invocation, pixels);
  CHECK(!(std::as_const(pixels).data()[7 * 4] == std::as_const(before).data()[7 * 4] &&
          std::as_const(pixels).data()[7 * 4 + 2] == std::as_const(before).data()[7 * 4 + 2]));

  // 16-bit and 32-bit (linear) layers run at their own depth.
  for (const auto depth : {patchy::BitDepth::UInt16, patchy::BitDepth::Float32}) {
    CHECK(patchy::deep_filter_support("gegl.gaussian-blur", depth) == patchy::DeepFilterSupport::Native);
    patchy::PixelBuffer deep(16, 8, patchy::PixelFormat{patchy::ColorMode::RGB, depth, 4});
    auto rows = deep.data();
    const auto pixel_bytes = patchy::bytes_per_pixel(deep.format());
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 16; ++x) {
        auto* px = rows.data() + static_cast<std::size_t>(y) * deep.stride_bytes() + static_cast<std::size_t>(x) * pixel_bytes;
        if (depth == patchy::BitDepth::UInt16) {
          const std::uint16_t v[4] = {static_cast<std::uint16_t>(x < 8 ? 65535 : 0), 0, 0, 65535};
          std::memcpy(px, v, sizeof(v));
        } else {
          const float v[4] = {x < 8 ? 1.0F : 0.0F, 0.0F, 0.0F, 1.0F};
          std::memcpy(px, v, sizeof(v));
        }
      }
    }
    const auto deep_before = deep;
    registry->apply(invocation, deep);
    const auto after = std::as_const(deep).data();
    const auto earlier = std::as_const(deep_before).data();
    CHECK(!std::equal(after.begin(), after.end(), earlier.begin(), earlier.end()));
  }

  // A color setting is a choice: Color Overlay in the foreground color paints it.
  if (const auto* overlay = registry->find("gegl.color-overlay"); overlay != nullptr) {
    const auto* value = parameter(*overlay, "value");
    CHECK(value != nullptr && value->kind == patchy::FilterParameterKind::Option);
    auto paint = registry->default_invocation("gegl.color-overlay", patchy::RgbColor{0, 200, 0});
    paint.parameters["value"] = std::string("foreground");
    patchy::PixelBuffer gray(4, 4, patchy::PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 4});
    std::fill(gray.data().begin(), gray.data().end(), std::uint8_t{128});
    registry->apply(paint, gray);
    const auto px = std::as_const(gray).data();
    CHECK(px[1] > 180 && px[0] < 40 && px[2] < 40);
  } else {
    CHECK(false);  // GIMP 3.2's GEGL has Color Overlay
  }

  // GEGL's labels follow the language Patchy asks for, from GEGL's own catalog.
  if (const auto installs = patchy::gimp::detect_gimp_installs(); !installs.empty()) {
    const auto library = patchy::ui::find_gegl_library(installs.front());
    const auto german = patchy::ui::scan_gegl_filters(patchy::ui::gegl_helper_command(installs.front(), library),
                                                      QStringLiteral("de"));
    const auto* german_blur = german.registry != nullptr ? german.registry->find("gegl.gaussian-blur") : nullptr;
    CHECK(german_blur != nullptr && german_blur->display_name != blur->display_name);
    // A second scan in the same language comes from the cache, the same list.
    const auto again = patchy::ui::scan_gegl_filters(patchy::ui::gegl_helper_command(installs.front(), library),
                                                     QStringLiteral("de"));
    CHECK(again.registry != nullptr && german.registry != nullptr &&
          again.registry->filters().size() == german.registry->filters().size());
    CHECK(QFileInfo::exists(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("gegl-filters/operations.json"))));
  }

  // Scripts list them and run one by id.
  CHECK(run_script_verbose(window, QStringLiteral(
      "var filters = patchy.gimpPlugins.geglFilters();\n"
      "var blur = filters.filter(function (f) { return f.id === 'gegl.gaussian-blur'; })[0];\n"
      "var sd = blur.parameters.filter(function (p) { return p.name === 'std-dev-x'; })[0];\n"
      "console.log('gegl list ' + (filters.length > 50) + ' ' + sd.type + ' ' + sd['default']);\n"
      "var doc = app.newDocument(32, 16, {name: 'gegl script'});\n"
      "doc.activeLayer.fillRect(0, 0, 16, 16, '#ff0000');\n"
      "doc.activeLayer.fillRect(16, 0, 16, 16, '#0000ff');\n"
      "doc.activeLayer.applyFilter('gegl.gaussian-blur', {'std-dev-x': 4, 'std-dev-y': 4});\n")));
  CHECK(backlog_contains(window, QStringLiteral("gegl list true number 1.5")));
  {
    // The blur mixed red and blue where they meet.
    auto& scripted = patchy::ui::MainWindowTestAccess::document(window);
    const auto* layer = std::as_const(scripted).find_layer(*scripted.active_layer_id());
    CHECK(layer != nullptr);
    if (layer != nullptr) {
      const auto scripted_bytes = layer->pixels().data();
      const auto* mid = scripted_bytes.data() + 8 * layer->pixels().stride_bytes() +
                        16 * patchy::bytes_per_pixel(layer->pixels().format());
      CHECK(mid[0] > 30 && mid[2] > 30);
    }
  }

  // Plugins > GEGL Filters lists it.
  bool listed = false;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("geglFilterAction"))) {
    listed = listed || action->data().toString() == QStringLiteral("gegl.gaussian-blur");
  }
  CHECK(listed);

  // Through apply_filter onto a layer, one undo step.
  CHECK(run_script_verbose(window, QStringLiteral("var doc = app.newDocument(64, 48, {name: 'gegl'});\n"
                                                  "doc.activeLayer.fillRect(0, 0, 32, 48, '#ff0000');\n"
                                                  "doc.activeLayer.fillRect(32, 0, 32, 48, '#0000ff');\n")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  if (!layer_id.has_value()) return;
  const auto original = std::as_const(document).find_layer(*layer_id)->pixels();
  patchy::ui::MainWindowTestAccess::apply_filter_without_dialog(window, QStringLiteral("gegl.gaussian-blur"), invocation);
  const auto& filtered = std::as_const(document).find_layer(*layer_id)->pixels();
  CHECK(!(filtered.width() == original.width() &&
          std::equal(filtered.data().begin(), filtered.data().end(), original.data().begin(), original.data().end())));
  CHECK(run_script_verbose(window, QStringLiteral("app.activeDocument.undo();\n")));
  const auto& undone = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(*layer_id)->pixels();
  CHECK(std::equal(undone.data().begin(), undone.data().end(), original.data().begin(), original.data().end()));

  // The menu command opens Patchy's filter dialog from GEGL's settings (saved to
  // test-artifacts/gegl-<id>-dialog.png for visual QA); Cancel leaves the layer alone.
  // Vignette shows a color setting as a choice.
  const auto open_dialog = [&](const QString& identifier, const QString& control) {
    bool saw_dialog = false;
    int polls = 0;
    QTimer poll;
    poll.setInterval(250);
    QObject::connect(&poll, &QTimer::timeout, &window, [&] {
      ++polls;
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
      if (dialog == nullptr) {
        if (polls > 80) {  // 20 s: close whatever is modal so a failure ends the test
          poll.stop();
          if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
        }
        return;
      }
      if (polls < 10) {
        return;  // let the previews land first
      }
      poll.stop();
      saw_dialog = dialog->property("patchy.filterIdentifier").toString() == identifier &&
                   dialog->findChild<QWidget*>(control) != nullptr;
      save_widget_artifact(identifier.mid(5).prepend(QStringLiteral("gegl-")).toStdString() + "-dialog", *dialog);
      dialog->reject();
    });
    poll.start();
    patchy::ui::MainWindowTestAccess::apply_filter_with_dialog(window, identifier);
    CHECK(saw_dialog);
    const auto& cancelled = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(*layer_id)->pixels();
    CHECK(std::equal(cancelled.data().begin(), cancelled.data().end(), original.data().begin(), original.data().end()));
  };
  open_dialog(QStringLiteral("gegl.cartoon"), QStringLiteral("geglParam_mask-radiusSlider"));
  open_dialog(QStringLiteral("gegl.vignette"), QStringLiteral("geglParam_colorCombo"));

  // Search Commands finds them by name, GEGL's description as the line under it.
  bool searched = false;
  for (const auto& entry : patchy::ui::collect_menu_commands(window.menuBar())) {
    if (entry.action != nullptr && entry.action->data().toString() == QLatin1String("gegl.cartoon")) {
      searched = entry.name.contains(QStringLiteral("Cartoon"));
    }
  }
  CHECK(searched);
}

void ui_gimp_plugins_menu_lists_test_plugin_if_available() {
  patchy::ui::MainWindow window;
  show_window(window);
  use_test_plugin_folder(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("gimpPluginsMenu"));
  CHECK(menu != nullptr);
  QAction* invert = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("gimpPluginAction"))) {
    if (action->data().toString() == QStringLiteral("gimp.patchy-test-invert-bands")) {
      invert = action;
    }
  }
  CHECK(invert != nullptr);
  if (invert != nullptr) {
    CHECK(invert->text() == QStringLiteral("Invert Bands..."));
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("gimpPluginsRescanAction")) != nullptr);
  qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
}

}  // namespace

std::vector<patchy::test::TestCase> gimp_plugin_tests() {
  return {
      {"ui_gimp_plugin_run_dialog_states", ui_gimp_plugin_run_dialog_states},
      {"ui_gimp_plugin_script_api_lists_and_runs_test_plugin_if_available",
       ui_gimp_plugin_script_api_lists_and_runs_test_plugin_if_available},
      {"ui_gimp_plugin_runs_on_deep_layers_if_available", ui_gimp_plugin_runs_on_deep_layers_if_available},
      {"ui_gimp_plugins_menu_lists_test_plugin_if_available", ui_gimp_plugins_menu_lists_test_plugin_if_available},
      {"ui_gegl_filters_list_and_apply_if_available", ui_gegl_filters_list_and_apply_if_available},
      {"ui_gimp_plugins_menu_scan_finishing_under_a_popup_updates_the_menu",
       ui_gimp_plugins_menu_scan_finishing_under_a_popup_updates_the_menu},
      {"ui_gimp_plugins_menu_lists_refused_entries_with_reasons_if_available",
       ui_gimp_plugins_menu_lists_refused_entries_with_reasons_if_available},
      {"ui_gimp_plugin_settings_dialog_sweep_if_requested", ui_gimp_plugin_settings_dialog_sweep_if_requested},
      {"ui_gimp_plugin_settings_dialog_capture_if_requested", ui_gimp_plugin_settings_dialog_capture_if_requested},
      {"ui_gimp_plugin_settings_dialog_previews_on_canvas_if_available",
       ui_gimp_plugin_settings_dialog_previews_on_canvas_if_available},
      {"ui_gimp_plugins_preferences_page_lists_test_plugin_if_available",
       ui_gimp_plugins_preferences_page_lists_test_plugin_if_available},
#ifdef Q_OS_WIN
      {"ui_gimp_plugin_dialog_ok_and_cancel_if_available", ui_gimp_plugin_dialog_ok_and_cancel_if_available},
      {"ui_gimp_plugin_menu_run_survives_menu_deletion_if_available",
       ui_gimp_plugin_menu_run_survives_menu_deletion_if_available},
#endif
  };
}
