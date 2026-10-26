#include "ui/theme_script_host.hpp"

#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_engine.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_backdrop.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_package.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QEvent>
#include <QJSEngine>
#include <QJsonObject>
#include <QKeySequence>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <chrono>

namespace patchy::ui {

namespace {

// Persisted identifiers.
const QString& run_theme_scripts_key() {
  static const QString key = QStringLiteral("preferences/runThemeScripts");
  return key;
}
const QString& trust_key() {
  static const QString key = QStringLiteral("themeScripts/trust");
  return key;
}

constexpr int kMaxLogLines = 200;
constexpr int kMaxCursorSide = 128;
constexpr int kMaxSplashSide = 2048;
constexpr int kMaxPanelImageSide = 2048;

ThemeScriptHost*& host_slot() {
  static ThemeScriptHost* host = nullptr;
  return host;
}

// The panel ids a theme may name, and the selector each one styles.
const QHash<QString, QString>& panel_selectors() {
  static const QHash<QString, QString> selectors{
      {QStringLiteral("layers"), QStringLiteral("QWidget#layersPanel")},
      {QStringLiteral("channels"), QStringLiteral("QWidget#channelPanel")},
      {QStringLiteral("paths"), QStringLiteral("QWidget#pathsPanel")},
      {QStringLiteral("history"), QStringLiteral("QWidget#historyPanel")},
      {QStringLiteral("properties"), QStringLiteral("QWidget#propertiesPanel")},
      {QStringLiteral("info"), QStringLiteral("QWidget#infoPanel")},
      {QStringLiteral("palette"), QStringLiteral("QWidget#palettePanel")},
      {QStringLiteral("color"), QStringLiteral("QWidget#colorPanel")},
      {QStringLiteral("tools"), QStringLiteral("QToolBar#toolPalette")},
      {QStringLiteral("dialogs"), QStringLiteral("QDialog")},
  };
  return selectors;
}

// Appends the theme's "dialogs" background to a dialog's own sheet as it shows, and
// takes the previous addition out first, so a change or a removal reaches the next show.
class DialogBackgroundFilter final : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::Show) {
      return false;
    }
    auto* dialog = qobject_cast<QDialog*>(watched);
    const auto* host = ThemeScriptHost::existing();
    if (dialog == nullptr || host == nullptr) {
      return false;
    }
    static const char* const kAdded = "patchy.themeDialogBackground";
    const auto selector = dialog->objectName().isEmpty() ? QStringLiteral("QDialog")
                                                         : QStringLiteral("QDialog#") + dialog->objectName();
    const auto addition = host->dialog_background_stylesheet().arg(selector);
    auto sheet = dialog->styleSheet();
    if (const auto previous = dialog->property(kAdded).toString(); !previous.isEmpty() && sheet.endsWith(previous)) {
      sheet.chop(previous.size());
    }
    dialog->setProperty(kAdded, addition);
    if (sheet + addition != dialog->styleSheet()) {
      dialog->setStyleSheet(sheet + addition);
    }
    return false;
  }
};

