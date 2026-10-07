// Legacy Photoshop plug-ins in the application (docs/plugins.md): the folder
// scan and category submenu, the scripting surface (patchy.plugins,
// layer.applyPlugin), a selection-limited undoable run through the
// out-of-process host, and the handling of files that are not filters. The
// runs are Windows-only (the probe rejects every plug-in elsewhere); the scan
// and listing work everywhere.

#include "core/document.hpp"
#include "core/layer_tree.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/legacy_plugin_folder.hpp"
#include "ui/legacy_plugin_run_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

QString fixture_path(const char* name) {
  return patchy::ui::to_qstring(patchy::test::source_root_path() / "test-fixtures" / "photoshop-plugins" / name);
}

// A fresh folder under test-artifacts holding renamed copies of the fixtures
// (renamed so their identifiers never collide with the auto-scanned fixtures).
QString make_plugin_folder(const QString& leaf, const std::vector<std::pair<const char*, QString>>& copies) {
  ensure_artifact_dir();
  const auto dir = QDir::current().filePath(QStringLiteral("test-artifacts/legacy-plugins/") + leaf);
  remove_test_scratch_dir(dir);
  CHECK(QDir().mkpath(dir + QStringLiteral("/sub")));
  for (const auto& [fixture, target] : copies) {
    CHECK(QFile::copy(fixture_path(fixture), dir + QLatin1Char('/') + target));
  }
  return dir;
}

bool run_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("legacy-plugin-test");
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  // Menu rebuilds retire their old actions with deleteLater; deliver those so
  // findChildren no longer sees them.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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

QAction* find_plugin_action(patchy::ui::MainWindow& window, const QString& identifier) {
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->data().toString() == identifier) {
      return action;
    }
  }
  return nullptr;
}

void ui_legacy_plugin_folder_scan_builds_category_menu() {
  const auto folder = make_plugin_folder(QStringLiteral("scan"), {{"Greyscale64.8bf", QStringLiteral("Custom Grey64.8bf")},
                                                                  {"Greyscale.8bf", QStringLiteral("sub/Custom Grey32.8bf")},
                                                                  {"Greyscale.8bf", QStringLiteral("Custom Grey32 twin.8bf")}});
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  // The folder is added through the scripting API (the same persisted setting
  // the Preferences tab writes); the rescan rebuilds the menu.
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];\n"
                                          "var list = patchy.plugins.list();\n"
                                          "for (var i = 0; i < list.length; ++i) {\n"
                                          "  console.log('plugin ' + list[i].id + '|' + list[i].name + '|' + list[i].category + '|' +\n"
                                          "              list[i].architecture + '|' + list[i].supported);\n"
                                          "}\n")
                               .arg(folder)));
  CHECK(backlog_contains(window, QStringLiteral("plugin legacy.photoshop.Custom Grey64|Greyscale|ViaThinkSoft|x64|")));
  CHECK(backlog_contains(window, QStringLiteral("plugin legacy.photoshop.Custom Grey32|Greyscale|ViaThinkSoft|x86|")));
#ifdef Q_OS_WIN
  CHECK(backlog_contains(window, QStringLiteral("|x64|true")));
  CHECK(backlog_contains(window, QStringLiteral("|x86|true")));
  // Both sit in the plug-in's category submenu under the fixed menu, with the
  // PiPL name as the action text.
  auto* legacy_menu = window.findChild<QMenu*>(QStringLiteral("legacyPluginsMenu"));
  CHECK(legacy_menu != nullptr);
  QMenu* category = nullptr;
  for (auto* menu : legacy_menu->findChildren<QMenu*>(QStringLiteral("legacyPluginCategoryMenu"))) {
    if (menu->title() == QStringLiteral("ViaThinkSoft")) {
      category = menu;
    }
  }
  CHECK(category != nullptr);
  auto* grey64 = find_plugin_action(window, QStringLiteral("legacy.photoshop.Custom Grey64"));
  auto* grey32 = find_plugin_action(window, QStringLiteral("legacy.photoshop.Custom Grey32"));
  CHECK(grey64 != nullptr);
  // A 32-bit copy in another folder (sub/) is its own entry; the 32-bit twin
  // beside the 64-bit file (same name, category and folder) is not a second
  // menu entry, though it stays in the scripting list.
  CHECK(grey32 != nullptr);
  CHECK(find_plugin_action(window, QStringLiteral("legacy.photoshop.Custom Grey32 twin")) == nullptr);
  CHECK(backlog_contains(window, QStringLiteral("plugin legacy.photoshop.Custom Grey32 twin|Greyscale|ViaThinkSoft|x86|")));
  if (grey64 != nullptr && category != nullptr) {
    CHECK(grey64->text() == QStringLiteral("Greyscale"));
    CHECK(category->actions().contains(grey64));
  }
  if (grey32 != nullptr) {
    CHECK(grey32->text() == QStringLiteral("Greyscale"));
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("legacyPluginsEmptyNote")) == nullptr);
#else
  CHECK(backlog_contains(window, QStringLiteral("|x64|false")));
  CHECK(find_plugin_action(window, QStringLiteral("legacy.photoshop.Custom Grey64")) == nullptr);
