#pragma once

// The Colors and Themes page of Preferences (main_window_preferences_themes.cpp;
// docs/theme-files.md, "Preferences UI"). show_preferences builds the page's widgets
// at once and reads the themes on the page's first visit: loading every package
// (icons, backdrop, sounds) is the slow part, and most Preferences opens never look.

#include <functional>

namespace patchy::ui {

struct PreferencesThemesPage {
  std::function<void()> populate;  // reads and lists every theme; later calls do nothing
  std::function<void()> commit;    // OK: the shown theme, its scripts answer, sounds and effects
  std::function<void()> revert;    // any other way out: undoes the live preview
};

}  // namespace patchy::ui