// The prelude runs once per engine. It returns the globals to install (patchy, the
// timer functions, console) and the three entry points C++ calls; the bridge stays in
// its closure, so theme code can never reach it (or its QObject slots) directly.
const char* const kPrelude = R"JS(
(function (bridge) {
  'use strict';
  var handlers = {}, commands = {}, timers = {};
  function plain(value) {
    return value === undefined || value === null ? {} : JSON.parse(JSON.stringify(value));
  }
  function addTimer(fn, ms, repeat) {
    if (typeof fn !== 'function') throw new TypeError('setTimeout/setInterval: the first argument must be a function');
    var id = bridge.startTimer(Number(ms) || 0, repeat);
    if (id <= 0) throw new Error('Too many theme timers (16 at most)');
    timers[id] = fn;
    return id;
  }
  function clearTimer(id) {
    if (timers[id]) { delete timers[id]; bridge.stopTimer(Number(id)); }
  }
  var theme = Object.freeze({
    get id() { return bridge.themeId(); },
    get name() { return bridge.themeName(); },
    on: function (name, fn) {
      if (typeof fn !== 'function') throw new TypeError('patchy.theme.on(name, fn): fn must be a function');
      name = String(name);
      if (!bridge.isEvent(name)) throw new Error('Unknown theme event "' + name + '"');
      (handlers[name] = handlers[name] || []).push(fn);
      bridge.listen(name);
    },
    playSound: function (path) { return bridge.playSound(String(path)); },
    playEffect: function (effect, options) {
      var spec = (typeof effect === 'object' && effect !== null) ? plain(effect) : plain(options);
      if (typeof effect === 'string') spec.name = effect;
      return bridge.playEffect(spec);
    },
    showStatus: function (text, ms) { bridge.showStatus(String(text), ms === undefined ? 4000 : Number(ms)); },
    addCommand: function (spec, fn) {
      if (typeof fn !== 'function') throw new TypeError('patchy.theme.addCommand(spec, fn): fn must be a function');
      spec = plain(spec);
      var id = String(spec.id || '');
      if (!bridge.addCommand(id, String(spec.title || id), String(spec.hotkey || '')))
        throw new Error('patchy.theme.addCommand: refused "' + id + '" (bad id, a duplicate, or more than 32 commands)');
      commands[id] = fn;
    },
    setToolCursor: function (tool, image, hotX, hotY) {
      return bridge.setToolCursor(String(tool), image ? String(image) : '', Number(hotX) || 0, Number(hotY) || 0);
    },
    showSplash: function (image, ms) { return bridge.showSplash(String(image), ms === undefined ? 2500 : Number(ms)); },
    setPanelBackground: function (panel, image, mode, opacity) {
      return bridge.setPanelBackground(String(panel), image ? String(image) : '', String(mode || 'tile'),
                                       opacity === undefined ? 0.3 : Number(opacity));
    },
    log: function () { bridge.log(Array.prototype.map.call(arguments, String).join(' ')); }
  });
  function log() { bridge.log(Array.prototype.map.call(arguments, String).join(' ')); }
  return {
    globals: {
      patchy: Object.freeze({ theme: theme }),
      setTimeout: function (fn, ms) { return addTimer(fn, ms, false); },
      setInterval: function (fn, ms) { return addTimer(fn, ms, true); },
      clearTimeout: clearTimer,
      clearInterval: clearTimer,
      console: Object.freeze({ log: log, info: log, warn: log, error: log })
    },
    dispatch: function (name, payload) {
      var list = handlers[name];
      if (!list) return;
      for (var i = 0; i < list.length; ++i) {
        try { list[i](payload); } catch (e) { bridge.reportError(name + ': ' + e); }
      }
    },
    command: function (id) { var fn = commands[id]; if (fn) fn(); },
    timer: function (id, repeat) { var fn = timers[id]; if (!repeat) delete timers[id]; if (fn) fn(); }
  };
})
)JS";

[[nodiscard]] qint64 now_ms(const QElapsedTimer& clock) { return clock.elapsed(); }

}  // namespace

bool theme_scripts_enabled() { return app_settings().value(run_theme_scripts_key(), true).toBool(); }

void set_theme_scripts_enabled(bool enabled) {
  app_settings().setValue(run_theme_scripts_key(), enabled);
  if (auto* host = ThemeScriptHost::existing(); host != nullptr) {
    host->restart();
  }
}

QString theme_scripts_digest(const ThemeAssets& assets) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  auto paths = assets.files.keys();
  paths.sort();
  for (const auto& path : paths) {
    if (path.startsWith(QStringLiteral("scripts/"))) {
      hash.addData(path.toUtf8());
      hash.addData(QByteArrayView("\0", 1));
      hash.addData(assets.files.value(path));
    }
  }
  return QString::fromLatin1(hash.result().toHex());
}