#endif
  // The same plug-in copied into a second scanned folder (same file name, size,
  // name and bitness) is listed once, not twice.
  const auto copy = make_plugin_folder(QStringLiteral("scan-copy"), {{"Greyscale64.8bf", QStringLiteral("Custom Grey64.8bf")}});
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1', '%2'];\n"
                                          "console.log('copies ' + patchy.plugins.list().filter(function(p) { return p.id.indexOf('Custom Grey64') >= 0; }).length);\n")
                               .arg(folder, copy)));
  CHECK(backlog_contains(window, QStringLiteral("copies 1")));
  // Removing the folder drops its entries again.
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];\n"
                                          "console.log('after ' + patchy.plugins.list().filter(function(p) { return p.id.indexOf('Custom Grey') >= 0; }).length);\n")));
  CHECK(backlog_contains(window, QStringLiteral("after 0")));
  CHECK(find_plugin_action(window, QStringLiteral("legacy.photoshop.Custom Grey64")) == nullptr);
}

void ui_legacy_plugin_non_filter_files_are_listed_not_offered() {
  const auto folder = make_plugin_folder(QStringLiteral("formats"), {{"Greyscale64.8bf", QStringLiteral("Not A Filter.8bi")}});
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];\n"
                                          "var list = patchy.plugins.list();\n"
                                          "for (var i = 0; i < list.length; ++i) {\n"
                                          "  if (list[i].id === 'legacy.photoshop.Not A Filter') {\n"
                                          "    console.log('format ' + list[i].supported + '|' + list[i].reason);\n"
                                          "  }\n"
                                          "}\n")
                               .arg(folder)));
  CHECK(backlog_contains(window, QStringLiteral("format false|")));
  CHECK(find_plugin_action(window, QStringLiteral("legacy.photoshop.Not A Filter")) == nullptr);
  // Asking for it by id throws with the reason instead of running anything.
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('legacy.photoshop.Not A Filter', {dialog: false});")));
  CHECK(backlog_contains(window, QStringLiteral("cannot run")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('legacy.photoshop.no such plug-in');")));
  CHECK(backlog_contains(window, QStringLiteral("Unknown plug-in id")));
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];")));
}

#ifdef Q_OS_WIN
// The auto-scanned fixture next to the binary (developer builds copy
// test-fixtures there).
constexpr const char* kGreyscale64Id = "legacy.photoshop.Greyscale64";
constexpr const char* kGreyscale32Id = "legacy.photoshop.Greyscale";

QColor layer_pixel(patchy::ui::MainWindow& window, int x, int y) {
  auto& document = MainWindowTestAccess::document(window);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  const auto* layer = active.has_value() ? document.find_layer(*active) : nullptr;
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return {};
  }
  const auto& pixels = std::as_const(*layer).pixels();
  const auto* px = pixels.pixel(x - layer->bounds().x, y - layer->bounds().y);
  return QColor(px[0], px[1], px[2]);
}

