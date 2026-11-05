# Menu and recent-file conventions

Split from [ui-conventions.md](ui-conventions.md): menu-bar and context-menu structure rules, the Open Recent menus and the start panel's recent list, and the recent-file history behind them.

## Menus

- The layer context menu keeps **Edit Layer Styles...** first (`ui_layer_context_menu_keeps_edit_styles_on_top`), except the Background's ([background-layer.md](background-layer.md)).
- `reserve_menu_icon_column` keeps the check-and-icon column (Qt adds it only for an icon or check) in every menu-bar menu (issue 131). `install_menu_separator_click_guard` (app-wide, from MainWindow) keeps a QMenu open on a separator click (issue 317).
- A menu-bar command live with no document is listed in `command_works_without_document`; others `register_document_action` (issue 162).
- Help > Search Commands lists every enabled menu-bar command: a menu that refreshes its commands in `aboutToShow` refreshes them in `show_command_search` too ([command-search.md](command-search.md)).
- The canvas right button opens context menus and never pans ([tools.md](tools.md), "Canvas right-click menu").
- The menu-bar Layer menu groups its new-layer, layer-mask, stacking-order, rotate/flip, visibility and lock, and layer style sets into the `layerNewMenu`, `layerMaskMenu`, `layerArrangeMenu`, `layerRotateFlipMenu`, `layerHideLockMenu` and `layerStyleMenu` submenus (Image has its own `imageRotateFlipMenu`) so the menu fits a short wasm browser viewport; `ui_main_window_renders_color_controls` bounds its direct row count, so add new Layer commands inside an existing submenu or group rather than as top-level rows. The Layer Mask submenu mirrors the layer context menu's item order and separators; keep the two in step.
- New import/export commands go in the `fileImportMenu`/`fileExportMenu` submenus (the title carries the verb), except Export for Web under Save As ([web-export.md](web-export.md)); `ui_file_export_menu_actions_registered` pins both.

## Recent files

- The recent-files filters (home screen and Open Recent) match each space-separated token against the FILE NAME, extension included; `.jpg` or `*.jpg` filters by extension and a token containing a slash matches the folder path (`recent_path_matches`, issue 158).
- Open Recent File/Folder are each a filter row (`fileOpenRecent[Folder]FilterEdit`) over one list (`fileOpenRecent[Folder]List`, `build_recent_list_menu`, 12 rows, capped width so it opens beside File). Rebuilds recreate both (filter resets); `aboutToShow` focuses the edit; the MainWindow event filter forwards arrow/page keys to the list; Enter and clicks go through `open_recent_list_path`, deferred by `QTimer::singleShot(0)` (opening rebuilds the menu mid-delivery). Native menu bars keep a plain paged menu ([platform-sites.md](platform-sites.md)). Tests read entries via `recent_menu_paths`. The start panel's `startPanelRecentList` (8 rows) shares the rows, delegate, matching rule and `show_recent_path_context_menu`; Enter opens its top row; `StartPanel::showEvent` clears the filter and takes focus. No match leaves one placeholder row with an empty `kRecentPathRole` path, which the delegate, click and context-menu handlers test for; never give a real row an empty path.

Recent files and folders record every successful open and save (interactive, scripted,
headless and MCP, flat copies included); saving records the containing folder too.
`recent_history_settings()` shares the persistent history with owned MCP workspaces while
their other preferences stay temporary. Native updates lock the whole read/modify/write
transaction and merge against the latest stored list, so another process's additions or
Clear survive. The File menu and Save As refresh before opening; the empty start panel
refreshes periodically while no popup is active and when it reappears. **A refresh only
rereads settings and never stats an entry** (a cold, spun-down or unreachable volume would
stall startup and every File menu open). Existence checks run on a tracked worker
(`schedule_recent_history_check`) after startup, whenever a stored list changes, and
otherwise at a throttled interval; a result arriving under an open popup waits for the next
refresh. Network paths (UNC, and `DRIVE_REMOTE` drive letters on Windows) are never checked,
and missing entries stay stored so an unplugged drive's entries return. A click on a
vanished entry reports "Recent file is missing" and removes it. Rebuilds occur only when the
displayed list changes, preserving a live start-panel filter. A row's context menu and its
hover x (both lists) drop one entry and keep the list's place (`remove_recent_path`, issue 157).
