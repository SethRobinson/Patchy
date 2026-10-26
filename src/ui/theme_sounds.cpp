#include "ui/theme_sounds.hpp"

#include "ui/app_settings.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_package.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QPointer>
#include <QProgressDialog>
#include <QSettings>

#include <array>

namespace patchy::ui {

namespace {

// Persisted identifier.
const QString& play_theme_sounds_key() {
  static const QString key = QStringLiteral("preferences/playThemeSounds");
  return key;
}

constexpr std::array<ThemeSoundEvent, 6> kEvents{
    ThemeSoundEvent::SaveDone, ThemeSoundEvent::ExportDone, ThemeSoundEvent::LongOperationDone,
    ThemeSoundEvent::Error,    ThemeSoundEvent::Warning,    ThemeSoundEvent::ScriptAlert,
};

// Times every QProgressDialog from Show to Hide; one that stayed up past the
// threshold plays the event as it closes.
class LongOperationSoundWatcher final : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    const auto type = event->type();
    if (type != QEvent::Show && type != QEvent::Hide) {
      return false;
    }
    auto* dialog = qobject_cast<QProgressDialog*>(watched);
    if (dialog == nullptr) {
      return false;
    }
    if (type == QEvent::Show) {
      QElapsedTimer timer;
      timer.start();
      shown_.insert(dialog, timer);
      return false;
    }
    const auto found = shown_.find(dialog);
    if (found != shown_.end()) {
      const auto elapsed = found.value().elapsed();
      shown_.erase(found);
      if (elapsed >= kLongOperationSoundMs) {
        play_theme_sound(ThemeSoundEvent::LongOperationDone);
      }
    }
    return false;
  }

 private:
  QHash<QObject*, QElapsedTimer> shown_;
};

QPointer<LongOperationSoundWatcher>& watcher_slot() {
  static QPointer<LongOperationSoundWatcher> watcher;
  return watcher;
}

}  // namespace

QLatin1StringView theme_sound_event_id(ThemeSoundEvent event) {
  switch (event) {
    case ThemeSoundEvent::SaveDone:
      return QLatin1StringView("save-done");
    case ThemeSoundEvent::ExportDone:
      return QLatin1StringView("export-done");
    case ThemeSoundEvent::LongOperationDone:
      return QLatin1StringView("long-operation-done");
    case ThemeSoundEvent::Error:
      return QLatin1StringView("error");
    case ThemeSoundEvent::Warning:
      return QLatin1StringView("warning");
    case ThemeSoundEvent::ScriptAlert:
      return QLatin1StringView("script-alert");
  }
  return QLatin1StringView("error");
}

std::optional<ThemeSoundEvent> theme_sound_event_from_id(const QString& id) {
  for (const auto event : kEvents) {
    if (id == theme_sound_event_id(event)) {
      return event;
    }
  }
  return std::nullopt;
}

std::span<const ThemeSoundEvent> theme_sound_events() { return kEvents; }

QByteArray theme_sound_for_event(const ThemeAssets* assets, ThemeSoundEvent event) {
  if (assets == nullptr) {
    return {};
  }
  return assets->sounds.value(QString(theme_sound_event_id(event)));
}

bool theme_sounds_enabled() { return app_settings().value(play_theme_sounds_key(), true).toBool(); }

void set_theme_sounds_enabled(bool enabled) { app_settings().setValue(play_theme_sounds_key(), enabled); }

bool play_theme_sound(ThemeSoundEvent event) {
  auto wav = theme_sound_for_event(active_theme_assets(), event);
  if (wav.isEmpty() || !theme_sounds_enabled()) {
    return false;
  }
  play_wav_bytes(std::move(wav));
  return true;
}

void refresh_long_operation_sound_watcher() {
  auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
  if (app == nullptr) {
    return;
  }
  const bool wanted = !theme_sound_for_event(active_theme_assets(), ThemeSoundEvent::LongOperationDone).isEmpty();
  auto& watcher = watcher_slot();
  if (wanted && watcher == nullptr) {
    watcher = new LongOperationSoundWatcher(app);
    app->installEventFilter(watcher);
  } else if (!wanted && watcher != nullptr) {
    app->removeEventFilter(watcher);
    delete watcher.data();
  }
}

}  // namespace patchy::ui
