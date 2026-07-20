#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJSValue>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QString>
#include <QStringList>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

class QJSEngine;
class QTimer;

namespace patchy::ui {

class CanvasWidget;
class MainWindow;
class ScriptCanvasWindow;

// Interrupts a runaway script from a helper thread: the UI thread arms a deadline
// around every evaluate()/callback invocation, and when it passes the callback
// (QJSEngine::setInterrupted, documented thread-safe) aborts the evaluation.
// Timer-driven scripts stay responsive because each individual callback is short.
class ScriptWatchdog {
public:
  explicit ScriptWatchdog(std::function<void()> on_timeout);
  ~ScriptWatchdog();

  void arm(std::chrono::milliseconds timeout);
  void disarm();

private:
  std::function<void()> on_timeout_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::chrono::steady_clock::time_point deadline_{};
  bool armed_{false};
  bool quit_{false};
  std::thread thread_;
};

// The JavaScript scripting engine host: owns the per-run QJSEngine, the timer
// registry (setTimeout/setInterval/requestAnimationFrame), the watchdog, the
// run lifecycle (one active run; it stays live until the synchronous evaluation
// AND every timer and script canvas window are done), the one-undo-entry-per-run
// snapshot rule, and the coalesced canvas/panel refresh. The API wrapper objects
// (script_api.hpp) reach MainWindow only through the service methods here; they
// hold session ids + LayerIds, never pointers (sessions close and the layers
// vector reallocates). See docs/scripting.md.
class ScriptEngineHost : public QObject {
  Q_OBJECT

public:
  explicit ScriptEngineHost(MainWindow& window);
  ~ScriptEngineHost() override;

  struct RunOptions {
    // Display name for errors, the undo label, and the history panel.
    QString name;
    // Source file path; empty for editor-buffer runs. include() resolves
    // relative paths against the running script's directory.
    QString path;
    // Raw "key=value" tokens (CLI --script-arg), surfaced as patchy.args.
    QStringList args;
  };

  // Starts a run (false when one is already active). Errors and console output
  // go to message_sink; completion is announced through run_state_changed.
  bool run_source(const QString& source, RunOptions options);
  bool run_file(const QString& path, QStringList args = {});
  // User stop: interrupts a stuck synchronous phase and tears down timers and
  // script windows. Safe to call when no run is active.
  void stop_active_run();
  [[nodiscard]] bool run_active() const noexcept { return run_ != nullptr; }
  [[nodiscard]] QString active_run_name() const;

  enum class MessageKind { Log, Warn, Error };
  // The last messages (bounded), kept so the editor dialog can show output that
  // happened while it was closed (menu/CLI runs).
  [[nodiscard]] const QStringList& message_backlog() const noexcept { return message_backlog_; }
  [[nodiscard]] bool last_run_had_error() const noexcept { return last_run_had_error_; }

signals:
  // Console output and errors (kind is int(MessageKind)); listeners: the editor
  // dialog pane and the CLI output-file capture.
  void message_emitted(int kind, const QString& text);
  // Fired when a run starts and when it fully completes (poll run_active()).
  void run_state_changed();

public:

  // --- services for the API wrappers and script canvas windows ---
  [[nodiscard]] MainWindow& window() noexcept { return window_; }
  [[nodiscard]] QJSEngine* engine() noexcept { return engine_.get(); }
  // Throws a JS error out of the currently executing script code.
  void throw_js_error(const QString& message);

  [[nodiscard]] std::vector<std::int64_t> session_ids() const;
  [[nodiscard]] std::int64_t active_session_id() const;  // 0 = none
  [[nodiscard]] Document* session_document(std::int64_t session_id) noexcept;
  [[nodiscard]] const Document* session_document_const(std::int64_t session_id) const noexcept;
  [[nodiscard]] QString session_title(std::int64_t session_id) const;
  [[nodiscard]] QString session_file_path(std::int64_t session_id) const;
  std::int64_t open_document_file(const QString& path);  // 0 on failure
  std::int64_t create_document(int width, int height);
  bool save_session_to_path(std::int64_t session_id, const QString& path);
  bool close_session(std::int64_t session_id);
  void activate_session(std::int64_t session_id);