void ui_legacy_plugin_run_respects_selection_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  auto* canvas = require_canvas(window);
  CHECK(find_plugin_action(window, QString::fromLatin1(kGreyscale64Id)) != nullptr);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#dc1e1e');")));
  CHECK(layer_pixel(window, 50, 50) == QColor(220, 30, 30));

  // Select the top-left 100 x 100 square, then run the 64-bit fixture without
  // its dialog: only the selected square turns grey, in one undo step.
  auto& document = MainWindowTestAccess::document(window);
  patchy::PixelBuffer mask(document.width(), document.height(), patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Square selection"));
  CHECK(canvas->has_selection());
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('%1', {dialog: false});")
                               .arg(QString::fromLatin1(kGreyscale64Id))));
  const auto inside = layer_pixel(window, 50, 50);
  CHECK(inside.red() == inside.green() && inside.green() == inside.blue());
  CHECK(inside.red() > 60 && inside.red() < 120);
  CHECK(layer_pixel(window, 150, 150) == QColor(220, 30, 30));
  CHECK(layer_pixel(window, 50, 150) == QColor(220, 30, 30));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == QColor(220, 30, 30));

  // A selection that is not a rectangle: the plug-in works on the bounding
  // box, only the shape may change (a lasso once became its bounding box).
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    for (int x = 0; x < mask.width(); ++x) {
      row[static_cast<std::size_t>(x)] = (x < 100 && y < 100 && x + y < 100) ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(mask, QStringLiteral("Triangle selection"));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('%1', {dialog: false});")
                               .arg(QString::fromLatin1(kGreyscale64Id))));
  const auto in_triangle = layer_pixel(window, 20, 20);
  CHECK(in_triangle.red() == in_triangle.green() && in_triangle.green() == in_triangle.blue());
  CHECK(layer_pixel(window, 80, 80) == QColor(220, 30, 30));  // in the box, outside the shape
  CHECK(layer_pixel(window, 150, 150) == QColor(220, 30, 30));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 20, 20) == QColor(220, 30, 30));

  // The 32-bit fixture goes through the 32-bit host. It has no menu entry of
  // its own (its 64-bit twin stands for both), so the script runs it by id.
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(find_plugin_action(window, QString::fromLatin1(kGreyscale32Id)) == nullptr);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('%1', {dialog: false});")
                               .arg(QString::fromLatin1(kGreyscale32Id))));
  {
    const auto after = layer_pixel(window, 150, 150);
    CHECK(after.red() == after.green() && after.green() == after.blue());
    CHECK(after.red() > 60 && after.red() < 120);
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(layer_pixel(window, 150, 150) == QColor(220, 30, 30));
  }
  // The menu action is the ordinary path (its own undo entry, whole layer
  // without a selection). It runs the Parameters selector with the real
  // window as the plug-in's owner, which the 64-bit Greyscale fixture cannot
  // take (Filter Foundry's 64-bit standalone build tries a dialog it does not
  // carry and shows a "DialogBoxParam failed" box that waits for a click), so
  // the menu path takes a folder holding only a 32-bit copy.
  const auto menu_folder = make_plugin_folder(QStringLiteral("menu32"), {{"Greyscale.8bf", QStringLiteral("Menu Grey32.8bf")}});
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];").arg(menu_folder)));
  auto* menu_action = find_plugin_action(window, QStringLiteral("legacy.photoshop.Menu Grey32"));
  CHECK(menu_action != nullptr);
  if (menu_action != nullptr) {
    menu_action->trigger();
    QApplication::processEvents();
    const auto after = layer_pixel(window, 150, 150);
    CHECK(after.red() == after.green() && after.green() == after.blue());
    CHECK(after.red() > 60 && after.red() < 120);
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
    CHECK(layer_pixel(window, 150, 150) == QColor(220, 30, 30));
  }
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];")));
  save_widget_artifact("ui_legacy_plugin_selection_run", window);
}

// Third-party plug-ins from local-test-fixtures/photoshop-plugins/mehdi (not
// committed; see agents_local.md). Each is run without its dialog; a plug-in
// that opens one anyway is answered by the runner's auto-accept.
void ui_legacy_plugin_mehdi_filters_run_if_available() {
  const auto mehdi = patchy::test::source_root_path() / "local-test-fixtures" / "photoshop-plugins" / "mehdi";
  if (!std::filesystem::exists(mehdi)) {
    std::cout << "[SKIP] local-test-fixtures/photoshop-plugins/mehdi not present\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];\n"
                                          "var list = patchy.plugins.list();\n"
                                          "for (var i = 0; i < list.length; ++i) {\n"
                                          "  if (list[i].category === 'Mehdi') {\n"  // by name: a copy in the app's plugins folder is listed instead of the fixture
                                          "    console.log('mehdi ' + list[i].id + '|' + list[i].name + '|' + list[i].architecture + '|' + list[i].supported);\n"
                                          "  }\n"
                                          "}\n")
                               .arg(QDir::fromNativeSeparators(patchy::ui::to_qstring(mehdi)))));
  std::vector<QString> ids;
  for (const auto& line : window.script_engine_host().message_backlog()) {
    if (line.contains(QStringLiteral("mehdi legacy.photoshop.")) && line.endsWith(QStringLiteral("|true"))) {
      const auto start = line.indexOf(QStringLiteral("legacy.photoshop."));
      ids.push_back(line.mid(start, line.indexOf(QLatin1Char('|'), start) - start));
    }
  }
  CHECK(!ids.empty());
  int applied = 0;
  for (const auto& id : ids) {
    CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fillRect(0, 0, 300, 300, '#4080c0');")));
    const bool ok = run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyPlugin('%1', {dialog: false});").arg(id));
    std::cout << "  " << id.toStdString() << (ok ? " ran" : " FAILED") << "\n";
    CHECK(ok);
    if (ok) {
      ++applied;
    }
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
  }
  CHECK(applied == static_cast<int>(ids.size()));
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];")));
}

