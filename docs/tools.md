# Paste placement, shape and Fill tools, Free Transform modifiers, the Background lock, Merge Down, and the canvas menu

Small tool/command behaviors. See [selection-tools.md](selection-tools.md), [brushes.md](brushes.md), [text-tool.md](text-tool.md), [vector-tools.md](vector-tools.md), and [crop-tool.md](crop-tool.md) for those features; deep (16/32-bit) layer support for these tools is in [high-bit-depth.md](high-bit-depth.md). The legacy Pixels-mode raster behavior here must stay byte-identical.

Related documents:

- [tools-transform-modifiers.md](tools-transform-modifiers.md): Free Transform and path transform modifiers (corner aspect lock and its Shift preference, the reference point, Alt scaling, the options-bar fields), what a pending session refuses or cancels, the drag readout, pixel-grid snapping, and the Move-tool double-click.

## Clipboard pixel placement

- File > Open from Clipboard: [behavior, platforms, and legal review](clipboard.md).
- Edit > Paste (`edit.paste`, Ctrl+V) places pixels by the first rule that applies (Photoshop 2026 over COM, `scripts\dev\artboards\ps-artboard-audit.jsx`, plus one Patchy rule): an active selection centers the paste on its bounds, on the artboard under its center or at the root, so the marquee a Copy or Cut leaves behind puts the pixels back; on an artboard (the selected layer's, or the selected artboard) it centers on that artboard ([artboards.md](artboards.md), "Feature interactions"); pixels copied from this document (`ClipboardPayload::source_session_id`) go back to their place while its center is in view (GitHub issue 193; Photoshop centers them); everything else (external images, other documents, a place scrolled out of view) centers on the viewport at the current pan and zoom. Each axis is clamped so the pasted rectangle fits inside the document.
- Edit > Paste in Place (`edit.paste_in_place`, Ctrl+Shift+V) ignores the selection and uses the pixels' original document coordinates, including across documents, with the same bounds clamp; onto another artboard it keeps their place on the artboard. External images have no source coordinates and use the viewport center.
- Pasting preserves pixel dimensions and content. If the pixels exceed the canvas on an axis, that axis is centered with overflow instead of being resized or cropped. Both commands clear the selection in the paste history entry ([selection-tools.md](selection-tools.md), "Paste deselects"); Undo restores it and Redo restores placement.
- Both commands insert directly above the topmost selected layer, preserving the copied stack's order. A selected child keeps the paste in its folder; a selected folder places it above that folder. With no selected rows, paste appends at the document top. This applies to pixels, whole layer trees, and SVG; the topmost pasted layer becomes active and selected (`ui_paste_inserts_above_topmost_selected_layer`, `ui_paste_above_selected_folder_or_at_top_without_selection`).
- Pixel placement lives in `MainWindow::paste_clipboard` (`main_window_layer_ops.cpp`); whole layer trees and SVG use their own preserving clone/import paths, and Palette or color-picker focus keeps its color paste routing. The `paste` UI filter covers viewport placement, all four edges, cross-document coordinates, oversized pixels, the shortcut, document/channel guards, external clipboard replacement, and undo/redo.

## Crop tool

The interactive Crop tool (hotkey C, canvas_widget_crop.cpp, `MainWindow::commit_crop_rect`, the `crop_document` overloads, ratio presets, overlay, and its `ui_crop_*` coverage) lives in [crop-tool.md](crop-tool.md). It cancels on a tool switch (never commits), its drag rect is the third unmerged twin of `marquee_selection_rect`/`shape_drag_rect`, and `transform_drag_keeps_aspect` deliberately does not apply to it.

## Shape tools (Rectangle/Ellipse/Line)

- Rectangle/ellipse honor brush Opacity/Soft through the signed-distance renderer in pixel_tools.cpp.
- Options-bar Style/Width/Height are deliberately session-only (mirrored via `current_shape_*`, never persisted). The row is registered pixel-only: the vector modes hide it and show the active shape's W / H instead ([vector-tools.md](vector-tools.md)). With Style Normal the Width/Height spins are greyed out (the marquee's twin row greys the same way); Fixed Ratio and Fixed Size enable them.
- A Pixels-mode click keeps the legacy 1 px commit (its `begin_edit` snapshot is pushed at press); the vector modes open the Create dialog instead. Coverage: `ui_shape_tap_*`, `ui_shape_style_row_is_pixel_only_and_greys_size_at_normal`.
- Alt draws from the center, so Rectangle/Ellipse are exempt from the Alt temporary eyedropper.
- Mode is always the first options-bar item for the shape tools (built before the shared brush controls), so switching to Pixels never shifts it. Coverage: `ui_options_bar_mode_combo_stays_first_for_shape_tools`.
- ALL constraint math lives in `CanvasWidget::shape_drag_rect()`. It intentionally duplicates `marquee_selection_rect()` (selection-only concerns, separately pinned). Do not merge them.
- The Fill command and Fill tool share their OWN persisted Opacity/Soft (see the Fill tool section); tests laying down setup color call `use_solid_fill_settings(canvas)`.
- Coverage: `ui_shape_fill_and_corner_radius_apply_to_new_documents` also pins the `current_*` mirror pattern from [ui-conventions-dialogs.md](ui-conventions-dialogs.md#options-bar).

## Fill tool (Paint Bucket) and the Fill command

The Fill tool click (Shift+G from the Gradient tool, `CanvasWidget::flood_fill` over core `patchy::flood_fill`) floods from the clicked pixel; the Fill command (Alt+Backspace, `fill_rect`) paints the whole selection (or canvas) and never reads existing colors. The Fill tool's options-bar row holds Opacity, Soft, Tol, and Contiguous, persisted as `tools/fillOpacity` / `fillSoftness` / `fillTolerance` / `fillContiguous`, mirrored through `current_fill_*`, and turned into `EditOptions` in one place, `apply_fill_settings` (canvas_widget_shared.cpp), which both paths call. Tol and Contiguous only matter to the flood. **Layer reach** (GitHub issue 498): a layer's buffer often covers only its content (rasterized text, a paste), and the rest of the canvas is that layer's transparency. Without the transparency lock the flood runs over the whole canvas (clipped to the selection's rect), reads outside the buffer as transparent, and grows the layer to hold what it fills; the Fill command with no selection targets the canvas, and the Gradient tool covers the canvas (its drag preview too), both through `expand_layer_to_include_rect`. With the lock all three stay inside the buffer. Tests: `tool_fill_bucket_grows_a_layer_smaller_than_the_canvas`, `tool_gradient_and_fill_rect_grow_a_layer_smaller_than_the_canvas`, `ui_fill_tool_wand_and_fill_command_reach_past_a_small_layer_buffer`. The Magic Wand, Grow and Similar already sample a canvas-sized image, and layer-mask floods grow the mask first.

