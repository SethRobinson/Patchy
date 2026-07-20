// The JS scripting engine host (docs/scripting.md): per-run QJSEngine lifecycle,
// the bootstrap prelude (console/timers/include/patchy namespace), the watchdog,
// undo/refresh integration, and the MainWindow-facing services the API wrappers
// (script_api.cpp) and script canvas windows call. ScriptEngineHost is a friend
// of MainWindow; every wrapper reaches MainWindow through here.

#include "ui/script_engine.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/script_api.hpp"
#include "ui/script_canvas_window.hpp"
#include "ui/script_folders.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QDir>
#include <QInputDialog>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QLineEdit>
#include <QPushButton>
#include <QQmlEngine>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kDefaultWatchdogTimeoutMs = 30000;

// Defines console/timers/include and the `patchy` namespace on the global
// object, backed by the hidden host bridge objects. Runs non-strict so the
// IIFE's `this` is the global object.
constexpr const char* kBootstrapSource = R"JS(
(function() {
  var g = this;
  var host = g.__patchy_host;
  function fmt(value) {
    if (typeof value === 'string') { return value; }
    if (value === undefined) { return 'undefined'; }
    if (value === null) { return 'null'; }
    try {
      var json = JSON.stringify(value);
      if (json !== undefined) { return json; }
    } catch (e) {}
    return String(value);
  }
  function joined(args) { return Array.prototype.map.call(args, fmt).join(' '); }
  g.console = {
    log: function() { host.consoleEmit(0, joined(arguments)); },
    info: function() { host.consoleEmit(0, joined(arguments)); },
    warn: function() { host.consoleEmit(1, joined(arguments)); },
    error: function() { host.consoleEmit(2, joined(arguments)); }
  };
  g.setTimeout = function(fn, ms) { return host.scriptSetTimer(fn, ms | 0, false); };
  g.setInterval = function(fn, ms) { return host.scriptSetTimer(fn, ms | 0, true); };
  g.clearTimeout = function(id) { host.scriptClearTimer(id | 0); };
  g.clearInterval = function(id) { host.scriptClearTimer(id | 0); };
  g.requestAnimationFrame = function(fn) { return host.scriptSetTimer(fn, 16, false); };
  g.include = function(path) { host.includeScript(String(path)); };
  g.patchy = {
    app: g.app,
    io: g.__patchy_io,
    ui: g.__patchy_ui,
    apiVersion: g.app.apiVersion,
    version: g.app.version,
    args: g.__patchy_args,
    isMainScript: function() { return !host.scriptIsIncluded(); }
  };
})();
)JS";

}  // namespace

// ---------------------------------------------------------------------------
// ScriptWatchdog

ScriptWatchdog::ScriptWatchdog(std::function<void()> on_timeout)
    : on_timeout_(std::move(on_timeout)) {
  thread_ = std::thread([this] {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      cv_.wait(lock, [this] { return quit_ || armed_; });
      if (quit_) {
        return;
      }
      if (cv_.wait_until(lock, deadline_, [this] { return quit_ || !armed_; })) {
        if (quit_) {
          return;
        }
        continue;  // disarmed in time
      }
      armed_ = false;
      // Deadline passed with the guard still armed: interrupt the engine. The
      // callback only flips an atomic inside QJSEngine, safe from this thread.
      on_timeout_();
    }
  });
}

ScriptWatchdog::~ScriptWatchdog() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  cv_.notify_all();
  thread_.join();
}

void ScriptWatchdog::arm(std::chrono::milliseconds timeout) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    deadline_ = std::chrono::steady_clock::now() + timeout;
    armed_ = true;
  }
  cv_.notify_all();
}

void ScriptWatchdog::disarm() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    armed_ = false;
  }
  cv_.notify_all();
}

// ---------------------------------------------------------------------------
// ScriptEngineHost

ScriptEngineHost::ScriptEngineHost(MainWindow& window) : QObject(&window), window_(window) {}

ScriptEngineHost::~ScriptEngineHost() {
  if (run_ != nullptr) {
    teardown_run_resources();
    run_.reset();
  }
  engine_.reset();
}

QString ScriptEngineHost::active_run_name() const {
  return run_ != nullptr ? run_->name : QString();
}

std::chrono::milliseconds ScriptEngineHost::watchdog_timeout() const {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("PATCHY_SCRIPT_TIMEOUT_MS", &ok);
  return std::chrono::milliseconds(ok && value > 0 ? value : kDefaultWatchdogTimeoutMs);
}

void ScriptEngineHost::emit_message(MessageKind kind, const QString& text) {
  constexpr int kBacklogLimit = 500;
  message_backlog_.append(text);
  if (message_backlog_.size() > kBacklogLimit) {
    message_backlog_.removeFirst();
  }
  emit message_emitted(static_cast<int>(kind), text);
}

void ScriptEngineHost::report_error(const QJSValue& error) {
  QString text = error.toString();
  const auto file = error.property(QStringLiteral("fileName")).toString();
  const auto line = error.property(QStringLiteral("lineNumber")).toInt();
  if (!file.isEmpty()) {
    text += QStringLiteral(" (%1:%2)").arg(QFileInfo(file).fileName()).arg(line);
  } else if (line > 0) {
    text += QStringLiteral(" (line %1)").arg(line);
  }
  emit_message(MessageKind::Error, text);
}

bool ScriptEngineHost::run_file(const QString& path, QStringList args) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    emit_message(MessageKind::Error,
                 tr("Could not read script file: %1").arg(QDir::toNativeSeparators(path)));
    return false;
  }
  RunOptions options;
  options.name = QFileInfo(path).fileName();
  options.path = path;
  options.args = std::move(args);
  return run_source(QString::fromUtf8(file.readAll()), std::move(options));
}

