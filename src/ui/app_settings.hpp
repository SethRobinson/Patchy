#pragma once

#include "color/color_management.hpp"

#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <utility>

namespace patchy::ui {

[[nodiscard]] QSettings app_settings();
#ifndef Q_OS_WASM
// Patchy.ini, beside the preset libraries: `<settings root>/RTsoft/Patchy/Patchy.ini`. The
// default root makes it the AppDataLocation folder on Windows (`%APPDATA%`) and macOS
// (`~/Library/Application Support`); Linux keeps config in `~/.config`. Through 1.19 it was
// `<Qt user ini root>/Patchy/Patchy.ini`; see app_data_migration.hpp.
[[nodiscard]] QString app_settings_file_path();
// Redirects the settings root for isolated runs (PATCHY_SETTINGS_DIR, the test suites) and
// points QSettings' user ini root there too, so the legacy-folder lookup follows. Call it
// before the first app_settings() read; a QSettings::setPath call alone is not enough on macOS.
void set_app_settings_root(const QString& root);
#endif
// Brush assets and recent paths persist across owned MCP workspaces.
[[nodiscard]] QSettings brush_library_settings();
[[nodiscard]] QSettings recent_history_settings();

// Interface-scale range offered in Preferences and honored at startup, in whole percent
// (GitHub issue 333: any 1% step). Kept here because main.cpp reads the stored value
// before QApplication exists and the Preferences field takes its range from here.
inline constexpr int kMinGuiScalePercent = 50;
inline constexpr int kMaxGuiScalePercent = 300;
// The presets in the Preferences field's drop-down; any other whole percent in the range
// can be typed. 67, 75 and 90 are the reciprocals of the 150%/133%/111% display steps, so
// a browser tab can be brought back to the size the desktop build renders at; 80 to 120
// run in 5% steps for fine tuning around full size (GitHub issue 333).
inline constexpr std::array<int, 17> kGuiScalePresetPercents{67,  75,  80,  85,  90,  95,  100, 105, 110,
                                                             115, 120, 125, 150, 175, 200, 250, 300};
// The field's arrow step.
inline constexpr int kGuiScaleStepPercent = 5;

// The web build starts smaller than the desktop build: browser chrome already eats
// vertical space and the panels read large in a tab. The web shell page
// (packaging/web/patchy.html.in) applies the scale browser-side and duplicates this
// default and the range; keep them in sync when changing either.
#ifdef Q_OS_WASM
inline constexpr int kDefaultGuiScalePercent = 75;
#else
inline constexpr int kDefaultGuiScalePercent = 100;
#endif

// Returns the stored percent clamped to kMinGuiScalePercent..kMaxGuiScalePercent. Zero
// or below (a missing or non-numeric value read as an int) falls back to the platform
// default rather than shrinking the interface to the minimum.
[[nodiscard]] int normalize_gui_scale_percent(int stored);

// The effective interface scale in percent. Safe to call before QApplication exists.
[[nodiscard]] int stored_gui_scale_percent();

// Persists the interface scale. Paired with stored_gui_scale_percent() so the settings key,
// a compatibility contract, lives in exactly one place.
void set_stored_gui_scale_percent(int percent);

// Toolbar icon size (Preferences, beside the interface scale): a multiplier on top of the
// interface scale for the tool palette's icons, buttons and footer (docs/tool-palette.md).
// Named steps rather than percentages, because the interface scale already is one. Stored
// under `preferences/toolbarIconSize` as the token, never the translated label; the key and
// the tokens are compatibility contracts. An unknown or missing token reads as Normal.
enum class ToolbarIconSize { Smallest, Small, Normal, Bigger, Biggest };
inline constexpr std::array<ToolbarIconSize, 5> kToolbarIconSizes{
    ToolbarIconSize::Smallest, ToolbarIconSize::Small, ToolbarIconSize::Normal, ToolbarIconSize::Bigger,
    ToolbarIconSize::Biggest};
[[nodiscard]] QString toolbar_icon_size_token(ToolbarIconSize size);
[[nodiscard]] ToolbarIconSize toolbar_icon_size_from_token(const QString& token);
// The multiplier applied to the Normal palette lengths: 0.75, 0.875, 1, 1.25, 1.5.
[[nodiscard]] double toolbar_icon_size_factor(ToolbarIconSize size);
[[nodiscard]] ToolbarIconSize stored_toolbar_icon_size();
void set_stored_toolbar_icon_size(ToolbarIconSize size);

// The Layers panel footer buttons' size (GitHub issue 213 follow-up; docs/layer-panel.md),
// set in Customize Tool Palette's Layers buttons tab: the same steps and tokens
// as the toolbar icon size, stored apart under `layersPanel/footerIconSize` (a compatibility
// contract). Normal is stored as no key; an unknown or missing token reads as Normal.
[[nodiscard]] ToolbarIconSize stored_layer_footer_icon_size();
void set_stored_layer_footer_icon_size(ToolbarIconSize size);

// When a tool palette flyout (a tool group) opens (GitHub issue 459; docs/tool-palette.md):
// press and hold the button (the default), a click on it, or the pointer resting on it.
// Stored under `preferences/toolFlyoutOpenMode` as the token (`hold`, `click`, `hover`); the
// key and the tokens are compatibility contracts. An unknown or missing token reads as Hold,
// and Hold is stored as no key.
enum class ToolFlyoutOpenMode { Hold, Click, Hover };
inline constexpr std::array<ToolFlyoutOpenMode, 3> kToolFlyoutOpenModes{
    ToolFlyoutOpenMode::Hold, ToolFlyoutOpenMode::Click, ToolFlyoutOpenMode::Hover};
[[nodiscard]] QString tool_flyout_open_mode_token(ToolFlyoutOpenMode mode);
[[nodiscard]] ToolFlyoutOpenMode tool_flyout_open_mode_from_token(const QString& token);
[[nodiscard]] ToolFlyoutOpenMode stored_tool_flyout_open_mode();
void set_stored_tool_flyout_open_mode(ToolFlyoutOpenMode mode);

// Automatic document recovery (docs/document-recovery.md): the timer interval steps
// offered in Preferences, in minutes. Photoshop's own list.
inline constexpr std::array<int, 5> kRecoveryIntervalMinutes{5, 10, 15, 30, 60};
inline constexpr int kDefaultRecoveryIntervalMinutes = 10;

// Returns the stored interval when it is one of kRecoveryIntervalMinutes, the default
// otherwise (a hand-edited ini, a step a later build dropped).
[[nodiscard]] int normalize_recovery_interval_minutes(int stored);

// The persisted recovery preferences (keys `recovery/enabled`, default true, and
// `recovery/intervalMinutes`; both are compatibility contracts). The web build has no
// recovery store, so it reports disabled and ignores writes.
[[nodiscard]] bool stored_recovery_enabled();
void set_stored_recovery_enabled(bool enabled);
[[nodiscard]] int stored_recovery_interval_minutes();
void set_stored_recovery_interval_minutes(int minutes);

// How often the startup update check runs (GitHub issue 92; 1, "Daily", issue 189), in
// calendar days; 0 checks on every launch and is the default. Key `updates/checkIntervalDays`; a stored value outside the
// steps falls back to the default. `updates/lastCheckSecsSinceEpoch` is when a startup
// check last got an answer (UTC seconds, 0 when never). `updates/skippedVersion` is the
// version the user ticked "Don't ask again" for on the update prompt (empty when none).
// All three keys are compatibility contracts. See docs/auto-update.md.
inline constexpr std::array<int, 5> kUpdateCheckIntervalDays{0, 1, 7, 14, 30};
inline constexpr int kDefaultUpdateCheckIntervalDays = 0;
[[nodiscard]] int normalize_update_check_interval_days(int stored);
[[nodiscard]] int stored_update_check_interval_days();
void set_stored_update_check_interval_days(int days);
[[nodiscard]] qint64 stored_update_last_check_secs();
void set_stored_update_last_check_secs(qint64 secs_since_epoch);
[[nodiscard]] QString stored_update_skipped_version();
void set_stored_update_skipped_version(const QString& version);

// AppImage only: the user chose "Don't Ask Again" on the startup offer to add Patchy to the
// applications menu, or removed the entry Patchy added (key `appimage/menuEntryPromptDeclined`,
// default false, a compatibility contract). Help keeps the command either way.
[[nodiscard]] bool stored_appimage_menu_entry_prompt_declined();
void set_stored_appimage_menu_entry_prompt_declined(bool declined);

// Linux only: whether the main window keeps the desktop's own title bar and frame
// instead of Patchy's frameless chrome (key `window/systemTitleBar`, default false, a
// compatibility contract). Read once at startup by MainWindow::use_custom_window_chrome;
// a change applies on the next launch. Stored but inert on other platforms.
[[nodiscard]] bool stored_system_title_bar();
// The user's explicit choice, or nullopt when the key was never written: Linux then
// follows the desktop (on where a global menu bar takes the menus, GitHub issue 161).
[[nodiscard]] std::optional<bool> stored_system_title_bar_choice();
void set_stored_system_title_bar(bool enabled);

// The size the user dragged the Preferences dialog to (key `preferences/dialogSize`, a
// compatibility contract); invalid when never resized.
[[nodiscard]] QSize stored_preferences_dialog_size();
void set_stored_preferences_dialog_size(QSize size);
// The id of the Preferences page the dialog was last closed on (key `preferences/lastPage`,
// a compatibility contract, as are the page ids); empty when never stored.
[[nodiscard]] QString stored_preferences_last_page();
void set_stored_preferences_last_page(const QString& page_id);

// Legacy Photoshop plug-in folders the user added (key `plugins/userFolders`, a
// compatibility contract), on top of the automatic `plugins` folders next to the
// application and in the per-user app-data directory. See docs/plugins.md.
[[nodiscard]] QStringList stored_legacy_plugin_folders();
void set_stored_legacy_plugin_folders(const QStringList& folders);

// The largest screen a legacy plug-in is told about (key `plugins/screenSize`,
// "<width>x<height>" or "monitor"; a compatibility contract). Plug-in windows
// open on the monitor showing Patchy; full-screen plug-in interfaces size
// themselves to this, so a cap keeps them usable on large monitors. (0, 0)
// means the monitor's whole work area. The choices are the Preferences combo;
// anything else stored falls back to the default. See docs/plugins.md.
inline constexpr std::array<std::pair<int, int>, 5> kLegacyPluginScreenSizes{
    {{0, 0}, {1920, 1200}, {1600, 1200}, {1280, 1024}, {1024, 768}}};
inline constexpr std::pair<int, int> kDefaultLegacyPluginScreenSize{1280, 1024};
[[nodiscard]] std::pair<int, int> stored_legacy_plugin_screen_size();
void set_stored_legacy_plugin_screen_size(std::pair<int, int> size);

// The working CMYK space (docs/cmyk.md): what the color picker's C/M/Y/K fields, the Info
// panel and CMYK swatches mean. Keys `color/cmykProfile`, `color/cmykIntent` (a
// rendering_intent_token) and `color/cmykBlackPointCompensation`; all three, and the two
// reserved profile values, are compatibility contracts. `profile` is empty for Automatic
// (an installed U.S. Web Coated (SWOP) v2, else the bundled profile), kCmykProfileFormula
// for no profile, else a profile file.
inline constexpr auto kCmykProfileFormula = "formula";
struct CmykWorkingSettings {
  QString profile;
  RenderingIntent intent{RenderingIntent::RelativeColorimetric};
  bool black_point_compensation{true};

  friend bool operator==(const CmykWorkingSettings&, const CmykWorkingSettings&) = default;
};
[[nodiscard]] CmykWorkingSettings stored_cmyk_working_settings();
void set_stored_cmyk_working_settings(const CmykWorkingSettings& settings);
// The ISO Coated v2 profile compiled into the application (docs/cmyk.md).
[[nodiscard]] const std::vector<std::uint8_t>& bundled_cmyk_profile();
// The space those settings describe. A chosen file that is missing or unusable falls back
// to Automatic; never null.
[[nodiscard]] std::shared_ptr<const CmykWorkingSpace> cmyk_working_space_for(const CmykWorkingSettings& settings);
// The gamut test and printed color for single colors in the current working space (the
// color picker's warning, app.colorInCmykGamut). Null under the formula. Building one
// samples the profile (a fraction of a second), so it is kept until the space changes.
[[nodiscard]] std::shared_ptr<const CmykProofTransform> cmyk_gamut_check();

}  // namespace patchy::ui
