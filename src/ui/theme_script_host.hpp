#pragma once

// Theme scripts and the theme hooks (docs/theme-files.md, "Theme scripts"): a theme
// package may ship scripts/main.js, which runs in its OWN QJSEngine, never the user
// scripting engine. That engine holds the standard JavaScript built-ins plus
// patchy.theme, timers and console, nothing else: no patchy.io, no documents, no file
// paths, no processes, no network. Every call into theme code runs under a time
// budget (an interrupt watchdog); a theme that keeps overrunning is stopped. Sounds,
// effects, status messages, cursors, splashes and panel backgrounds are rate limited,
// and errors go to the log and a throttled status message, never a dialog.
//
// The host also carries the declarative theme.json hooks (cursors, splash,
// panelBackgrounds, menuTitle), so a theme without scripts gets them too. MainWindow
// renders what the host asks for through its signals.

#include "ui/theme_sounds.hpp"

#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class QJSEngine;

namespace patchy::ui {

struct ThemeAssets;
class ScriptWatchdog;
enum class CanvasTool;

// "preferences/runThemeScripts" (a persisted key), on by default.
[[nodiscard]] bool theme_scripts_enabled();
void set_theme_scripts_enabled(bool enabled);

enum class ThemeScriptTrust { Unknown, Allowed, Denied };
// SHA-256 over every scripts/ file, path and bytes, in path order: a stored answer
// holds only while the scripts are unchanged.
[[nodiscard]] QString theme_scripts_digest(const ThemeAssets& assets);
// "themeScripts/trust" (a persisted key): theme id -> "<digest>:allow" or ":deny".
[[nodiscard]] ThemeScriptTrust stored_theme_script_trust(const QString& theme_id, const QString& digest);
void store_theme_script_trust(const QString& theme_id, const QString& digest, bool allowed);

// Plays the event's theme sound and effect and hands it to the theme scripts. Every
// theme event goes through here (docs/theme-files.md, "Events and effects").
void fire_theme_event(ThemeSoundEvent event, const QVariantMap& payload = {});

struct ThemeCommand {
  QString id;      // [A-Za-z0-9_-], unique per theme
  QString title;
  QString hotkey;  // QKeySequence portable text; empty for none
};

class ThemeScriptHost;

// What theme code sees as its bridge. Reached only through the prelude's closure, never
// from the script's globals.
class ThemeScriptBridge final : public QObject {
  Q_OBJECT
 public:
  explicit ThemeScriptBridge(ThemeScriptHost& host) : host_(host) {}

  Q_INVOKABLE bool isEvent(const QString& name) const;
  Q_INVOKABLE void listen(const QString& name);
  Q_INVOKABLE bool playSound(const QString& path);
  Q_INVOKABLE bool playEffect(const QVariantMap& spec);
  Q_INVOKABLE void showStatus(const QString& text, int ms);
  Q_INVOKABLE bool addCommand(const QString& id, const QString& title, const QString& hotkey);
  Q_INVOKABLE int startTimer(int ms, bool repeat);
  Q_INVOKABLE void stopTimer(int id);
  Q_INVOKABLE bool setToolCursor(const QString& tool, const QString& image, int hot_x, int hot_y);
  Q_INVOKABLE bool showSplash(const QString& image, int ms);
  Q_INVOKABLE bool setPanelBackground(const QString& panel, const QString& image, const QString& mode,
                                      double opacity);
  Q_INVOKABLE QString themeId() const;
  Q_INVOKABLE QString themeName() const;
  Q_INVOKABLE void log(const QString& text);
  Q_INVOKABLE void reportError(const QString& text);

 private:
  ThemeScriptHost& host_;
};

class ThemeScriptHost final : public QObject {
  Q_OBJECT
 public:
  // Created on first use, parented to the application.
  [[nodiscard]] static ThemeScriptHost& instance();
  [[nodiscard]] static ThemeScriptHost* existing();

  // ThemeManager calls this on every theme apply and clear: drops the previous theme's
  // engine, commands, cursors and backgrounds, applies the new theme's declarative
  // hooks, then starts its scripts when they are on and trusted, or asks.
  void theme_changed(const QString& theme_id, std::shared_ptr<const ThemeAssets> assets);
  // Answers trust_needed: records the answer for this theme and these scripts and,
  // when allowed and still the active theme, starts them.
  void set_trust(const QString& theme_id, bool allowed);
  // Stops the running scripts and starts them again under the current preference.
  void restart();