- **Tolerance** (0..255) is the Magic Wand's metric through core `color_within_tolerance` (sum of squared per-channel differences within 4 x tolerance^2), so Fill Tol N accepts exactly the pixels Wand Tol N selects (GitHub issue 30). 0 is an exact match. A fully transparent pixel has no visible color: when either side has alpha 0 only alpha is compared, so every alpha-0 pixel matches every other whatever RGB it still holds (GitHub issue 498: moved or deleted content leaves its color behind; `tool_fill_bucket_transparent_pixels_match_whatever_rgb_they_hold`, 8, 16 and 32 bits). The Magic Wand, Grow and Similar call the same helper; `tool_fill_bucket_tolerance_metric_matches_magic_wand` pins the numbers. **Contiguous** off fills every matching pixel of the layer, still clipped by the selection.
- **Membership is decided against the clicked color, never against written pixels:** a first pass builds a region mask (4-connected stack flood, or a full scan when Contiguous is off), otherwise a translucent fill at a wide tolerance spreads through its own output forever. Under the transparency lock, alpha-0 pixels are outside the region.
- **Opacity** runs 0..100 like the Brush bar's Opacity and Flow (issue 253; Photoshop's Paint Bucket stops at 1%, a deliberate difference so every options-bar percentage shares one minimum): at 0% `flood_fill` and the Fill command return before touching the layer or the history, because the core floors a written alpha at 1/255. Mask and channel fills ignore Opacity at every value. `ui_fill_opacity_accepts_zero_and_fills_nothing` pins it. Opacity **blends** through `write_pixel` (palette mode snaps there like every tool) instead of stamping a reduced alpha; **Soft** feathers inward from the region's edge with the Fill command's chamfer transform (`compute_fill_feather`, up to 50 px). Tolerance 0 at 100% opacity must stay byte-identical to the verbatim copy path pinned by the `flood_fill_background` digest in `tool_write_paths_digest_baseline`.
- Mask and channel floods (`flood_fill_mask`) apply Tol and Contiguous over the one gray channel and write the mask value verbatim. Anti-alias is deliberately not offered.
- Both paths test the selection per pixel through `selection_alpha_at`, which rasterizes a many-span hard selection once (GitHub issue 34; see the lookup section of [selection-tools-regions.md](selection-tools-regions.md)).
- **Edit > Fill...** and its Content-Aware contents (issues 530, 575): [fill-command.md](fill-command.md).
- Coverage: core `tool_fill_bucket_*`; UI `ui_fill_tool_tolerance_and_contiguous_persist_across_documents`, `ui_fill_tool_click_honors_tolerance_contiguous_and_opacity`, and `ui_fill_of_wand_selection_with_many_spans_is_fast` (both fills on a 1110 x 1388 wand selection).

## Free Transform modifiers

The corner aspect lock and its Shift preference (`input/shiftKeepsTransformAspect`), the reference point and Alt scaling, the drag readout (`view/showTransformValues`), pixel-grid snapping (`core/pixel_grid.hpp`, `input/snapTransformsToPixelGrid`), and the Move-tool double-click: [tools-transform-modifiers.md](tools-transform-modifiers.md). Session steps and undo inside a pending transform: [free-transform.md](free-transform.md).

## Image and layer rotation and flips

