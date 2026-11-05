#pragma once

#include "ui/hotkey_registry.hpp"

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

}  // namespace patchy::ui
