#pragma once

#include "ui/hotkey_registry.hpp"
#include "ui/tool_modifier_style.hpp"

#include <QString>
#include <QStringView>
#include <span>
#include <vector>

namespace patchy::ui {

// Built-in shortcut sets for Preferences > Hotkeys > "Shortcut set" (GitHub issue 633).
// A set is a list of overrides on top of Patchy's defaults: choosing one stages exactly
// those overrides, so the user can still change any key and Export saves the result. Each
// table is Patchy's own data, built from the other programs' published shortcut lists
// (sources and the legal limits: docs/hotkey-sets.md); it maps only commands Patchy has.
struct HotkeySetEntry {
  const char* id;
  // QKeySequence PortableText, several joined by '\n'; "" leaves the command without a
  // shortcut (it frees a key the set gives to another command).
  const char* keys;
  // macOS keys when they differ (Ctrl is Cmd there either way); nullptr = `keys`.
  const char* mac_keys{nullptr};
};

struct HotkeySet {
  // Stable id ("photoshop", "gimp", "krita"): the combo and the tests name sets by it.
  const char* id;
  // QT_TRANSLATE_NOOP("patchy::ui::HotkeyEditorPanel", ...): translate in that context.
  const char* label;
  std::span<const HotkeySetEntry> entries;
};

// The id of Patchy's own defaults (an empty override map) in the combo.
inline constexpr const char* kPatchyHotkeySetId = "patchy";

[[nodiscard]] std::span<const HotkeySet> builtin_hotkey_sets();
[[nodiscard]] const HotkeySet* find_builtin_hotkey_set(QStringView id);
// The overrides a set stages for these commands: ids Patchy does not register are skipped,
// and entries equal to a command's defaults are dropped (the registry would drop them too).
[[nodiscard]] HotkeyOverrideMap hotkey_set_overrides(const HotkeySet& set, const std::vector<HotkeyCommand>& commands);
// kPatchyHotkeySetId for an empty map, a set's id when `overrides` equal that set's, else empty.
[[nodiscard]] QString matching_hotkey_set_id(const HotkeyOverrideMap& overrides,
                                             const std::vector<HotkeyCommand>& commands);
// Whether two override maps bind the same keys (each list compared as a set of keys).
[[nodiscard]] bool hotkey_override_maps_equivalent(const HotkeyOverrideMap& a, const HotkeyOverrideMap& b);

// Sets the user owns (docs/hotkey-sets.md "Your own sets"): imported files, the Custom set an
// edit of a built-in set turns into, and either one renamed. Built-in sets never change.
// The id of the unnamed Custom set; a named set's id is "user-<n>". Ids are persisted.
inline constexpr const char* kCustomHotkeySetId = "custom";
struct UserHotkeySet {
  QString id;
  QString name;  // empty for the Custom set, which shows as "Custom" in the current language
  HotkeyOverrideMap overrides;
  // The tool modifiers of the set it came from (GitHub issue 302): Custom made from GIMP-style
  // keeps GIMP's, an imported file carries its own.
  ToolModifierStyle tool_modifiers{ToolModifierStyle::Patchy};
};
// The stored sets, read against the registered commands (unknown ids and keys are dropped).
[[nodiscard]] std::vector<UserHotkeySet> load_user_hotkey_sets(const std::vector<HotkeyCommand>& commands);
// The set Preferences > Hotkeys last applied: kPatchyHotkeySetId, a built-in id or a user set id.
[[nodiscard]] QString load_active_hotkey_set_id();
// Also stores the active set's tool modifiers (`hotkeySets/toolModifiers`), which the canvas
// reads at startup (load_active_tool_modifier_style).
void store_user_hotkey_sets(const std::vector<UserHotkeySet>& sets, const QString& active_id);
// The folder Import... and Export... open in: the last one used, else Documents.
[[nodiscard]] QString hotkey_file_folder();
void remember_hotkey_file_folder(const QString& file_path);

}  // namespace patchy::ui