bool ScriptEngineHost::run_source(const QString& source, RunOptions options) {
  if (run_ != nullptr) {
    emit_message(MessageKind::Error, tr("A script is already running: %1").arg(run_->name));
    return false;
  }
  if (watchdog_ == nullptr) {
    watchdog_ = std::make_unique<ScriptWatchdog>([this] {
      if (engine_ != nullptr) {
        engine_->setInterrupted(true);
      }
    });
  }
  run_ = std::make_unique<ScriptRun>();
  run_->name = options.name.isEmpty() ? tr("Untitled Script") : options.name;
  if (!options.path.isEmpty()) {
    run_->include_dir_stack.push_back(QFileInfo(options.path).absolutePath());
  }
  engine_ = std::make_unique<QJSEngine>();
  install_bindings(options);
  emit run_state_changed();

  run_->sync_running = true;
  engine_->setInterrupted(false);
  watchdog_->arm(watchdog_timeout());
  const auto file_name = options.path.isEmpty() ? run_->name : options.path;
  const QJSValue result = engine_->evaluate(source, file_name, 1);
  watchdog_->disarm();
  run_->sync_running = false;
  if (engine_->isInterrupted()) {
    run_->had_error = true;
    engine_->setInterrupted(false);
    emit_message(MessageKind::Error, run_->stop_requested
                                         ? tr("Script stopped.")
                                         : tr("Script stopped: it exceeded the time limit."));
  } else if (result.isError()) {
    run_->had_error = true;
    report_error(result);
  }
  schedule_completion_check();
  return !run_->had_error;
}

void ScriptEngineHost::install_bindings(const RunOptions& options) {
  auto& engine = *engine_;
  auto global = engine.globalObject();

  // patchy.args: the CLI --script-arg key=value tokens (empty object
  // otherwise). Tokens without '=' become keys with an empty-string value.
  auto args_object = engine.newObject();
  for (const auto& token : options.args) {
    const auto separator = token.indexOf(QLatin1Char('='));
    const auto key = separator < 0 ? token : token.left(separator);
    const auto value = separator < 0 ? QString() : token.mid(separator + 1);
    if (!key.isEmpty()) {
      args_object.setProperty(key, value);
    }
  }
  global.setProperty(QStringLiteral("__patchy_args"), args_object);

  // The bridge objects stay C++-owned: `this` is parented to the window, and
  // the singleton wrappers are parented to `this`, so the JS GC never deletes
  // them (per-access document/layer wrappers are parentless and JS-owned).
  const auto host_value = engine.newQObject(this);
  QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
  global.setProperty(QStringLiteral("__patchy_host"), host_value);

  auto* app_object = new ScriptAppObject(*this);
  app_object->setParent(this);
  global.setProperty(QStringLiteral("app"), engine.newQObject(app_object));

  auto* io_object = new ScriptIoObject(*this);
  io_object->setParent(this);
  global.setProperty(QStringLiteral("__patchy_io"), engine.newQObject(io_object));

  auto* ui_object = new ScriptUiObject(*this);
  ui_object->setParent(this);
  global.setProperty(QStringLiteral("__patchy_ui"), engine.newQObject(ui_object));

  const QJSValue bootstrap = engine.evaluate(QString::fromLatin1(kBootstrapSource),
                                             QStringLiteral("<patchy-bootstrap>"), 1);
  Q_ASSERT(!bootstrap.isError());
}

void ScriptEngineHost::stop_active_run() {
  if (run_ == nullptr) {
    return;
  }
  run_->stop_requested = true;
  if (run_->sync_running || run_->in_callback) {
    // Script code is executing (or a wrapper opened a nested event loop from
    // it); the engine cannot be destroyed from under it. Interrupt and let the
    // evaluate/callback caller finish the run.
    if (engine_ != nullptr) {
      engine_->setInterrupted(true);
    }
    return;
  }
  emit_message(MessageKind::Warn, tr("Script stopped."));
  finish_run();
}

bool ScriptEngineHost::call_script_callback(QJSValue callback, const QJSValueList& args) {
  if (run_ == nullptr || run_->finishing || engine_ == nullptr || !callback.isCallable()) {
    return false;
  }
  run_->in_callback = true;
  engine_->setInterrupted(false);
  watchdog_->arm(watchdog_timeout());
  const QJSValue result = callback.call(args);
  watchdog_->disarm();
  if (run_ == nullptr) {
    // The callback itself stopped the run.
    return false;
  }
  run_->in_callback = false;
  bool failed = false;
  if (engine_ != nullptr && engine_->isInterrupted()) {
    run_->had_error = true;
    engine_->setInterrupted(false);
    emit_message(MessageKind::Error, run_->stop_requested
                                         ? tr("Script stopped.")
                                         : tr("Script stopped: a callback exceeded the time limit."));
    failed = true;
  } else if (result.isError()) {
    run_->had_error = true;
    report_error(result);
    failed = true;
  }
  // Callers may be a canvas window's event filter or a timer slot; finishing
  // tears both down, so it must never run from inside them.
  schedule_completion_check();
  return !failed;
}

void ScriptEngineHost::schedule_completion_check() {
  if (completion_check_scheduled_) {
    return;
  }
  completion_check_scheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    completion_check_scheduled_ = false;
    check_run_completion();
  });
}

void ScriptEngineHost::check_run_completion() {
  if (run_ == nullptr || run_->sync_running || run_->in_callback || run_->finishing) {
    return;
  }
  if (run_->had_error) {
    finish_run();
    return;
  }
  const bool windows_open = std::any_of(run_->windows.begin(), run_->windows.end(),
                                        [](const QPointer<ScriptCanvasWindow>& window) {
                                          return window != nullptr && window->is_open();
                                        });
  if (!run_->timers.empty() || windows_open) {
    return;
  }
  finish_run();
}

