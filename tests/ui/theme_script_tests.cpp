// Theme events, effects and theme scripts (docs/theme-files.md, "Events and effects",
// "Theme scripts"): the event lookup, the effect overlay staying idle, the theme script
// engine's isolation and watchdog, the trust prompt and its stored answer, the theme
// menu and its hotkeys, cursors, panel backgrounds, the splash, the preferences and
// Unicode paths.

#include "core/document.hpp"
#include "test_harness.hpp"
#include "ui/canvas_types.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/qt_paths.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_package.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_script_host.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::ThemeManager;
using patchy::ui::ThemeScriptHost;
using patchy::ui::ThemeSoundEvent;

class ScriptThemesDir {
public:
  ScriptThemesDir()
      : restore_dir_("PATCHY_THEMES_DIR"), restore_sound_("PATCHY_NO_SOUND"),
        restore_trust_(QStringLiteral("themeScripts/trust")),
        restore_scripts_(QStringLiteral("preferences/runThemeScripts")),
        restore_effects_(QStringLiteral("preferences/playThemeEffects")) {
    CHECK(dir_.isValid());
    qputenv("PATCHY_THEMES_DIR", dir_.path().toUtf8());
    qputenv("PATCHY_NO_SOUND", "1");
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("themeScripts/trust"));
    settings.setValue(QStringLiteral("preferences/runThemeScripts"), true);
    settings.setValue(QStringLiteral("preferences/playThemeEffects"), true);
  }
  ~ScriptThemesDir() {
    // Leave no theme (and no watched folder) behind before the folder goes.
    ThemeManager::instance().clear_custom_theme(/*persist=*/false);
    QApplication::processEvents();
  }
  [[nodiscard]] QString file(const QString& relative) const { return QDir(dir_.path()).filePath(relative); }

private:
  EnvironmentVariableRestorer restore_dir_;
  EnvironmentVariableRestorer restore_sound_;
  SettingsValueRestorer restore_trust_;
  SettingsValueRestorer restore_scripts_;
  SettingsValueRestorer restore_effects_;
  QTemporaryDir dir_;
};

void write_bytes(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(bytes);
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

QByteArray tone() { return patchy::ui::build_tone_wav(520.0, 30, 0.4, patchy::ui::ToneWave::Sine); }

// A package: theme.json with `extra` keys, an image, two sounds and, when given, main.js.
void write_package(const QString& root, const QJsonObject& extra, const QByteArray& main_js = {}) {
  auto palette = patchy::ui::dark_palette();
  auto object =
      QJsonDocument::fromJson(patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark,
                                                                  QStringLiteral("Scripted")))
          .object();
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    object.insert(it.key(), it.value());
  }
  write_bytes(QDir(root).filePath(QStringLiteral("theme.json")), QJsonDocument(object).toJson());
  QImage image(16, 12, QImage::Format_ARGB32);
  image.fill(QColor(200, 40, 40));
  write_bytes(QDir(root).filePath(QStringLiteral("images/pic.png")), png_bytes(image));
  QImage sheet(32, 8, QImage::Format_ARGB32);
  sheet.fill(QColor(40, 200, 40));
  write_bytes(QDir(root).filePath(QStringLiteral("images/walk.png")), png_bytes(sheet));
  write_bytes(QDir(root).filePath(QStringLiteral("sounds/fx/hurt.wav")), tone());
  write_bytes(QDir(root).filePath(QStringLiteral("sounds/document-new.wav")), tone());
  if (!main_js.isEmpty()) {
    write_bytes(QDir(root).filePath(QStringLiteral("scripts/main.js")), main_js);
  }
}

// Loads and applies a theme id; with `trusted`, stores an Allow first so no prompt runs.
void apply(const QString& id, bool trusted) {
  auto result = patchy::ui::load_theme_by_id(id);
  CHECK(result.theme.has_value());
  if (!result.theme) {
    return;
  }
  for (const auto& warning : result.warnings) {
    fprintf(stderr, "  warning: %s\n", warning.toUtf8().constData());
  }
  if (trusted && result.theme->assets != nullptr && result.theme->assets->has_scripts()) {
    patchy::ui::store_theme_script_trust(id, patchy::ui::theme_scripts_digest(*result.theme->assets), true);
  }
  ThemeManager::instance().set_custom_theme(id, *result.theme, /*persist=*/false);
  QApplication::processEvents();
}

