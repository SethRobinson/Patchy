#pragma once

#include <QString>
#include <QStringView>
#include <Qt>

namespace patchy::ui {

enum class CanvasTool;

// Which key the on-canvas tool modifiers use (GitHub issue 302; docs/hotkey-sets.md "Tool
// modifiers"). A style belongs to a shortcut set: Patchy default and Photoshop-style use Alt
// as Photoshop does; GIMP-style and Krita-style move the gestures their program's published
// manual gives Ctrl (zoom out, the painting tools' color pick, the clone source and, for GIMP,
// the Dodge/Burn and Blur/Sharpen swap, selection subtract and Ctrl+wheel zoom). A tool whose
// pick, source or swap moves to Ctrl takes its temporary Move on Alt instead. Every other
// gesture keeps Patchy's key. Ctrl is Command on macOS, as everywhere in Patchy.
enum class ToolModifierStyle { Patchy, Gimp, Krita };

// Persisted tokens (the `hotkeySets/toolModifiers` setting, each user set's `toolModifiers`,
// a hotkey file's "toolModifiers" key). Never rename.
inline constexpr const char* kPatchyToolModifiersToken = "patchy";
inline constexpr const char* kGimpToolModifiersToken = "gimp";
inline constexpr const char* kKritaToolModifiersToken = "krita";

[[nodiscard]] QString tool_modifier_style_token(ToolModifierStyle style);
// Empty or unknown text reads as Patchy, so a file from a later Patchy never fails.
[[nodiscard]] ToolModifierStyle tool_modifier_style_from_token(QStringView token);
// The style of a built-in set (kPatchyHotkeySetId and "photoshop" are Patchy's).
[[nodiscard]] ToolModifierStyle builtin_set_tool_modifier_style(QStringView set_id);

// The style the canvas follows: set at startup from the settings and when Preferences applies.
[[nodiscard]] ToolModifierStyle active_tool_modifier_style() noexcept;
void set_active_tool_modifier_style(ToolModifierStyle style) noexcept;
[[nodiscard]] ToolModifierStyle load_active_tool_modifier_style();

// The gestures a style can move between Alt and Ctrl.
enum class ToolModifierRole {
  ZoomOut,            // a Zoom tool click zooms out
  WheelZoom,          // the wheel zooms the view (the other key pans sideways)
  ColorPick,          // a painting tool's click picks a color
  RetouchSource,      // Clone and Healing: a click sets the source
  ToningSwap,         // Dodge paints as Burn, Blur as Sharpen, and back
  SelectionSubtract,  // selection tools: subtract, with Shift intersect; mid-drag the marquee grows from its center
  TemporaryMove,      // a painting tool's drag moves the layer
};

// Qt::AltModifier or Qt::ControlModifier for `role` on `tool`.
[[nodiscard]] Qt::KeyboardModifier tool_modifier(ToolModifierRole role, CanvasTool tool,
                                                 ToolModifierStyle style) noexcept;
[[nodiscard]] Qt::KeyboardModifier tool_modifier(ToolModifierRole role, CanvasTool tool) noexcept;
[[nodiscard]] Qt::Key tool_modifier_key(ToolModifierRole role, CanvasTool tool) noexcept;
[[nodiscard]] constexpr Qt::KeyboardModifier other_tool_modifier(Qt::KeyboardModifier modifier) noexcept {
  return modifier == Qt::AltModifier ? Qt::ControlModifier : Qt::AltModifier;
}
// Whether `modifiers` hold the role's key.
[[nodiscard]] bool tool_modifier_held(ToolModifierRole role, CanvasTool tool, Qt::KeyboardModifiers modifiers) noexcept;
// Whether a gesture of `tool` (or the selection tools' subtract) sits on Ctrl in the active
// style: the canvas then refreshes the cursor when Ctrl alone goes down or up.
[[nodiscard]] bool tool_has_ctrl_tool_modifier(CanvasTool tool) noexcept;

// Tokens for sentences that name a role's key; translators keep them like %ALT% and %CTRL%.
// resolve_tool_modifier_names replaces them for `tool`, then the plain modifier tokens.
inline constexpr auto kZoomOutKeyToken = "%ZOOM_OUT_KEY%";
inline constexpr auto kPickKeyToken = "%PICK_KEY%";
inline constexpr auto kSourceKeyToken = "%SOURCE_KEY%";
inline constexpr auto kSwapKeyToken = "%SWAP_KEY%";
inline constexpr auto kSubtractKeyToken = "%SUBTRACT_KEY%";
[[nodiscard]] QString resolve_tool_modifier_names(QString text, CanvasTool tool);

}  // namespace patchy::ui