ThemeScriptTrust stored_theme_script_trust(const QString& theme_id, const QString& digest) {
  const auto stored = app_settings().value(trust_key()).toMap().value(theme_id).toString();
  if (stored == digest + QStringLiteral(":allow")) {
    return ThemeScriptTrust::Allowed;
  }
  if (stored == digest + QStringLiteral(":deny")) {
    return ThemeScriptTrust::Denied;
  }
  return ThemeScriptTrust::Unknown;
}

void store_theme_script_trust(const QString& theme_id, const QString& digest, bool allowed) {
  auto settings = app_settings();
  auto map = settings.value(trust_key()).toMap();
  map.insert(theme_id, digest + (allowed ? QStringLiteral(":allow") : QStringLiteral(":deny")));
  settings.setValue(trust_key(), map);
}

void fire_theme_event(ThemeSoundEvent event, const QVariantMap& payload) {
  play_theme_sound(event);
  if (const auto* assets = active_theme_assets(); assets != nullptr) {
    const auto action = assets->events.constFind(QString(theme_sound_event_id(event)));
    if (action != assets->events.constEnd() && !action->effect.isEmpty()) {
      play_theme_effect(action->effect);
    }
  }
  if (auto* host = ThemeScriptHost::existing(); host != nullptr) {
    host->dispatch(event, payload);
  }
}

// ---------------------------------------------------------------------------
// ThemeScriptBridge: every entry validates, rate limits and clamps.

bool ThemeScriptBridge::isEvent(const QString& name) const { return theme_sound_event_from_id(name).has_value(); }

void ThemeScriptBridge::listen(const QString& name) {
  if (!host_.listened_.contains(name)) {
    host_.listened_.push_back(name);
  }
}

bool ThemeScriptBridge::playSound(const QString& path) {
  if (host_.assets_ == nullptr || !is_safe_theme_package_path(path) || !host_.allow(host_.sound_limit_)) {
    return false;
  }
  auto bytes = host_.assets_->files.value(path);
  if (bytes.size() < 12 || !bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE" || !theme_sounds_enabled()) {
    return false;
  }
  play_wav_bytes(std::move(bytes));
  return true;
}

bool ThemeScriptBridge::playEffect(const QVariantMap& spec) {
  if (!host_.allow(host_.effect_limit_)) {
    return false;
  }
  return play_theme_effect(spec);
}

void ThemeScriptBridge::showStatus(const QString& text, int ms) {
  if (host_.allow(host_.status_limit_)) {
    emit host_.status_requested(text.left(200), std::clamp(ms, 1000, 10000));
  }
}

bool ThemeScriptBridge::addCommand(const QString& id, const QString& title, const QString& hotkey) {
  static const QRegularExpression valid_id(QStringLiteral("^[A-Za-z0-9_-]{1,40}$"));
  if (!valid_id.match(id).hasMatch() || title.trimmed().isEmpty() ||
      static_cast<int>(host_.commands_.size()) >= ThemeScriptHost::kMaxCommands ||
      std::any_of(host_.commands_.begin(), host_.commands_.end(),
                  [&id](const ThemeCommand& command) { return command.id == id; })) {
    return false;
  }
  QString shortcut;
  if (!hotkey.isEmpty()) {
    const auto sequence = QKeySequence::fromString(hotkey, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() > 1) {
      host_.note(QStringLiteral("addCommand(%1): hotkey \"%2\" is not a key (ignored)").arg(id, hotkey));
    } else {
      shortcut = sequence.toString(QKeySequence::PortableText);
    }
  }
  host_.commands_.push_back({id, title.trimmed().left(60), shortcut});
  host_.schedule("commands");
  return true;
}

