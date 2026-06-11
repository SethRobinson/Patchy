#include "ui/hotkey_registry.hpp"

#include "ui/app_settings.hpp"

#include <QAction>
#include <QObject>
#include <QStringList>
#include <algorithm>

namespace patchy::ui {

namespace {

const QString kHotkeysSettingsGroup = QStringLiteral("hotkeys");
// Newline-separated: shortcut text can itself contain ';' or ',' (Ctrl+; is a
// Patchy default), so visible separators cannot round-trip every sequence.
const QString kStorageSeparator = QStringLiteral("\n");

QStringList sorted_portable_strings(const QList<QKeySequence>& shortcuts) {
  QStringList texts;
  for (const auto& shortcut : shortcuts) {
    if (!shortcut.isEmpty()) {
      texts << shortcut.toString(QKeySequence::PortableText);
    }
  }
  texts.sort();
  return texts;
}

}  // namespace

QString clean_action_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  auto label = action->text();
  label.remove(QLatin1Char('&'));
  return label.trimmed();
}

QString action_shortcut_text(const QAction* action) {
  if (action == nullptr) {
    return {};
  }
  QStringList shortcut_labels;
  for (const auto& shortcut : action->shortcuts()) {
    if (!shortcut.isEmpty()) {
      shortcut_labels << shortcut.toString(QKeySequence::NativeText);
    }
  }
  return shortcut_labels.join(QStringLiteral(", "));
}

void refresh_action_tooltip(QAction* action) {
  if (action == nullptr || action->isSeparator()) {
    return;
  }
  const auto label = clean_action_text(action);
  const auto shortcut = action_shortcut_text(action);
  action->setToolTip(shortcut.isEmpty() ? label : QObject::tr("%1 (%2)").arg(label, shortcut));
}

HotkeyResolution resolve_hotkey_assignments(const std::vector<HotkeyCommand>& commands,
                                            const HotkeyOverrideMap& overrides) {
  HotkeyResolution result;
  QHash<QKeySequence, QString> claimed;
  for (const auto& command : commands) {
    const auto it = overrides.constFind(command.id);
    if (it == overrides.constEnd()) {
      continue;
    }
    QList<QKeySequence> kept;
    for (const auto& sequence : *it) {
      if (sequence.isEmpty()) {
        continue;
      }
      if (const auto owner = claimed.value(sequence); !owner.isEmpty()) {
        result.suppressions.push_back({command.id, sequence, owner});
        continue;
      }
      claimed.insert(sequence, command.id);
      kept << sequence;
    }
    result.effective.insert(command.id, kept);
  }
  for (const auto& command : commands) {
    if (overrides.contains(command.id)) {
      continue;
    }
    QList<QKeySequence> kept;
    for (const auto& sequence : command.default_shortcuts) {
      if (sequence.isEmpty()) {
        continue;
      }
      if (const auto owner = claimed.value(sequence); !owner.isEmpty()) {
        result.suppressions.push_back({command.id, sequence, owner});
        continue;
      }
      claimed.insert(sequence, command.id);
      kept << sequence;
    }
    result.effective.insert(command.id, kept);
  }
  return result;
}

QString hotkey_shortcuts_to_storage(const QList<QKeySequence>& shortcuts) {
  QStringList texts;
  for (const auto& shortcut : shortcuts) {
    if (!shortcut.isEmpty()) {
      texts << shortcut.toString(QKeySequence::PortableText);
    }
  }
  return texts.join(kStorageSeparator);
}

QList<QKeySequence> hotkey_shortcuts_from_storage(const QString& stored) {
  QList<QKeySequence> shortcuts;
  const auto parts = stored.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const auto& part : parts) {
    const auto sequence = QKeySequence::fromString(part.trimmed(), QKeySequence::PortableText);
    if (!sequence.isEmpty() && !shortcuts.contains(sequence)) {
      shortcuts << sequence;
    }
  }
  return shortcuts;
}

bool hotkey_shortcut_lists_equivalent(const QList<QKeySequence>& a, const QList<QKeySequence>& b) {
  return sorted_portable_strings(a) == sorted_portable_strings(b);
}

HotkeyRegistry::HotkeyRegistry() {
  load_overrides();
}

void HotkeyRegistry::register_command(QAction* action, QString id, QList<QKeySequence> default_shortcuts,
                                      QString category) {
  if (action == nullptr || id.isEmpty()) {
    return;
  }
  Q_ASSERT_X(find_command(id) == nullptr, "HotkeyRegistry::register_command", "duplicate hotkey command id");
  default_shortcuts.removeAll(QKeySequence());
  commands_.push_back({std::move(id), std::move(category), action, std::move(default_shortcuts)});
}

const HotkeyCommand* HotkeyRegistry::find_command(const QString& id) const {
  const auto it = std::find_if(commands_.begin(), commands_.end(),
                               [&id](const HotkeyCommand& command) { return command.id == id; });
  return it != commands_.end() ? &*it : nullptr;
}

HotkeyResolution HotkeyRegistry::resolution() const {
  return resolve_hotkey_assignments(commands_, overrides_);
}

void HotkeyRegistry::apply_overrides(HotkeyOverrideMap overrides) {
  for (auto it = overrides.begin(); it != overrides.end();) {
    const auto* command = find_command(it.key());
    if (command == nullptr || hotkey_shortcut_lists_equivalent(it.value(), command->default_shortcuts)) {
      it = overrides.erase(it);
    } else {
      ++it;
    }
  }
  overrides_ = std::move(overrides);
  persist_overrides();
  apply_to_actions();
}

void HotkeyRegistry::apply_to_actions() {
  const auto resolved = resolution();
  for (const auto& command : commands_) {
    auto* action = command.action.data();
    if (action == nullptr) {
      continue;
    }
    action->setShortcuts(resolved.effective.value(command.id));
    action->setShortcutContext(Qt::ApplicationShortcut);
    refresh_action_tooltip(action);
  }
}

void HotkeyRegistry::load_overrides() {
  overrides_.clear();
  auto settings = app_settings();
  settings.beginGroup(kHotkeysSettingsGroup);
  const auto keys = settings.childKeys();
  for (const auto& key : keys) {
    overrides_.insert(key, hotkey_shortcuts_from_storage(settings.value(key).toString()));
  }
  settings.endGroup();
}

void HotkeyRegistry::persist_overrides() const {
  auto settings = app_settings();
  settings.remove(kHotkeysSettingsGroup);
  settings.beginGroup(kHotkeysSettingsGroup);
  for (auto it = overrides_.constBegin(); it != overrides_.constEnd(); ++it) {
    settings.setValue(it.key(), hotkey_shortcuts_to_storage(it.value()));
  }
  settings.endGroup();
}

}  // namespace patchy::ui
