# Layers panel

Read this before changing layer rows, thumbnails, click selection, the disclosure arrow, the visibility eye, or the blend and opacity row. Generic item-widget row rules (selection painting, transparent containers, `bind_widget_text`) stay in [ui-conventions.md](ui-conventions.md). Shape and style editors opened from a selected row or badge keep an existing multi-selection; an unselected row becomes the sole target ([batch-appearance.md](batch-appearance.md) has the matching rules).

## Row styling

Layer rows use dynamic properties and application rules such as `QWidget#layerRowWidget[layerRowSelected="true"]`; repolish after property changes. The `layerTargetActive` pattern is the reference. `ui_layer_row_selected_highlight_paints` pins the rendered colors.

## Thumbnails

In document-mapped mode (the zoom preference below turned off), layer-panel previews show the whole document, shaped like it and centered in a fixed slot (Photoshop): a layer smaller than the canvas sits at its own position inside that rect with checkerboard around it. `thumbnail_tile_size` and `thumbnail_preview_space` in `src/ui/main_window_layer_panel.cpp` do the fitting and the document-space mapping for the content, mask, vector-mask, and Smart Filter mask previews, so the previews in one row share a shape. Folder, text, and adjustment thumbnails are glyphs drawn against fixed 28px coordinates and stay square. Because the tile depends on the canvas extent, `LayerThumbnailCacheEntry` keys on it alongside the layer revision and `refresh_layer_thumbnails` restamps every row when it changes. `layerTargetActive` frames the slot rather than the pixmap, so switching the edit target is a property flip with no thumbnail rebuild (`ui_layer_thumbnails_preview_the_whole_document`). The `view/zoomLayerThumbnailsToContent` preference (default on, Preferences > Application) swaps the document space for a per-layer crop: the visible-alpha extent (revision-cached in core), else the declared pixel bounds, else the raster mask bounds. `MainWindow::layer_thumbnail_crop` gates it; every preview in a row shares the crop, glyph thumbnails stay exempt, and toggling the preference clears `layer_thumbnail_cache_` because the mode is a shape input the revision keys do not track (`ui_layer_thumbnails_zoom_to_content_preference`).

## Rebuilds and absent rows

`MainWindow::refresh_layer_list` (main_window_layer_panel.cpp) rebuilds in
passes: configure every `QListWidgetItem` while still parentless, insert ALL
items, then attach the row widgets. The order is load-bearing twice over: a
`setData`/`setToolTip` on an inserted item emits a model `dataChanged` the view
answers with layout work, and `setItemWidget` registers a
persistent editor index that every LATER model insert pays an update walk over,
which makes interleaved insert-and-attach quadratic in row count. Never mutate an inserted
item mid-rebuild and never attach a row widget before the last item is in.
Profiling knobs and the `layerpanel` / `manylayers` perf scenarios live in
[performance.md](performance.md).

New sessions build rows once. Their row-attachment callback pumps paints/timers
with input excluded and the preview edit lock held; recursive rebuilds are refused.
Slow setup shows an opening dialog while the first-render spinner animates. Hide the
welcome panel before inserting the tab. `ui_large_document_session_keeps_loading_responsive`
pins spinner frames during row construction; the recent-file open test pins one rebuild.

The Layer Style dialog's CANCEL path deliberately skips `refresh_layer_list`: it
restores the exact pre-dialog state, so only the previewed layer's thumbnail revision
moved (`refresh_layer_thumbnails` + `refresh_layer_controls` cover it). Committing
keeps the full rebuild.

During guarded automation, script-originated rebuilds call `refresh_layer_list(true)` to capture old row widgets before clearing the list and deliver their deferred deletion after detachment. Manual callbacks and editable pauses use ordinary deferred deletion to preserve the current input receiver's lifetime. `ui_mcp_layer_rows_stay_bounded_during_long_script` pins the live-widget bound during execution.

