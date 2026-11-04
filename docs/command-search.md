# Command search

Help > Search Commands (`help.search_commands`, default Ctrl+F (Command+F) as in Photoshop, with
Ctrl+Shift+/ as a second key; GitHub issue 554) opens a
popup that finds menu-bar commands, tools and keyboard commands by name, menu or description.
Code: `src/ui/command_search.cpp`, `MainWindow::show_command_search` in
`src/ui/main_window_actions_menus.cpp`. Tests: `ui_command_search_popup_filters_and_runs_commands`,
`ui_command_search_lists_tools_keys_and_descriptions`, `ui_command_search_every_command_has_a_description`.

## What it lists

- `collect_menu_commands` walks the menu bar's menus through `for_each_menu_bar_command`, the
  same walk the Keyboard Shortcuts editor uses for its rows. Every enabled, visible command is
  listed once, at its first place in menu order. Separators, submenu entries, widget actions
  (the Open Recent filter rows) and anything inside a hidden or disabled submenu are left out.
- `collect_search_commands` adds what the menus lack, from the hotkey registry: every
  command in the `tools` category (the palette tools and the Cycle commands, in registration
  order) with the path `Tools`, then the other registered commands that are in no menu:
  the `color` and `brush` categories (paths `Color`, `Brush`) and the uncategorized keys
  such as Blend Mode: Normal (no path). Channels, Paths, theme and script commands are left
  out (panel targets, or already in a menu). Disabled and unnamed commands are skipped like
  menu commands.
- Each row shows the menu path in the muted color with its separator (`Filter › Blur › `),
  then the name (mnemonics and trailing ellipsis removed), the current shortcut at the right,
  and a check mark for a checked command. The name keeps its full width; the path elides from
  the left and is dropped when only its ellipsis would fit. The description is the row's
  tooltip only (`Qt::ToolTipRole`), never drawn on the row: on every row it was too much
  (issue 554), and it is still searched.
- Some menus refresh their commands only when they open (`aboutToShow`).
  `show_command_search` runs the same refreshers first (Smart Object, merge, layer color and
  visibility states, the AppImage entry), rebuilds File > Scripts and the Window menu's
  document entries, so the list matches what the menus would show. A new menu that refreshes
  its commands in `aboutToShow` adds its refresher there too. Never emit `aboutToShow` instead:
  some handlers disable commands only while their menu is open.

## Descriptions

Every command the search can list has a short "what it does" line in other words than its
name, so a search by synonym finds it ("resize" finds Image Size).

- `kCommandDescriptions` in `command_search.cpp` maps a command's key to its phrase
  (`patchy::ui::CommandSearch` context, `QT_TRANSLATE_NOOP`, translated in every catalog).
  The key (`MenuCommandEntry::id`, `command_search_id`) is the hotkey command id, else the
  action's objectName; both are permanent. A command without a table line may use its status
  tip, but only when the tip does not contain the name ("Apply Gaussian Blur to the active
  layer" adds nothing).
- Exempt: rows built from data, which no fixed line can describe: recent files and folders,
  the Window menu's open documents, scripts, legacy 8BF plug-ins and that menu's "No plug-ins
  found" and "Scanning plug-in folders..." notes, GEGL filters with their menu's scan notes and
  About GEGL..., and companion apps with their Quit entries and Companions' "No companion apps
  installed" and "Manifests That Could Not Be Read..." entries. Their builders set
  `kCommandSearchDataEntryProperty`; `command_search_exempt` reads it. A new data-built menu
  sets it too. Theme commands and panel commands are not listed, so they need none.
- The rule: a new command gets its description in the same change, like its translation.
  `ui_command_search_every_command_has_a_description` collects every command the search can
  list on this platform (`collect_search_commands` with `unavailable_too`: hidden and disabled
  ones included) and fails naming each one without a description (`missing command
  description: <id> | <path>`) or without an id or objectName. `scripts\update-translations.ps1`
  runs that test from the last built `patchy_ui_visual_tests.exe` and lists the same lines, so
  catalog updates show what is missing. Nothing writes descriptions at build time: the check
  names them, a person or agent writes them.
- `ui_command_search_lists_tools_keys_and_descriptions` fails for a table key that no listed
  command has (a typo, a renamed objectName), except the commands another platform builds
  (the Windows Plugins commands, the Windows and macOS scanner imports, the non-Windows 8BF
  note).
## Matching

`match_menu_commands`: the query splits on spaces and every word must appear, ignoring case,
in the name or the path, or start a word of the description ("move" does not find "Remove
EXIF"). A name or description equal to the query ranks
first, then one that starts with it, then everything else; each group keeps the list's order
(menu bar order, then tools, then other keys), so File results come before Edit results. An
empty query lists everything.

## The popup

- A `Qt::Popup` frame (`commandSearchPopup`, deleted on close) with `commandSearchEdit` and
  `commandSearchList`, themed from `menu_bg`, `menu_border` and `menu_item_selected_bg` like a
  menu. Typing stays in the field: Up/Down/PageUp/PageDown move the list, Enter or a click runs
  the chosen command (a tool row selects the tool), Esc closes. The command is triggered on the next event-loop turn, after the
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
