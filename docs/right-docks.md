# Right panel column

The right column holds Layers, Channels and Paths (one tab group), then History, Properties, Info and Palette. It is a `PanelColumn` (`src/ui/panel_column.hpp/.cpp`) that Patchy owns: a vertical `QSplitter` of sections, each a title row (collapse toggle and title) over its panel. Nothing in it is a `QDockWidget` and nothing floats; a panel can only move up or down within the column (see "Reordering"). The central widget is the `workspaceSplitter` (`QSplitter`): the document tabs (`documentTabs`, stretch 1) and the column (stretch 0). Tests reach the tabs with `findChild<QTabWidget*>("documentTabs")`, never through `centralWidget()`. `create_docks` (`src/ui/main_window_docks.cpp`) builds each panel and hands it over as a `PanelSpec`.

Panel ids are the old dock object names (`layersDock`, `channelsDock`, `pathsDock`, `historyDock`, `propertiesDock`, `infoDock`, `paletteDock`). They name the panel's widget, and the saved layout stores them, so they never change. A title row is `<prefix>DockTitle` with `<prefix>DockTitleLabel` and `<prefix>DockCollapseButton` (`dockCollapseButton` property). The theme, the MCP allowlist and the tests rely on these names. The tab group's section is `panelTabGroup`, its bar `panelTabGroupTabBar` (`drawBase(false)`, so no Fusion base line: GitHub issue 123, `ui_dock_tab_bars_draw_no_native_base`). `MainWindowTestAccess::panel_column`/`panel_section`/`show_panel` reach it from tests.

## Width