  // Undo integration: the FIRST mutation a run makes to a session pushes one
  // "Script: <name>" snapshot; later mutations in the same run ride it, so the
  // whole run undoes in one step. Returns false when the session is gone.
  bool prepare_mutation(std::int64_t session_id);
  // Scripts can opt out of the undo snapshot for speed (app.undoEnabled = false;
  // per-run state, default on). Off = mutations from that point cannot be
  // undone; sessions are still marked modified so closing protects the work.
  [[nodiscard]] bool undo_enabled() const noexcept;
  void set_undo_enabled(bool enabled) noexcept;
  // Coalesced refresh (flushed once per event-loop turn): pixel changes mark the
  // canvas dirty (empty rect = whole canvas); structure changes also rebuild the
  // layer panel and action states.
  void note_pixels_changed(std::int64_t session_id, const QRect& dirty_document_rect);
  void note_structure_changed(std::int64_t session_id);

  // Palette-mode write constraint for script pixel writes (setPixels/fill are
  // tool-like writes and snap; filters deliberately stay advisory, matching the
  // interactive behavior). No-ops when palette mode is off.
  void palette_snap_buffer(std::int64_t session_id, PixelBuffer& pixels);
  [[nodiscard]] QColor palette_snap_color(std::int64_t session_id, QColor color) const;

  // Selection, through the session's canvas.
  void select_all(std::int64_t session_id);
  void deselect(std::int64_t session_id);
  void select_region(std::int64_t session_id, const QRegion& region);
  [[nodiscard]] QRegion selection_region(std::int64_t session_id) const;
  [[nodiscard]] bool has_selection(std::int64_t session_id) const;

  // Text layers, driven through the real inline-editor pipeline (the
  // cli_append_text_to_text_layers technique) so rasters render normally.
  struct TextLayerParams {
    QString text;
    QString family;      // empty = current default
    double size_pt{0.0};  // <= 0 = current default
    bool bold{false};
    bool italic{false};
    QColor color;        // invalid = current default
    QPoint position{0, 0};
  };
  std::optional<LayerId> add_text_layer(std::int64_t session_id, const TextLayerParams& params);
  bool set_text_layer_text(std::int64_t session_id, LayerId layer_id, const QString& text);
  [[nodiscard]] QString text_layer_text(std::int64_t session_id, LayerId layer_id) const;
  [[nodiscard]] bool layer_is_text_layer(std::int64_t session_id, LayerId layer_id) const;

  // Filter application onto a layer's pixel buffer by registry id.
  bool apply_filter_to_layer(std::int64_t session_id, LayerId layer_id, const QString& filter_id,
                             const QJSValue& params);

  // Interactive helpers (suppressed under CLI automation: alert logs instead,
  // prompt returns its default, pickers return empty, showDialog returns the
  // field defaults). Each pauses the watchdog while its dialog is up.
  void show_alert(const QString& text);
  [[nodiscard]] QString show_prompt(const QString& text, const QString& default_value, bool* accepted);
  [[nodiscard]] QString choose_folder(const QString& title);
  [[nodiscard]] QString choose_open_file(const QString& title, const QString& filter);
  [[nodiscard]] QString choose_save_file(const QString& title, const QString& filter);
  // Declarative form dialog (patchy.ui.showDialog): builds widgets from the
  // spec's field list, returns a values object, or null when cancelled.
  [[nodiscard]] QJSValue show_form_dialog(const QJSValue& spec);

  // app.runCommand / app.commandIds: registered app actions by their stable
  // HotkeyRegistry command id. run_app_command returns false for unknown or
  // currently disabled commands.
  bool run_app_command(const QString& command_id);
  [[nodiscard]] QStringList app_command_ids() const;

