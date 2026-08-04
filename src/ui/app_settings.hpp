#pragma once

#include <QSettings>

#include <array>

namespace patchy::ui {

[[nodiscard]] QSettings app_settings();

// Interface-scale steps offered in Preferences and honored at startup, in percent. The
// sub-100 entries are the reciprocals of the 150%/133%/111% display steps, so a browser
// tab can be brought back to the size the desktop build renders at. Kept here because
// main.cpp reads the stored value before QApplication exists and the Preferences dialog
// builds its combo from the same list.
inline constexpr std::array<int, 8> kGuiScalePercents{67, 75, 90, 100, 125, 150, 175, 200};

// The web build starts smaller than the desktop build: browser chrome already eats
// vertical space and the panels read large in a tab.
#ifdef Q_OS_WASM
inline constexpr int kDefaultGuiScalePercent = 75;
#else
inline constexpr int kDefaultGuiScalePercent = 100;
#endif

// Returns the stored percent when it is one of kGuiScalePercents. Anything else (a
// hand-edited ini, a step a later build dropped) falls back to the platform default
// rather than applying a scale the Preferences combo cannot show.
[[nodiscard]] int normalize_gui_scale_percent(int stored);

// The effective interface scale in percent. Safe to call before QApplication exists.
[[nodiscard]] int stored_gui_scale_percent();

// Persists the interface scale. Paired with stored_gui_scale_percent() so the settings key,
// a compatibility contract, lives in exactly one place.
void set_stored_gui_scale_percent(int percent);

}  // namespace patchy::ui