// The plug-in's own dialog, saved to a PNG through the documented captureDialog
// option (the mechanism behind the README screenshot). The 64-bit Absolute
// Color opens its dialog from Start even without a Parameters call, which is
// what makes it capturable unattended.
void ui_legacy_plugin_mehdi_dialog_capture_if_available() {
  const auto mehdi = patchy::test::source_root_path() / "local-test-fixtures" / "photoshop-plugins" / "mehdi";
  if (!std::filesystem::exists(mehdi)) {
    std::cout << "[SKIP] local-test-fixtures/photoshop-plugins/mehdi not present\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  ensure_artifact_dir();
  const auto png = QDir::current().filePath(QStringLiteral("test-artifacts/ui_legacy_plugin_dialog_capture.png"));
  QFile::remove(png);
  // While the plug-in's own dialog is up (the acceptor leaves it alone for
  // 0.7 s first), the companion box must say so and show no busy bar; the
  // busy state belongs to the filtering pass after OK.
  bool saw_waiting = false;
  bool saw_busy_bar_while_waiting = false;
  QTimer probe;
  QObject::connect(&probe, &QTimer::timeout, [&] {
    auto* dialog = qobject_cast<patchy::ui::LegacyPluginRunDialog*>(
        find_top_level_dialog(QStringLiteral("legacyPluginRunDialog")));
    if (dialog == nullptr || dialog->state() != patchy::ui::LegacyPluginRunDialog::State::WaitingForUser) {
      return;
    }
    saw_waiting = true;
    auto* bar = dialog->findChild<QProgressBar*>(QStringLiteral("legacyPluginRunProgressBar"));
    if (bar != nullptr && bar->isVisible()) {
      saw_busy_bar_while_waiting = true;
    }
  });
  probe.start(15);
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];\n"
                                          "app.activeDocument.activeLayer.fillRect(0, 0, 300, 300, '#4080c0');\n"
                                          "var list = patchy.plugins.list();\n"
                                          "for (var i = 0; i < list.length; ++i) {\n"
                                          "  if (list[i].category === 'Mehdi' && list[i].architecture === 'x64' && list[i].supported) {\n"
                                          "    app.activeDocument.activeLayer.applyPlugin(list[i].id, {dialog: false, captureDialog: '%2'});\n"
                                          "    console.log('captured ' + list[i].id);\n"
                                          "    break;\n"
                                          "  }\n"
                                          "}\n"
                                          "patchy.plugins.folders = [];\n")
                               .arg(QDir::fromNativeSeparators(patchy::ui::to_qstring(mehdi)), png)));
  probe.stop();
  CHECK(backlog_contains(window, QStringLiteral("captured legacy.photoshop.")));
  CHECK(saw_waiting);
  CHECK(!saw_busy_bar_while_waiting);
  CHECK(find_top_level_dialog(QStringLiteral("legacyPluginRunDialog")) == nullptr);
  const QImage image(png);
  CHECK(!image.isNull());
  CHECK(image.width() > 200 && image.height() > 100);
}

// Plugins > Repeat Last Plug-in (Ctrl+F) and Last Plug-in Settings...
// (Ctrl+Alt+F): disabled until a plug-in completes a run, then named after it,
// repeating it on the active layer with its own undo step; gone with the
// plug-in's folder.
void ui_legacy_plugin_repeat_last_commands() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  require_canvas(window);
  auto* repeat = require_action(window, "pluginsRepeatLastAction");
  auto* settings = require_action(window, "pluginsLastSettingsAction");
  CHECK(!repeat->isEnabled());
  CHECK(!settings->isEnabled());
  CHECK(repeat->text() == QStringLiteral("Repeat Last Plug-in"));
  CHECK(settings->text() == QStringLiteral("Last Plug-in Settings..."));
  {
    const auto* command = window.hotkey_registry().find_command(QStringLiteral("plugins.repeat_last"));
    CHECK(command != nullptr);
    CHECK(command != nullptr && command->default_shortcuts.contains(QKeySequence(Qt::CTRL | Qt::Key_F)));
    const auto* command2 = window.hotkey_registry().find_command(QStringLiteral("plugins.last_settings"));
    CHECK(command2 != nullptr);
    CHECK(command2 != nullptr && command2->default_shortcuts.contains(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F)));
  }

  // A folder of its own, so the plug-in can be taken away again below.
  const auto folder = make_plugin_folder(QStringLiteral("repeat32"), {{"Greyscale.8bf", QStringLiteral("Repeat Grey32.8bf")}});
  // Two scripts: a script's edits undo as one step, and only the run is undone below.
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];\n"
                                          "app.activeDocument.activeLayer.fillRect(0, 0, 200, 200, '#dc1e1e');")
                               .arg(folder)));
  CHECK(layer_pixel(window, 50, 50) == QColor(220, 30, 30));
  CHECK(run_script(window, QStringLiteral(
                               "app.activeDocument.activeLayer.applyPlugin('legacy.photoshop.Repeat Grey32', {dialog: false});")));
  CHECK(repeat->isEnabled());
  CHECK(settings->isEnabled());
  CHECK(repeat->text() == QStringLiteral("Repeat Greyscale"));
  CHECK(settings->text() == QStringLiteral("Greyscale Settings..."));
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == QColor(220, 30, 30));

  repeat->trigger();
  QApplication::processEvents();
  {
    const auto after = layer_pixel(window, 50, 50);
    CHECK(after.red() == after.green() && after.green() == after.blue());
    CHECK(after.red() > 60 && after.red() < 120);
  }
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Applied Greyscale (")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("undoes it")));
  CHECK(find_top_level_dialog(QStringLiteral("legacyPluginRunDialog")) == nullptr);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(layer_pixel(window, 50, 50) == QColor(220, 30, 30));

  // "Settings..." is not triggered here: the Filter Foundry fixture answers a
  // Parameters call made with a stored parameter block with its "DialogBoxParam
  // failed" box, which waits for a click. The menu path with the Parameters
  // selector is covered by ui_legacy_plugin_run_respects_selection_and_undoes.

  // The folder goes away: the commands fall back to their generic text.
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];")));
  CHECK(!repeat->isEnabled());
  CHECK(!settings->isEnabled());
  CHECK(repeat->text() == QStringLiteral("Repeat Last Plug-in"));
  CHECK(settings->text() == QStringLiteral("Last Plug-in Settings..."));
}

