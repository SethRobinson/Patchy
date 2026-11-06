# Layers panel

Item-widget row rules: [ui-conventions.md](ui-conventions.md). Layer keys (digits, "/", modes): [layer-shortcuts.md](layer-shortcuts.md). Artboard rows (glyph, `layerRowArtboard`, drop rules): [artboards.md](artboards.md). Shape and style editors opened from a selected row or badge keep an existing multi-selection; an unselected row becomes the sole target ([batch-appearance.md](batch-appearance.md)).

Related documents:

- [layer-panel-selection.md](layer-panel-selection.md): row click, modifier and range selection, thumbnail Ctrl-click selection loads with the thumbnail and badge menus, canvas-requested selection, the deselected state and the selected count.
- [layer-panel-row-controls.md](layer-panel-row-controls.md): the folder disclosure arrow, row and thumbnail double-clicks with inline rename, the visibility eye and its sweep, and the Layer Styles and Smart Filters rows listed under a layer.
- [layer-drag-duplicate.md](layer-drag-duplicate.md): drags to another document, Duplicate Layer, and layer export (Quick Export as PNG, Export As...).

## Row styling

Layer rows use dynamic properties and application rules such as `QWidget#layerRowWidget[layerRowSelected="true"]`; repolish after property changes (`layerTargetActive` is the reference). `ui_layer_row_selected_highlight_paints` pins the rendered colors. Never give a row widget with a tooltip its own selector-less sheet: Qt cascades it into the tip and `background: transparent` cleared its panel (#347).

Main row (`make_layer_row_widget`): eye, tree indent, folder arrow, clip badge, thumbnail, 4 px apart. Columns (#386 follow-up): once the document has a folder (`layer_list_needs_arrow_column`), every row reserves the arrow slot (a non-folder's `layerTreeIndent` takes it), so content thumbnails, masks and plain rows' names sit in one column per depth, each level `kLayerChildIndent` (16 px) right of its folder's, at every Thumbnail Size; a document without folders reserves nothing. Duplicate's partial insert (#402) rebuilds when that changes. The folder, Layer Styles and Smart Filters arrows are 10 px plus the QSS border (`kLayerDisclosureSlotWidth`, 12 px) by 20 px. The clip badge takes the thumbnail's width in its depth's thumbnail column, so its `clip` glyph (`action_icons.cpp`, also the clip cursor and action icon; from the right, pointing down) centers on the base row's thumbnail and the clipped thumbnail takes the next column (#397; `ui_layer_rows_line_up_thumbnails_by_depth`, `ui_layer_row_spacing_keeps_eye_clip_and_thumbnail_close`, `ui_layer_clip_badge_arrow_points_down`).

Hidden rows disable name, details, mask link and clip badge; other badges stay clickable, dimmed (`layerRowBadgeDimmed`, issue 230).

Blending badge (#494, `layerBlendingBadgeButton`): shown while `layer_advanced_blending_differences` lists non-default Blending Options (channels, Knockout, effect switches, 'tsly' off, Blend If), named in its tooltip; a click opens them. Adjustment thumbnail tooltips name the kind (#492). Crowded rows: badges stay 20 px after QSS polish (`restore_layer_row_button_size`), the name keeps "Www…", then `LayerRowLockStrip` folds the locks into one padlock; `refresh_row_widths` sizes rows to it, then scrolls (`ui_layer_rows_with_every_badge_*`).

Locks: panel buttons, the row's Lock submenu, Layer > Hide & Lock and Lock Layers... ([layer-shortcuts.md](layer-shortcuts.md)); the last two add Prevent Auto-Nesting (`layer.prevent_auto_nesting`; #246, [artboards.md](artboards.md)). Badges (`make_layer_lock_badge`) show each set lock; a Background shows one padlock instead ([background-layer.md](background-layer.md)); folders pass on editing locks only. Buttons and badges share `layer_lock_icon` (action_icons.cpp, #484): glyphs 14 of 16 px in the buttons, 11 of 14 in badges, pixel art with no partly inked pixel at any scale, inked from a theme role at paint time (`ui_layer_lock_buttons_share_one_icon_height`, `ui_layer_lock_icons_land_on_whole_pixels`). The row tooltip names the effective locks on one line (`layer_list_item_tooltip`, #483): "Locked: All", else the partial ones joined ("Locked: Transparent, Position"), then "Auto-nesting"; the Background reads "Locked: Background", plus Image (`ui_layer_row_tooltip_names_every_lock`).

## Thumbnails

Tile shapes and the zoom-to-content preference, the Thumbnail Size and Display submenus, the title-row gear: [layer-thumbnails.md](layer-thumbnails.md).

## Rebuilds and absent rows

`MainWindow::refresh_layer_list` (main_window_layer_panel.cpp) rebuilds in
passes: configure every `QListWidgetItem` while parentless, insert ALL items, then
attach row widgets. The order is load-bearing: `setData`/`setToolTip` on an inserted
item emits `dataChanged` and layout work, and each `setItemWidget` editor index makes
every LATER insert pay an update walk (interleaving goes quadratic). Never mutate an
inserted item mid-rebuild or attach a row widget before the last item is in.
Duplicate Layer adds only its copies' rows (`insert_duplicated_layer_rows`, #402).
Profiling: [performance.md](performance.md) (`layerpanel`, `manylayers`, `duplicate`).

New sessions build rows once; row attachment pumps paints/timers with input excluded
and the preview edit lock held, refusing recursive rebuilds. Slow setup shows an opening
dialog while the spinner animates; hide the welcome panel before inserting the tab. `ui_large_document_session_keeps_loading_responsive`
pins spinner frames during row construction; `ui_start_panel_recent_files_open_on_click` pins one rebuild.

Layer Style CANCEL skips `refresh_layer_list`: it restores the pre-dialog state, so
only the previewed thumbnail moved (`refresh_layer_thumbnails` + `refresh_layer_controls`).
Committing rebuilds. Artboard edits that keep every layer in place (resize, plain move,
background) restamp thumbnails the same way (`finish_artboard_edit`'s `rows_unchanged`).

During guarded automation, script rebuilds call `refresh_layer_list(true)`: old row widgets are captured before clearing and deleted after detachment. Manual callbacks and editable pauses use ordinary deferred deletion so the input receiver outlives the event. `ui_mcp_layer_rows_stay_bounded_during_long_script` pins the live-widget bound during execution.

Canvas-driven selection never rebuilds existing rows. `reveal_layer_in_layer_list` (Move-tool auto-select, `active_layer_changed_callback`) and `select_layers_in_layer_list` (rectangle and modifier selection) rebuild only when a collapsed ancestor must open, the name filter must clear, or a target has no row; else they select the existing row (`layer_row_item`) for the ordinary selection handler. Pinned by `ui_move_auto_select_click_keeps_existing_layer_rows`. Both scroll through `scroll_layer_row_into_view`: a row already wholly on screen keeps the scroll position, an off-screen row is centered, and the badge buttons pass `clicked_in_panel` so a partly visible row under the pointer stays put too (#266, `ui_layer_smart_object_badge_click_keeps_panel_scroll`).

Row masks: `LayerListWidget::update_row_viewport_masks` (on scroll, resize, range, value and focus changes) masks the rows under the raised scroll bars. Scroll-bar rects map through global coordinates (the bars are siblings of the viewport). Only rows in view change. Test: `ui_layer_list_row_masks_map_scroll_bars_without_warnings`.

The list may omit rows: collapsed folders and the name filter (`layerNameFilterEdit`) rebuild without rows for excluded layers. Never assume every layer has a row, and never add a "row exists but hidden" state: absent rows are the one not-shown state consumers handle. While the name filter is active, `LayerListWidget::set_drag_blocked` refuses drag reordering because a reorder would silently move filtered-out layers; each refused attempt reports through `show_status_error` and leftover held-button moves are swallowed so the base view cannot start a drag-selection sweep.

## Blend and opacity row

Dragging the "Opacity:" or "Fill:" prefix scrubs the value (`install_prefix_scrub`, [ui-conventions-dialogs.md](ui-conventions-dialogs.md#options-bar)): the drag ends with `editingFinished`, so `finish_pending_layer_opacity_edit` closes it as one undo entry; a prefix click focuses the field, number selected. Combos from `add_blend_mode_items` also step with Left/Right. Panel blend changes merge into one undo entry until a pause or another history change (`ui_layer_blend_mode_steps_coalesce_into_one_undo_entry`). Pass Through is offered first, for folders only (`set_blend_mode_pass_through_offered` per active layer, `BlendModeMenu::Folder` in Blending Options), and never lands on a selected non-folder (#235). `refresh_layer_thumbnails` re-syncs each row's mode and opacity line (`layer_row_details_text`, #236). The blend row, the lock/filter row and the footer wrap in a narrow column (#96, #199; [right-docks.md](right-docks.md)).

While the list is open (#367, `PopupAwareComboBox`), the highlighted mode shows on every selected layer (`preview_layer_blend`, coalesced, async render) with no history entry, session revision or row refresh. Closing restores the modes before Qt reports a choice, which commits one entry; a history change, edit snapshot, or another document or selection closes the list and reverts (`ui_layer_blend_list_*`).

## New Layer dialog

The New Layer dialog and the footer's modifier clicks on New Layer and Add Layer Mask: [new-layer.md](new-layer.md).

## New adjustment layers

Clipping controls and row badges use `effective_clip_base`: pixel layers and
folders can host a clipped run. Adjustments clipped above a folder affect its
merged content. The folder itself cannot be a clipped member.

Every New Adjustment Layer entry inserts directly above the topmost selected row, so
inside a selected child's folder or above a selected folder. With no selected rows, the active layer is the anchor, else the document
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

A layer's color label (#244) is `Layer::color_label()`, Photoshop's `lclr` index (0 none, 1 to 7 the classic colors, 8 to 11 Photoshop 2024's Seafoam, Indigo, Magenta and Fuchsia, #635; constants and permanent ids in `core/layer.hpp`). Menus and the New Layer dialog list them in hue order, `kLayerColorMenuOrder` (None, Red, Orange, Yellow, Green, Seafoam, Blue, Indigo, Magenta, Fuchsia, Violet, Gray), while `layer_color_actions_` stays indexed by label. It is not an edit to pixels, so it moves no revision, and locked layers take one. The row shows it as `layerRowColorStrip`, a 4 px strip at x 0 of `layerMainRow` inside the base indent, off the layout and transparent to the mouse, created only for a known nonzero label; QSS paints it from the `layer_color_*` theme roles through its `layerColor` property. Layer > Color (`layerColorMenu`) and the row's context menu (`layerContextColorMenu`) share the `layerColor*Action` actions (hotkey ids `layer.color_<id>`), built by `populate_layer_color_actions`; `set_selected_layers_color` labels the selected rows, groups included, as one undo step, and `refresh_layer_color_action_states` checks the common label (none for a mixed selection). Scripts: `layer.colorLabel`. PSD rules in [ps-compat.md](ps-compat.md). A right-click in the main row's eye column (the eye and the strip left of it) opens `layerEyeColorMenu` (`show_layer_eye_color_menu`): Hide/Show This Layer, Show/Hide All Other Layers (the Alt-click `isolate_layer_visibility`), Show All and Hide All Layers (`set_all_layers_visible`, every depth), outside history (#407), then the color actions (#276). Pinned by `ui_layer_color_labels_menu_undo_and_row_strip`, `ui_layer_color_labels_photoshop_2024_colors`, `ui_layer_eye_right_click_opens_color_menu` and `ui_layer_eye_menu_*`.

## Background layer

Bottom of the stack, no styles, one padlock, the short menu, PSD adoption: [background-layer.md](background-layer.md).

## Add Layer Mask and Layer Styles buttons

The footer's `layerAddMaskButton` runs `MainWindow::add_layer_mask` (reveal-all, or from the active selection; Alt-click hides, Ctrl-click adds a vector mask, [new-layer.md](new-layer.md)). The row context menu's `layerContextVectorMaskMenu` (#449), right after `layerContextMaskMenu`, holds the Layer > Vector Mask actions themselves (`layerVectorMask*Action`, built by `populate_vector_mask_menu`), so they run from their own signals and match the Layer menu (`ui_layer_row_menu_vector_mask_submenu_adds_vector_mask`). `layerStylesButton` (#94; the row badge's `badge-fx` art) pops up `layerStylesButtonMenu`: the shared Edit Layer Styles... action (`layerBlendingOptionsAction`), a separator, then one entry per effect in the dialog's category order. An effect entry calls `edit_active_layer_style(LayerStyleEffect)`: the one Layer Style dialog opens on that effect's first row and ticks it the way its checkbox does, so OK keeps it, Cancel restores, and a turned-off instance is turned on instead of duplicated. Neither is a document widget. `refresh_add_layer_mask_button_state` owns the mask button from `can_add_layer_mask` (any selected layer of kind pixel/adjustment/group with no raster mask yet and image pixels unlocked, no preview-dialog lock); `refresh_layer_styles_button_state` owns the styles button from `can_edit_layer_styles` (a selected or active layer that is not effectively locked, `edit_active_layer_style`'s own filter, no preview-dialog lock). Both run from `refresh_layer_controls` and `update_document_action_state`. Tests: `ui_layer_add_mask_button_*`, `ui_layer_styles_button_*`, `ui_layer_styles_menu_entry_*`. The footer's one-row width (eight buttons) sets the column's opening width (`ui_layer_fx_and_smart_badges_stay_visible_in_narrow_panel`); five more start hidden, Rasterize a menu ([new-layer.md](new-layer.md#optional-footer-buttons)). Footer buttons hide (#199): right-clicking one opens `layerFooterButtonsMenu` (a tick per button by its tooltip name, the last shown locked, Show All Buttons, Customize Tool Palette...); keys and dialog: [tool-palette.md](tool-palette.md). `apply_layer_footer_visibility` hides them and re-adds them in the stored order (#471, presets in tool-palette.md); `WrappingButtonRowLayout` lays out visible buttons only, so the row and the opening width shrink with them; narrower, they wrap one at a time ([right-docks.md](right-docks.md); `ui_layer_footer_wraps_one_button_at_a_time`). Size (#213 follow-up): the Layers Panel tab's `layerFooterIconSizeCombo` (the toolbar icon size's steps; shown at once, Cancel restores; key `layersPanel/footerIconSize`). `apply_layer_footer_icon_size` scales the 24 px icon and sets `layerFooterIconSize`, which `layer_footer_size_rules` (main_window_theme.cpp) scales the button rule for (`ui_layer_footer_icon_size_persists_and_applies`). Art (#470): Move Up/Down a layer and a triangle, Merge Down a three-layer stack with the top layer in the accent and a down arrow at its right (menus too; `ui_layer_footer_arrange_icons_draw_in_both_schemes`). Commands stay in the menus (`ui_layer_footer_buttons_hide_from_their_menu`).

## Row context menu

`show_layer_context_menu` (main_window_layer_ops.cpp) fits 1366x768 (#461): Edit Layer Styles... first, the kind-only entries, Create/Release Clipping Mask, Layer Style; New, Duplicate, Rename, Delete, Color; Merge Down and the `layerContextMergeMenu`, `layerContextRasterizeMenu` (Trace Image to Shapes last) and Smart Objects submenus; Lock, Load Layer Transparency, Layer and Vector Mask; `layerContextExportMenu` (Thumbnail Size only at None, #481). Visibility is the eye's. New entries go in submenus: `ui_layer_context_menu_fits_short_screens` caps rows (18 pixel, 20 text) and height (680 px). Export (#462) holds `layerQuickExportPngAction` and `layerExportAsAction`, then Duplicate Layer to Document... ([layer-drag-duplicate.md](layer-drag-duplicate.md#layer-export-quick-export-as-png-export-as)).

## Footer menus

`layerNewAdjustmentButton` lists the fill layers, a separator, then the adjustments, built by the Layer menu's `populate_new_fill_layer_menu` and `populate_new_adjustment_layer_menu` (issue 252). Film button (`layerAnimationButton`): [animation.md](animation.md#animation-preview-window).

## Mask commands over several layers

Layer > Layer Mask (Add, Link, Disable, Invert, Apply, Delete), Layer > Vector Mask (Reveal All, Hide All, Current Path, Delete, Disable, Rasterize), the row menu's `layerContextMaskMenu` (`layerContext*MaskAction`) and the footer's Add Layer Mask act on every selected layer the command applies to, as one undo step (local-029). `mask_command_targets` picks them from `mask_command_layer_ids`: the selection, or the active layer alone when the selection does not hold it (a thumbnail menu made another row active). Layers that do not qualify (no mask, image pixels locked, a kind Apply refuses) are skipped; an entry stays enabled while one qualifies (`layer_mask_command_state`). Toggles set every layer to the toggled state of the active layer's mask, or the first selected masked layer's when the active one has none, never a flip each. Selection and active layer stay. Per-row controls act on their own layer only (`MaskCommandScope::ActiveLayer`): the row link buttons, the mask thumbnail menu's toggle and link entries, and Shift-click on a mask thumbnail. Edit, Show Mask Overlay and View Layer Mask stay single-layer. Tests: `ui_layer_mask_commands_act_on_every_selected_layer`, `ui_layer_row_menu_delete_mask_acts_on_every_selected_layer`, `ui_vector_mask_commands_act_on_every_selected_layer`, `ui_layer_mask_delete_on_all_layers_of_apple_worm_if_available` (local fixture `psd/apple-worm-layers.psd`).

## Layer commands: delete, duplicate, drags to other documents

Delete Layer (menu, footer button, Delete key, trash drop) runs `MainWindow::delete_layers`: it activates the sibling below the lowest deleted root, else the one above, else its group (issue 249). The footer button goes through `delete_from_layers_panel`: with one layer selected and its raster or vector mask the edit target, it deletes that mask instead; a raster mask on a pixel layer that Apply Layer Mask accepts asks first (`deleteLayerMaskPrompt`: Apply, Delete, Cancel; #441, `ui_layer_footer_delete_removes_targeted_mask`).

Drags onto another document, Duplicate Layer, Alt-drop duplicates, Duplicate Layer to Document and layer export: [layer-drag-duplicate.md](layer-drag-duplicate.md).

Footer drop targets: a layer drag over New Layer or Duplicate duplicates, over New Folder groups, over Delete deletes (`handle_layer_action_button_drag_event`, `layerDropAction`). The entry reads the layer ids, sets `layerDropActive` (one deferred repolish, never from inside the filter: issue 62) and shows `layerDropHint`, a child of the window above the button that names the drop and is transparent to input; moves only accept. Never a QToolTip there (local-033): the tip is its own top-level window, a large pointer (Windows pointer size 48) makes Qt put a one-line tip beside the point, over the next button, and a drag crossing it left Patchy until the tip closed (`ui_layer_footer_drop_hint_stays_clear_of_pointer`).

## File drops onto the panel

OS files dragged onto the list become layers of the active document: [import.md](import.md#files-as-layers) has the panel-side drag rules.