  // Script canvas windows join the run lifecycle: the run stays live while any
  // window is open, and stopping the run closes them.
  void adopt_canvas_window(ScriptCanvasWindow* window);
  void canvas_window_closed(ScriptCanvasWindow* window);
  // The surface of the most recently opened still-open script canvas window
  // (the Script Manager's Set Icon capture source); null when none is open.
  [[nodiscard]] QImage active_canvas_window_image() const;

  // JS bridge (bound behind the bootstrap prelude; not for direct script use).
  Q_INVOKABLE int scriptSetTimer(const QJSValue& callback, int interval_ms, bool repeat);
  Q_INVOKABLE void scriptClearTimer(int timer_id);
  Q_INVOKABLE void consoleEmit(int kind, const QString& text);
  Q_INVOKABLE void includeScript(const QString& path);
  // True while an include()d file's top-level code runs (patchy.isMainScript
  // is its negation - the `if __name__ == "__main__"` pattern).
  Q_INVOKABLE bool scriptIsIncluded() const;

  // Runs a stored JS callback under the watchdog with error trapping. Used by
  // the timer registry and the canvas windows so every entry into script code
  // shares one guard path. Returns false when the callback errored (the run is
  // then finishing; callers must not run further script code).
  bool call_script_callback(QJSValue callback, const QJSValueList& args);

private:
  struct ScriptRun {
    QString name;
    QStringList include_dir_stack;
    // Depth of nested include() evaluations (0 = top-level script code).
    int include_depth{0};
    std::set<std::int64_t> snapshotted_sessions;
    bool undo_enabled{true};
    std::map<int, QTimer*> timers;
    int next_timer_id{1};
    std::vector<QPointer<ScriptCanvasWindow>> windows;
    bool sync_running{false};
    // True while a stored JS callback executes (timer tick, window event); the
    // engine must not be destroyed from inside either, so stop/finish requests
    // arriving then are deferred.
    bool in_callback{false};
    bool stop_requested{false};
    bool finishing{false};
    bool had_error{false};
  };

  struct PendingRefresh {
    QRegion dirty;
    bool full_canvas{false};
    bool structure{false};
  };

  // RAII: disarms the watchdog while a modal interactive helper (alert,
  // prompt, pickers, showDialog, runCommand) blocks in a nested event loop,
  // and re-arms it with a FRESH timeout on exit. Without this, a user who
  // thinks at a dialog for longer than the timeout gets the script
  // interrupted the moment it resumes.
  struct ModalWatchdogPause {
    explicit ModalWatchdogPause(ScriptEngineHost& host);
    ~ModalWatchdogPause();
    ModalWatchdogPause(const ModalWatchdogPause&) = delete;
    ModalWatchdogPause& operator=(const ModalWatchdogPause&) = delete;
    ScriptEngineHost& host_;
    bool rearm_{false};
  };

  void install_bindings(const RunOptions& options);
  void report_error(const QJSValue& error);
  void emit_message(MessageKind kind, const QString& text);
  [[nodiscard]] std::chrono::milliseconds watchdog_timeout() const;
  // Deferred completion check: a run may complete from inside a JS callback, and
  // the engine cannot be destroyed while it is executing.
  void schedule_completion_check();
  void check_run_completion();
  void finish_run();
  void teardown_run_resources();
  void schedule_refresh_flush();
  void flush_pending_refresh();
  [[nodiscard]] QString resolve_include_path(const QString& path) const;
  // The session's canvas (nullptr when the session is gone); MainWindow access
  // stays inside host members (the friend grant does not reach free helpers).
  [[nodiscard]] CanvasWidget* session_canvas(std::int64_t session_id) const;

  MainWindow& window_;
  std::unique_ptr<QJSEngine> engine_;
  std::unique_ptr<ScriptRun> run_;
  std::unique_ptr<ScriptWatchdog> watchdog_;
  std::map<std::int64_t, PendingRefresh> pending_refresh_;
  QStringList message_backlog_;
  bool last_run_had_error_{false};
  bool refresh_flush_scheduled_{false};
  bool completion_check_scheduled_{false};
};

}  // namespace patchy::ui