// Plugins menu: the folder command creates the folder with its README, the
// rescan command rebuilds the submenu on a worker and reports in the status bar.
void ui_legacy_plugin_menu_opens_folder_and_rescans() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  CHECK(window.findChild<QAction*>(QStringLiteral("pluginsOpenFolderAction")) != nullptr);
  auto* rescan = window.findChild<QAction*>(QStringLiteral("pluginsRescanAction"));
  CHECK(rescan != nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("pluginsScanLegacyAction")) == nullptr);

  // patchy.plugins.folder is the same folder the menu opens, created on read.
  const auto folder = patchy::ui::legacy_plugins_folder_path();
  CHECK(!folder.isEmpty());
  CHECK(run_script(window, QStringLiteral("console.log('folder ' + patchy.plugins.folder);")));
  CHECK(backlog_contains(window, QStringLiteral("folder ") + QDir::fromNativeSeparators(folder)));
  CHECK(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("README.txt"))));

  // Rescan: the fixture plug-ins come back and the status bar reports the count.
  CHECK(find_plugin_action(window, QString::fromLatin1(kGreyscale64Id)) != nullptr);
  if (rescan != nullptr) {
    rescan->trigger();
    CHECK(window.legacy_plugin_scan_in_flight());
    wait_for_legacy_plugin_scan(window);
    CHECK(window.findChild<QAction*>(QStringLiteral("legacyPluginsScanningNote")) == nullptr);
    CHECK(find_plugin_action(window, QString::fromLatin1(kGreyscale64Id)) != nullptr);
    CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Plug-in scan finished:")));
  }
}

// The About dialog offers the same folder.
void ui_about_dialog_has_plugins_folder_row() {
  bool inspected = false;
  QTimer::singleShot(0, [&inspected] {
    auto* dialog = QApplication::activeModalWidget();
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("patchySplashScreen") && widget->isVisible()) {
          dialog = widget;
        }
      }
    }
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* caption = dialog->findChild<QLabel*>(QStringLiteral("splashPluginsCaption"));
    CHECK(caption != nullptr);
    if (caption != nullptr) {
      CHECK(caption->text() == QStringLiteral("Plug-ins folder (.8bf filters):"));
    }
    auto* path = dialog->findChild<QLabel*>(QStringLiteral("splashPluginsPath"));
    CHECK(path != nullptr);
    if (path != nullptr) {
      CHECK(path->text() == QDir::toNativeSeparators(patchy::ui::legacy_plugins_folder_path()));
    }
    auto* button = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenPluginsFolderButton"));
    CHECK(button != nullptr);
    if (button != nullptr) {
      CHECK(button->text() == QStringLiteral("Open Plug-ins Folder"));
    }
    inspected = true;
    if (auto* as_dialog = qobject_cast<QDialog*>(dialog); as_dialog != nullptr) {
      as_dialog->accept();
    }
  });
  patchy::ui::show_about_splash();
  CHECK(inspected);
}
#else
// Off Windows the plug-ins cannot run, so no folder, commands or About row exist.
void ui_legacy_plugin_ui_absent_off_windows() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(window.findChild<QAction*>(QStringLiteral("pluginsOpenFolderAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("pluginsRescanAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("pluginsLegacyWindowsOnlyNote")) != nullptr);
  CHECK(patchy::ui::legacy_plugins_folder_path().isEmpty());
  CHECK(run_script(window, QStringLiteral("console.log('folder [' + patchy.plugins.folder + ']');")));
  CHECK(backlog_contains(window, QStringLiteral("folder []")));
}
#endif