bool log_contains(const QString& text) {
  const auto log = ThemeScriptHost::instance().log();
  return std::any_of(log.begin(), log.end(), [&text](const QString& line) { return line.contains(text); });
}

QJsonObject events_json() {
  return QJsonObject{
      {QStringLiteral("events"),
       QJsonObject{{QStringLiteral("layer-deleted"),
                    QJsonObject{{QStringLiteral("sound"), QStringLiteral("sounds/fx/hurt.wav")},
                                {QStringLiteral("effect"), QStringLiteral("fireworks")}}},
                   {QStringLiteral("document-opened"), QJsonObject{{QStringLiteral("effect"), QStringLiteral("confetti")}}},
                   {QStringLiteral("undo"),
                    QJsonObject{{QStringLiteral("effect"),
                                 QJsonObject{{QStringLiteral("name"), QStringLiteral("sprite")},
                                             {QStringLiteral("image"), QStringLiteral("images/walk.png")},
                                             {QStringLiteral("frames"), 4},
                                             {QStringLiteral("fps"), 8}}}}}}}};
}

void ui_theme_events_play_their_sound_and_effect() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Events")), events_json());
  const auto loaded = patchy::ui::load_theme_by_id(QStringLiteral("Events"));
  CHECK(loaded.theme.has_value() && loaded.warnings.isEmpty());
  if (!loaded.theme) {
    return;
  }
  const auto* assets = loaded.theme->assets.get();
  CHECK(patchy::ui::theme_sound_for_event(assets, ThemeSoundEvent::LayerDeleted) == tone());
  CHECK(patchy::ui::theme_sound_for_event(assets, ThemeSoundEvent::DocumentNew) == tone());  // the shorthand
  CHECK(patchy::ui::theme_sound_for_event(assets, ThemeSoundEvent::Redo).isEmpty());
  CHECK(assets->events.value(QStringLiteral("layer-deleted")).effect.value(QStringLiteral("name")) ==
        QStringLiteral("fireworks"));

  patchy::ui::MainWindow window;
  show_window(window);
  apply(QStringLiteral("Events"), false);
  auto* overlay = window.theme_effects_overlay();
  CHECK(overlay != nullptr);
  if (overlay == nullptr) {
    return;
  }
  CHECK(!overlay->animating());
  // A real delete through the Layer menu plays the layer-deleted effect.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(overlay->active_effect_count() == 1);
  CHECK(overlay->animating() && overlay->isVisible());
  patchy::ui::fire_theme_event(ThemeSoundEvent::DocumentOpened);
  patchy::ui::fire_theme_event(ThemeSoundEvent::Undo);
  CHECK(overlay->active_effect_count() == 3);
  patchy::ui::fire_theme_event(ThemeSoundEvent::Redo);  // nothing configured
  CHECK(overlay->active_effect_count() == 3);
  overlay->advance_for_testing(6000);
  CHECK(overlay->active_effect_count() == 0);
  CHECK(!overlay->animating() && !overlay->isVisible());

  // Play theme effects off: nothing starts.
  patchy::ui::set_theme_effects_enabled(false);
  patchy::ui::fire_theme_event(ThemeSoundEvent::LayerDeleted);
  CHECK(overlay->active_effect_count() == 0);
  patchy::ui::set_theme_effects_enabled(true);
}