int ThemeScriptBridge::startTimer(int ms, bool repeat) {
  if (host_.timers_.size() >= ThemeScriptHost::kMaxTimers) {
    return 0;
  }
  const int id = host_.next_timer_id_++;
  auto* timer = new QTimer(&host_);
  timer->setSingleShot(!repeat);
  // A repeating timer faster than 30 ms is a busy loop by another name.
  timer->setInterval(std::clamp(ms, repeat ? 30 : 0, 24 * 60 * 60 * 1000));
  QObject::connect(timer, &QTimer::timeout, &host_, [this, id, repeat] {
    if (!host_.running()) {
      return;
    }
    if (!repeat) {
      if (auto* done = host_.timers_.take(id); done != nullptr) {
        done->deleteLater();
      }
    }
    host_.guarded_call(host_.timer_fn_, {QJSValue(id), QJSValue(repeat)});
  });
  host_.timers_.insert(id, timer);
  timer->start();
  return id;
}

void ThemeScriptBridge::stopTimer(int id) {
  if (auto* timer = host_.timers_.take(id); timer != nullptr) {
    timer->stop();
    timer->deleteLater();
  }
}

bool ThemeScriptBridge::setToolCursor(const QString& tool, const QString& image, int hot_x, int hot_y) {
  return host_.allow(host_.cursor_limit_) && host_.set_cursor(tool, image, hot_x, hot_y);
}

bool ThemeScriptBridge::showSplash(const QString& image, int ms) {
  if (host_.assets_ == nullptr) {
    return false;
  }
  const auto now = now_ms(host_.clock_);
  if (now - host_.last_splash_ms_ < 5000) {
    return false;
  }
  const auto picture = theme_package_image(*host_.assets_, image, kMaxSplashSide);
  if (picture.isNull()) {
    return false;
  }
  host_.last_splash_ms_ = now;
  emit host_.splash_requested(picture, std::clamp(ms, 500, 6000));
  return true;
}

bool ThemeScriptBridge::setPanelBackground(const QString& panel, const QString& image, const QString& mode,
                                           double opacity) {
  return host_.allow(host_.panel_limit_) && host_.set_panel_background(panel, image, mode, opacity);
}

QString ThemeScriptBridge::themeId() const { return host_.theme_id_; }

QString ThemeScriptBridge::themeName() const { return host_.theme_name(); }

void ThemeScriptBridge::log(const QString& text) { host_.note(text.left(500)); }

void ThemeScriptBridge::reportError(const QString& text) { host_.note(QStringLiteral("Error: ") + text.left(500)); }

// ---------------------------------------------------------------------------
// ThemeScriptHost

ThemeScriptHost& ThemeScriptHost::instance() {
  auto*& host = host_slot();
  if (host == nullptr) {
    host = new ThemeScriptHost(QCoreApplication::instance());
  }
  return *host;
}

ThemeScriptHost* ThemeScriptHost::existing() { return host_slot(); }

ThemeScriptHost::ThemeScriptHost(QObject* parent) : QObject(parent) { clock_.start(); }

ThemeScriptHost::~ThemeScriptHost() {
  stop();
  watchdog_.reset();
  host_slot() = nullptr;
}

QString ThemeScriptHost::theme_name() const {
  if (assets_ == nullptr) {
    return {};
  }
  const auto name = assets_->manifest.value(QStringLiteral("name")).toString();
  return name.isEmpty() ? theme_id_ : name;
}

QString ThemeScriptHost::menu_title() const {
  if (assets_ == nullptr) {
    return {};
  }
  const auto title = assets_->manifest.value(QStringLiteral("menuTitle")).toString().trimmed();
  return title.isEmpty() ? theme_name() : title.left(40);
}

bool ThemeScriptHost::allow(RateLimit& limit) {
  const auto now = now_ms(clock_);
  while (!limit.stamps.empty() && now - limit.stamps.front() >= limit.window_ms) {
    limit.stamps.pop_front();
  }
  if (static_cast<int>(limit.stamps.size()) >= limit.limit) {
    return false;
  }
  limit.stamps.push_back(now);
  return true;
}

