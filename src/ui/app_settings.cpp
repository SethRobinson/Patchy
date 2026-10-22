#include "ui/app_settings.hpp"

#include "ui/qt_paths.hpp"

#include <QFile>

#include <mutex>

#include <QString>

#include <algorithm>

// Q_INIT_RESOURCE must run from outside any namespace; naming the initializer here also
// keeps the resource object in executables that link the static patchy_ui library.
static void ensure_color_profile_resources_registered() {
  static const bool registered = [] {
    Q_INIT_RESOURCE(color_profiles);
    return true;
  }();
  (void)registered;
}

namespace patchy::ui {
namespace {

// Persisted identifiers: never rename them (see AGENTS.md).
QString gui_scale_key() { return QStringLiteral("preferences/guiScalePercent"); }
#ifndef Q_OS_WASM
QString recovery_enabled_key() { return QStringLiteral("recovery/enabled"); }
#endif
QString recovery_interval_key() { return QStringLiteral("recovery/intervalMinutes"); }
QString legacy_plugin_folders_key() { return QStringLiteral("plugins/userFolders"); }
QString legacy_plugin_screen_size_key() { return QStringLiteral("plugins/screenSize"); }
QString update_check_interval_key() { return QStringLiteral("updates/checkIntervalDays"); }
QString update_last_check_key() { return QStringLiteral("updates/lastCheckSecsSinceEpoch"); }
QString update_skipped_version_key() { return QStringLiteral("updates/skippedVersion"); }
QString appimage_menu_entry_prompt_declined_key() { return QStringLiteral("appimage/menuEntryPromptDeclined"); }
QString preferences_dialog_size_key() { return QStringLiteral("preferences/dialogSize"); }
QString system_title_bar_key() { return QStringLiteral("window/systemTitleBar"); }
QString cmyk_profile_key() { return QStringLiteral("color/cmykProfile"); }
QString cmyk_intent_key() { return QStringLiteral("color/cmykIntent"); }
QString cmyk_black_point_key() { return QStringLiteral("color/cmykBlackPointCompensation"); }

}  // namespace

bool stored_system_title_bar() { return app_settings().value(system_title_bar_key(), false).toBool(); }

std::optional<bool> stored_system_title_bar_choice() {
  const auto settings = app_settings();
  if (!settings.contains(system_title_bar_key())) {
    return std::nullopt;
  }
  return settings.value(system_title_bar_key(), false).toBool();
}

void set_stored_system_title_bar(bool enabled) {
  auto settings = app_settings();
  settings.setValue(system_title_bar_key(), enabled);
}

int normalize_update_check_interval_days(int stored) {
  const auto match = std::find(kUpdateCheckIntervalDays.begin(), kUpdateCheckIntervalDays.end(), stored);
  return match != kUpdateCheckIntervalDays.end() ? *match : kDefaultUpdateCheckIntervalDays;
}

int stored_update_check_interval_days() {
  return normalize_update_check_interval_days(
      app_settings().value(update_check_interval_key(), kDefaultUpdateCheckIntervalDays).toInt());
}

void set_stored_update_check_interval_days(int days) {
  auto settings = app_settings();
  settings.setValue(update_check_interval_key(), normalize_update_check_interval_days(days));
}

qint64 stored_update_last_check_secs() {
  return app_settings().value(update_last_check_key(), 0).toLongLong();
}

void set_stored_update_last_check_secs(qint64 secs_since_epoch) {
  auto settings = app_settings();
  settings.setValue(update_last_check_key(), secs_since_epoch);
}

QString stored_update_skipped_version() {
  return app_settings().value(update_skipped_version_key()).toString().trimmed();
}

void set_stored_update_skipped_version(const QString& version) {
  auto settings = app_settings();
  settings.setValue(update_skipped_version_key(), version.trimmed());
}

bool stored_appimage_menu_entry_prompt_declined() {
  return app_settings().value(appimage_menu_entry_prompt_declined_key(), false).toBool();
}

void set_stored_appimage_menu_entry_prompt_declined(bool declined) {
  auto settings = app_settings();
  settings.setValue(appimage_menu_entry_prompt_declined_key(), declined);
}

QSize stored_preferences_dialog_size() {
  return app_settings().value(preferences_dialog_size_key()).toSize();
}

void set_stored_preferences_dialog_size(QSize size) {
  auto settings = app_settings();
  settings.setValue(preferences_dialog_size_key(), size);
}

QSettings app_settings() {
#ifdef Q_OS_WASM
  // IniFormat would resolve to a real .ini in MEMFS, which is recreated empty on
  // every page load. The localStorage backend is synchronous (empty flush()) and
  // survives reloads, which is what makes preferences persist in the browser.
  return QSettings(QSettings::WebLocalStorageFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
#else
  return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
#endif
}

int normalize_gui_scale_percent(int stored) {
  if (stored <= 0) {
    return kDefaultGuiScalePercent;
  }
  return std::clamp(stored, kMinGuiScalePercent, kMaxGuiScalePercent);
}

QSettings brush_library_settings() {
  const auto file = qEnvironmentVariable("PATCHY_BRUSH_SETTINGS_FILE");
  if (!file.isEmpty()) return QSettings(file, QSettings::IniFormat);
  return app_settings();
}

QSettings recent_history_settings() {
  const auto file = qEnvironmentVariable("PATCHY_RECENT_SETTINGS_FILE");
  if (!file.isEmpty()) return QSettings(file, QSettings::IniFormat);
  return app_settings();
}

int stored_gui_scale_percent() {
  return normalize_gui_scale_percent(
      app_settings().value(gui_scale_key(), kDefaultGuiScalePercent).toInt());
}

void set_stored_gui_scale_percent(int percent) {
  auto settings = app_settings();
  settings.setValue(gui_scale_key(), normalize_gui_scale_percent(percent));
}

int normalize_recovery_interval_minutes(int stored) {
  const auto match = std::find(kRecoveryIntervalMinutes.begin(), kRecoveryIntervalMinutes.end(), stored);
  return match != kRecoveryIntervalMinutes.end() ? *match : kDefaultRecoveryIntervalMinutes;
}

bool stored_recovery_enabled() {
#ifdef Q_OS_WASM
  return false;
#else
  return app_settings().value(recovery_enabled_key(), true).toBool();
#endif
}

void set_stored_recovery_enabled(bool enabled) {
#ifdef Q_OS_WASM
  Q_UNUSED(enabled);
#else
  auto settings = app_settings();
  settings.setValue(recovery_enabled_key(), enabled);
#endif
}

int stored_recovery_interval_minutes() {
  return normalize_recovery_interval_minutes(
      app_settings().value(recovery_interval_key(), kDefaultRecoveryIntervalMinutes).toInt());
}

void set_stored_recovery_interval_minutes(int minutes) {
#ifdef Q_OS_WASM
  Q_UNUSED(minutes);
#else
  auto settings = app_settings();
  settings.setValue(recovery_interval_key(), normalize_recovery_interval_minutes(minutes));
#endif
}

QStringList stored_legacy_plugin_folders() {
  QStringList folders = app_settings().value(legacy_plugin_folders_key()).toStringList();
  folders.removeAll(QString());
  folders.removeDuplicates();
  return folders;
}

void set_stored_legacy_plugin_folders(const QStringList& folders) {
  auto settings = app_settings();
  QStringList cleaned = folders;
  cleaned.removeAll(QString());
  cleaned.removeDuplicates();
  settings.setValue(legacy_plugin_folders_key(), cleaned);
}

std::pair<int, int> stored_legacy_plugin_screen_size() {
  const auto stored = app_settings().value(legacy_plugin_screen_size_key()).toString().trimmed().toLower();
  if (stored == QLatin1String("monitor")) {
    return {0, 0};
  }
  const auto parts = stored.split(QLatin1Char('x'));
  if (parts.size() == 2) {
    const std::pair<int, int> size{parts[0].toInt(), parts[1].toInt()};
    if (std::find(kLegacyPluginScreenSizes.begin(), kLegacyPluginScreenSizes.end(), size) !=
        kLegacyPluginScreenSizes.end()) {
      return size;
    }
  }
  return kDefaultLegacyPluginScreenSize;
}

void set_stored_legacy_plugin_screen_size(std::pair<int, int> size) {
  if (std::find(kLegacyPluginScreenSizes.begin(), kLegacyPluginScreenSizes.end(), size) ==
      kLegacyPluginScreenSizes.end()) {
    size = kDefaultLegacyPluginScreenSize;
  }
  auto settings = app_settings();
  settings.setValue(legacy_plugin_screen_size_key(),
                    size.first == 0 ? QStringLiteral("monitor")
                                    : QStringLiteral("%1x%2").arg(size.first).arg(size.second));
}

CmykWorkingSettings stored_cmyk_working_settings() {
  const auto settings = app_settings();
  CmykWorkingSettings stored;
  stored.profile = settings.value(cmyk_profile_key()).toString();
  stored.intent = rendering_intent_from_token(settings.value(cmyk_intent_key()).toString().toStdString())
                      .value_or(RenderingIntent::RelativeColorimetric);
  stored.black_point_compensation = settings.value(cmyk_black_point_key(), true).toBool();
  return stored;
}

void set_stored_cmyk_working_settings(const CmykWorkingSettings& stored) {
  auto settings = app_settings();
  settings.setValue(cmyk_profile_key(), stored.profile);
  const auto token = rendering_intent_token(stored.intent);
  settings.setValue(cmyk_intent_key(), QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size())));
  settings.setValue(cmyk_black_point_key(), stored.black_point_compensation);
}

