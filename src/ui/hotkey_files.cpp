#include "ui/hotkey_files.hpp"

#include "support/atomic_file_write.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <span>

namespace patchy::ui {

namespace {

const QString kFormatKey = QStringLiteral("format");
const QString kVersionKey = QStringLiteral("version");
const QString kShortcutsKey = QStringLiteral("shortcuts");
// Optional since GitHub issue 302: the set's tool modifiers token (tool_modifier_style.hpp).
const QString kToolModifiersKey = QStringLiteral("toolModifiers");

HotkeyFileReadResult failure(QString error) {
  HotkeyFileReadResult result;
  result.error = std::move(error);
  return result;
}

const HotkeyCommand* find_command(const std::vector<HotkeyCommand>& commands, const QString& id) {
  const auto it = std::find_if(commands.begin(), commands.end(),
                               [&id](const HotkeyCommand& command) { return command.id == id; });
  return it != commands.end() ? &*it : nullptr;
}

// One chord, a real key, and not a key the canvas consumes (the capture field's rule), unless
// one of Patchy's own defaults already uses it as a shortcut (Tab hides the panels, Shift+Backspace
// fills), so a built-in set's export always imports whole.
bool usable_imported_sequence(const QKeySequence& sequence, const std::vector<HotkeyCommand>& commands) {
  if (sequence.count() != 1) {
    return false;
  }
  const auto combination = sequence[0];
  const int key = static_cast<int>(combination.key());
  if (key == 0 || key == Qt::Key_unknown) {
    return false;
  }
  if (!is_reserved_binding_key(key, combination.keyboardModifiers() & ~Qt::KeypadModifier)) {
    return true;
  }
  return std::any_of(commands.begin(), commands.end(), [&sequence](const HotkeyCommand& command) {
    return command.default_shortcuts.contains(sequence);
  });
}

}  // namespace

QByteArray hotkey_overrides_to_json(const HotkeyOverrideMap& overrides, ToolModifierStyle tool_modifiers) {
  QJsonObject shortcuts;
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    QJsonArray keys;
    for (const auto& sequence : it.value()) {
      if (!sequence.isEmpty()) {
        keys.append(sequence.toString(QKeySequence::PortableText));
      }
    }
    shortcuts.insert(it.key(), keys);
  }
  QJsonObject root;
  root.insert(kFormatKey, QString::fromLatin1(kHotkeyFileFormat));
  root.insert(kVersionKey, kHotkeyFileVersion);
  root.insert(kShortcutsKey, shortcuts);
  if (tool_modifiers != ToolModifierStyle::Patchy) {
    root.insert(kToolModifiersKey, tool_modifier_style_token(tool_modifiers));
  }
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

HotkeyFileReadResult hotkey_overrides_from_json(const QByteArray& bytes, const std::vector<HotkeyCommand>& commands) {
  const auto not_a_hotkey_file =
      QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "This is not a Patchy hotkey file.");
  QJsonParseError parse_error{};
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return failure(not_a_hotkey_file);
  }
  const auto root = document.object();
  if (root.value(kFormatKey).toString() != QString::fromLatin1(kHotkeyFileFormat)) {
    return failure(not_a_hotkey_file);
  }
  const auto version = root.value(kVersionKey);
  if (!version.isDouble() || version.toDouble() < 1.0) {
    return failure(not_a_hotkey_file);
  }
  const auto shortcuts_value = root.value(kShortcutsKey);
  if (!shortcuts_value.isObject()) {
    return failure(not_a_hotkey_file);
  }

  HotkeyFileReadResult result;
  result.ok = true;
  result.tool_modifiers = tool_modifier_style_from_token(root.value(kToolModifiersKey).toString());
  const auto shortcuts = shortcuts_value.toObject();
  for (auto it = shortcuts.constBegin(); it != shortcuts.constEnd(); ++it) {
    const auto* command = find_command(commands, it.key());
    if (command == nullptr) {
      ++result.skipped_commands;
      continue;
    }
    QStringList texts;
    if (it.value().isString()) {
      texts << it.value().toString();
    } else if (it.value().isArray()) {
      for (const auto& entry : it.value().toArray()) {
        if (entry.isString()) {
          texts << entry.toString();
        } else {
          ++result.skipped_keys;
        }
      }
    } else {
      ++result.skipped_keys;
      continue;
    }
    QList<QKeySequence> sequences;
    for (const auto& text : texts) {
      if (text.trimmed().isEmpty()) {
        continue;
      }
      const auto sequence = QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
      if (sequence.isEmpty() || !usable_imported_sequence(sequence, commands)) {
        ++result.skipped_keys;
        continue;
      }
      if (!sequences.contains(sequence)) {
        sequences << sequence;
      }
    }
    // A command whose every key was unusable keeps its default rather than losing it.
    if (sequences.isEmpty() && !texts.isEmpty()) {
      continue;
    }
    result.overrides.insert(command->id, sequences);
  }
  return result;
}

HotkeyFileReadResult read_hotkey_file(const QString& path, const std::vector<HotkeyCommand>& commands) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return failure(QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Could not open the file."));
  }
  if (file.size() > kHotkeyFileMaxBytes) {
    return failure(QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "This is not a Patchy hotkey file."));
  }
  return hotkey_overrides_from_json(file.readAll(), commands);
}

bool write_hotkey_file(const QString& path, const HotkeyOverrideMap& overrides, QString* error,
                       ToolModifierStyle tool_modifiers) {
  const auto bytes = hotkey_overrides_to_json(overrides, tool_modifiers);
  try {
    write_file_bytes_atomically(
        to_filesystem_path(path),
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())),
        "Could not create the hotkey file", "Could not write the hotkey file");
  } catch (const std::exception&) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Could not write the file.");
    }
    return false;
  }
  return true;
}

}  // namespace patchy::ui
