// GIMP 3 plug-ins in the application (docs/gimp-plugins.md): the run box states
// offscreen, and, when GIMP 3 is installed, Patchy's own test plug-in
// (test-fixtures/gimp-plugins/patchy-test) found through patchy.gimpPlugins and run
// through the real helper: tiles in and out limited to the selection, the context and
// message calls, a clear refusal for a GIMP function Patchy lacks, 16 and 32-bit
// layers, and undo. Tests that need GIMP skip without it.

#include "core/pixel_depth.hpp"
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
#include <QFile>
#include <QImage>
#include <QElapsedTimer>
#include <QEvent>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScopeGuard>
#include <QString>

#include <cstdio>

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
                                          "console.log('gimp found ' + install.found + ' ' + (install.version || '') + ' ' + install.problem);\n")
                               .arg(folder)));
  if (!backlog_contains(window, QStringLiteral("gimp found true"))) {
    qunsetenv("PATCHY_GIMP_SCAN_ONLY_ADDED");
    patchy::test::skip("GIMP 3 is not installed");
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
      {"ui_gimp_plugins_preferences_page_lists_test_plugin_if_available",
       ui_gimp_plugins_preferences_page_lists_test_plugin_if_available},
#ifdef Q_OS_WIN
      {"ui_gimp_plugin_dialog_ok_and_cancel_if_available", ui_gimp_plugin_dialog_ok_and_cancel_if_available},
#endif
  };
}