void ScriptEngineHost::finish_run() {
  if (run_ == nullptr || run_->finishing) {
    return;
  }
  run_->finishing = true;
  last_run_had_error_ = run_->had_error;
  teardown_run_resources();
  flush_pending_refresh();
  run_.reset();
  // The engine must outlive every stored QJSValue; the canvas windows released
  // theirs in teardown and the run owned the rest.
  engine_.reset();
  emit run_state_changed();
}

void ScriptEngineHost::teardown_run_resources() {
  for (auto& [id, timer] : run_->timers) {
    timer->stop();
    delete timer;
  }
  run_->timers.clear();
  for (auto& window : run_->windows) {
    if (window != nullptr) {
      window->release_script_state();
      delete window.data();
    }
  }
  run_->windows.clear();
}

// ---------------------------------------------------------------------------
// JS bridge

int ScriptEngineHost::scriptSetTimer(const QJSValue& callback, int interval_ms, bool repeat) {
  if (run_ == nullptr || run_->finishing) {
    return 0;
  }
  if (!callback.isCallable()) {
    throw_js_error(tr("setTimeout/setInterval needs a function."));
    return 0;
  }
  const int id = run_->next_timer_id++;
  auto* timer = new QTimer(this);
  timer->setInterval(std::max(0, interval_ms));
  timer->setSingleShot(!repeat);
  QElapsedTimer elapsed;
  elapsed.start();
  connect(timer, &QTimer::timeout, this, [this, id, repeat, callback, elapsed]() mutable {
    if (run_ == nullptr || run_->finishing) {
      return;
    }
    if (!repeat) {
      const auto found = run_->timers.find(id);
      if (found != run_->timers.end()) {
        found->second->deleteLater();
        run_->timers.erase(found);
      }
    }
    const double dt_ms = static_cast<double>(elapsed.restart());
    call_script_callback(callback, QJSValueList{QJSValue(dt_ms)});
  });
  run_->timers.emplace(id, timer);
  timer->start();
  return id;
}

void ScriptEngineHost::scriptClearTimer(int timer_id) {
  if (run_ == nullptr) {
    return;
  }
  const auto found = run_->timers.find(timer_id);
  if (found == run_->timers.end()) {
    return;
  }
  found->second->stop();
  found->second->deleteLater();
  run_->timers.erase(found);
  schedule_completion_check();
}

void ScriptEngineHost::consoleEmit(int kind, const QString& text) {
  switch (kind) {
    case 1:
      emit_message(MessageKind::Warn, text);
      break;
    case 2:
      emit_message(MessageKind::Error, text);
      break;
    default:
      emit_message(MessageKind::Log, text);
      break;
  }
}

namespace {

// A resolved include that lands inside the bundled scripts folder is mapped
// through the shadow-override store: a user copy at the same relative path
// wins, matching what the Scripts menu and editor run (script_folders.hpp).
QString apply_user_script_override(const QString& resolved) {
  const auto relative =
      relative_path_under(MainWindow::bundled_scripts_directory(), resolved);
  if (relative.isEmpty()) {
    return resolved;  // not a bundled script
  }
  const auto candidate = QDir(MainWindow::user_scripts_directory()).absoluteFilePath(relative);
  return QFileInfo::exists(candidate) ? candidate : resolved;
}

}  // namespace

QString ScriptEngineHost::resolve_include_path(const QString& path) const {
  const QFileInfo info(path);
  if (info.isAbsolute()) {
    return info.absoluteFilePath();
  }
  // Search order: relative to the including script, then the user scripts
  // root, then the bundled scripts root - so include("Effects/foo.js") works
  // from any script, and a user copy shadows the bundled one.
  if (run_ != nullptr && !run_->include_dir_stack.isEmpty()) {
    const QFileInfo relative(QDir(run_->include_dir_stack.last()).absoluteFilePath(path));
    if (relative.exists()) {
      return relative.absoluteFilePath();
    }
  }
  const QFileInfo in_user(QDir(MainWindow::user_scripts_directory()).absoluteFilePath(path));
  if (in_user.exists()) {
    return in_user.absoluteFilePath();
  }
  const auto bundled_root = MainWindow::bundled_scripts_directory();
  if (!bundled_root.isEmpty()) {
    const QFileInfo in_bundled(QDir(bundled_root).absoluteFilePath(path));
    if (in_bundled.exists()) {
      return in_bundled.absoluteFilePath();
    }
  }
  // Nothing exists; keep the historical script-relative shape so the error
  // message names the most likely intended location.
  if (run_ != nullptr && !run_->include_dir_stack.isEmpty()) {
    return QDir(run_->include_dir_stack.last()).absoluteFilePath(path);
  }
  return info.absoluteFilePath();
}

void ScriptEngineHost::includeScript(const QString& path) {
  if (run_ == nullptr || engine_ == nullptr) {
    return;
  }
  const auto resolved = apply_user_script_override(resolve_include_path(path));
  QFile file(resolved);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    throw_js_error(tr("include: could not read %1").arg(QDir::toNativeSeparators(resolved)));
    return;
  }
  run_->include_dir_stack.push_back(QFileInfo(resolved).absolutePath());
  ++run_->include_depth;
  const QJSValue result =
      engine_->evaluate(QString::fromUtf8(file.readAll()), resolved, 1);
  --run_->include_depth;
  run_->include_dir_stack.pop_back();
  if (result.isError()) {
    // Re-throw so the includer's call site fails with the nested error.
    engine_->throwError(result);
  }
}

