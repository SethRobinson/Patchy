# Command search

Help > Search Commands (`help.search_commands`, default Ctrl+Shift+/, GitHub issue 554) opens a
popup that finds menu-bar commands by name. Code: `src/ui/command_search.cpp`,
`MainWindow::show_command_search` in `src/ui/main_window_actions_menus.cpp`. Test:
`ui_command_search_popup_filters_and_runs_commands`.

## What it lists

- `collect_menu_commands` walks the menu bar's menus through `for_each_menu_bar_command`, the
  same walk the Keyboard Shortcuts editor uses for its rows. Every enabled, visible command is
  listed once, at its first place in menu order. Separators, submenu entries, widget actions
  (the Open Recent filter rows) and anything inside a hidden or disabled submenu are left out.
- Each row shows the name (mnemonics and trailing ellipsis removed), its menu path in the
  muted color (`Filter › Blur`), the current shortcut at the right, and a check mark for a
  checked command.
- Some menus refresh their commands only when they open (`aboutToShow`).
  `show_command_search` runs the same refreshers first (Smart Object, merge, layer color and
  visibility states, the AppImage entry), rebuilds File > Scripts and the Window menu's
  document entries, so the list matches what the menus would show. A new menu that refreshes
  its commands in `aboutToShow` adds its refresher there too. Never emit `aboutToShow` instead:
  some handlers disable commands only while their menu is open.

## Matching

`match_menu_commands`: the query splits on spaces and every word must appear, ignoring case,
in the name or the path. Names that start with the query rank first, then names holding every
word, then the rest (a word found only in the path); ties keep menu order. An empty query lists
everything.

## The popup

- A `Qt::Popup` frame (`commandSearchPopup`, deleted on close) with `commandSearchEdit` and
  `commandSearchList`, themed from `menu_bg`, `menu_border` and `menu_item_selected_bg` like a
  menu. Typing stays in the field: Up/Down/PageUp/PageDown move the list, Enter or a click runs
  the chosen command, Esc closes. The command is triggered on the next event-loop turn, after the
  popup has closed, so a dialog or menu it opens never competes with the popup's grab; a
  command disabled in between does not run.
- Triggering the action again while the popup is open closes it.
- It opens below the menu bar's search button, or centered under the menu bar when the button
  is hidden, kept on the screen.
- While MCP automation runs, the activity guard lets the menu entry and shortcut open it (a
  `help.` command) but blocks the button, typing and clicks in it, so it cannot run a command
  the guard would refuse.

## The menu bar button

`add_menu_bar_search_button` adds `menuBarCommandSearchButton`, a `QToolButton` whose default
action is the Search Commands action (its `search` icon and its "Search Commands... (shortcut)"
tooltip). It is a child of the menu bar placed right after the last visible menu title, not a
`QMenuBar` corner widget: the custom window chrome positions its own controls at the right
edge. It hides when a title moved into the bar's overflow menu, when it would overlap
`windowChromeControls`, and when the menu bar is native. macOS gets no button (its native Help
menu has its own search); the Help menu entry and shortcut stay. On Windows,
`title_bar_drag_area_contains` excludes the button so `WM_NCHITTEST` leaves its clicks to Qt.