Image > Rotate Right / Left / 180 and Flip Horizontal / Vertical act on the whole document (`rotate_document_*`, `flip_document_*` in core/document_geometry.cpp: every layer's pixels, bounds and mask, the channels, text transforms, smart-object placements, vector data). Layer > Arrange > Rotate Layer Right / Left / 180 turn the selected pixel layers about their own centers (`rotate_layer_*(Document&, LayerId)`: a quarter turn about the canvas plus the translation that restores the center, rounded to whole pixels), with the flip handler's guards (no smart objects, pixel locks, channel targets). Hotkey ids `image.rotate_180`, `image.flip_horizontal`, `image.flip_vertical`, `layer.rotate_cw`, `layer.rotate_ccw`, `layer.rotate_180` (GitHub issue 139). Tests: `document_flips_and_half_turn_mirror_every_layer_and_channel`, `layer_quarter_turns_keep_the_layer_center`, `ui_image_flip_and_layer_rotate_menu_actions_apply`.

## Eyedropper

The Pick tool (`CanvasTool::Eyedropper`) and the paint tools' Alt eyedropper: [pick-tool.md](pick-tool.md).

## Arrow-key nudging

Canvas arrow nudges ignore `Qt::KeypadModifier` before checking for plain or
Shift input. Physical macOS arrow keys can carry that flag while remote input
may omit it; both move by 1 px, or 10 px with Shift. Ctrl, Alt and Meta still block
ordinary nudges. This applies to layers, pending Free Transform and Crop boxes,
and selection outlines (`ui_layer_and_free_transform_nudges_accept_keypad_modifier`,
`ui_crop_handles_resize_move_and_nudge`, `ui_selection_arrow_keys_nudge`).

Layer nudges refresh the Move hover outline at the stationary pointer after the
document update. The outline follows the new bounds, disappears if the layer
moves away from the pointer, and returns when it moves back beneath it. A pointer
outside the canvas widget does not regain a hover outline. The refresh uses the
existing old/new outline repaint regions, including the pasteboard
(`ui_move_nudge_refreshes_stationary_hover_outline`).

## Move tool alignment

Visible alignment snapping (magenta guides while a Move drag snaps to another layer, the selection, or the canvas), the options-bar Snap checkbox, and the Layer > Arrange > Align / Distribute commands with their Move options-bar buttons live in [alignment.md](alignment.md), with the legal boundary in [legal-constraints.md](legal-constraints.md).

## Background lock, Eraser and Clear under a transparency lock
- File > New (`MainWindow::reset_document`; its empty Paint Layer above the Background follows a preference, [new-document.md](new-document.md)) and Image > Flatten Image ([layer-merging.md](layer-merging.md)) give an opaque Background (Photoshop's Background rules: [background-layer.md](background-layer.md)) (RGB, no alpha) both lock position and lock transparent pixels, as Photoshop does. A transparent Background stays unlocked (a transparency lock would make it unpaintable). Opened flat files and `app.newDocument` keep unlocked layers, so scripts and photo erasing still make transparency (GitHub issue 97).
- With transparency locked, the Eraser and Clear (Delete, Cut) paint the background color and keep each pixel's alpha, in the stroke compositor (`render_brush_stroke_pixel_from_snapshot_target`, `render_brush_stroke_pixel_deep`) and in core (`write_pixel_blend*`, `clear_pixel_would_change`, `clear_rect`). Alpha-0 pixels stay empty, and a locked alpha-less layer never gains alpha (`ensure_alpha_for_erase` is skipped). Unlocked layers without alpha still gain alpha and erase to transparency.
- The lock has no effect on what an alpha-less layer can become, so it never blocks a raster merge into one (`layer_merge.cpp`); merges into the Background behave as with the position lock alone. Pinned by `tool_eraser_and_clear_paint_background_color_under_transparency_lock` and `ui_eraser_on_background_paints_background_color_and_size_cursor`.

## Merge Down
- `MainWindow::merge_down` normalizes the selection, includes a single leaf's lower sibling, and routes selections containing vectors through the preserving planner and Merge Layers dialog. Compatible shapes merge immediately. The dialog offers separate vector/bitmap outputs, merges within each group, and separate vector paint categories. [layer-merging.md](layer-merging.md) owns the options, preservation boundaries, transaction, and scripting contracts.
- Bitmap-only selections retain the existing CPU-composited flattening path: hidden items contribute no pixels and are removed; the bottom visible layer keeps its id and name. Groups flatten in place, and pixels land within the canvas-clipped render bounds. Pattern resources accompany the scratch document. The Background's position and transparency locks permit merge; image-pixel locking refuses it.

## Canvas right-click menu

The right mouse button on the canvas opens the context menu and never pans. The builder (`CanvasWidget::show_canvas_context_menu`), every section, the session menus (transform, crop, text editing, Hand), the menu lifetime rule and the rule that menus use only Patchy's own commands: [canvas-context-menu.md](canvas-context-menu.md).

## Tool palette

The palette layout and tool icons live in [tool-palette.md](tool-palette.md), the flyout groups and their Shift+letter cycle commands in [tool-flyouts.md](tool-flyouts.md). Which tool and options-bar values come back on the next launch: [tool-options.md](tool-options.md).