Canvas-driven selection must not rebuild rows that already exist. `reveal_layer_in_layer_list` (Move-tool auto-select, `active_layer_changed_callback`) and `select_layers_in_layer_list` (rectangle and modifier selection) call `refresh_layer_list` only when a collapsed ancestor has to open, the name filter has to clear, or a target has no row; otherwise they select the existing row (`layer_row_item`) and let the ordinary selection handler run. Pinned by `ui_move_auto_select_click_keeps_existing_layer_rows` (a click on a child of a collapsed folder still rebuilds and reveals it). Press-phase profiling for the Move tool is in [performance.md](performance.md).

Row masks: `LayerListWidget::update_row_viewport_masks` (run on scroll, resize, scroll-range and value changes, and focus changes) masks the rows that sit under the raised scroll bars. The scroll-bar rects are mapped through global coordinates because the bars live in QAbstractScrollArea's own container widgets, siblings of the viewport rather than ancestors of the rows. Only rows intersecting the viewport are touched; rows scrolled in later get their mask from that scroll's pass. Pinned by `ui_layer_list_row_masks_map_scroll_bars_without_warnings`.

The layer list may omit rows entirely: collapsed folders and the Layers panel name filter (`layerNameFilterEdit`) both rebuild without rows for excluded layers. Never assume every document layer has a row, and never introduce a "row exists but hidden" state; absent rows are the single not-shown state all consumers are hardened for. While the name filter is active, `LayerListWidget::set_drag_blocked` refuses drag reordering because a reorder would silently move filtered-out layers; each refused attempt reports through `show_status_error` and leftover held-button moves are swallowed so the base view cannot start a drag-selection sweep.

## Blend and opacity row

Dragging the "Opacity:" or "Fill:" prefix scrubs the value (`install_prefix_scrub`, [ui-conventions.md](ui-conventions.md) Options bar): the drag ends with `editingFinished`, so `finish_pending_layer_opacity_edit` closes it as one undo entry; a click on the prefix focuses the field with the number selected. Combos from `add_blend_mode_items` also step with Left/Right. Panel blend changes merge into one undo entry until a pause or another history change (`ui_layer_blend_mode_steps_coalesce_into_one_undo_entry`).

## Click selection