- **The edge.** The column's draggable edge is the workspace splitter's handle, named `rightDockResizeHandle` (theme and MCP selectors). `apply_right_column_width` sets it exactly with `setSizes`. The column's stretch factor is 0, so it keeps its width as the window resizes. Do not reintroduce a fixed-width widget or an overlay handle.
- **Widths.** `update_right_dock_minimum_width` measures two. The content-fit width `right_dock_minimum_width_` takes each wrapping Layers row at its `comfortable_width()` (the footer buttons on one row, or the blend row at its wrapped width; see [layer-panel.md](layer-panel.md)) plus the column chrome, never below `kRightDockMinimumWidth`. The compact width `right_dock_compact_width_` is the panel layout's minimum, every row fully wrapped, plus the chrome (GitHub issue 199): between the two the rows reflow, and only below the compact width does the panel scroll sideways (`ui_layer_panel_compact_width_keeps_controls_whole`). Channels, Paths and the other panels are not measured. The chrome is the column minus the Layers panel's scroll viewport, measured once laid out (on the column's first show, through `handle_right_dock_resize_event`, and lazily). The panel can be wider than the viewport, so never measure against the panel. Never add a competing hardcoded width.
- **Opening width.** Until the user or a script picks a width (`right_dock_width_follows_minimum_`), the column sits at the content-fit width and follows it when it changes (language switch, restyle), so it opens with no dead strip beside the Layers footer in every language (GitHub issue 96, `ui_right_dock_opens_at_content_width_without_gap`).
- **Picked widths.** `patchy.ui.setSidePanelWidth` goes through `set_right_dock_stack_width`, which clamps to `kRightDockFloorWidth` (120) .. window width - 260 and keeps the result, narrower than the content included; it never snaps. A drag of the edge lands in `handle_right_column_moved` (`splitterMoved`). Within `kRightDockSnapDistance` (10 px) of the content-fit width it snaps onto it and follows that width again. Otherwise the dragged width is kept as picked (`ui_right_dock_drag_snaps_to_content_width`). It stays usable while a non-modal preview dialog is open; modal windows block it, as Qt blocks all main-window input (`ui_right_dock_drag_works_during_preview_dialog`).
- **Overflow.** Every panel but Properties (already a `QScrollArea`) sits in a `RightDockOverflowArea` (`rightDockOverflowScroll`). Below a panel's minimum width the panel keeps that width and scrolls sideways, with the bar at the bottom. The area never scrolls vertically: it hands the panel the viewport's full height, and its minimum height grows by the bar while it shows. It places the panel by hand because `widgetResizable` would size the height-for-width Layers panel to its preferred height and clip the footer. Title labels use an `Ignored` width policy, so a long title clips instead of holding the column wide (`ui_right_dock_drags_below_content_minimum_with_scroll`).
- Tests read the content-fit width with `MainWindowTestAccess::right_dock_content_minimum_width` and the narrowest no-scroll width with `right_dock_compact_width`. Keep staged `setSidePanelWidth` values in tests above it unless the test is about scrolling. A splitter handle moves with its drag, so a test drags it with one move, not several in its own coordinates.

## Narrow Layers panel

Three Layers rows wrap instead of clipping (`WrappingControlRowLayout`, `WrappingButtonRowLayout` in main_window_docks.cpp; GitHub issues 96 and 199):
- **Blend row.** Opacity and Fill go under the combo when one line does not fit, then Fill under Opacity (`ui_layer_blend_row_wraps_in_narrow_panel`).
- **Lock row.** The name filter leaves the Lock buttons for a line of its own when its share drops under 90 px (`kLayerNameFilterMinimumWidth`).
- **Footer.** The buttons narrow from their maximum to their minimum width at 3 px spacing (the theme's `layerActionButton` rule sets both; its widths exclude the border), then wrap into balanced rows, four over four.
- **List floor.** `kLayerListMinimumWidth` (256) keeps a top-level row with a mask, its link and two badges whole, so it is the widest item and sets the compact width (268 px offscreen in English, against the 367 px opening width). Narrowing the row itself would lower it.
- **Two widths per row.** `minimumSize()` is the row fully wrapped (it sets the compact width); `comfortable_width()` the row at the opening width (footer on one row, Opacity and Fill on one line under the combo). Nothing changes until the column is narrower than the content-fit width (`ui_layer_panel_compact_width_keeps_controls_whole`).
- **Height.** Wrapped rows raise the panel's minimum height. A growth that pushes the window's minimum past the screen refits the floors (`handle_right_dock_resize_event` watches the panel's layout requests). Only then: the refit also raises the panel's own floor.

## Heights and collapse

Panel heights must never pin the window taller.
- **Collapsed.** A collapsed section is exactly its title row, plus the group's tab bar. Its min == max is set by `apply_section_state`, recomputed on a style or font change (`ui_collapsed_right_docks_have_uniform_title_height`).
- **Freed height.** QSplitter keeps a slot's remembered size after the section's maximum shrinks, which leaves a band under a collapsed header until a divider moves. Every collapse, expand, close and reopen ends in `settle_sizes`, which clamps each slot to its section's limits and gives the difference to the nearest expanded sections, the ones above first (`ui_panel_collapse_and_close_leave_no_gap`).
- **Expanded.** An expanded section has its spec floor: Layers group 300, Channels and Paths 190, History 90 (prefers 190), Properties at most 230, Palette prefers 320. Expanding hands the section its preferred height through `QSplitter::setSizes`, synchronously, taken from the other expanded sections, tallest first and down to their floors (`grant_preferred_height`). There are no temporary minimums and no deferred releases.
- **Tab group.** The group is one section, so it collapses and expands as one: its toggles stay in step, and a background tab's toggle expands the whole group (GitHub issue 101, `ui_tabbed_right_dock_group_collapses_as_one`).
- **First show.** The first show distributes the heights with `apply_preferred_heights`; every section but the group takes its preferred height and the group takes the rest.
- **The fill section.** The first section added (the group, `Section::fills`) has stretch 1 and takes the column's spare height wherever it has been moved; `apply_preferred_heights` gives it the rest and `lower_floors` lowers it last.
- **Dividers.** The dividers between sections are the column splitter's handles and drag (`ui_right_dock_separator_drags_between_docks`).
- **Screen fit.** The window's minimum must fit `available_window_height()` (GitHub issue 108). `fit_right_dock_floors_to_screen` lowers the expanded sections' floors, bottom up and the group last, never below their title row plus 48 (`PanelColumn::lower_floors`). Then, if needed, it lowers the Layers list's 120 px and panel's 240 px floors (the list never below 48). Once shown, it activates the layout so a resize right after sees the lower minimum. It runs from the startup clamp, the column's first show, and every expand or visibility change. Panels that can outgrow their share scroll internally; never give a panel an unbounded minimum height (`ui_right_dock_floors_fit_the_screen_height`, `ui_right_dock_panels_expand_within_window_height`; the Wayland side is in [wayland.md](wayland.md)).

## Showing, hiding and the header menu

- **Showing and hiding.** `PanelColumn::set_panel_visible` closes or reopens a panel. In the group it hides the tab, passes the front to the next open tab, and hides the section once no tab is open. A reopened section takes its preferred height back from its neighbors. Every section always spans the column's full width (`ui_panel_close_and_reopen_keeps_full_width`). The old dock column could re-dock a reopened panel narrower, with the window background beside it.
- **Header menu.** Right-clicking a title row or a tab emits `header_context_menu_requested`, and `show_panel_header_menu` opens `panelHeaderMenu`. It holds Collapse Panel or Expand Panel, then Close Panel (Close Tab Group on the tab bar, which closes every tab of the group), the panel and toolbar toggles, and Reset Panels (`ui_panel_header_right_click_opens_panel_menu`). The palette-mode status chip calls `show_panel("paletteDock")`, and `patchy.ui.expandPanel` does the same by script name ([scripting.md](scripting.md)).

## Reordering

Dragging a title row, or the group's tab bar, up or down moves that section. `PanelColumn` filters the title rows and the tab bar (a press on the label reaches the row; the collapse button keeps its press, and a press on a tab still picks it). Past `startDragDistance` the drag starts: a closed-hand override cursor, the Layers list's insertion line (`panelDropIndicator`, `@drop_indicator_bg`) at the divider the section would land above, and an application event filter so Escape or any other press cancels. A drop above a section's middle lands above it; a drop that changes nothing hides the line. Release moves the section with `move_section_index` through `QSplitter::insertWidget`, each section keeping its height, and emits `section_order_changed`. Sections are named by their first panel's id (`section_order`, `set_section_order`, `move_section`), the group as `layersDock`. Tabs do not move between or out of the group (`ui_panel_title_drag_reorders_sections`).

## Window menu entries

The Window menu lists every panel and both toolbars as checkable entries (`install_window_panel_entries`, GitHub issue 163). `sorted_window_panel_actions` sorts them by their current text with `QCollator` each time the menu opens. The document-window commands sit under Window > Arrange.
- **Panel entries.** An entry follows Photoshop (`toggle_window_panel`): a panel that is open, in front and expanded closes; any other opens, comes to the front and expands (`show_panel`). The check mark is `panel_visible`, re-read on every open.
- **Toolbars.** The toolbars use Qt's own `toggleViewAction`.
- **Reset Panels.** Window > Reset Panels (`window.reset_panels`) ends the list.
- **During automation.** Panel entries and Reset Panels stay usable while MCP automation runs (`McpActivity::action_allowed`).

`ui_window_menu_lists_panels_and_toolbars` covers the entries.

## Saved panel layout

A consented close (`closeEvent`, beside `save_window_geometry`) writes the layout, and the next launch reads it right after the window geometry, before the first show (`save_panel_layout`, `restore_panel_layout`).

The keys are compatibility contracts:
- `window/panelLayoutVersion` (`kPanelLayoutVersion`, now 2). Bump it when panels are added, renamed or regrouped; an older record is then ignored.
- `window/hiddenPanels` (panel ids and the toolbar names `toolPalette`, `Options`).
- `window/collapsedPanels`.
- `window/frontPanel` (the group's front tab).
- `window/panelOrder` (`section_order`, applied before the heights, which are by position; a record without it keeps the default order).
- `window/panelSplitterState` (the column splitter's `saveState`).
- `window/panelColumnWidth` (present only while the width is picked, not following the content).

Offscreen runs neither read nor write the record (`panel_layout_persists`). A UI test that closes a panel must not leak into the next one, and `--headless` or the MCP connector's own window must not change the user's layout. Tests drive it through `MainWindowTestAccess::write_panel_layout`/`read_panel_layout` (`ui_panel_layout_returns_in_the_next_window`).

Window > Reset Panels (`reset_panels`) does the following:
- reopens every panel,
- restores the first-launch order (`default_panel_order_`),
- applies the collapse states captured right after `create_docks` (`default_collapsed_panels_`),
- brings Layers to the front,
- re-adds a moved toolbar to its first-launch area,
- re-runs `apply_preferred_heights`,
- returns the column to the content-fit width.

It re-parents nothing, so the window keeps its size and place. Floors a short screen lowered stay lowered: restoring them would ask for more height than the window has (`ui_reset_panels_restores_first_launch_layout`, `ui_reset_panels_keeps_the_window_geometry`).