void ui_theme_effect_overlay_costs_nothing_while_idle() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* overlay = window.theme_effects_overlay();
  CHECK(overlay != nullptr);
  if (overlay == nullptr) {
    return;
  }
  const int before = overlay->paint_count();
  process_events_for(250);
  CHECK(overlay->paint_count() == before);
  CHECK(!overlay->animating() && !overlay->isVisible());

  for (const auto& name : patchy::ui::theme_effect_names()) {
    if (name != QStringLiteral("sprite")) {
      CHECK(overlay->play({{QStringLiteral("name"), name}}, nullptr));
    }
  }
  CHECK(!overlay->play({{QStringLiteral("name"), QStringLiteral("lasers")}}, nullptr));
  CHECK(!overlay->play({{QStringLiteral("name"), QStringLiteral("sprite")}}, nullptr));  // no image
  CHECK(overlay->animating());
  process_events_for(150);
  CHECK(overlay->paint_count() > before);
  // Bounded: the cap refuses more than kMaxEffects at once.
  int started = overlay->active_effect_count();
  while (overlay->play({{QStringLiteral("name"), QStringLiteral("sparkles")}}, nullptr)) {
    ++started;
  }
  CHECK(started <= patchy::ui::ThemeEffectsOverlay::kMaxEffects);
  overlay->advance_for_testing(8000);
  CHECK(!overlay->animating() && !overlay->isVisible());
  const int settled = overlay->paint_count();
  process_events_for(250);
  CHECK(overlay->paint_count() == settled);

  // Start-screen effects play only over the empty start screen.
  CHECK(!window.start_screen_showing());
  CHECK(!overlay->play({{QStringLiteral("name"), QStringLiteral("sparkles")}, {QStringLiteral("where"), QStringLiteral("start-screen")}},
                       nullptr));
}

void ui_theme_script_engine_is_isolated() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Iso")), {}, QByteArrayLiteral("patchy.theme.log('main ran');\n"));
  apply(QStringLiteral("Iso"), true);
  auto& host = ThemeScriptHost::instance();
  CHECK(host.running());
  CHECK(log_contains(QStringLiteral("main ran")));
  // Nothing beyond the built-ins and the theme API.
  for (const auto* expression :
       {"typeof app", "typeof patchy.io", "typeof patchy.ui", "typeof patchy.scripts", "typeof include", "typeof Qt",
        "typeof XMLHttpRequest", "typeof require", "typeof print", "typeof fetch", "typeof documents",
        "typeof patchy.theme.bridge", "typeof __bridge", "typeof gc"}) {
    const auto result = host.evaluate_for_testing(QString::fromLatin1(expression));
    if (result != QStringLiteral("undefined")) {
      fprintf(stderr, "  %s -> %s\n", expression, qPrintable(result));
    }
    CHECK(result == QStringLiteral("undefined"));
  }
  CHECK(host.evaluate_for_testing(QStringLiteral("Object.keys(patchy).join(',')")) == QStringLiteral("theme"));
  CHECK(host.evaluate_for_testing(QStringLiteral("typeof patchy.theme.playEffect")) == QStringLiteral("function"));
  CHECK(host.evaluate_for_testing(QStringLiteral("typeof setTimeout")) == QStringLiteral("function"));
  CHECK(host.evaluate_for_testing(QStringLiteral("patchy.theme.id")) == QStringLiteral("Iso"));
  // The API object is frozen: a script cannot swap a hook for another theme's.
  host.evaluate_for_testing(QStringLiteral("patchy.theme.playSound = function () { return 'x'; }"));
  CHECK(host.evaluate_for_testing(QStringLiteral("patchy.theme.playSound('nope.wav')")) == QStringLiteral("false"));
  // Package paths only: nothing outside the package plays or loads.
  CHECK(host.evaluate_for_testing(QStringLiteral("patchy.theme.playSound('../x.wav')")) == QStringLiteral("false"));
  CHECK(host.evaluate_for_testing(QStringLiteral("patchy.theme.showSplash('C:/Windows/x.png')")) ==
        QStringLiteral("false"));
}

void ui_theme_script_watchdog_stops_a_runaway_loop() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Loop")), {}, QByteArrayLiteral("var ticks = 0;\n"));
  apply(QStringLiteral("Loop"), true);
  auto& host = ThemeScriptHost::instance();
  host.set_budget_for_testing(100);
  CHECK(host.running());
  QElapsedTimer clock;
  clock.start();
  const auto result = host.evaluate_for_testing(QStringLiteral("while (true) { ticks++; }"));
  CHECK(clock.elapsed() < 3000);
  CHECK(result.startsWith(QStringLiteral("Error")));
  CHECK(host.running());  // one overrun is forgiven
  CHECK(host.evaluate_for_testing(QStringLiteral("1 + 1")) == QStringLiteral("2"));
  // A timer that loops forever is stopped too, and three overruns turn the scripts off.
  host.evaluate_for_testing(QStringLiteral("for (var i = 0; i < 2; ++i) setTimeout(function () { while (true) {} }, 0);"));
  CHECK(process_events_until([&host] { return !host.running(); }, 5000));
  CHECK(log_contains(QStringLiteral("too long")));
  host.set_budget_for_testing(ThemeScriptHost::kCallBudgetMs);
}