  [[nodiscard]] bool running() const { return engine_ != nullptr; }
  [[nodiscard]] bool trust_pending() const { return trust_pending_; }
  [[nodiscard]] QString theme_id() const { return theme_id_; }
  [[nodiscard]] QString theme_name() const;
  // theme.json "menuTitle", else the theme's name.
  [[nodiscard]] QString menu_title() const;
  [[nodiscard]] const std::vector<ThemeCommand>& commands() const { return commands_; }
  void run_command(const QString& id);
  void dispatch(ThemeSoundEvent event, const QVariantMap& payload);
  [[nodiscard]] bool listens(ThemeSoundEvent event) const;

  [[nodiscard]] std::optional<QCursor> tool_cursor(const QString& tool_id) const;
  // QSS rules for the panel and dialog backgrounds; revision() moves whenever they change.
  [[nodiscard]] QString panel_background_stylesheet() const { return panel_stylesheet_; }
  [[nodiscard]] int panel_background_revision() const { return panel_revision_; }
  // Script output and errors, newest last (capped).
  [[nodiscard]] QStringList log() const { return log_; }

  // Tests: runs source in the theme engine under the budget and returns the result
  // (or "Error: ...") as text; the budget for every call.
  QString evaluate_for_testing(const QString& source);
  void set_budget_for_testing(int milliseconds) { budget_ms_ = milliseconds; }

  static constexpr int kCallBudgetMs = 250;
  static constexpr int kMaxTimeouts = 3;
  static constexpr int kMaxTimers = 16;
  static constexpr int kMaxCommands = 32;

 signals:
  void trust_needed(const QString& theme_id, const QString& theme_name);
  void commands_changed();
  void cursors_changed();
  void panel_backgrounds_changed();
  void status_requested(const QString& text, int milliseconds);
  void splash_requested(const QImage& image, int milliseconds);

 private:
  friend class ThemeScriptBridge;
  struct RateLimit {
    int limit = 4;
    qint64 window_ms = 1000;
    std::deque<qint64> stamps;
  };
  struct PanelBackground {
    QString url;
    QString mode;
  };

  explicit ThemeScriptHost(QObject* parent);
  ~ThemeScriptHost() override;

  void start();
  void stop(const QString& reason = {});
  // Runs `function` (or evaluates `source` when it is not a function) under the budget.
  QJSValue guarded_call(QJSValue function, const QJSValueList& arguments);
  QJSValue guarded_evaluate(const QString& source, const QString& file_name);
  void after_call(const QJSValue& result);
  void note(const QString& text);
  [[nodiscard]] bool allow(RateLimit& limit);
  void apply_declarative_hooks();
  bool set_cursor(const QString& tool, const QString& image, int hot_x, int hot_y);
  bool set_panel_background(const QString& panel, const QString& image, const QString& mode, double opacity);
  void rebuild_panel_stylesheet();
  void schedule(const char* what);

  QString theme_id_;
  std::shared_ptr<const ThemeAssets> assets_;
  std::unique_ptr<QJSEngine> engine_;
  std::unique_ptr<ScriptWatchdog> watchdog_;
  ThemeScriptBridge* bridge_ = nullptr;
  QJSValue dispatch_fn_;
  QJSValue command_fn_;
  QJSValue timer_fn_;
  QStringList listened_;
  std::vector<ThemeCommand> commands_;
  QHash<int, QTimer*> timers_;
  int next_timer_id_ = 1;
  QHash<QString, QCursor> cursors_;
  QHash<QString, PanelBackground> panels_;
  QString panel_stylesheet_;
  int panel_revision_ = 0;
  QStringList log_;
  int timeouts_ = 0;
  int budget_ms_ = kCallBudgetMs;
  bool in_call_ = false;
  bool trust_pending_ = false;
  qint64 last_status_error_ms_ = -100000;
  qint64 last_splash_ms_ = -100000;
  QElapsedTimer clock_;
  RateLimit sound_limit_{4, 1000, {}};
  RateLimit effect_limit_{4, 1000, {}};
  RateLimit status_limit_{4, 1000, {}};
  RateLimit cursor_limit_{8, 1000, {}};
  RateLimit panel_limit_{4, 1000, {}};
  bool commands_signal_pending_ = false;
  bool cursors_signal_pending_ = false;
  bool panels_signal_pending_ = false;
  std::mutex interrupt_mutex_;
  std::atomic<bool> timed_out_{false};
};

// The theme cursor for a tool's plain pointer (scripts or theme.json "cursors");
// feedback cursors (handles, modifiers, brush footprints) are never replaced.
[[nodiscard]] std::optional<QCursor> theme_tool_cursor(CanvasTool tool);
// The id a theme names a tool by: its hotkey command id without "tools." ("crop").
[[nodiscard]] QString theme_tool_id(CanvasTool tool);
// The panel backgrounds' QSS, appended to the window sheet, and its revision.
[[nodiscard]] QString theme_panel_background_stylesheet();
[[nodiscard]] int theme_panel_background_revision();

}  // namespace patchy::ui