#ifdef Q_OS_WIN
// With nothing selected, a one-layer document still has an obvious target: the
// plug-in runs on it instead of asking for a layer.
void ui_legacy_plugin_runs_on_only_layer_when_none_selected() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  // The menu path needs a 32-bit copy (see ui_legacy_plugin_run_respects_selection_and_undoes).
  const auto menu_folder =
      make_plugin_folder(QStringLiteral("onlylayer32"), {{"Greyscale.8bf", QStringLiteral("Only Grey32.8bf")}});
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = ['%1'];").arg(menu_folder)));
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Layer 1", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(220, 30, 30)));
  window.add_document_session(std::move(document), QStringLiteral("Only Layer"));
  QApplication::processEvents();
  auto* menu_action = find_plugin_action(window, QStringLiteral("legacy.photoshop.Only Grey32"));
  CHECK(menu_action != nullptr);
  if (menu_action != nullptr) {
    require_hotkey_action(window, QStringLiteral("select.deselect_layers"))->trigger();
    QApplication::processEvents();
    CHECK(!MainWindowTestAccess::document(window).active_layer_id().has_value());
    menu_action->trigger();
    QApplication::processEvents();
    const auto after = layer_pixel(window, 32, 32);  // also checks that the layer is active again
    CHECK(after.red() == after.green() && after.green() == after.blue());
    CHECK(after.red() > 60 && after.red() < 120);
  }
  CHECK(run_script(window, QStringLiteral("patchy.plugins.folders = [];")));
}

// A plug-in run on a text layer offers to rasterize it first (a plug-in only
// ever sees pixels); Rasterize runs the plug-in on the pixels, Cancel leaves
// the text layer alone.
void ui_legacy_plugin_offers_to_rasterize_text_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  QAction* greyscale = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(QStringLiteral("Greyscale"), Qt::CaseInsensitive)) {
      greyscale = action;
      break;
    }
  }
  CHECK(greyscale != nullptr);
  if (greyscale == nullptr) {
    return;
  }
  const auto answer_prompt = [&](const QString& button_text, bool& saw) {
    QTimer::singleShot(0, [&, button_text] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("rasterizeOrConvertMessageBox")));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      for (auto* button : box->buttons()) {
        if (button->text().remove(QLatin1Char('&')) == button_text) {
          saw = true;
          button->click();
          return;
        }
      }
      box->reject();
    });
  };

  CHECK(run_script(window, QStringLiteral("var d = app.activeDocument; var t = d.addTextLayer('Plug-in', {size: 48, x: 20, y: 60, color: '#ff0000'}); d.activeLayer = t;\n"
                                          "console.log('text ' + d.activeLayer.isText);")));
  CHECK(backlog_contains(window, QStringLiteral("text true")));
  bool saw_cancel = false;
  answer_prompt(QStringLiteral("Cancel"), saw_cancel);
  greyscale->trigger();
  QApplication::processEvents();
  CHECK(saw_cancel);
  CHECK(run_script(window, QStringLiteral("console.log('after cancel ' + app.activeDocument.activeLayer.isText);")));
  CHECK(backlog_contains(window, QStringLiteral("after cancel true")));

  bool saw_rasterize = false;
  answer_prompt(QStringLiteral("Rasterize"), saw_rasterize);
  greyscale->trigger();
  QApplication::processEvents();
  CHECK(saw_rasterize);
  CHECK(run_script(window, QStringLiteral("console.log('after rasterize ' + app.activeDocument.activeLayer.isText);")));
  CHECK(backlog_contains(window, QStringLiteral("after rasterize false")));
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Applied Greyscale")));
}

// The screen size reported to plug-ins (the helper's virtual screen) is a
// Preferences choice with a capped default and a stable settings key.
void ui_preferences_plugin_screen_size_round_trips() {
  patchy::ui::set_stored_legacy_plugin_screen_size(patchy::ui::kDefaultLegacyPluginScreenSize);
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesPluginScreenSizeCombo"));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(combo->count() == static_cast<int>(patchy::ui::kLegacyPluginScreenSizes.size()));
    CHECK(combo->currentData().toSize() == QSize(1280, 1024));
    CHECK(combo->itemText(0) == QStringLiteral("Whole monitor"));
    combo->setCurrentIndex(combo->findData(QSize(1024, 768)));
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::stored_legacy_plugin_screen_size() == std::make_pair(1024, 768));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("plugins/screenSize")).toString() ==
        QStringLiteral("1024x768"));
  patchy::ui::set_stored_legacy_plugin_screen_size({0, 0});
  CHECK(patchy::ui::app_settings().value(QStringLiteral("plugins/screenSize")).toString() ==
        QStringLiteral("monitor"));
  CHECK(patchy::ui::stored_legacy_plugin_screen_size() == std::make_pair(0, 0));
  // Anything else stored falls back to the default.
  patchy::ui::app_settings().setValue(QStringLiteral("plugins/screenSize"), QStringLiteral("640x480"));
  CHECK(patchy::ui::stored_legacy_plugin_screen_size() == patchy::ui::kDefaultLegacyPluginScreenSize);
  patchy::ui::set_stored_legacy_plugin_screen_size(patchy::ui::kDefaultLegacyPluginScreenSize);
}
#endif

