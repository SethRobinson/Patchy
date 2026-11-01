#pragma once

// The plug-in's own dialog, as Patchy reaches it for scripts and tests: find it by the
// plug-in's process, save an image of it, and answer it with Enter (OK) or Escape
// (Cancel) sent to that window only (docs/gimp-plugins.md, "Scripting"). Windows only;
// elsewhere every call reports failure. Window-targeted messages and PrintWindow touch
// nothing but the plug-in's window (AGENTS.md, automation of hosted plug-in windows).

#include <QString>

#include <cstdint>

namespace patchy::ui {

// The largest visible top-level window of the process, or 0.
[[nodiscard]] std::uintptr_t gimp_plugin_window_for_process(std::uint64_t pid);
// Saves the window as it is drawn (PrintWindow) to a PNG.
bool capture_gimp_plugin_window(std::uintptr_t window, const QString& png_path);
// Answers the window: the first attempt sends Enter (accept) or Escape (cancel); the
// second Alt+O, the OK button's mnemonic in GIMP's dialogs (a focused notebook tab or list
// does not pass Enter on to the default button); the third clicks where GIMP's dialogs put
// OK (left of Cancel in the bottom-right corner). Cancel's later attempts ask the window
// to close.
bool answer_gimp_plugin_window(std::uintptr_t window, bool accept, int attempt = 0);

}  // namespace patchy::ui