void ui_theme_script_trust_prompt_and_stored_answer() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Ask")), {}, QByteArrayLiteral("patchy.theme.log('v1');\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = ThemeScriptHost::instance();

  bool saw_allow = false;
  QTimer::singleShot(50, [&saw_allow] {
    auto* prompt = find_top_level_dialog(QStringLiteral("themeScriptTrustPrompt"));
    CHECK(prompt != nullptr);
    if (auto* allow = prompt != nullptr ? prompt->findChild<QPushButton*>(QStringLiteral("themeScriptTrustAllowButton"))
                                        : nullptr;
        allow != nullptr) {
      saw_allow = true;
      allow->click();
    }
  });
  apply(QStringLiteral("Ask"), false);
  CHECK(process_events_until([&saw_allow] { return saw_allow; }, 3000));
  CHECK(host.running());
  const auto v1 = patchy::ui::load_theme_by_id(QStringLiteral("Ask"));
  CHECK(patchy::ui::stored_theme_script_trust(QStringLiteral("Ask"), patchy::ui::theme_scripts_digest(*v1.theme->assets)) ==
        patchy::ui::ThemeScriptTrust::Allowed);

  // Changed scripts ask again; this time the answer is no.
  write_bytes(themes.file(QStringLiteral("Ask/scripts/main.js")), QByteArrayLiteral("patchy.theme.log('v2');\n"));
  bool saw_deny = false;
  QTimer::singleShot(50, [&saw_deny] {
    auto* prompt = find_top_level_dialog(QStringLiteral("themeScriptTrustPrompt"));
    CHECK(prompt != nullptr);
    if (auto* deny = prompt != nullptr ? prompt->findChild<QPushButton*>(QStringLiteral("themeScriptTrustDenyButton"))
                                       : nullptr;
        deny != nullptr) {
      saw_deny = true;
      deny->click();
    }
  });
  apply(QStringLiteral("Ask"), false);
  CHECK(process_events_until([&saw_deny] { return saw_deny; }, 3000));
  CHECK(!host.running());
  // The stored no holds: no prompt, no scripts, and the rest of the theme still applies.
  apply(QStringLiteral("Ask"), false);
  process_events_for(100);
  CHECK(find_top_level_dialog(QStringLiteral("themeScriptTrustPrompt")) == nullptr);
  CHECK(!host.running());
  CHECK(ThemeManager::instance().active_custom_theme_id() == QStringLiteral("Ask"));
}