bool ScriptEngineHost::scriptIsIncluded() const {
  return run_ != nullptr && run_->include_depth > 0;
}

void ScriptEngineHost::throw_js_error(const QString& message) {
  if (engine_ != nullptr) {
    engine_->throwError(QJSValue::GenericError, message);
  }
}

// ---------------------------------------------------------------------------
// Session services

std::vector<std::int64_t> ScriptEngineHost::session_ids() const {
  std::vector<std::int64_t> ids;
  ids.reserve(window_.sessions_.size());
  for (const auto& session : window_.sessions_) {
    if (session != nullptr) {
      ids.push_back(session->session_id);
    }
  }
  return ids;
}

std::int64_t ScriptEngineHost::active_session_id() const {
  const auto* session = window_.active_session();
  return session != nullptr ? session->session_id : 0;
}

Document* ScriptEngineHost::session_document(std::int64_t session_id) noexcept {
  auto* session = window_.session_with_id(session_id);
  return session != nullptr ? &session->document : nullptr;
}

const Document* ScriptEngineHost::session_document_const(std::int64_t session_id) const noexcept {
  const auto* session = const_cast<MainWindow&>(window_).session_with_id(session_id);
  return session != nullptr ? &session->document : nullptr;
}

QString ScriptEngineHost::session_title(std::int64_t session_id) const {
  const auto* session = const_cast<MainWindow&>(window_).session_with_id(session_id);
  return session != nullptr ? session->title : QString();
}

QString ScriptEngineHost::session_file_path(std::int64_t session_id) const {
  const auto* session = const_cast<MainWindow&>(window_).session_with_id(session_id);
  return session != nullptr ? session->path : QString();
}

std::int64_t ScriptEngineHost::open_document_file(const QString& path) {
  std::set<std::int64_t> before;
  for (const auto id : session_ids()) {
    before.insert(id);
  }
  window_.open_document_path(path);
  for (const auto id : session_ids()) {
    if (before.count(id) == 0) {
      return id;
    }
  }
  return 0;
}

std::int64_t ScriptEngineHost::create_document(int width, int height) {
  if (width < 1 || height < 1 || width > 30000 || height > 30000) {
    return 0;
  }
  Document document(width, height, PixelFormat::rgba8());
  PixelBuffer background(width, height, PixelFormat::rgba8());
  background.clear(255);
  auto& layer = document.add_pixel_layer(tr("Background").toStdString(), std::move(background));
  document.set_active_layer(layer.id());
  window_.add_document_session(std::move(document), tr("Untitled"));
  return active_session_id();
}

bool ScriptEngineHost::save_session_to_path(std::int64_t session_id, const QString& path) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return false;
  }
  window_.activate_document_session(*session);
  return window_.save_document_to_path(path, std::nullopt, /*flatten_confirmed=*/true);
}

bool ScriptEngineHost::close_session(std::int64_t session_id) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return false;
  }
  // Scripts close without prompting (the script author decided); mark saved so
  // the close path cannot raise a modified-document confirmation.
  window_.set_session_saved(*session);
  return window_.close_document_session(*session);
}

void ScriptEngineHost::activate_session(std::int64_t session_id) {
  auto* session = window_.session_with_id(session_id);
  if (session != nullptr) {
    window_.activate_document_session(*session);
  }
}

bool ScriptEngineHost::prepare_mutation(std::int64_t session_id) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return false;
  }
  if (run_ != nullptr) {
    if (!run_->undo_enabled) {
      // Undo opted out (app.undoEnabled = false): skip the snapshot, but the
      // session is still modified work that closing must protect.
      window_.mark_session_modified(*session);
      return true;
    }
    if (run_->snapshotted_sessions.count(session_id) == 0) {
      window_.push_undo_snapshot(*session, tr("Script: %1").arg(run_->name));
      run_->snapshotted_sessions.insert(session_id);
    }
  } else {
    // Defensive: wrappers should never outlive their run, but a mutation with
    // no run still deserves an undo entry.
    window_.push_undo_snapshot(*session, tr("Script"));
  }
  return true;
}

bool ScriptEngineHost::undo_enabled() const noexcept {
  return run_ == nullptr || run_->undo_enabled;
}

void ScriptEngineHost::set_undo_enabled(bool enabled) noexcept {
  if (run_ != nullptr) {
    run_->undo_enabled = enabled;
  }
}

void ScriptEngineHost::note_pixels_changed(std::int64_t session_id, const QRect& dirty_document_rect) {
  auto& pending = pending_refresh_[session_id];
  if (dirty_document_rect.isEmpty()) {
    pending.full_canvas = true;
  } else if (!pending.full_canvas) {
    pending.dirty += dirty_document_rect;
  }
  schedule_refresh_flush();
}

void ScriptEngineHost::note_structure_changed(std::int64_t session_id) {
  auto& pending = pending_refresh_[session_id];
  pending.structure = true;
  pending.full_canvas = true;
  schedule_refresh_flush();
}

void ScriptEngineHost::schedule_refresh_flush() {
  if (refresh_flush_scheduled_) {
    return;
  }
  refresh_flush_scheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    refresh_flush_scheduled_ = false;
    flush_pending_refresh();
  });
}