Layer-row click selection (`LayerListWidget::eventFilter` for row widgets, `viewportEvent` for bare viewport; keep the two branches in step): plain click selects one layer (collapse from a multi-selection is deferred to release so drags work), Ctrl-click toggles the row, Shift-click selects `currentRow()`..target replacing the selection, and Ctrl+Shift-click selects the same range but adds it to the existing selection (`select_range_to_item`'s `additive` flag, Explorer/Photoshop style). The Ctrl branch is tested first, so a Ctrl-click on a thumbnail always loads the layer pixels as a selection, with or without Shift.

### Move tool on the canvas

Ctrl+click (Command on macOS) selects only the clicked layer and Shift+click toggles it, with Auto-Select on or off (#73, Ctrl too); a held Ctrl shows the hover outline with Auto-Select off. Alt-drag duplicates the dragged selection roots on the first drag frame and moves the copies, one "Duplicate layer" step, never on a bare Alt+click (#69). Auto-Select persists (`tools/moveAutoSelect`). With Auto-Select on, a plain click selects only the clicked leaf (a selected member or folder included); collapse waits for release so dragging a selected member still moves the whole set. Shift-drag adds an unselected target before moving the enlarged selection, keeps a selected one, and constrains movement to an axis. Ctrl-drag always draws a layer-selection rectangle, over artwork and with Auto-Select off. With Auto-Select on, dragging empty space also draws a rectangle, the pasteboard outside the selected transform box included; a position-locked Background is empty space. "Empty space" means outside every movable layer's Move rect (`move_layer_outline_bounds`: the opaque raster extent, or a text layer's frame): `CanvasWidget::topmost_move_layer_at` runs three passes (Photoshop parity): a visible pixel under the point, a selected layer whose rect contains it, the topmost such layer, so a press on a transparent pixel inside an outline grabs it. The hover outline and the lock status message share the lookup; the right-click layer menu lists only layers with real pixels under the pointer, and the pasteboard picks like the canvas: a layer outside the document is outlined, grabbed and listed there (`ui_move_tool_outlines_and_moves_off_canvas_layer`). Ctrl bypasses passive transform handles; rulers, guides, panning and transform sessions keep priority. Pinned by `ui_move_tool_grabs_transparent_pixel_inside_layer_rect`, `ui_move_tool_prefers_selected_layer_rect_over_topmost_rect`.

With Auto-Select off, a plain drag anywhere in the document workspace moves the selected movable layers, even when their pixels and transform box are entirely offscreen. Passive controls never consume the first drag outside their box. With Auto-Select on and Show Transform Controls enabled, the selected box's off-canvas interior moves the selected set, including folders and multiple layers, before the empty-space rectangle path. Its Move cursor and resize/rotate handles stay usable outside the document. Ctrl-drag, guide handling, panning, locks, and active transform sessions retain their existing priority. Manual moves preserve off-canvas coordinates. Tests: `ui_move_auto_select_off_recovers_offscreen_layer_from_anywhere`, `ui_move_auto_select_on_drags_off_canvas_box_interior`, `ui_move_off_canvas`.

Rectangle intent and Shift-add mode latch at press; a plain rectangle replaces the selection, Shift adds. Matching runs once on release through the const layer tree, using cached Move outline bounds, clipped to the document. Any positive overlap selects an eligible leaf, occluded leaves and children of collapsed folders included; hidden, zero-opacity, position-locked and non-movable layers are skipped, group restrictions inherited. The active layer stays active if it remains selected; otherwise the topmost match does. Toggling the last selected layer keeps it. With Auto-Select on (or Ctrl held), a plain click on empty space or on the pasteboard that never becomes a rectangle, and an in-document rectangle that matches nothing, both deselect every layer (`CanvasWidget::request_layer_deselection`); Shift-clicks and Shift-rectangles keep the selection, and a position-locked Background counts as empty space, so clicking it deselects. A rectangle drawn entirely outside the document keeps the selection: matching is clipped to the document, so the box may enclose off-canvas artwork the matcher cannot see. Escape, focus loss, tool/document changes and edit locking discard pending gestures. Layer selection changes neither pixel selections nor history.

The canvas requests panel selection changes through `CanvasWidget::set_layer_selection_requested_callback` -> `MainWindow::select_layers_in_layer_list` (the panel stays the source of truth and pushes the result back via `set_selected_layer_ids`); `activate_layer`'s single-id path still collapses to one row by design. The host reveals selected children of collapsed folders and clears a name filter that would hide selected layers. Rectangle drawing and pending modifier clicks live in `canvas_widget_move.cpp`; tests use the `ui_move_` filter.

### Deselected state

"No selected rows and no active layer" is a resting state. `MainWindow::deselect_all_layers` (Select > Deselect Layers, hotkey id `select.deselect_layers`, no default shortcut) clears the panel selection model under a `QSignalBlocker` (`QItemSelectionModel::clear()` emits `selectionChanged` before dropping the current index), calls `Document::clear_active_layer`, resets the canvas edit target to Content, and pushes an empty selection to the canvas; `refresh_layer_list` keeps the state because a null active layer selects no row. A plain Escape reaches it from the canvas (the last branch of `CanvasWidget::keyPressEvent`, so every cancel above it keeps priority and a live drag keeps the selection) and from the focused layer list (`LayerListWidget::set_escape_callback`). The canvas signals the host with an empty id list; `select_layers_in_layer_list` treats empty ids as deselect-all. Deselect writes no history entry; undo restores the earlier active layer (snapshots carry it). A click on the panel's blank area empties the list selection while Qt keeps the current row; `set_active_layer_from_selection` treats that empty selection as deselect-all too, so the active layer and the Move tool's transform box go with it. Tests: `ui_move_escape_deselects_layers_without_gesture`, `ui_move_empty_click_and_rectangle_deselect_layers`, `ui_move_deselect_layers_clears_panel_rows_and_active_layer`, `ui_layer_panel_blank_click_deselects_and_hides_transform_box`.

One-layer documents: with no active layer, a command or tool that needs one selects the only layer (`only_layer_id`). Commands call `MainWindow::select_only_layer_if_none_active` right before reading their target (after any Quick Mask or channel-view refusal), new layer commands too; canvas tools get it from `begin_edit`, the reporting `can_begin_pixel_edit`, Magic Wand, Quick Select and the Auto-Select-off Move drag. Delete Layer and the panel's opacity, blend and lock controls are left alone on purpose. Test: `ui_move_deselected_only_layer_is_selected_on_demand`.

### Range selection and the selected count

Range selection normalizes selected ancestors with one const tree traversal
(`root_drop_layer_ids`), preserving requested order and rejecting missing ids.
Thumbnail target styles repolish only when their active state changes. Slow selection
updates show the delayed **Selecting layers...** canvas message. The selection handler
and single-layer reveal path report the selected layer count in the status bar for
canvas clicks, rectangle and panel selection. The count is one const traversal that
touches neither selection nor history: a selected folder counts itself and every
descendant (nested folders and hidden, locked, collapsed, or filtered-out layers
included), a selected parent and child never count a layer twice, and expanding a
folder cannot change it (`ui_layer_selection_count`).

## New adjustment layers

Clipping controls and row badges use `effective_clip_base`: pixel layers and
folders can host a clipped run. Adjustments clipped above a folder affect its
merged content. The folder itself cannot be a clipped member.

Every New Adjustment Layer entry inserts directly above the topmost selected row. A
selected child keeps the adjustment in that child's folder; a selected folder places it
above the folder. With no selected rows, the active layer is the anchor, falling back to
the document top.
Live previews use the same placement and preserve the original active layer.
Accepting selects the new adjustment and records one undo step; cancelling
removes the preview without changing the selection or history.

New adjustments always carry an enabled raster mask. An active pixel selection
supplies its coverage, with black outside the selection; otherwise the mask
covers the canvas in white and defaults to white outside it. Previews and
committed layers share it in `main_window_adjustments.cpp`.
Tests: `ui_adjustment_layer_inserts_above_selection_with_white_mask`,
`ui_adjustment_layer_preview_uses_topmost_selection_and_cancels_cleanly`, and
`ui_hue_saturation_creates_masked_adjustment_layer` (selection masks).

## Move-tool layer menu

A right-click on the canvas with Move active opens `canvasContextMenu` on
release (the right button never pans; the Move-tool layer entries are the first
section of the shared canvas context menu, see "Canvas right-click menu" in
[tools.md](tools.md)). It lists the hit leaf layers from top to bottom, including occluded
layers and children of collapsed or filtered folders. Folder paths distinguish
nested names. Picking a row replaces the layer selection; **Select All Layers
Here** appears for multiple hits and selects them with the topmost active. Works
with Auto-Select off; locks do not block explicit selection.

Hit testing reads the const tree once, using raster alpha and masks or the text
rectangle. Hidden and zero-opacity trees, transparent pixels, and masked-out
folder contents are excluded. Empty canvas space opens no menu. Selection uses the
rectangle-selection panel callback (rows revealed, count updated, no pixel or history edits).

Crossing Qt's drag threshold cancels the pending menu even when the pointer returns to
its start. Rulers, tablet-button actions, Space/middle-button panning, and active
transform sessions keep their handling. Tool/document changes and edit locks close the
popup; focus loss cancels a pending click. The `ui_move_layer_menu` tests pin it.

## Disclosure arrow, double-click, visibility eye

The folder disclosure arrow (`layerFolderDisclosureButton`) toggles one folder, Alt-click also toggles the folders nested inside it, and Ctrl+Alt-click sets every folder in the document to the clicked folder's toggled state (`MainWindow::toggle_all_layer_folders_expanded`, Photoshop's binding). `QToolButton::clicked` carries no modifiers, so a `ClickModifierRecorder` event filter stores them off the button's own mouse events; the handler defers through `QTimer::singleShot(0)` because the toggle rebuilds the rows. Runtime expand state is the session's `collapsed_layer_groups` set; layer metadata only seeds it at open.

Double-clicking a row opens its editor (layer styles, or an adjustment's settings; shape rows open layer styles too; the vector badge opens Shape Appearance), except on the name label and the content thumbnail. Every row button (eye, mask link, the fx, smart-object, vector and clipping badges, the Smart Filter buttons) must be listed in `layer_row_button_owns_clicks` in `layer_list_widget.cpp`, or the list eats the press as a row drag start and routes the double-click to the row's editor (`ui_layer_vector_badge_double_click_opens_shape_appearance`). The name's text extent edits in place (Photoshop; empty space right of a short name opens the editor): `LayerListWidget::begin_inline_rename` swaps `layerRowName` for a `QLineEdit` (`layerRowNameEdit`) in the label's slot at its height and font (the row does not move). Return and focus loss commit, keyed by layer id so a click on another row still renames the edited one; Escape cancels before the list's Escape-to-deselect; the commit runs deferred through `MainWindow::apply_layer_rename` (one undo step; empty or unchanged names record nothing). `refresh_layer_list` cancels an open editor before clearing rows; a queued commit is dropped when the list's session stamp changed (ids restart per document). Rename Layer (menu, context menu, footer button, F2) enters the same editor when the active layer's row exists and is the only selected one, else the rename dialog (`ui_layer_row_name_double_click_renames_inline`, `ui_layer_rename_hotkey_enters_inline_edit`, `ui_layer_inline_rename_drops_commit_after_document_switch`; dialog-opening tests double-click `layerRowDetails`). The content thumbnail routes to `MainWindow::zoom_canvas_to_layer_content`, which fits the view to the layer's alpha-trimmed bounds (`move_layer_outline_bounds`, else `layer_render_bounds`, a folder's descendant union) via `CanvasWidget::zoom_to_document_rect`. Text thumbnails enter inline editing with all text selected and preserve zoom and pan (the callback defers until the thumbnail event returns and checks the session); adjustment thumbnails open their settings (`ui_layer_thumbnail_double_click_zooms_canvas_to_layer`).

The visibility eye (`layerVisibilityCheck`) does not toggle through its QToolButton on real mouse input: `LayerListWidget::eventFilter` eats the press first. A plain press toggles on press and starts a visibility sweep (dragging along the eye column paints the first toggle's state across crossed rows, skipping disabled eyes; `setSelection` is suppressed while sweeping so the drag cannot rubber-band the selection). The sweep explicitly grabs the mouse on the persistent viewport: toggling a folder rebuilds every row and destroys the pressed eye, losing Qt's implicit grab. Release, including outside the list, ends the grab; losing capture, hiding, disabling, or deactivating the viewport cancels the sweep. `ui_layer_eye_sweep_survives_folder_row_rebuild` must route input through Qt's window system (direct viewport moves cannot detect the lost-capture regression). An Alt press calls `MainWindow::isolate_layer_visibility`, which hides every other layer and restores the per-session snapshot on the second Alt-click; any outside visibility change invalidates the snapshot and the next Alt-click isolates fresh. Both handlers defer through `QTimer::singleShot(0)` because folder toggles rebuild the rows, and the button's `toggled` connection remains for programmatic `click()` (tests rely on it). Visibility stays non-undoable. Plain eye changes queue a coalesced immutable-snapshot render and keep the previous canvas frame visible, so rendering cannot interrupt the sweep. Inherited folder-control states still rebuild through the normal row path. See [interactive-previews.md](interactive-previews.md#visibility-render-waits).

## Add Layer Mask button

The footer's `layerAddMaskButton` runs `MainWindow::add_layer_mask` (reveal-all, or from the active selection). It is deliberately NOT a registered document widget: `refresh_add_layer_mask_button_state` owns its enabled state from `can_add_layer_mask` (exactly one selected layer that is the active one, kind pixel/adjustment/group, no raster mask yet, image pixels unlocked, no preview-dialog lock), called from `refresh_layer_controls` and `update_document_action_state`. Pinned by `ui_layer_add_mask_button_tracks_selection_and_adds_mask`. The footer button row (sizes in `main_window_docks.cpp`) must stay within 340 px; a wider row raises the dock's minimum width and breaks `ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel`.

## Animation preview

The film button (`layerAnimationButton`) toggles `AnimationPreviewWindow` (`src/ui/animation_preview_window.{hpp,cpp}`, the TilePreviewWindow pattern). Play cycles initially visible top-level layers from top to bottom, hiding the others. A trailing seconds token overrides the default; the spin edits shared `saveOptions/animationFrameDelayMs` with three decimal places and rounds the legacy GIF default to centiseconds ([animation.md](animation.md)). Stop, closing the preview, a tab switch, or document close restores captured visibility. MainWindow calls `stop_playback_for` in `activate_document_canvas` while the outgoing document is active and early in `close_document_session`, before the save prompt. Playback mutates visibility without undo or dirtying the session. Per-frame updates use `sync_layer_row_visibility_indicators`; a full `refresh_layer_list` runs once at stop. Tests: `ui_animation_preview_plays_visible_layers_and_restores`, `ui_animation_preview_stops_on_tab_switch_and_close`.

The panel's "Selected layers" row edits the name tokens: Set Time renames the selected (else active) layers to end with the row's own time spin (replacing any existing token, via `gif::strip_layer_name_delay_token` plus `format_delay_seconds_token`), Remove strips the token, and a name that is nothing but a token keeps it. Both go through `MainWindow::set_selected_layers_frame_time` as one undo snapshot ("Set frame time"/"Remove frame time"), and the panel stops playback (with restore) before invoking it so the snapshot never captures a preview frame's visibility; a no-op click pushes nothing and reports through `show_status_error`. Pinned by `ui_animation_preview_sets_and_removes_frame_time_names`.

## Drag to another document

Dragging rows out of the panel onto another open document copies the layers there. The
drag's mime data carries the layer ids (`application/x-patchy-layer-ids`) plus the source session as `pid:session_id`
(`application/x-patchy-layer-source-session`, written by `LayerListWidget::mimeData` from the
id `refresh_layer_list` stamps on the list) because layer ids restart per document, and a
drag from another Patchy process never matches. `startDrag` offers `Copy | Move` so the
cursor shows the copy badge over a foreign document; the panel's own reorder and the
footer buttons keep forcing Move. Drop targets are every session canvas (a tab page or a
float) and the document tabs, all handled by
`MainWindow::handle_cross_document_layer_drag_event` from the app event filter, ahead of
the tab-bar tear-off branch: the tab bar has `setAcceptDrops` so it becomes the DnD
receiver, and non-layer mime stays unaccepted so file drops still propagate to the tab
widget. A drag over its own document, a missing or foreign source token, a gap between
tabs, or the preview-dialog edit lock refuses the drop (no drop cursor, and the file-drop
handlers never see a layer drag); the hovered tab lights through the shared tab-strip
highlight (`show_tab_strip_highlight`, the float-docking overlay).

The drop returns before any document work: the payload (ids, session ids, drop point,
Shift) is copied out and `duplicate_layers_to_session` runs from `QTimer::singleShot(0)`
(the source panel's `QDrag::exec` is still on the stack and the copy rebuilds its rows).
`copy_layers_between_sessions` (main_window_layer_ops.cpp; `copy_layers_between_documents`
is its Document-level core, shared with Files as Layers) backs the dialog and scripts
too: it builds the payload Edit > Copy builds (root ids, referenced
smart-object sources, Smart Filter records, pattern tiles), validates the Smart Filter
caches against the target, runs the caller's `before_mutation` hook (the UI pushes the
target's "Duplicate layer" snapshot there), clones every root with
`clone_layer_tree_with_document_ids` BEFORE inserting anything (there is no
session-targeted undo to roll back a half-inserted stack), keeps the source names (a
collision earns the copy suffix), applies one shared placement offset, re-bakes vector
rasters against the target canvas, and inserts the stack directly above the target's
active layer in source order, making the topmost copy active. Placement: a canvas drop
centers the copied set's movable extent on the drop point; Shift-drop, a tab drop, the
dialog and scripts keep the source coordinates when the documents share dimensions and
center on the target canvas otherwise. A root whose Smart Filters depend on position keeps
its coordinates (the adopted cache would go stale; re-rendering it is a follow-up). Linked
masks move inside `translate_moved_layer_metadata`; unlinked raster and vector masks are
shifted explicitly so the copy stays one unit. The UI wrapper then refreshes the target
canvas, activates the target session and selects the copies. `ui_layer_drag_*`
(layer_panel_organization_tests_cross_document.cpp) and
`ui_layer_drag_to_float_canvas_centers_at_drop_point` (float_window_tests.cpp) pin it;
`send_layer_drop_to_widget` synthesizes the drag.

Duplicate Layer (`layerDuplicateAction`, the footer button and its drop target) runs
`MainWindow::duplicate_layers`: the copies land as one block directly above the topmost
selected root, in its parent and in source order (Photoshop),
and become the selection. `ui_duplicate_layer_inserts_copies_above_source` pins it.

Inside the panel, an Alt-drop duplicates instead of moving (Photoshop's Alt-drag):
`LayerListWidget::dropEvent` records `LayerDropRequest::copy` from the event's
modifiers (the enter/move handlers report CopyAction so the cursor shows the badge), and
`MainWindow::duplicate_layers_for_drop` clones each dragged root above its source, then
runs `move_layers_for_drop` on the CLONES, so the originals never move; the copies become
the selection under one snapshot. `ui_layer_alt_drag_duplicates_in_panel` pins it.

The same core backs Duplicate Layer to Document... (`layerDuplicateToDocumentAction`, hotkey
id `layer.duplicate_to_document`, in the layer context menu and added to the window itself
because the Layer menu's row count is pinned): its dialog (`duplicateLayerToDocumentDialog`)
takes a name for a lone copy (`duplicateLayerNameEdit`) and a destination
(`duplicateLayerTargetCombo`: every other open document, then New Document, a fresh
session the source's size and print resolution), always with keep-position placement.
The scripting API's `layer.duplicate(targetDocument)` routes through
`ScriptEngineHost::duplicate_layers_to_session`, so the run's own snapshot covers the
target and no activation happens. `ui_duplicate_layer_to_document_dialog_copies` and
`ui_script_layer_duplicate_to_document` pin them.

## File drops onto the panel

OS files dragged onto the list become layers of the active document (Files as Layers, [import.md](import.md)). `dragEnterEvent` asks the owner's `set_file_drop_paths_callback` once per drag and refuses a drag with neither layer ids nor usable files, so a text drag can no longer reach the selected-rows reorder fallback. File drags show the normal insertion preview with CopyAction; the drop records a `LayerFileDropRequest` (paths plus the `drop_target_at` result) that `MainWindow::handle_layer_drop` takes ahead of the layer request and hands to `add_files_as_layers_interactive` (plain opens when no document is active).