void ThemeScriptHost::note(const QString& text) {
  log_.push_back(text);
  while (log_.size() > kMaxLogLines) {
    log_.pop_front();
  }
  if (text.startsWith(QStringLiteral("Error: "))) {
    qWarning("Theme script: %s", qPrintable(text));
    const auto now = now_ms(clock_);
    if (now - last_status_error_ms_ >= 3000) {
      last_status_error_ms_ = now;
      emit status_requested(QCoreApplication::translate("ThemeScripts", "Theme script error: %1").arg(text.mid(7, 160)),
                            6000);
    }
  }
}

void ThemeScriptHost::schedule(const char* what) {
  const QString kind = QString::fromLatin1(what);
  bool* pending = kind == QStringLiteral("commands")  ? &commands_signal_pending_
                  : kind == QStringLiteral("cursors") ? &cursors_signal_pending_
                                                      : &panels_signal_pending_;
  if (*pending) {
    return;
  }
  *pending = true;
  // Coalesced into one signal per event-loop turn, outside the script call.
  QTimer::singleShot(0, this, [this, pending, kind] {
    *pending = false;
    if (kind == QStringLiteral("commands")) {
      emit commands_changed();
    } else if (kind == QStringLiteral("cursors")) {
      emit cursors_changed();
    } else {
      rebuild_panel_stylesheet();
      emit panel_backgrounds_changed();
    }
  });
}

bool ThemeScriptHost::set_cursor(const QString& tool, const QString& image, int hot_x, int hot_y) {
  static const QRegularExpression valid_tool(QStringLiteral("^[a-z_]{1,40}$"));
  if (!valid_tool.match(tool).hasMatch()) {
    return false;
  }
  if (image.isEmpty()) {
    cursors_.remove(tool);
    schedule("cursors");
    return true;
  }
  if (assets_ == nullptr) {
    return false;
  }
  const auto picture = theme_package_image(*assets_, image, kMaxCursorSide);
  if (picture.isNull()) {
    return false;
  }
  cursors_.insert(tool, QCursor(QPixmap::fromImage(picture), std::clamp(hot_x, 0, picture.width() - 1),
                                std::clamp(hot_y, 0, picture.height() - 1)));
  schedule("cursors");
  return true;
}

bool ThemeScriptHost::set_panel_background(const QString& panel, const QString& image, const QString& mode,
                                           double opacity) {
  if (!panel_selectors().contains(panel) ||
      (mode != QStringLiteral("tile") && mode != QStringLiteral("stretch") && mode != QStringLiteral("center"))) {
    return false;
  }
  if (image.isEmpty()) {
    panels_.remove(panel);
    schedule("panels");
    return true;
  }
  if (assets_ == nullptr) {
    return false;
  }
  const auto picture = theme_package_image(*assets_, image, kMaxPanelImageSide);
  if (picture.isNull()) {
    return false;
  }
  // Drawn faint over the panel's own background color, so text on top stays readable
  // whatever the image holds.
  QImage faint(picture.size(), QImage::Format_ARGB32_Premultiplied);
  faint.fill(Qt::transparent);
  {
    QPainter painter(&faint);
    painter.setOpacity(std::clamp(std::isfinite(opacity) ? opacity : 0.3, 0.05, 0.6));
    painter.drawImage(0, 0, picture);
  }
  const auto url = theme_cache_png_url(faint, QStringLiteral("panel"));
  if (url.isEmpty()) {
    return false;
  }
  panels_.insert(panel, {url, mode});
  schedule("panels");
  return true;
}

