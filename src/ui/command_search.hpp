#pragma once

#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QAction;
class QFrame;
class QMenuBar;
class QToolButton;
class QWidget;

namespace patchy::ui {

class HotkeyRegistry;

// Help > Search Commands (GitHub issue 554; docs/command-search.md).

// A menu label without its "opens a dialog" ellipsis ("Levels..." -> "Levels").
[[nodiscard]] QString strip_menu_ellipsis(QString label);

// Calls `visit` for every command in the menu bar's menus, depth first in menu order, with
// the top-level menu's title and the submenu titles on the way down (mnemonics removed).
// Separators, submenu entries and widget actions (the Open Recent filter rows) are not
// commands. With `visible_only`, hidden commands and hidden or disabled submenus (with all
// they hold) are skipped. Shared by the Keyboard Shortcuts editor and the command search.
void for_each_menu_bar_command(
    const QMenuBar* menu_bar, bool visible_only,
    const std::function<void(QAction* action, const QString& top, const QStringList& submenus)>& visit);

struct MenuCommandEntry {
  QPointer<QAction> action;
  QString name;  // mnemonics and the trailing ellipsis removed
  QString path;  // "Filter › Blur": the menus the command sits in; "Tools"; empty for a key outside them
  QString description{};  // optional short "what it does" line, searched and shown on the row
};

// What the command search lists: every enabled, visible menu-bar command, in menu order.
// A command reachable from two menus is listed once, at its first place.
[[nodiscard]] std::vector<MenuCommandEntry> collect_menu_commands(const QMenuBar* menu_bar);

// collect_menu_commands, each with its description (command_search_description, else the
// action's status tip when it says more than the name), followed by the registered commands
// that are in no menu: the tools as "Tools" in palette order, the other keyboard commands
// (Color, Brush, Blend Mode keys) after them. Disabled or unnamed commands are left out.
[[nodiscard]] std::vector<MenuCommandEntry> collect_search_commands(const QMenuBar* menu_bar,
                                                                    const HotkeyRegistry& registry);

// The translated short description written for the hotkey command `id` (GitHub issue 554
// follow-up), or an empty string.
[[nodiscard]] QString command_search_description(const QString& id);
// Every hotkey command id with a written description, for the test that checks each one
// still names a registered command.
[[nodiscard]] QStringList command_search_described_ids();

// Indexes into `entries` of the commands that match `query`, best first. Every
// whitespace-separated word must appear (case-insensitive) in the name or the path, or start
// a word of the description. A name (or description) equal to the query comes first, then one that starts
// with it, then the rest; each group keeps the entries' order (menu bar order, then tools,
// then keys outside the menus). An empty query lists everything in that order.
[[nodiscard]] std::vector<int> match_menu_commands(const std::vector<MenuCommandEntry>& entries,
                                                   const QString& query);

// Opens the command search popup (a Qt::Popup frame, deleted on close; objectNames
// commandSearchPopup, commandSearchEdit, commandSearchList) at the global point `top_left`,
// kept on the screen. Up/Down move, Enter or a click runs the chosen command after the popup
// has closed, Esc closes it.
QFrame* show_command_search_popup(QWidget* parent, std::vector<MenuCommandEntry> entries, QPoint top_left);

// The menu bar's search button (objectName menuBarCommandSearchButton): shows `action`
// (icon and tooltip) and sits right after the last menu title, hidden when it would not
// fit before the window controls or when the menu bar is native.
QToolButton* add_menu_bar_search_button(QMenuBar* menu_bar, QAction* action);

}  // namespace patchy::ui