void ScriptEngineHost::flush_pending_refresh() {
  if (pending_refresh_.empty()) {
    return;
  }
  auto pending = std::move(pending_refresh_);
  pending_refresh_.clear();
  bool active_structure = false;
  bool active_pixels = false;
  for (auto& [session_id, refresh] : pending) {
    auto* session = window_.session_with_id(session_id);
    if (session == nullptr) {
      continue;
    }
    if (session->canvas != nullptr) {
      if (refresh.full_canvas) {
        session->canvas->document_changed();
      } else if (!refresh.dirty.isEmpty()) {
        session->canvas->document_changed_effect_bounds(refresh.dirty);
      }
    }
    if (session == window_.active_session()) {
      active_structure = active_structure || refresh.structure;
      active_pixels = true;
    }
  }
  // Panels mirror the active session only.
  if (active_structure) {
    window_.refresh_layer_list();
    window_.refresh_layer_controls();
    window_.update_document_action_state();
  } else if (active_pixels) {
    window_.refresh_layer_thumbnails();
  }
  if (active_pixels) {
    window_.refresh_document_info();
  }
}

// ---------------------------------------------------------------------------
// Palette-mode write constraint

void ScriptEngineHost::palette_snap_buffer(std::int64_t session_id, PixelBuffer& pixels) {
  const auto* document = session_document_const(session_id);
  if (document == nullptr || !document->palette_editing().has_value()) {
    return;
  }
  const auto& editing = *document->palette_editing();
  PaletteLut lut;
  lut.build(editing.palette.colors);
  if (lut.empty()) {
    return;
  }
  (void)apply_palette_to_pixels(pixels, lut, PaletteDither::None, editing.alpha_threshold);
}

QColor ScriptEngineHost::palette_snap_color(std::int64_t session_id, QColor color) const {
  const auto* document = session_document_const(session_id);
  if (document == nullptr || !document->palette_editing().has_value()) {
    return color;
  }
  const auto& editing = *document->palette_editing();
  PaletteLut lut;
  lut.build(editing.palette.colors);
  if (lut.empty()) {
    return color;
  }
  const auto snapped = lut.snap(static_cast<std::uint8_t>(color.red()),
                                static_cast<std::uint8_t>(color.green()),
                                static_cast<std::uint8_t>(color.blue()));
  const int alpha = color.alpha() >= editing.alpha_threshold ? 255 : 0;
  return QColor(snapped.red, snapped.green, snapped.blue, alpha);
}

// ---------------------------------------------------------------------------
// Selection

CanvasWidget* ScriptEngineHost::session_canvas(std::int64_t session_id) const {
  auto* session = const_cast<MainWindow&>(window_).session_with_id(session_id);
  return session != nullptr ? session->canvas : nullptr;
}

void ScriptEngineHost::select_all(std::int64_t session_id) {
  if (auto* canvas = session_canvas(session_id)) {
    canvas->select_all();
  }
}

void ScriptEngineHost::deselect(std::int64_t session_id) {
  if (auto* canvas = session_canvas(session_id)) {
    canvas->clear_selection();
  }
}

void ScriptEngineHost::select_region(std::int64_t session_id, const QRegion& region) {
  auto* canvas = session_canvas(session_id);
  if (canvas == nullptr) {
    return;
  }
  CanvasWidget::SelectionSnapshot snapshot;
  snapshot.selection = region;
  snapshot.display_region = region;
  canvas->apply_selection_snapshot(snapshot);
}

QRegion ScriptEngineHost::selection_region(std::int64_t session_id) const {
  auto* canvas = session_canvas(session_id);
  return canvas != nullptr ? canvas->capture_selection_snapshot().selection : QRegion();
}

bool ScriptEngineHost::has_selection(std::int64_t session_id) const {
  auto* canvas = session_canvas(session_id);
  return canvas != nullptr && canvas->has_selection();
}

// ---------------------------------------------------------------------------
// Text layers (the cli_append_text_to_text_layers technique: drive the real
// inline-editor pipeline so rasters render through the normal commit path)

namespace {

QTextEdit* wait_for_inline_text_editor(CanvasWidget* canvas) {
  QTextEdit* editor = nullptr;
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 5000) {
    editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor != nullptr) {
      return editor;
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  }
  return nullptr;
}

void collect_layer_ids(const std::vector<Layer>& layers, std::set<LayerId>& out) {
  for (const auto& layer : layers) {
    out.insert(layer.id());
    collect_layer_ids(layer.children(), out);
  }
}

}  // namespace

std::optional<LayerId> ScriptEngineHost::add_text_layer(std::int64_t session_id,
                                                        const TextLayerParams& params) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr || session->canvas == nullptr) {
    return std::nullopt;
  }
  window_.activate_document_session(*session);
  if (!prepare_mutation(session_id)) {
    return std::nullopt;
  }
  std::set<LayerId> before;
  collect_layer_ids(std::as_const(session->document).layers(), before);
  // add_text_at edits the ACTIVE layer when the point lands inside its bounds;
  // clearing the active layer guarantees a fresh text layer instead.
  session->document.clear_active_layer();
  window_.add_text_at(params.position);
  QTextEdit* editor = wait_for_inline_text_editor(session->canvas);
  if (editor == nullptr) {
    return std::nullopt;
  }
  QTextCharFormat format = editor->currentCharFormat();
  QFont font = format.font();
  if (!params.family.isEmpty()) {
    font.setFamily(params.family);
  }
  if (params.size_pt > 0.0) {
    font.setPointSizeF(params.size_pt);
  }
  font.setBold(params.bold);
  font.setItalic(params.italic);
  format.setFont(font);
  if (params.color.isValid()) {
    format.setForeground(params.color);
  }
  editor->setCurrentCharFormat(format);
  editor->insertPlainText(params.text);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  window_.finish_active_text_editor();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return std::nullopt;
  }
  std::optional<LayerId> created;
  std::function<void(const std::vector<Layer>&)> find_new = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (before.count(layer.id()) == 0 && layer_is_text(layer)) {
        created = layer.id();
      }
      find_new(layer.children());
    }
  };
  find_new(std::as_const(session->document).layers());
  note_structure_changed(session_id);
  return created;
}