void ThemeScriptHost::rebuild_panel_stylesheet() {
  const auto rule = [](const QString& selector, const PanelBackground& background) {
    if (background.mode == QStringLiteral("stretch")) {
      return QStringLiteral("%1 { border-image: url(%2) 0 0 0 0 stretch stretch; }\n").arg(selector, background.url);
    }
    if (background.mode == QStringLiteral("center")) {
      return QStringLiteral("%1 { background-image: url(%2); background-repeat: no-repeat; "
                            "background-position: center; }\n")
          .arg(selector, background.url);
    }
    return QStringLiteral("%1 { background-image: url(%2); background-repeat: repeat-xy; }\n")
        .arg(selector, background.url);
  };
  QString sheet;
  auto names = panels_.keys();
  names.sort();
  for (const auto& panel : names) {
    sheet += rule(panel_selectors().value(panel), panels_.value(panel));
  }
  // Every dialog styles itself (QDialog#name { background: ... } in its own sheet, which
  // a window-level rule cannot beat), so "dialogs" also goes into each dialog's own
  // sheet as it shows (DialogBackgroundFilter); %1 is the dialog's selector.
  const auto dialogs = panels_.constFind(QStringLiteral("dialogs"));
  dialog_stylesheet_ = dialogs == panels_.constEnd()
                           ? QString()
                           : QStringLiteral("\n") + rule(QStringLiteral("%1"), dialogs.value()) +
                                 rule(QStringLiteral("QWidget#dialogChromeContent"), dialogs.value());
  if (!dialog_stylesheet_.isEmpty() && dialog_filter_ == nullptr) {
    // Installed the first time a theme sets one, kept for the session; it only reads
    // Show events of dialogs.
    dialog_filter_ = new DialogBackgroundFilter(this);
    QCoreApplication::instance()->installEventFilter(dialog_filter_);
  }
  if (sheet != panel_stylesheet_) {
    panel_stylesheet_ = sheet;
    ++panel_revision_;
  }
}

void ThemeScriptHost::apply_declarative_hooks() {
  if (assets_ == nullptr) {
    return;
  }
  const auto cursors = assets_->manifest.value(QStringLiteral("cursors")).toObject();
  for (auto it = cursors.begin(); it != cursors.end(); ++it) {
    const auto entry = it.value().toObject();
    set_cursor(it.key(), entry.value(QStringLiteral("image")).toString(), entry.value(QStringLiteral("hotX")).toInt(),
               entry.value(QStringLiteral("hotY")).toInt());
  }
  const auto panels = assets_->manifest.value(QStringLiteral("panelBackgrounds")).toObject();
  for (auto it = panels.begin(); it != panels.end(); ++it) {
    const auto entry = it.value().toObject();
    set_panel_background(it.key(), entry.value(QStringLiteral("image")).toString(),
                         entry.value(QStringLiteral("mode")).toString(QStringLiteral("tile")),
                         entry.value(QStringLiteral("opacity")).toDouble(0.3));
  }
}

void ThemeScriptHost::theme_changed(const QString& theme_id, std::shared_ptr<const ThemeAssets> assets) {
  stop();
  const bool had_commands = !commands_.empty();
  const bool had_cursors = !cursors_.isEmpty();
  const bool had_panels = !panels_.isEmpty();
  commands_.clear();
  cursors_.clear();
  panels_.clear();
  trust_pending_ = false;
  theme_id_ = assets != nullptr ? theme_id : QString();
  assets_ = std::move(assets);
  apply_declarative_hooks();
  if (had_commands) {
    schedule("commands");
  }
  if (had_cursors || !cursors_.isEmpty()) {
    schedule("cursors");
  }
  if (had_panels || !panels_.isEmpty()) {
    schedule("panels");
  }
  restart();
}

void ThemeScriptHost::restart() {
  stop();
  if (assets_ == nullptr || !assets_->has_scripts()) {
    return;
  }
  if (!commands_.empty()) {
    commands_.clear();
    schedule("commands");
  }
  if (!theme_scripts_enabled()) {
    return;
  }
  switch (stored_theme_script_trust(theme_id_, theme_scripts_digest(*assets_))) {
    case ThemeScriptTrust::Allowed:
      start();
      return;
    case ThemeScriptTrust::Denied:
      return;
    case ThemeScriptTrust::Unknown:
      trust_pending_ = true;
      emit trust_needed(theme_id_, theme_name());
      return;
  }
}

