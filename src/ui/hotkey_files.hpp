#pragma once

#include "ui/hotkey_registry.hpp"
#include "ui/tool_modifier_style.hpp"

#include <QByteArray>
#include <QString>
#include <vector>

namespace patchy::ui {

// Hotkey files (Preferences > Hotkeys > Export... / Import...): the user's override set as
// JSON, {"format": "patchy-hotkeys", "version": 1, "shortcuts": {"<command id>": ["Ctrl+K", ...]}}.
// Keys are QKeySequence PortableText ("Ctrl" is Cmd on macOS); an empty array means the
// command was deliberately left without a shortcut. The format token, the "version" and
// "shortcuts" keys and the suffix are persisted identifiers: later versions may only add
// keys, and readers ignore keys they do not know. See docs/hotkey-sets.md.
inline constexpr const char* kHotkeyFileFormat = "patchy-hotkeys";
inline constexpr int kHotkeyFileVersion = 1;
inline constexpr const char* kHotkeyFileSuffix = "patchykeys";
// Larger files are refused before parsing (every real set is a few kilobytes).
inline constexpr qint64 kHotkeyFileMaxBytes = 1024 * 1024;

// A style other than Patchy's adds "toolModifiers": "gimp" or "krita" (GitHub issue 302);
// readers that do not know the key ignore it.
[[nodiscard]] QByteArray hotkey_overrides_to_json(const HotkeyOverrideMap& overrides,
                                                  ToolModifierStyle tool_modifiers = ToolModifierStyle::Patchy);

struct HotkeyFileReadResult {
  bool ok{false};
  QString error;  // translated; set when !ok
  // Overrides for commands `commands` knows; an empty list unbinds the command.
  HotkeyOverrideMap overrides;
  int skipped_commands{0};  // command ids this Patchy does not have
  int skipped_keys{0};      // key texts that did not parse, or keys reserved for the canvas
  // The file's "toolModifiers"; Patchy's when it has none or names one this Patchy lacks.
  ToolModifierStyle tool_modifiers{ToolModifierStyle::Patchy};
};

// Parses a hotkey file against the registered commands. Unknown ids and unusable keys are
// counted and dropped; only a file that is not a hotkey file at all fails.
[[nodiscard]] HotkeyFileReadResult hotkey_overrides_from_json(const QByteArray& bytes,
                                                              const std::vector<HotkeyCommand>& commands);
[[nodiscard]] HotkeyFileReadResult read_hotkey_file(const QString& path, const std::vector<HotkeyCommand>& commands);
// Writes atomically (write_file_bytes_atomically). False with a translated `error` on failure.
[[nodiscard]] bool write_hotkey_file(const QString& path, const HotkeyOverrideMap& overrides, QString* error,
                                     ToolModifierStyle tool_modifiers = ToolModifierStyle::Patchy);

}  // namespace patchy::ui