void ui_theme_menu_sits_right_of_help_with_hotkeys() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Menu Theme")), QJsonObject{{QStringLiteral("menuTitle"), QStringLiteral("Dink")}},
                QByteArrayLiteral(
                    "patchy.theme.addCommand({id: 'celebrate', title: 'Celebrate', hotkey: 'Ctrl+Alt+Shift+F9'},\n"
                    "  function () { patchy.theme.log('celebrated'); });\n"
                    "patchy.theme.addCommand({id: 'quiet', title: 'Quiet'}, function () {});\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  apply(QStringLiteral("Menu Theme"), true);
  QApplication::processEvents();
  auto* menu = window.theme_menu();
  CHECK(menu != nullptr);
  if (menu == nullptr) {
    return;
  }
  CHECK(menu->menuAction()->isVisible());
  CHECK(menu->title() == QStringLiteral("Dink"));
  // Right of Help: the last visible top-level menu, Help just before it.
  QList<QAction*> visible;
  for (auto* action : window.menuBar()->actions()) {
    if (action->isVisible()) {
      visible.push_back(action);
    }
  }
  CHECK(visible.size() >= 2 && visible.back() == menu->menuAction());
  CHECK(visible.size() >= 2 && visible[visible.size() - 2]->text().remove(QLatin1Char('&')) == QStringLiteral("Help"));
  CHECK(menu->actions().size() == 2);
  for (auto* action : menu->actions()) {
    CHECK(action->menuRole() == QAction::NoRole);
  }
  const auto id = QStringLiteral("theme.Menu%20Theme.celebrate");
  const auto* command = window.hotkey_registry().find_command(id);
  CHECK(command != nullptr);
  if (command != nullptr && command->action != nullptr) {
    CHECK(command->action->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F9")));
    command->action->trigger();
    CHECK(log_contains(QStringLiteral("celebrated")));
  }
  // Another theme: the menu and its hotkeys go away.
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  process_events_for(50);
  CHECK(!menu->menuAction()->isVisible());
  CHECK(window.hotkey_registry().find_command(id) == nullptr);
}

void ui_theme_cursors_backgrounds_and_splash() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Hooks")),
                QJsonObject{{QStringLiteral("cursors"),
                             QJsonObject{{QStringLiteral("polygon"),
                                          QJsonObject{{QStringLiteral("image"), QStringLiteral("images/pic.png")},
                                                      {QStringLiteral("hotX"), 2},
                                                      {QStringLiteral("hotY"), 3}}}}}},
                QByteArrayLiteral("patchy.theme.setToolCursor('crop', 'images/pic.png', 1, 1);\n"
                                  "patchy.theme.setPanelBackground('layers', 'images/pic.png', 'tile', 0.25);\n"
                                  "patchy.theme.on('startup', function () { patchy.theme.showSplash('images/pic.png', 900); });\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  apply(QStringLiteral("Hooks"), true);
  process_events_for(50);
  CHECK(patchy::ui::theme_tool_cursor(patchy::ui::CanvasTool::Crop).has_value());
  CHECK(patchy::ui::theme_tool_cursor(patchy::ui::CanvasTool::Polygon).has_value());  // theme.json "cursors"
  CHECK(!patchy::ui::theme_tool_cursor(patchy::ui::CanvasTool::Brush).has_value());
  // The Polygon tool's plain cross is the pointer a theme cursor replaces.
  require_action(window, "toolPolygonAction")->trigger();
  QApplication::processEvents();
  canvas->refresh_tool_cursor();
  CHECK(canvas->cursor().pixmap().size() == QSize(16, 12));
  CHECK(patchy::ui::theme_panel_background_stylesheet().contains(QStringLiteral("QWidget#layersPanel")));
  CHECK(window.styleSheet().contains(QStringLiteral("patchy-theme-cache/panel-")));

  patchy::ui::fire_theme_event(ThemeSoundEvent::Startup);
  process_events_for(50);
  CHECK(window.findChild<QWidget*>(QStringLiteral("themeSplash")) != nullptr);

  // Back to a built-in scheme: the cursor and the background are restored.
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  process_events_for(50);
  CHECK(!patchy::ui::theme_tool_cursor(patchy::ui::CanvasTool::Crop).has_value());
  canvas->refresh_tool_cursor();
  CHECK(canvas->cursor().shape() == Qt::CrossCursor);
  CHECK(!window.styleSheet().contains(QStringLiteral("patchy-theme-cache/panel-")));
}

void ui_theme_script_events_reach_the_script() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  write_package(themes.file(QStringLiteral("Listen")), {},
                QByteArrayLiteral("['layer-deleted','document-opened','document-new','document-closed','undo','redo',\n"
                                  " 'startup','tool-changed','idle','save-done'].forEach(function (name) {\n"
                                  "  patchy.theme.on(name, function (data) { patchy.theme.log('got ' + name + ' ' + JSON.stringify(data)); });\n"
                                  "});\n"
                                  "setTimeout(function () { patchy.theme.log('timer fired'); }, 10);\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  apply(QStringLiteral("Listen"), true);
  auto& host = ThemeScriptHost::instance();
  CHECK(host.running());
  for (const auto event : patchy::ui::theme_sound_events()) {
    patchy::ui::fire_theme_event(event);
  }
  CHECK(log_contains(QStringLiteral("got layer-deleted")));
  CHECK(log_contains(QStringLiteral("got idle")));
  CHECK(!log_contains(QStringLiteral("got error")));  // not listened to
  require_action(window, "toolCropAction")->trigger();
  QApplication::processEvents();
  CHECK(log_contains(QStringLiteral("got tool-changed {\"tool\":\"crop\"}")));
  CHECK(process_events_until([] { return log_contains(QStringLiteral("timer fired")); }, 2000));
  // An unknown event name is the script's error, not a crash.
  CHECK(host.evaluate_for_testing(QStringLiteral("patchy.theme.on('explode', function () {})")).startsWith(
      QStringLiteral("Error")));
}

void ui_theme_effects_and_scripts_preferences() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  write_package(themes.file(QStringLiteral("Prefs")), {}, QByteArrayLiteral("patchy.theme.log('on');\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  apply(QStringLiteral("Prefs"), true);
  auto& host = ThemeScriptHost::instance();
  CHECK(host.running());
  bool saw = false;
  QTimer::singleShot(0, [&saw] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* effects = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPlayThemeEffectsCheck"));
    auto* scripts = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesRunThemeScriptsCheck"));
    CHECK(effects != nullptr && scripts != nullptr);
    if (effects != nullptr && scripts != nullptr) {
      CHECK(effects->isChecked() && scripts->isChecked());
      effects->setChecked(false);
      scripts->setChecked(false);
      saw = true;
    }
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw);
  CHECK(!patchy::ui::theme_effects_enabled());
  CHECK(!patchy::ui::theme_scripts_enabled());
  CHECK(!host.running());
  patchy::ui::set_theme_scripts_enabled(true);
  CHECK(host.running());
  ThemeManager::instance().clear_custom_theme(/*persist=*/true);
}

void ui_theme_scripts_on_unicode_paths() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  ScriptThemesDir themes;
  const auto dir = patchy::test::unicode_artifact_dir(patchy::test::kUnicodePathStems[2]);
  const auto folder = QDir(patchy::ui::to_qstring(dir))
                          .filePath(patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodePathStems[4])));
  write_package(folder, events_json(), QByteArrayLiteral("patchy.theme.log('unicode ok');\n"));
  auto result = patchy::ui::load_theme_package_directory(folder);
  CHECK(result.theme.has_value());
  if (!result.theme) {
    return;
  }
  CHECK(result.theme->assets->has_scripts());
  CHECK(result.theme->assets->events.contains(QStringLiteral("undo")));
  patchy::ui::store_theme_script_trust(QStringLiteral("unicode"), patchy::ui::theme_scripts_digest(*result.theme->assets),
                                       true);
  ThemeManager::instance().set_custom_theme(QStringLiteral("unicode"), *result.theme, false);
  CHECK(ThemeScriptHost::instance().running());
  CHECK(log_contains(QStringLiteral("unicode ok")));
}

}  // namespace

std::vector<patchy::test::TestCase> theme_script_tests() {
  return {
      {"ui_theme_events_play_their_sound_and_effect", ui_theme_events_play_their_sound_and_effect},
      {"ui_theme_effect_overlay_costs_nothing_while_idle", ui_theme_effect_overlay_costs_nothing_while_idle},
      {"ui_theme_script_engine_is_isolated", ui_theme_script_engine_is_isolated},
      {"ui_theme_script_watchdog_stops_a_runaway_loop", ui_theme_script_watchdog_stops_a_runaway_loop},
      {"ui_theme_script_trust_prompt_and_stored_answer", ui_theme_script_trust_prompt_and_stored_answer},
      {"ui_theme_menu_sits_right_of_help_with_hotkeys", ui_theme_menu_sits_right_of_help_with_hotkeys},
      {"ui_theme_cursors_backgrounds_and_splash", ui_theme_cursors_backgrounds_and_splash},
      {"ui_theme_script_events_reach_the_script", ui_theme_script_events_reach_the_script},
      {"ui_theme_effects_and_scripts_preferences", ui_theme_effects_and_scripts_preferences},
      {"ui_theme_scripts_on_unicode_paths", ui_theme_scripts_on_unicode_paths},
  };
}