void ThemeScriptHost::set_trust(const QString& theme_id, bool allowed) {
  if (assets_ == nullptr || theme_id != theme_id_) {
    return;
  }
  store_theme_script_trust(theme_id, theme_scripts_digest(*assets_), allowed);
  trust_pending_ = false;
  if (allowed && !running()) {
    start();
  }
}

void ThemeScriptHost::start() {
  if (assets_ == nullptr || !assets_->has_scripts()) {
    return;
  }
  timeouts_ = 0;
  listened_.clear();
  if (watchdog_ == nullptr) {
    watchdog_ = std::make_unique<ScriptWatchdog>([this] {
      const std::lock_guard lock(interrupt_mutex_);
      if (engine_ != nullptr) {
        timed_out_ = true;
        engine_->setInterrupted(true);
      }
    });
  }
  {
    const std::lock_guard lock(interrupt_mutex_);
    engine_ = std::make_unique<QJSEngine>();  // no installExtensions: built-ins only
  }
  bridge_ = new ThemeScriptBridge(*this);
  QJSEngine::setObjectOwnership(bridge_, QJSEngine::CppOwnership);
  const auto factory = engine_->evaluate(QString::fromLatin1(kPrelude), QStringLiteral("theme-prelude.js"));
  const auto parts = factory.call({engine_->newQObject(bridge_)});
  if (parts.isError()) {
    note(QStringLiteral("Error: theme prelude: ") + parts.toString());
    stop();
    return;
  }
  auto globals = parts.property(QStringLiteral("globals"));
  for (const auto* name : {"patchy", "setTimeout", "setInterval", "clearTimeout", "clearInterval", "console"}) {
    engine_->globalObject().setProperty(QLatin1String(name), globals.property(QLatin1String(name)));
  }
  dispatch_fn_ = parts.property(QStringLiteral("dispatch"));
  command_fn_ = parts.property(QStringLiteral("command"));
  timer_fn_ = parts.property(QStringLiteral("timer"));
  const auto source = QString::fromUtf8(assets_->files.value(QStringLiteral("scripts/main.js")));
  guarded_evaluate(source, QStringLiteral("scripts/main.js"));
}

void ThemeScriptHost::stop(const QString& reason) {
  for (auto* timer : std::as_const(timers_)) {
    timer->stop();
    timer->deleteLater();
  }
  timers_.clear();
  if (engine_ != nullptr) {
    if (watchdog_ != nullptr) {
      watchdog_->disarm();
    }
    dispatch_fn_ = QJSValue();
    command_fn_ = QJSValue();
    timer_fn_ = QJSValue();
    {
      const std::lock_guard lock(interrupt_mutex_);
      engine_.reset();
    }
    delete bridge_;
    bridge_ = nullptr;
    if (!commands_.empty()) {
      commands_.clear();
      schedule("commands");
    }
  }
  listened_.clear();
  if (!reason.isEmpty()) {
    note(QStringLiteral("Error: ") + reason);
  }
}

void ThemeScriptHost::after_call(const QJSValue& result) {
  if (watchdog_ != nullptr) {
    watchdog_->disarm();
  }
  bool timed_out = false;
  {
    const std::lock_guard lock(interrupt_mutex_);
    timed_out = timed_out_.exchange(false);
    if (engine_ != nullptr) {
      engine_->setInterrupted(false);
    }
  }
  in_call_ = false;
  if (timed_out) {
    ++timeouts_;
    note(QStringLiteral("Error: a theme script ran longer than %1 ms and was stopped").arg(budget_ms_));
    if (timeouts_ >= kMaxTimeouts) {
      // Deferred: the call that overran may still be unwinding through the engine.
      QTimer::singleShot(0, this, [this] {
        if (running()) {
          stop(QCoreApplication::translate("ThemeScripts", "the theme's scripts kept running too long and were turned off"));
        }
      });
    }
  } else if (result.isError()) {
    note(QStringLiteral("Error: ") + result.toString());
  }
}