bool ScriptEngineHost::set_text_layer_text(std::int64_t session_id, LayerId layer_id,
                                           const QString& text) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr || session->canvas == nullptr) {
    return false;
  }
  const auto* layer = std::as_const(session->document).find_layer(layer_id);
  if (layer == nullptr || !layer_is_text(*layer) || window_.layer_id_locks_image_pixels(layer_id)) {
    return false;
  }
  window_.activate_document_session(*session);
  if (!prepare_mutation(session_id)) {
    return false;
  }
  const auto bounds = layer->bounds();
  const QPoint anchor(bounds.x + std::max(1, bounds.width) / 2,
                      bounds.y + std::max(1, bounds.height) / 2);
  session->document.set_active_layer(layer_id);
  window_.add_text_at(anchor);
  QTextEdit* editor = wait_for_inline_text_editor(session->canvas);
  if (editor == nullptr) {
    return false;
  }
  if (editor->property("patchy.editingLayerId").toULongLong() != static_cast<qulonglong>(layer_id)) {
    window_.cancel_active_text_editor();
    return false;
  }
  auto cursor = editor->textCursor();
  cursor.select(QTextCursor::Document);
  editor->setTextCursor(cursor);
  editor->insertPlainText(text);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  window_.finish_active_text_editor();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  note_structure_changed(session_id);
  return true;
}

QString ScriptEngineHost::text_layer_text(std::int64_t session_id, LayerId layer_id) const {
  const auto* document = session_document_const(session_id);
  if (document == nullptr) {
    return QString();
  }
  const auto* layer = document->find_layer(layer_id);
  if (layer == nullptr) {
    return QString();
  }
  const auto found = layer->metadata().find(kLayerMetadataText);
  return found == layer->metadata().end() ? QString() : QString::fromStdString(found->second);
}

bool ScriptEngineHost::layer_is_text_layer(std::int64_t session_id, LayerId layer_id) const {
  const auto* document = session_document_const(session_id);
  if (document == nullptr) {
    return false;
  }
  const auto* layer = document->find_layer(layer_id);
  return layer != nullptr && layer_is_text(*layer);
}

// ---------------------------------------------------------------------------
// Filters

bool ScriptEngineHost::apply_filter_to_layer(std::int64_t session_id, LayerId layer_id,
                                             const QString& filter_id, const QJSValue& params) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    throw_js_error(tr("The document is no longer open."));
    return false;
  }
  const auto& registry = window_.filters_;
  const auto* definition = registry.find(filter_id.toStdString());
  if (definition == nullptr) {
    throw_js_error(tr("Unknown filter id: %1").arg(filter_id));
    return false;
  }
  auto invocation = registry.default_invocation(definition->identifier);
  if (params.isObject()) {
    QJSValueIterator it(params);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name().toStdString();
      const auto* parameter = [&]() -> const FilterParameterDefinition* {
        for (const auto& candidate : definition->catalog.parameters) {
          if (candidate.key == key) {
            return &candidate;
          }
        }
        return nullptr;
      }();
      if (parameter == nullptr) {
        throw_js_error(tr("Filter %1 has no parameter named %2")
                           .arg(filter_id, QString::fromStdString(key)));
        return false;
      }
      const auto value = it.value();
      switch (parameter->kind) {
        case FilterParameterKind::Integer:
          invocation.parameters[key] = static_cast<std::int64_t>(value.toInt());
          break;
        case FilterParameterKind::Double:
          invocation.parameters[key] = value.toNumber();
          break;
        case FilterParameterKind::Boolean:
          invocation.parameters[key] = value.toBool();
          break;
        case FilterParameterKind::Option:
          invocation.parameters[key] = value.toString().toStdString();
          break;
      }
    }
  }
  const auto normalized = registry.normalize(invocation);
  if (!normalized.has_value()) {
    throw_js_error(tr("Filter %1 rejected those parameters.").arg(filter_id));
    return false;
  }
  auto* layer = session->document.find_layer(layer_id);
  if (layer == nullptr) {
    throw_js_error(tr("The layer no longer exists."));
    return false;
  }
  if (layer->kind() != LayerKind::Pixel && layer->kind() != LayerKind::Text) {
    throw_js_error(tr("applyFilter needs a pixel layer."));
    return false;
  }
  if (std::as_const(*layer).pixels().empty()) {
    return true;  // nothing to filter
  }
  if (!prepare_mutation(session_id)) {
    return false;
  }
  const auto before_bounds = to_qrect(layer_render_bounds(std::as_const(*layer)));
  registry.apply(*normalized, layer->pixels());
  note_pixels_changed(session_id, before_bounds);
  return true;
}

// ---------------------------------------------------------------------------
// Interactive helpers

ScriptEngineHost::ModalWatchdogPause::ModalWatchdogPause(ScriptEngineHost& host) : host_(host) {
  rearm_ = host_.run_ != nullptr && host_.watchdog_ != nullptr &&
           (host_.run_->sync_running || host_.run_->in_callback);
  if (rearm_) {
    host_.watchdog_->disarm();
  }
}

ScriptEngineHost::ModalWatchdogPause::~ModalWatchdogPause() {
  if (rearm_ && host_.run_ != nullptr && host_.watchdog_ != nullptr) {
    host_.watchdog_->arm(host_.watchdog_timeout());
  }
}

void ScriptEngineHost::show_alert(const QString& text) {
  if (window_.cli_automation_mode_) {
    emit_message(MessageKind::Log, tr("[alert] %1").arg(text));
    return;
  }
  const ModalWatchdogPause pause(*this);
  show_information_message(&window_, tr("Script"), text, QStringLiteral("scriptAlertMessageBox"));
}