// Every Preferences tab is visible at the dialog's default size: the last one
// (Plug-ins on Windows) must not hide behind the tab bar's scroll arrows.
void ui_preferences_dialog_shows_every_tab() {
  patchy::ui::MainWindow window;
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("preferencesTabWidget"));
    CHECK(tabs != nullptr);
    if (tabs != nullptr) {
      QApplication::processEvents();  // the shown dialog's layout pass
      auto* bar = tabs->tabBar();
      CHECK(!bar->usesScrollButtons());
      CHECK(tabs->count() >= 5);
      const auto last = bar->tabRect(tabs->count() - 1);
      CHECK(last.right() <= bar->width());
      CHECK(bar->width() <= dialog->width());
      inspected = true;
    }
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
}

// The companion box of a plug-in run, driven the way the runner drives it
// (docs/plugins.md): no plug-in involved, so it runs on every platform.
void ui_legacy_plugin_run_dialog_states() {
  using patchy::ui::LegacyPluginPhase;
  using patchy::ui::LegacyPluginRunDialog;
  LegacyPluginRunDialog dialog(QStringLiteral("Greyscale"));
  auto* message = dialog.findChild<QLabel*>(QStringLiteral("legacyPluginRunMessage"));
  auto* hint = dialog.findChild<QLabel*>(QStringLiteral("legacyPluginRunHint"));
  auto* bar = dialog.findChild<QProgressBar*>(QStringLiteral("legacyPluginRunProgressBar"));
  auto* show_window = dialog.findChild<QPushButton*>(QStringLiteral("legacyPluginShowWindowButton"));
  auto* stop = dialog.findChild<QPushButton*>(QStringLiteral("legacyPluginStopButton"));
  CHECK(message != nullptr && hint != nullptr && bar != nullptr && show_window != nullptr && stop != nullptr);
  if (message == nullptr || hint == nullptr || bar == nullptr || show_window == nullptr || stop == nullptr) {
    return;
  }
  CHECK(dialog.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
  CHECK(dialog.windowModality() == Qt::WindowModal);
  CHECK(!dialog.isVisible());  // the caller reveals it after a second, or when a window appears

  // Helper starting: busy, force stop.
  CHECK(dialog.state() == LegacyPluginRunDialog::State::Starting);
  CHECK(message->text() == QStringLiteral("Starting Greyscale..."));
  CHECK(stop->text() == QStringLiteral("Force Stop Plug-in"));
  CHECK(!show_window->isVisibleTo(&dialog));
  CHECK(bar->isVisibleTo(&dialog));
  CHECK(bar->maximum() == 0);
  dialog.set_phase(LegacyPluginPhase::Parameters);
  CHECK(dialog.state() == LegacyPluginRunDialog::State::Starting);

  // The plug-in's dialog is up: calm text, no bar, both buttons, revealed and
  // placed by the window.
  dialog.set_plugin_window(QRect(100, 100, 700, 400));
  CHECK(dialog.state() == LegacyPluginRunDialog::State::WaitingForUser);
  CHECK(dialog.isVisible());
  CHECK(message->text().startsWith(QStringLiteral("Greyscale is open in its own window.")));
  CHECK(message->text().contains(QStringLiteral("click its OK button")));
  CHECK(hint->isVisibleTo(&dialog));
  CHECK(hint->text().contains(QStringLiteral("preview inside their own window")));
  CHECK(!bar->isVisibleTo(&dialog));
  CHECK(show_window->isVisibleTo(&dialog));
  CHECK(stop->text() == QStringLiteral("Force Stop Plug-in"));
  CHECK(!stop->toolTip().isEmpty());
  CHECK(!dialog.take_show_window_request());
  show_window->click();
  CHECK(dialog.take_show_window_request());
  CHECK(!dialog.take_show_window_request());  // consumed

  // A plug-in that opens its dialog from Start (Mehdi's, KPT) still waits.
  dialog.set_phase(LegacyPluginPhase::Prepare);
  dialog.set_phase(LegacyPluginPhase::Start);
  CHECK(dialog.state() == LegacyPluginRunDialog::State::WaitingForUser);

  // OK clicked: the window is gone and the filtering pass runs.
  dialog.set_plugin_window(QRect());
  CHECK(dialog.state() == LegacyPluginRunDialog::State::Applying);
  CHECK(message->text() == QStringLiteral("Applying Greyscale..."));
  CHECK(!hint->isVisibleTo(&dialog));
  CHECK(bar->isVisibleTo(&dialog));
  CHECK(!show_window->isVisibleTo(&dialog));
  CHECK(stop->text() == QStringLiteral("Cancel"));
  dialog.set_progress(50, 100);
  CHECK(bar->maximum() == 100);
  CHECK(bar->value() == 50);
  // The plug-in's own progress window during Continue is not a settings dialog.
  dialog.set_phase(LegacyPluginPhase::Continue);
  dialog.set_plugin_window(QRect(100, 100, 300, 80));
  CHECK(dialog.state() == LegacyPluginRunDialog::State::Applying);
  CHECK(show_window->isVisibleTo(&dialog));

  // Stopping, by the button or the title-bar close, is one request.
  CHECK(!dialog.stop_requested());
  stop->click();
  CHECK(dialog.stop_requested());
  CHECK(!dialog.isVisible());
  dialog.reveal();
  CHECK(!dialog.isVisible());  // never comes back after a stop

  // Straight to work with no dialog at all (dialog:false): applying from Prepare on.
  LegacyPluginRunDialog quiet(QStringLiteral("Quiet"));
  quiet.set_phase(LegacyPluginPhase::Prepare);
  CHECK(quiet.state() == LegacyPluginRunDialog::State::Applying);
  quiet.set_phase(LegacyPluginPhase::Continue);
  CHECK(quiet.state() == LegacyPluginRunDialog::State::Applying);
  CHECK(!quiet.stop_requested());
  quiet.reveal();
  CHECK(quiet.isVisible());
  quiet.close();  // the title-bar close button
  CHECK(quiet.stop_requested());
}

// The README the app writes into a fresh plug-ins folder is the one the Windows
// package ships.
void ui_legacy_plugins_readme_matches_packaged_file() {
  const auto packaged = patchy::test::source_root_path() / "packaging" / "plugins" / "README.txt";
  QFile file(patchy::ui::to_qstring(packaged));
  CHECK(file.open(QIODevice::ReadOnly));
  const auto shipped = QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  CHECK(shipped == patchy::ui::legacy_plugins_readme_text());
  CHECK(shipped.contains(QStringLiteral("Plugins > Rescan Plug-in Folders")));
}

}  // namespace

