#pragma once

// One theme apply, as the user sees it (GitHub issue 490): a busy cursor for its whole
// length, and its phase timings for PATCHY_THEME_TRACE=1. ThemeManager opens a
// ThemeApplyScope around every apply; nested scopes (a preference change that clears a
// custom theme) join the outermost one.
//
// With PATCHY_THEME_TRACE=1 each outermost apply prints its phases to stderr, the
// counts of the restyle events it sent (Polish, StyleChange, PaletteChange,
// ThemeChange, FontChange), and how long the event loop took to come back to idle
// afterwards (queued platform theme changes and the repaint).

#include <QString>

#include <vector>

namespace patchy::ui {

struct ThemeApplyPhase {
  QString name;
  double ms = 0.0;
};

class ThemeApplyScope {
 public:
  explicit ThemeApplyScope(const char* what);
  ~ThemeApplyScope();
  ThemeApplyScope(const ThemeApplyScope&) = delete;
  ThemeApplyScope& operator=(const ThemeApplyScope&) = delete;
};

// Ends the current phase under `name` (a string literal). A no-op outside an apply
// or with the trace off.
void theme_apply_phase(const char* name);

[[nodiscard]] bool theme_apply_trace_enabled();
// Test seam: turns the trace on or off regardless of the environment.
void set_theme_apply_trace_enabled_for_testing(bool enabled);

// The phases of the last finished outermost apply (trace on only).
[[nodiscard]] std::vector<ThemeApplyPhase> last_theme_apply_phases();

}  // namespace patchy::ui
