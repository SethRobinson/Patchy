#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QPointer>
#include <QString>
#include <vector>

class QAction;

namespace patchy::ui {

// Shared display helpers for action labels and shortcut-suffixed tooltips.
[[nodiscard]] QString clean_action_text(const QAction* action);
[[nodiscard]] QString action_shortcut_text(const QAction* action);
void refresh_action_tooltip(QAction* action);

struct HotkeyCommand {
  QString id;
  // Category key for commands that do not live in the menu bar ("tools",
  // "color", "brush"); empty for commands grouped by their menu location.
  QString category;
  QPointer<QAction> action;
  QList<QKeySequence> default_shortcuts;
};

struct HotkeySuppression {
  QString id;
  QKeySequence sequence;
  QString winner_id;
};

// Keys present in the map are overridden commands; an empty list means the
// user explicitly unbound the command.
using HotkeyOverrideMap = QHash<QString, QList<QKeySequence>>;

struct HotkeyResolution {
  QHash<QString, QList<QKeySequence>> effective;
  std::vector<HotkeySuppression> suppressions;
};

// Resolves the effective shortcut set. User overrides claim their sequences
// first (registration order breaks ties); commands on defaults lose any
// sequence an override already claimed. Losers are recorded as suppressions
// and never persisted, so removing the override restores the default.
[[nodiscard]] HotkeyResolution resolve_hotkey_assignments(const std::vector<HotkeyCommand>& commands,
                                                          const HotkeyOverrideMap& overrides);

// Storage format: PortableText sequences joined with newlines (visible
// separators collide with keys like Ctrl+;); the empty string is an explicit
// "no shortcut".
[[nodiscard]] QString hotkey_shortcuts_to_storage(const QList<QKeySequence>& shortcuts);
[[nodiscard]] QList<QKeySequence> hotkey_shortcuts_from_storage(const QString& stored);
[[nodiscard]] bool hotkey_shortcut_lists_equivalent(const QList<QKeySequence>& a, const QList<QKeySequence>& b);

class HotkeyRegistry {
public:
  HotkeyRegistry();

  void register_command(QAction* action, QString id, QList<QKeySequence> default_shortcuts,
                        QString category = QString());

  [[nodiscard]] const std::vector<HotkeyCommand>& commands() const noexcept { return commands_; }
  [[nodiscard]] const HotkeyCommand* find_command(const QString& id) const;
  [[nodiscard]] const HotkeyOverrideMap& overrides() const noexcept { return overrides_; }
  [[nodiscard]] HotkeyResolution resolution() const;

  // Replaces the override set, drops entries identical to a command's
  // defaults, persists only the remaining deltas, and reapplies effective
  // shortcuts (and tooltips) to every registered action.
  void apply_overrides(HotkeyOverrideMap overrides);

  // Applies effective shortcuts to all registered actions. Call once after
  // every command is registered.
  void apply_to_actions();

private:
  void load_overrides();
  void persist_overrides() const;

  std::vector<HotkeyCommand> commands_;
  HotkeyOverrideMap overrides_;
};

}  // namespace patchy::ui