std::vector<patchy::test::TestCase> legacy_plugin_tests() {
  return {
      {"ui_legacy_plugin_folder_scan_builds_category_menu", ui_legacy_plugin_folder_scan_builds_category_menu},
      {"ui_legacy_plugin_non_filter_files_are_listed_not_offered",
       ui_legacy_plugin_non_filter_files_are_listed_not_offered},
#ifdef Q_OS_WIN
      {"ui_legacy_plugin_run_respects_selection_and_undoes", ui_legacy_plugin_run_respects_selection_and_undoes},
      {"ui_legacy_plugin_mehdi_filters_run_if_available", ui_legacy_plugin_mehdi_filters_run_if_available},
      {"ui_legacy_plugin_mehdi_dialog_capture_if_available", ui_legacy_plugin_mehdi_dialog_capture_if_available},
      {"ui_legacy_plugin_menu_opens_folder_and_rescans", ui_legacy_plugin_menu_opens_folder_and_rescans},
      {"ui_about_dialog_has_plugins_folder_row", ui_about_dialog_has_plugins_folder_row},
      {"ui_preferences_plugin_screen_size_round_trips", ui_preferences_plugin_screen_size_round_trips},
      {"ui_legacy_plugin_offers_to_rasterize_text_layer", ui_legacy_plugin_offers_to_rasterize_text_layer},
      {"ui_legacy_plugin_runs_on_only_layer_when_none_selected",
       ui_legacy_plugin_runs_on_only_layer_when_none_selected},
      {"ui_legacy_plugin_repeat_last_commands", ui_legacy_plugin_repeat_last_commands},
#else
      {"ui_legacy_plugin_ui_absent_off_windows", ui_legacy_plugin_ui_absent_off_windows},
#endif
      {"ui_legacy_plugin_run_dialog_states", ui_legacy_plugin_run_dialog_states},
      {"ui_legacy_plugins_readme_matches_packaged_file", ui_legacy_plugins_readme_matches_packaged_file},
      {"ui_preferences_dialog_shows_every_tab", ui_preferences_dialog_shows_every_tab},
  };
}
