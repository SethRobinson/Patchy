#include "ui/app_settings.hpp"

#include "ui/qt_paths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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
QString ui_text_size_key() { return QStringLiteral("preferences/uiTextSizePercent"); }
QString toolbar_icon_size_key() { return QStringLiteral("preferences/toolbarIconSize"); }
QString tool_flyout_open_mode_key() { return QStringLiteral("preferences/toolFlyoutOpenMode"); }
QString layer_footer_icon_size_key() { return QStringLiteral("layersPanel/footerIconSize"); }
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
QString layer_style_dialog_size_key() { return QStringLiteral("layerStyle/dialogSize"); }
QString preferences_last_page_key() { return QStringLiteral("preferences/lastPage"); }
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

QSize stored_layer_style_dialog_size() {
  return app_settings().value(layer_style_dialog_size_key()).toSize();
}

void set_stored_layer_style_dialog_size(QSize size) {
  auto settings = app_settings();
  if (size.isValid()) {
    settings.setValue(layer_style_dialog_size_key(), size);
  } else {
    settings.remove(layer_style_dialog_size_key());
  }
}

QString stored_preferences_last_page() { return app_settings().value(preferences_last_page_key()).toString(); }

void set_stored_preferences_last_page(const QString& page_id) {
  auto settings = app_settings();
  settings.setValue(preferences_last_page_key(), page_id);
}

QSettings app_settings() {
#ifdef Q_OS_WASM
  // IniFormat would resolve to a real .ini in MEMFS, which is recreated empty on
  // every page load. The localStorage backend is synchronous (empty flush()) and
  // survives reloads, which is what makes preferences persist in the browser.
  return QSettings(QSettings::WebLocalStorageFormat, QSettings::UserScope, QStringLiteral("Patchy"),
                   QStringLiteral("Patchy"));
#else
  return QSettings(app_settings_file_path(), QSettings::IniFormat);
#endif
}

#ifndef Q_OS_WASM
namespace {

std::mutex& settings_root_mutex() {
  static std::mutex mutex;
  return mutex;
}

QString& settings_root_override() {
  static QString root;
  return root;
}

}  // namespace

QString app_settings_file_path() {
  QString root;
  {
    std::lock_guard lock(settings_root_mutex());
    root = settings_root_override();
  }
  if (root.isEmpty()) {
#ifdef Q_OS_MACOS
    // Qt's ini root on macOS is ~/.config; a Mac app keeps its files in Application Support.
    root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    // Qt's user-scope ini root (%APPDATA% on Windows, ~/.config on Linux).
    const QSettings probe(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("RTsoft"),
                          QStringLiteral("Patchy"));
    root = QFileInfo(QFileInfo(probe.fileName()).absolutePath()).absolutePath();
#endif
  }
  return root + QStringLiteral("/RTsoft/Patchy/Patchy.ini");
}

void set_app_settings_root(const QString& root) {
  const auto absolute = root.isEmpty() ? QString() : QDir(root).absolutePath();
  {
    std::lock_guard lock(settings_root_mutex());
    settings_root_override() = absolute;
  }
  if (!absolute.isEmpty()) {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, absolute);
  }
}
#endif

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

int normalize_ui_text_size_percent(int stored) {
  if (stored <= 0) {
    return kDefaultUiTextSizePercent;
  }
  return std::clamp(stored, kMinUiTextSizePercent, kMaxUiTextSizePercent);
}

int stored_ui_text_size_percent() {
  return normalize_ui_text_size_percent(
      app_settings().value(ui_text_size_key(), kDefaultUiTextSizePercent).toInt());
}

void set_stored_ui_text_size_percent(int percent) {
  auto settings = app_settings();
  const int normalized = normalize_ui_text_size_percent(percent);
  if (normalized == kDefaultUiTextSizePercent) {
    settings.remove(ui_text_size_key());
  } else {
    settings.setValue(ui_text_size_key(), normalized);
  }
}

QString toolbar_icon_size_token(ToolbarIconSize size) {
  switch (size) {
    case ToolbarIconSize::Smallest:
      return QStringLiteral("smallest");
    case ToolbarIconSize::Small:
      return QStringLiteral("small");
    case ToolbarIconSize::Normal:
      break;
    case ToolbarIconSize::Bigger:
      return QStringLiteral("bigger");
    case ToolbarIconSize::Biggest:
      return QStringLiteral("biggest");
  }
  return QStringLiteral("normal");
}

ToolbarIconSize toolbar_icon_size_from_token(const QString& token) {
  for (const auto size : kToolbarIconSizes) {
    if (token == toolbar_icon_size_token(size)) {
      return size;
    }
  }
  return ToolbarIconSize::Normal;
}

double toolbar_icon_size_factor(ToolbarIconSize size) {
  switch (size) {
    case ToolbarIconSize::Smallest:
      return 0.75;
    case ToolbarIconSize::Small:
      return 0.875;
    case ToolbarIconSize::Normal:
      break;
    case ToolbarIconSize::Bigger:
      return 1.25;
    case ToolbarIconSize::Biggest:
      return 1.5;
  }
  return 1.0;
}

ToolbarIconSize stored_toolbar_icon_size() {
  return toolbar_icon_size_from_token(app_settings().value(toolbar_icon_size_key()).toString());
}

void set_stored_toolbar_icon_size(ToolbarIconSize size) {
  auto settings = app_settings();
  settings.setValue(toolbar_icon_size_key(), toolbar_icon_size_token(size));
}

ToolbarIconSize stored_layer_footer_icon_size() {
  return toolbar_icon_size_from_token(app_settings().value(layer_footer_icon_size_key()).toString());
}

void set_stored_layer_footer_icon_size(ToolbarIconSize size) {
  auto settings = app_settings();
  if (size == ToolbarIconSize::Normal) {
    settings.remove(layer_footer_icon_size_key());
  } else {
    settings.setValue(layer_footer_icon_size_key(), toolbar_icon_size_token(size));
  }
}

QString tool_flyout_open_mode_token(ToolFlyoutOpenMode mode) {
  switch (mode) {
    case ToolFlyoutOpenMode::Hold:
      break;
    case ToolFlyoutOpenMode::Click:
      return QStringLiteral("click");
    case ToolFlyoutOpenMode::Hover:
      return QStringLiteral("hover");
  }
  return QStringLiteral("hold");
}

ToolFlyoutOpenMode tool_flyout_open_mode_from_token(const QString& token) {
  for (const auto mode : kToolFlyoutOpenModes) {
    if (token == tool_flyout_open_mode_token(mode)) {
      return mode;
    }
  }
  return ToolFlyoutOpenMode::Hold;
}

ToolFlyoutOpenMode stored_tool_flyout_open_mode() {
  return tool_flyout_open_mode_from_token(app_settings().value(tool_flyout_open_mode_key()).toString());
}

void set_stored_tool_flyout_open_mode(ToolFlyoutOpenMode mode) {
  auto settings = app_settings();
  if (mode == ToolFlyoutOpenMode::Hold) {
    settings.remove(tool_flyout_open_mode_key());
  } else {
    settings.setValue(tool_flyout_open_mode_key(), tool_flyout_open_mode_token(mode));
  }
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