QJSValue ThemeScriptHost::guarded_call(QJSValue function, const QJSValueList& arguments) {
  if (engine_ == nullptr || in_call_ || !function.isCallable()) {
    return {};
  }
  in_call_ = true;
  timed_out_ = false;
  watchdog_->arm(std::chrono::milliseconds(budget_ms_));
  auto result = function.call(arguments);
  after_call(result);
  return result;
}

QJSValue ThemeScriptHost::guarded_evaluate(const QString& source, const QString& file_name) {
  if (engine_ == nullptr || in_call_) {
    return {};
  }
  in_call_ = true;
  timed_out_ = false;
  // main.js sets everything up at once: twice the per-call budget.
  watchdog_->arm(std::chrono::milliseconds(budget_ms_ * 2));
  auto result = engine_->evaluate(source, file_name);
  after_call(result);
  return result;
}

void ThemeScriptHost::run_command(const QString& id) {
  if (running()) {
    guarded_call(command_fn_, {QJSValue(id)});
  }
}

bool ThemeScriptHost::listens(ThemeSoundEvent event) const {
  return running() && listened_.contains(QString(theme_sound_event_id(event)));
}

void ThemeScriptHost::dispatch(ThemeSoundEvent event, const QVariantMap& payload) {
  if (event == ThemeSoundEvent::Startup && assets_ != nullptr) {
    // theme.json "splash": shown at startup with or without scripts.
    const auto splash = assets_->manifest.value(QStringLiteral("splash")).toObject();
    if (const auto image = splash.value(QStringLiteral("image")).toString(); !image.isEmpty()) {
      const auto picture = theme_package_image(*assets_, image, kMaxSplashSide);
      if (!picture.isNull()) {
        last_splash_ms_ = now_ms(clock_);
        emit splash_requested(picture, std::clamp(splash.value(QStringLiteral("ms")).toInt(2500), 500, 6000));
      }
    }
  }
  if (!listens(event)) {
    return;
  }
  const QString name(theme_sound_event_id(event));
  if (in_call_) {
    // An event raised while theme code runs (never re-entered): delivered next turn.
    QTimer::singleShot(0, this, [this, event, payload] { dispatch(event, payload); });
    return;
  }
  guarded_call(dispatch_fn_, {QJSValue(name), engine_->toScriptValue(payload)});
}

QString ThemeScriptHost::evaluate_for_testing(const QString& source) {
  if (engine_ == nullptr) {
    return QStringLiteral("Error: not running");
  }
  const auto result = guarded_evaluate(source, QStringLiteral("test.js"));
  return result.isError() ? QStringLiteral("Error: ") + result.toString() : result.toString();
}

std::optional<QCursor> ThemeScriptHost::tool_cursor(const QString& tool_id) const {
  const auto found = cursors_.constFind(tool_id);
  if (found == cursors_.constEnd()) {
    return std::nullopt;
  }
  return found.value();
}

QString theme_tool_id(CanvasTool tool) {
  auto id = tool_hotkey_id(tool);
  return id.startsWith(QStringLiteral("tools.")) ? id.mid(6) : id;
}

std::optional<QCursor> theme_tool_cursor(CanvasTool tool) {
  const auto* host = ThemeScriptHost::existing();
  return host == nullptr ? std::nullopt : host->tool_cursor(theme_tool_id(tool));
}

QString theme_panel_background_stylesheet() {
  const auto* host = ThemeScriptHost::existing();
  return host == nullptr ? QString() : host->panel_background_stylesheet();
}

QString theme_dialog_background_stylesheet() {
  const auto* host = ThemeScriptHost::existing();
  return host == nullptr ? QString() : host->dialog_background_stylesheet();
}

int theme_panel_background_revision() {
  const auto* host = ThemeScriptHost::existing();
  return host == nullptr ? 0 : host->panel_background_revision();
}

}  // namespace patchy::ui
