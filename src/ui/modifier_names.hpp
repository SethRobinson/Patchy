#pragma once

#include <QString>

namespace patchy::ui {

// Modifier-key names for user-facing SENTENCES ("Alt-click to view it", "Ctrl+T transforms
// the path"). Windows and Linux say Ctrl and Alt; macOS says Command and Option, because
// that is what those keys physically are there: Patchy does not set
// AA_MacDontSwapCtrlAndMeta, so Qt maps Qt::ControlModifier onto the Command key and a hint
// reading "Ctrl+T" is describing Command+T on a Mac. Spelling the word rather than showing
// the glyph follows Apple's own prose convention and the house style that predates this
// helper ("Alt/Option-drag" in the Blend If editor).
//
// Shortcut CHIPS are a different surface and need nothing here: the "(Shift+A)" beside an
// action name comes from QKeySequence::NativeText in hotkey_registry.cpp, which already
// renders the macOS glyphs.
//
// Usage: write the token in the translatable string and resolve the result, so translators
// see and keep one stable marker instead of an English key name:
//
//   status_callback_(resolve_modifier_names(tr("%CTRL%-drag moves the segment.")));
//
// Shift, Delete, and the mouse verbs are spelled the same on every platform and take no
// token. Tokens are deliberately not %1-style: several of these sentences already use %1 for
// real arguments, the two tool-hint sources are translated later at a single choke point
// rather than at their call site, and QString::arg only consumes % followed by a digit, so
// the two mechanisms cannot collide.
inline constexpr auto kCtrlModifierToken = "%CTRL%";
inline constexpr auto kAltModifierToken = "%ALT%";

// "Ctrl" on Windows and Linux, "Command" on macOS.
[[nodiscard]] QString ctrl_key_name();
// "Alt" on Windows and Linux, "Option" on macOS.
[[nodiscard]] QString alt_key_name();

// Replaces every modifier token in `text`. A string with no token comes back unchanged, so
// this is safe to apply to any user-facing sentence.
[[nodiscard]] QString resolve_modifier_names(QString text);

}  // namespace patchy::ui