const std::vector<std::uint8_t>& bundled_cmyk_profile() {
  static const auto profile = [] {
    ensure_color_profile_resources_registered();
    QFile file(QStringLiteral(":/icc/ISOcoated_v2_bas.ICC"));
    std::vector<std::uint8_t> bytes;
    if (file.open(QIODevice::ReadOnly)) {
      const auto data = file.readAll();
      const auto* first = reinterpret_cast<const std::uint8_t*>(data.constData());
      bytes.assign(first, first + data.size());
    }
    return bytes;
  }();
  return profile;
}

std::shared_ptr<const CmykProofTransform> cmyk_gamut_check() {
  static std::mutex mutex;
  static std::shared_ptr<const CmykWorkingSpace> built_for;
  static std::shared_ptr<const CmykProofTransform> check;
  const auto space = working_cmyk_space();
  const std::scoped_lock lock(mutex);
  if (space != built_for) {
    built_for = space;
    check = CmykProofTransform::create(*space, true, true);
  }
  return check;
}

std::shared_ptr<const CmykWorkingSpace> cmyk_working_space_for(const CmykWorkingSettings& settings) {
  if (settings.profile == QLatin1String(kCmykProfileFormula)) {
    return CmykWorkingSpace::formula();
  }
  const auto load = [&settings](const std::filesystem::path& path) {
    return path.empty() ? nullptr
                        : CmykWorkingSpace::from_profile_file(path, settings.intent,
                                                              settings.black_point_compensation);
  };
  auto space = load(to_filesystem_path(settings.profile));
  if (space == nullptr) {
    space = load(automatic_cmyk_profile_path());
  }
  if (space == nullptr) {
    space = CmykWorkingSpace::from_icc_profile(bundled_cmyk_profile(), settings.intent,
                                               settings.black_point_compensation);
  }
  return space != nullptr ? space : CmykWorkingSpace::formula();
}

}  // namespace patchy::ui