QString ScriptEngineHost::show_prompt(const QString& text, const QString& default_value,
                                      bool* accepted) {
  if (window_.cli_automation_mode_) {
    if (accepted != nullptr) {
      *accepted = true;
    }
    return default_value;
  }
  const ModalWatchdogPause pause(*this);
  bool ok = false;
  const auto result =
      QInputDialog::getText(&window_, tr("Script"), text, QLineEdit::Normal, default_value, &ok);
  if (accepted != nullptr) {
    *accepted = ok;
  }
  return ok ? result : QString();
}

QString ScriptEngineHost::choose_folder(const QString& title) {
  if (window_.cli_automation_mode_) {
    return {};
  }
  const ModalWatchdogPause pause(*this);
  return QFileDialog::getExistingDirectory(&window_,
                                           title.isEmpty() ? tr("Choose Folder") : title);
}

QString ScriptEngineHost::choose_open_file(const QString& title, const QString& filter) {
  if (window_.cli_automation_mode_) {
    return {};
  }
  const ModalWatchdogPause pause(*this);
  return get_open_file_name(&window_, title.isEmpty() ? tr("Choose File") : title, QString(),
                            filter.isEmpty() ? tr("All files (*)") : filter, nullptr,
                            QStringLiteral("scriptChooseOpenFileDialog"));
}

QString ScriptEngineHost::choose_save_file(const QString& title, const QString& filter) {
  if (window_.cli_automation_mode_) {
    return {};
  }
  const ModalWatchdogPause pause(*this);
  return get_save_file_name(&window_, title.isEmpty() ? tr("Save File") : title, QString(),
                            filter.isEmpty() ? tr("All files (*)") : filter, nullptr,
                            QStringLiteral("scriptChooseSaveFileDialog"));
}

