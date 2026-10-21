# Layers panel

Read this before changing layer rows, thumbnails, click selection, the disclosure arrow, the eye, or the blend and opacity row. Generic item-widget row rules stay in [ui-conventions.md](ui-conventions.md). Artboard rows (glyph, `layerRowArtboard`, drop rules): [artboards.md](artboards.md). Shape and style editors opened from a selected row or badge keep an existing multi-selection; an unselected row becomes the sole target ([batch-appearance.md](batch-appearance.md)).

## Row styling

Layer rows use dynamic properties and application rules such as `QWidget#layerRowWidget[layerRowSelected="true"]`; repolish after property changes (`layerTargetActive` is the reference). `ui_layer_row_selected_highlight_paints` pins the rendered colors.

Hidden rows disable name, details, mask link and clip badge; fx, smart-object and vector badges stay clickable, drawn dimmed by `LayerRowBadgeButton` (`layerRowBadgeDimmed`, issue 230).

Locks: the panel's lock buttons and the row's right-click Lock submenu, the only home of Prevent Auto-Nesting (`layer.prevent_auto_nesting`; #246, [artboards.md](artboards.md)). Badges (`make_layer_lock_badge`) show each set lock; a Background shows one padlock instead ([background-layer.md](background-layer.md)); folders pass on editing locks only.

## Thumbnails

In document-mapped mode (the zoom preference below turned off), layer-panel previews show the whole document, shaped like it and centered in a fixed slot (Photoshop), a smaller layer at its own position with checkerboard around it. `thumbnail_tile_size` and `thumbnail_preview_space` in `src/ui/main_window_layer_panel.cpp` do the fitting and the document-space mapping for the content, mask, vector-mask, and Smart Filter mask previews, so the previews in one row share a shape. Folder, text, and adjustment thumbnails are glyphs drawn against fixed 28px coordinates and stay square. Because the tile depends on the canvas extent, `LayerThumbnailCacheEntry` keys on it alongside the layer revision and `refresh_layer_thumbnails` restamps every row when it changes. `layerTargetActive` frames the slot rather than the pixmap, so switching the edit target is a property flip with no thumbnail rebuild (`ui_layer_thumbnails_preview_the_whole_document`). The `view/zoomLayerThumbnailsToContent` preference (default on, Preferences > Application) swaps the document space for a per-layer crop: the visible-alpha extent (revision-cached in core), else the declared pixel bounds, else the raster mask bounds. `MainWindow::layer_thumbnail_crop` gates it; every preview in a row shares the crop, glyph thumbnails stay exempt, and toggling the preference clears `layer_thumbnail_cache_` because the mode is a shape input the revision keys do not track (`ui_layer_thumbnails_zoom_to_content_preference`).

## Rebuilds and absent rows

`MainWindow::refresh_layer_list` (main_window_layer_panel.cpp) rebuilds in
passes: configure every `QListWidgetItem` while still parentless, insert ALL
items, then attach the row widgets. The order is load-bearing: a `setData`/`setToolTip`
on an inserted item emits `dataChanged` and layout work, and each `setItemWidget`
persistent editor index makes every LATER insert pay an update walk (interleaving
goes quadratic). Never mutate an inserted
item mid-rebuild and never attach a row widget before the last item is in.
Profiling knobs and the `layerpanel` / `manylayers` perf scenarios live in
[performance.md](performance.md).

New sessions build rows once. Their row-attachment callback pumps paints/timers
with input excluded and the preview edit lock held; recursive rebuilds are refused.
Slow setup shows an opening dialog while the first-render spinner animates; hide the
welcome panel before inserting the tab. `ui_large_document_session_keeps_loading_responsive`
pins spinner frames during row construction; the recent-file open test pins one rebuild.

The Layer Style dialog's CANCEL path deliberately skips `refresh_layer_list`: it
restores the exact pre-dialog state, so only the previewed layer's thumbnail revision
moved (`refresh_layer_thumbnails` + `refresh_layer_controls` cover it). Committing
keeps the full rebuild. Artboard edits that keep every layer in place (resize, plain move,
background) restamp thumbnails the same way (`finish_artboard_edit`'s `rows_unchanged`):
row construction and QSS polish were most of each commit on Windows.

During guarded automation, script-originated rebuilds call `refresh_layer_list(true)` to capture old row widgets before clearing the list and deliver their deferred deletion after detachment. Manual callbacks and editable pauses use ordinary deferred deletion so the current input receiver outlives the event. `ui_mcp_layer_rows_stay_bounded_during_long_script` pins the live-widget bound during execution.

Canvas-driven selection must not rebuild rows that already exist. `reveal_layer_in_layer_list` (Move-tool auto-select, `active_layer_changed_callback`) and `select_layers_in_layer_list` (rectangle and modifier selection) call `refresh_layer_list` only when a collapsed ancestor has to open, the name filter has to clear, or a target has no row; otherwise they select the existing row (`layer_row_item`) and let the ordinary selection handler run. Pinned by `ui_move_auto_select_click_keeps_existing_layer_rows` (a click on a child of a collapsed folder still rebuilds and reveals it). Both scroll through `scroll_layer_row_into_view`: a row already wholly on screen keeps the scroll position, an off-screen row is centered, and the badge buttons pass `clicked_in_panel` so a partly visible row under the pointer stays put too (#266, `ui_layer_smart_object_badge_click_keeps_panel_scroll`). Press-phase profiling for the Move tool is in [performance.md](performance.md).

Row masks: `LayerListWidget::update_row_viewport_masks` (on scroll, resize, scroll-range and value changes, and focus changes) masks the rows under the raised scroll bars. The scroll-bar rects are mapped through global coordinates because the bars live in QAbstractScrollArea's own container widgets, siblings of the viewport, not ancestors of the rows. Only rows intersecting the viewport are touched; rows scrolled in later get their mask from that scroll's pass. Pinned by `ui_layer_list_row_masks_map_scroll_bars_without_warnings`.

The layer list may omit rows entirely: collapsed folders and the Layers panel name filter (`layerNameFilterEdit`) both rebuild without rows for excluded layers. Never assume every document layer has a row, and never introduce a "row exists but hidden" state; absent rows are the single not-shown state all consumers are hardened for. While the name filter is active, `LayerListWidget::set_drag_blocked` refuses drag reordering because a reorder would silently move filtered-out layers; each refused attempt reports through `show_status_error` and leftover held-button moves are swallowed so the base view cannot start a drag-selection sweep.

## Blend and opacity row

Dragging the "Opacity:" or "Fill:" prefix scrubs the value (`install_prefix_scrub`, [ui-conventions.md](ui-conventions.md) Options bar): the drag ends with `editingFinished`, so `finish_pending_layer_opacity_edit` closes it as one undo entry; a click on the prefix focuses the field with the number selected. Combos from `add_blend_mode_items` also step with Left/Right. Panel blend changes merge into one undo entry until a pause or another history change (`ui_layer_blend_mode_steps_coalesce_into_one_undo_entry`). Pass Through is offered first, for folders only (`set_blend_mode_pass_through_offered` per active layer, `BlendModeMenu::Folder` in Blending Options), and never lands on a selected non-folder (#235). `refresh_layer_thumbnails` re-syncs each row's mode and opacity line (`layer_row_details_text`, #236). The blend row, the lock/filter row and the footer wrap in a narrow column (#96, #199; [right-docks.md](right-docks.md)).

## Click selection

Layer-row click selection (`LayerListWidget::eventFilter` for row widgets, `viewportEvent` for bare viewport; keep the two branches in step): plain click selects one layer (collapse from a multi-selection is deferred to release so drags work), Ctrl-click toggles the row, Shift-click selects `currentRow()`..target replacing the selection, and Ctrl+Shift-click selects the same range but adds it to the existing selection (`select_range_to_item`'s `additive` flag, Explorer/Photoshop style). The Ctrl branch is tested first, so a Ctrl-click on a thumbnail always loads the layer pixels as a selection, with or without Shift.

### Move tool on the canvas

The Move tool's canvas-side layer selection (Ctrl/Shift/Alt clicks, Auto-Select, the selection rectangle, off-canvas drags) is in [move-tool.md](move-tool.md#layer-selection-from-the-canvas); the panel-side rules follow.

The canvas requests panel selection changes through `CanvasWidget::set_layer_selection_requested_callback` -> `MainWindow::select_layers_in_layer_list` (the panel stays the source of truth and pushes the result back via `set_selected_layer_ids`); `activate_layer`'s single-id path still collapses to one row by design. The host reveals selected children of collapsed folders and clears a name filter that would hide selected layers. Rectangle drawing and pending modifier clicks live in `canvas_widget_move.cpp` (tests: the `ui_move_` filter).

### Deselected state

"No selected rows and no active layer" is a resting state. `MainWindow::deselect_all_layers` (Select > Deselect Layers, hotkey id `select.deselect_layers`, no default shortcut) clears the panel selection model under a `QSignalBlocker` (`QItemSelectionModel::clear()` emits `selectionChanged` before dropping the current index), calls `Document::clear_active_layer`, resets the canvas edit target to Content and pushes an empty selection to the canvas; `refresh_layer_list` keeps the state since a null active layer selects no row. A plain Escape reaches it from the canvas (the last branch of `CanvasWidget::keyPressEvent`: every cancel above it keeps priority and a live drag keeps the selection) and from the focused layer list (`LayerListWidget::set_escape_callback`). The canvas signals the host with an empty id list, which `select_layers_in_layer_list` treats as deselect-all. Deselect writes no history entry; undo restores the earlier active layer (snapshots carry it). A blank-area click empties the list selection (Qt keeps the current row); `set_active_layer_from_selection` treats that as deselect-all too, so the active layer and the Move tool's transform box go with it. Tests: `ui_move_escape_deselects_layers_without_gesture`, `ui_move_empty_click_and_rectangle_deselect_layers`, `ui_move_deselect_layers_clears_panel_rows_and_active_layer`, `ui_layer_panel_blank_click_deselects_and_hides_transform_box`.

One-layer documents: with no active layer, a command or tool that needs one selects the only layer (`only_layer_id`). Commands call `MainWindow::select_only_layer_if_none_active` right before reading their target (after any Quick Mask or channel-view refusal), new layer commands too; canvas tools get it from `begin_edit`, the reporting `can_begin_pixel_edit`, Magic Wand, Quick Select and the Auto-Select-off Move drag. Delete Layer and the panel's opacity, blend and lock controls are deliberately left alone. Test: `ui_move_deselected_only_layer_is_selected_on_demand`.

### Range selection and the selected count

Range selection normalizes selected ancestors with one const tree traversal
(`root_drop_layer_ids`), keeping the requested order and rejecting missing ids.
Thumbnail target styles repolish only when their active state changes. Slow selection
updates show the delayed **Selecting layers...** canvas message. The selection handler
and single-layer reveal path put the selected layer count on the status bar for
canvas clicks, rectangle and panel selection, one const traversal that
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
above the folder. With no selected rows, the active layer is the anchor, else the document
top. Live previews use the same placement and keep the original active layer. Accepting
selects the new adjustment and records one undo step; cancelling removes the preview
without changing the selection or history.

New adjustments always carry an enabled raster mask: an active pixel selection
supplies its coverage (black outside), else the mask is white across the canvas and
outside it. Previews and committed layers share it in `main_window_adjustments.cpp`.
Tests: `ui_adjustment_layer_inserts_above_selection_with_white_mask`,
`ui_adjustment_layer_preview_uses_topmost_selection_and_cancels_cleanly`, and
`ui_hue_saturation_creates_masked_adjustment_layer` (selection masks).

## Color labels

A layer's color label (#244) is `Layer::color_label()`, Photoshop's `lclr` index (0 none, 1 red, 2 orange, 3 yellow, 4 green, 5 blue, 6 violet, 7 gray; constants and the permanent ids in `core/layer.hpp`). It is not an edit to pixels, so it moves no revision, and locked layers take one. The row shows it as `layerRowColorStrip`, a 4 px strip at x 0 of `layerMainRow` inside the base indent, off the layout and transparent to the mouse, created only for a known nonzero label; QSS paints it from the `layer_color_*` theme roles through its `layerColor` property. Layer > Color (`layerColorMenu`, kept inside the Layer menu's 23-row bound) and the row's context menu (`layerContextColorMenu`) share the `layerColor*Action` actions (hotkey ids `layer.color_<id>`), built by `populate_layer_color_actions`; `set_selected_layers_color` labels the selected rows, groups included, as one undo step, and `refresh_layer_color_action_states` checks the common label (none for a mixed selection). Scripts: `layer.colorLabel`. PSD rules in [ps-compat.md](ps-compat.md). A right-click on a row's eye opens `layerEyeColorMenu` with the same actions (Photoshop's short eye menu, #276; `show_layer_eye_color_menu`). Pinned by `ui_layer_color_labels_menu_undo_and_row_strip` and `ui_layer_eye_right_click_opens_color_menu`.

## Background layer

Photoshop's Background rules (stays at the bottom, no layer styles, one padlock badge, the short menu, PSD adoption): [background-layer.md](background-layer.md).

## Disclosure arrow, double-click, visibility eye

The folder disclosure arrow (`layerFolderDisclosureButton`) toggles one folder, Alt-click also its nested folders, and Ctrl+Alt-click sets every folder to the clicked folder's new state (`MainWindow::toggle_all_layer_folders_expanded`). Empty folders toggle too (#229), empty artboards included (`ui_artboard_empty_artboard_disclosure_toggles_and_saves`). A `ClickModifierRecorder` filter records the modifiers (`clicked` carries none); the handler defers (`QTimer::singleShot(0)`): the toggle rebuilds the rows. Runtime expand state is the session's `collapsed_layer_groups`: metadata seeds it at open; saves, recovery and smart-object commits write it back (`DocumentSession::document_for_write`). A toggle is not an edit (Photoshop parity).

Double-clicking a row opens its editor (layer styles, or an adjustment's settings; shape rows open layer styles too; the vector badge opens Shape Appearance), except on the name label and the content thumbnail. Every row button (eye, mask link, the fx, smart-object, vector and clipping badges, the Smart Filter and effect-row buttons) must be listed in `layer_row_button_owns_clicks` in `layer_list_widget.cpp`, or the list eats the press as a row drag start and routes the double-click to the row's editor (`ui_layer_vector_badge_double_click_opens_shape_appearance`). The name's text extent edits in place (Photoshop; empty space right of a short name opens the editor): `LayerListWidget::begin_inline_rename` swaps `layerRowName` for a `QLineEdit` (`layerRowNameEdit`) in the label's slot at its height and font. Return and focus loss commit, keyed by layer id so a click on another row still renames the edited one; Escape cancels before the list's Escape-to-deselect; the commit runs deferred through `MainWindow::apply_layer_rename` (one undo step; empty or unchanged names record nothing). `refresh_layer_list` cancels an open editor before clearing rows; a queued commit is dropped when the list's session stamp changed (ids restart per document). Rename Layer (menu, context menu, F2) enters the same editor when the active layer's row exists and is the only selected one, else the rename dialog (`ui_layer_row_name_double_click_renames_inline`, `ui_layer_rename_hotkey_enters_inline_edit`, `ui_layer_inline_rename_drops_commit_after_document_switch`; dialog-opening tests double-click `layerRowDetails`). The content thumbnail routes to `MainWindow::zoom_canvas_to_layer_content`, which fits the view to the layer's alpha-trimmed bounds (`move_layer_outline_bounds`, else `layer_render_bounds`, a folder's descendant union) via `CanvasWidget::zoom_to_document_rect`. Text thumbnails enter inline editing with all text selected, keeping zoom and pan (the callback defers past the thumbnail event and checks the session); adjustment thumbnails open their settings (`ui_layer_thumbnail_double_click_zooms_canvas_to_layer`).

The visibility eye (`layerVisibilityCheck`) does not toggle through its QToolButton on real mouse input: `LayerListWidget::eventFilter` eats the press first. A plain press toggles on press and starts a visibility sweep (dragging along the eye column paints the first toggle's state across crossed rows, skipping disabled eyes; `setSelection` is suppressed while sweeping so the drag cannot rubber-band the selection). The sweep grabs the mouse on the persistent viewport (toggling a folder rebuilds every row and destroys the pressed eye, losing Qt's implicit grab). Release, even outside the list, ends the grab; losing capture, hiding, disabling or deactivating the viewport cancels the sweep. `ui_layer_eye_sweep_survives_folder_row_rebuild` must route input through Qt's window system (direct viewport moves miss the lost-capture bug). An Alt press calls `MainWindow::isolate_layer_visibility`: every other layer hides, the second Alt-click restores the per-session snapshot, and any outside visibility change invalidates it so the next Alt-click isolates fresh. Both handlers defer through `QTimer::singleShot(0)` (folder toggles rebuild the rows); the button's `toggled` connection serves programmatic `click()`. Visibility stays non-undoable. The G'MIC preview leaves the list in `set_visibility_only` (eyes and scrolling only; [gmic.md](gmic.md)). Plain eye changes queue a coalesced immutable-snapshot render and keep the previous canvas frame visible, so rendering cannot interrupt the sweep. Inherited folder-control states rebuild through the normal row path. See [interactive-previews.md](interactive-previews.md#visibility-render-waits).

## Effect rows

A layer with any effect instance, hidden ones included, lists them under its row like Smart Filters (#256): a "Layer Styles" header (`layerEffectsRow`) with a disclosure arrow (`layerEffectsDisclosureButton`) and the master eye (`layerEffectsVisibilityButton`, `LayerStyle::effects_visible`), then, while open, one `layerEffectEntryRow` per instance in the dialog's category order (`layer_effect_rows`, main_window_shared.cpp) with its eye (`layerEffectVisibilityButton`, the instance's `enabled`), its name and an Edit pencil (`layerEffectEditButton`); a row double-click does the same, opening the Layer Style dialog on that instance without turning it on (`edit_layer_effect`). The list starts closed; `DocumentSession::expanded_layer_effects` remembers open layers for the session only (never the file), a toggle is not an edit, and an effect added by the Layer Style dialog or Paste Layer Style opens the list (`reveal_layer_effects`). With the master eye off, each effect eye keeps its own state but draws dimmed (`text_disabled`, property `layerEffectEyeDimmed`) and still toggles its flag. A right-click on an effect row opens `layerEffectContextMenu` (Edit, Hide/Show, Delete; `show_layer_context_menu` routes it) instead of the layer menu; Delete (`delete_layer_effect`) removes the instance so the saved PSD drops it. Each change is one undo step through `apply_layer_effect_row_edit`, which drops the preserved lfx2 bytes; a locked layer shows the rows read-only with Delete and Hide disabled. Rows are 26 px; the item height is `44 + 26 * (Smart Filter rows + 1 + open effect rows)`. Hidden instances keep the fx badge and survive a PSD save (the `present` rule in [layer-effects-render.md](layer-effects-render.md)). Tests: tests/ui/layer_effect_rows_tests.cpp, `psd_layer_style_hidden_effects_round_trip`.

## Add Layer Mask and Layer Styles buttons

The footer's `layerAddMaskButton` runs `MainWindow::add_layer_mask` (reveal-all, or from the active selection). `layerStylesButton` (#94; the row badge's `badge-fx` art) pops up `layerStylesButtonMenu`: the shared Edit Layer Styles... action (`layerBlendingOptionsAction`), a separator, then one entry per effect in the dialog's category order. An effect entry calls `edit_active_layer_style(LayerStyleEffect)`: the one Layer Style dialog opens on that effect's first row and ticks it the way its checkbox does, so OK keeps it, Cancel restores, and a turned-off instance is turned on instead of duplicated. Neither button is a registered document widget. `refresh_add_layer_mask_button_state` owns the mask button from `can_add_layer_mask` (exactly one selected layer, the active one, kind pixel/adjustment/group, no raster mask yet, image pixels unlocked, no preview-dialog lock); `refresh_layer_styles_button_state` owns the styles button from `can_edit_layer_styles` (a selected or active layer that is not effectively locked, `edit_active_layer_style`'s own filter, no preview-dialog lock). Both run from `refresh_layer_controls` and `update_document_action_state`. Pinned by `ui_layer_add_mask_button_tracks_selection_and_adds_mask`, `ui_layer_styles_button_tracks_selection_and_lists_effects` and `ui_layer_styles_menu_entry_opens_dialog_on_effect`. The footer's one-row width (eight buttons) sets the column's opening width (`ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel`).

## Footer menus

`layerNewAdjustmentButton` lists the fill layers, a separator, then the adjustments, built by the Layer menu's `populate_new_fill_layer_menu` and `populate_new_adjustment_layer_menu` (issue 252). Film button (`layerAnimationButton`): [animation.md](animation.md#animation-preview-window).

## Drag to another document

Dragging rows out of the panel onto another open document copies the layers there. The
drag's mime data carries the layer ids (`application/x-patchy-layer-ids`) plus the source session as `pid:session_id`
(`application/x-patchy-layer-source-session`, from the id `refresh_layer_list` stamps on the list): layer ids
restart per document, and a drag from another Patchy process never matches. `startDrag` offers `Copy | Move` so the
cursor shows the copy badge over a foreign document; the panel's own reorder and the footer buttons force Move. Drop targets are every session canvas (a tab page or a float) and the document tabs,
all handled by `MainWindow::handle_cross_document_layer_drag_event` from the app event
filter, ahead of the tab-bar tear-off branch: the tab bar has `setAcceptDrops` so it
becomes the DnD receiver, and non-layer mime stays unaccepted so file drops still reach
the tab widget. A drag over its own document, a missing or foreign source token, a gap
between tabs, or the preview-dialog edit lock refuses the drop (no drop cursor; the
file-drop handlers never see a layer drag); the hovered tab lights through the shared
tab-strip highlight (`show_tab_strip_highlight`, the float-docking overlay).

The drop returns before any document work: the payload is copied out and
`duplicate_layers_to_session` runs from `QTimer::singleShot(0)` (the source panel's
`QDrag::exec` is still on the stack and the copy rebuilds its rows).
`copy_layers_between_sessions` (main_window_layer_ops.cpp; `copy_layers_between_documents`
is its Document-level core, shared with Files as Layers) backs the dialog and scripts
too: it builds the payload Edit > Copy builds (root ids, referenced smart-object sources,
Smart Filter records, pattern tiles), validates the Smart Filter caches against the
target, runs the caller's `before_mutation` hook (the UI pushes the target's "Duplicate
layer" snapshot there), clones every root with `clone_layer_tree_with_document_ids`
BEFORE inserting anything (no session-targeted undo can roll back a half-inserted
stack), keeps the source names (a collision earns the copy suffix), applies one shared
placement offset, re-bakes vector rasters against the target canvas, and inserts the
stack directly above the target's active layer in source order, topmost copy active. Placement: a canvas drop
centers the copied set's movable extent on the drop point; Shift-drop, a tab drop, the
dialog and scripts keep the source coordinates when the documents share dimensions, else
center on the target canvas. A root whose Smart Filters depend on position keeps
its coordinates (the adopted cache would go stale). Linked
masks move inside `translate_moved_layer_metadata`; unlinked raster and vector masks shift
explicitly so the copy stays one unit. The UI wrapper then refreshes the target canvas, activates the target session and
selects the copies. Pinned by `ui_layer_drag_*` (layer_panel_organization_tests_cross_document.cpp)
and `ui_layer_drag_to_float_canvas_centers_at_drop_point` (float_window_tests.cpp);
`send_layer_drop_to_widget` synthesizes the drag.

Duplicate Layer (`layerDuplicateAction`, the footer button and its drop target) runs
`MainWindow::duplicate_layers`: the copies land as one block directly above the topmost
selected root, in its parent and in source order (Photoshop), and become the selection. `ui_duplicate_layer_inserts_copies_above_source` pins it.

Delete Layer (menu, footer button, Delete key, trash drop) runs `MainWindow::delete_layers`: it activates the sibling below the lowest deleted root, else the one above, else its group (issue 249).

Inside the panel, an Alt-drop duplicates instead of moving (Photoshop's Alt-drag):
`LayerListWidget::dropEvent` records `LayerDropRequest::copy` from the event's modifiers
(the enter/move handlers report CopyAction so the cursor shows the badge), and
`MainWindow::duplicate_layers_for_drop` clones each dragged root above its source, then
runs `move_layers_for_drop` on the CLONES, so the originals never move; the copies become
the selection under one snapshot. `ui_layer_alt_drag_duplicates_in_panel` pins it.

The same core backs Duplicate Layer to Document... (`layerDuplicateToDocumentAction`, hotkey
id `layer.duplicate_to_document`, in the layer context menu and on the window itself since
the Layer menu's row count is pinned): its dialog (`duplicateLayerToDocumentDialog`) takes
a name for a lone copy (`duplicateLayerNameEdit`) and a destination
(`duplicateLayerTargetCombo`: every other open document, then New Document, a fresh
session of the source's size and print resolution), always keep-position. The scripting
API's `layer.duplicate(targetDocument)` routes through
`ScriptEngineHost::duplicate_layers_to_session`, so the run's own snapshot covers the
target and no activation happens. `ui_duplicate_layer_to_document_dialog_copies` and
`ui_script_layer_duplicate_to_document` pin them.

## File drops onto the panel

OS files dragged onto the list become layers of the active document (Files as Layers, [import.md](import.md)). `dragEnterEvent` asks the owner's `set_file_drop_paths_callback` once per drag and refuses a drag with neither layer ids nor usable files (a text drag never reaches the selected-rows reorder fallback). File drags show the normal insertion preview with CopyAction; the drop records a `LayerFileDropRequest` (paths plus the `drop_target_at` result) that `MainWindow::handle_layer_drop` takes ahead of the layer request and hands to `add_files_as_layers_interactive` (plain opens with no active document).