QJSValue ScriptEngineHost::show_form_dialog(const QJSValue& spec) {
  if (engine_ == nullptr) {
    return QJSValue(QJSValue::NullValue);
  }
  const auto fields = spec.property(QStringLiteral("fields"));
  if (!fields.isArray()) {
    throw_js_error(tr("showDialog: spec.fields must be an array"));
    return QJSValue(QJSValue::UndefinedValue);
  }
  struct FieldSpec {
    QString key;
    QString label;
    QString type;
    QJSValue value;
  };
  std::vector<FieldSpec> parsed;
  const int count = fields.property(QStringLiteral("length")).toInt();
  for (int i = 0; i < count; ++i) {
    const auto field = fields.property(static_cast<quint32>(i));
    FieldSpec entry;
    entry.key = field.property(QStringLiteral("key")).toString();
    entry.type = field.property(QStringLiteral("type")).toString();
    entry.label = field.property(QStringLiteral("label")).isUndefined()
                      ? entry.key
                      : field.property(QStringLiteral("label")).toString();
    entry.value = field.property(QStringLiteral("value"));
    if (entry.key.isEmpty()) {
      throw_js_error(tr("showDialog: every field needs a non-empty \"key\""));
      return QJSValue(QJSValue::UndefinedValue);
    }
    static const QStringList kKnownTypes = {
        QStringLiteral("number"), QStringLiteral("slider"), QStringLiteral("checkbox"),
        QStringLiteral("choice"), QStringLiteral("text"),   QStringLiteral("color")};
    if (!kKnownTypes.contains(entry.type)) {
      throw_js_error(tr("showDialog: unknown field type \"%1\" (use number, slider, checkbox, "
                        "choice, text, or color)")
                         .arg(entry.type));
      return QJSValue(QJSValue::UndefinedValue);
    }
    parsed.push_back(std::move(entry));
  }

  // Unattended runs answer with the defaults, the app.prompt rule.
  if (window_.cli_automation_mode_) {
    auto result = engine_->newObject();
    for (std::size_t i = 0; i < parsed.size(); ++i) {
      result.setProperty(parsed[i].key, parsed[i].value);
    }
    return result;
  }

  const ModalWatchdogPause pause(*this);
  QDialog dialog(&window_);
  dialog.setObjectName(QStringLiteral("scriptFormDialog"));
  const auto title = spec.property(QStringLiteral("title")).toString();
  dialog.setWindowTitle(title.isEmpty() ? tr("Script") : title);
  auto* root = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  root->addLayout(form);

  // One getter per field reads its widget back into a JS value on accept.
  std::vector<std::function<QJSValue()>> getters;
  const auto fields_array = fields;
  for (std::size_t i = 0; i < parsed.size(); ++i) {
    const auto& entry = parsed[i];
    const auto field = fields_array.property(static_cast<quint32>(i));
    const auto object_name = QStringLiteral("scriptFormField_") + entry.key;
    if (entry.type == QLatin1String("number")) {
      auto* spin = new QDoubleSpinBox(&dialog);
      spin->setObjectName(object_name);
      const double minimum = field.property(QStringLiteral("min")).isNumber()
                                 ? field.property(QStringLiteral("min")).toNumber()
                                 : -1000000000.0;
      const double maximum = field.property(QStringLiteral("max")).isNumber()
                                 ? field.property(QStringLiteral("max")).toNumber()
                                 : 1000000000.0;
      const double step = field.property(QStringLiteral("step")).isNumber()
                              ? field.property(QStringLiteral("step")).toNumber()
                              : 1.0;
      const double value = entry.value.isNumber() ? entry.value.toNumber() : 0.0;
      const bool integral = qFuzzyCompare(minimum, std::floor(minimum)) &&
                            qFuzzyCompare(maximum, std::floor(maximum)) &&
                            qFuzzyCompare(step, std::floor(step)) &&
                            qFuzzyCompare(value, std::floor(value));
      const int decimals = field.property(QStringLiteral("decimals")).isNumber()
                               ? field.property(QStringLiteral("decimals")).toInt()
                               : (integral ? 0 : 2);
      spin->setDecimals(decimals);
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      spin->setValue(value);
      configure_dialog_spinbox(spin);
      form->addRow(entry.label, spin);
      getters.emplace_back([spin] { return QJSValue(spin->value()); });
    } else if (entry.type == QLatin1String("slider")) {
      const int minimum = field.property(QStringLiteral("min")).isNumber()
                              ? field.property(QStringLiteral("min")).toInt()
                              : 0;
      const int maximum = field.property(QStringLiteral("max")).isNumber()
                              ? field.property(QStringLiteral("max")).toInt()
                              : 100;
      const int value = entry.value.isNumber() ? entry.value.toInt() : minimum;
      auto* spin = add_dialog_slider_spin_row(form, &dialog, entry.label,
                                              object_name + QStringLiteral("Slider"), object_name,
                                              minimum, maximum, value);
      getters.emplace_back([spin] { return QJSValue(spin->value()); });
    } else if (entry.type == QLatin1String("checkbox")) {
      auto* box = new QCheckBox(entry.label, &dialog);
      box->setObjectName(object_name);
      box->setChecked(entry.value.toBool());
      form->addRow(QString(), box);
      getters.emplace_back([box] { return QJSValue(box->isChecked()); });
    } else if (entry.type == QLatin1String("choice")) {
      auto* combo = new QComboBox(&dialog);
      combo->setObjectName(object_name);
      const auto choices = field.property(QStringLiteral("choices"));
      const int choice_count =
          choices.isArray() ? choices.property(QStringLiteral("length")).toInt() : 0;
      for (int c = 0; c < choice_count; ++c) {
        combo->addItem(choices.property(static_cast<quint32>(c)).toString());
      }
      if (entry.value.isNumber()) {
        combo->setCurrentIndex(entry.value.toInt());
      } else if (!entry.value.isUndefined()) {
        combo->setCurrentText(entry.value.toString());
      }
      form->addRow(entry.label, combo);
      getters.emplace_back([combo] { return QJSValue(combo->currentText()); });
    } else if (entry.type == QLatin1String("color")) {
      auto* button = new QPushButton(&dialog);
      button->setObjectName(object_name);
      auto color = std::make_shared<QColor>(entry.value.toString());
      if (!color->isValid()) {
        *color = QColor(Qt::white);
      }
      const auto refresh_swatch = [button, color] {
        button->setText(color->alpha() < 255 ? color->name(QColor::HexArgb)
                                             : color->name(QColor::HexRgb));
        button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                                  .arg(color->name(QColor::HexRgb),
                                       color->lightness() < 128 ? QStringLiteral("#e8e8e8")
                                                                : QStringLiteral("#202020")));
      };
      refresh_swatch();
      QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, color, refresh_swatch] {
        const auto picked = QColorDialog::getColor(*color, &dialog, tr("Choose Color"),
                                                   QColorDialog::ShowAlphaChannel);
        if (picked.isValid()) {
          *color = picked;
          refresh_swatch();
        }
      });
      form->addRow(entry.label, button);
      getters.emplace_back([color] {
        return QJSValue(color->alpha() < 255 ? color->name(QColor::HexArgb)
                                             : color->name(QColor::HexRgb));
      });
    } else {  // "text"
      auto* line = new QLineEdit(entry.value.isUndefined() ? QString() : entry.value.toString(),
                                 &dialog);
      line->setObjectName(object_name);
      form->addRow(entry.label, line);
      getters.emplace_back([line] { return QJSValue(line->text()); });
    }
  }

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  root->addWidget(buttons);
  // Spin-box button styling must land AFTER all children exist (QSS
  // sub-control gotcha, dialog_utils.hpp).
  dialog.setStyleSheet(dialog_spinbox_button_style());

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return QJSValue(QJSValue::NullValue);
  }
  auto result = engine_->newObject();
  for (std::size_t i = 0; i < parsed.size(); ++i) {
    result.setProperty(parsed[i].key, getters[i]());
  }
  return result;
}

// ---------------------------------------------------------------------------
// App commands

bool ScriptEngineHost::run_app_command(const QString& command_id) {
  const auto* command = window_.hotkey_registry().find_command(command_id);
  if (command == nullptr || command->action.isNull()) {
    return false;
  }
  QAction* action = command->action.data();
  if (!action->isEnabled()) {
    return false;
  }
  // The command may open a modal dialog (and many do); the user's time in it
  // must not count against the script.
  const ModalWatchdogPause pause(*this);
  action->trigger();
  return true;
}

QStringList ScriptEngineHost::app_command_ids() const {
  QStringList ids;
  for (const auto& command : window_.hotkey_registry().commands()) {
    ids.append(command.id);
  }
  ids.sort();
  return ids;
}

// ---------------------------------------------------------------------------
// Script canvas windows

void ScriptEngineHost::adopt_canvas_window(ScriptCanvasWindow* window) {
  if (run_ == nullptr) {
    return;
  }
  window->setParent(this);
  QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
  run_->windows.push_back(window);
}

void ScriptEngineHost::canvas_window_closed(ScriptCanvasWindow* window) {
  Q_UNUSED(window);
  schedule_completion_check();
}

QImage ScriptEngineHost::active_canvas_window_image() const {
  if (run_ == nullptr) {
    return {};
  }
  for (auto it = run_->windows.rbegin(); it != run_->windows.rend(); ++it) {
    if (*it != nullptr && (*it)->is_open()) {
      return (*it)->surface();
    }
  }
  return {};
}

}  // namespace patchy::ui
