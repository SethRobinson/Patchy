# Move tool: options bar, Auto-Select, transform box zones, cursor

The Move tool's options-bar row, what a click or selection rectangle picks, where its transform box takes a grab, and its cursor. Canvas click, drag and rectangle selection rules live in the "Move tool on the canvas" section of [layer-panel.md](layer-panel.md); snapping and Align/Distribute in [alignment.md](alignment.md), whose legal boundaries ([legal-constraints.md](legal-constraints.md), Alignment guides) bind everything here: the buttons only trigger the one-shot commands, nothing joins the drag path.

## Options-bar row

Order: Auto-Select (`moveAutoSelectCheck`, `tools/moveAutoSelect`), the Layer/Group combo (`moveAutoSelectTargetCombo`), Show Transform Controls, Snap, a separator, the six Align buttons, a separator, the six feature Distribute buttons (`moveDistributeLeftButton` ... `moveDistributeBottomButton`, GitHub issue 172), then "..." (`moveAlignMoreButton`) holding Distribute Horizontal/Vertical Spacing and the Align To pair, then a separator and Auto-Nest Layers (GitHub issue 211; the Artboard bar has the same separator after Add Artboard). The Align and Distribute buttons mirror their Layer > Arrange QActions (icon, tooltip, enabled state; [alignment.md](alignment.md) explains why they are not default-action wrappers). Distribute glyphs come from `distribute_mode_icon` (action_icons.cpp): three bars on evenly spaced anchor lines, or three bars with two marked equal gaps for the spacing pair; the vertical modes are the horizontal glyphs transposed. The row must stay one line at 1366 px (`ui_options_bar_keeps_one_height_across_tools`).

## Auto-Select target (Layer / Group)

GitHub issue 168, Photoshop's Auto-Select dropdown. `MoveAutoSelectTarget` on the canvas; the combo writes `tools/moveAutoSelectTarget` (`kMoveAutoSelectTargetSettingsKey`) as `"layer"` or `"group"` on the user's change only, like `tools/moveAutoSelect` (tests flip the canvas directly and must not leak into later windows). Both tokens and the key are permanent; an unknown token reads as Layer. `load_tool_settings` reads it, the options-bar sync mirrors it under `QSignalBlocker`, and new sessions copy the combo's value.

- Group maps the hit leaf to its outermost group (`CanvasWidget::auto_select_target_layer`). An artboard is skipped: inside one, the outermost group within it is the target, and a direct child of the artboard is its own target. A top-level leaf is always its own target.
- Every path that selects by clicking reads it: a plain Auto-Select click and drag (the group is activated and its movable leaves move), Ctrl/Cmd+click (also with Auto-Select off, so the combo stays enabled), Shift+click toggles, Alt-drag duplication of the hit root, the hover outline (the union of the group's visible leaves' Move rects, `move_target_outline_bounds`), and selection rectangles. A press with Auto-Select off that drags the selection never maps anything.
- A group target counts as selected only when the group itself is in the selection, not through a selected child.

## Transform box hit zones

GitHub issue 176. `CanvasWidget::transform_handle_at(..., TransformHitZones)` (canvas_widget_transform.cpp) reports the handle a press grabs. `HandlesOnly` is the original test: 14 px squares at the eight handles, the Rotate stem handle, inside = Move. `EdgesAndCorners` adds, in widget pixels of the box's own (rotated) frame:

- **Edge bands:** a press within 6 px outside a side's line, or up to half a handle square (7 px) inside it, resizes that side anywhere along it; where two bands meet it is the corner handle.
- **Corner rotate band:** outside the box and within 24 px of a corner, a press rotates (the `rotate_cursor()` hint). Farther out is None.
- **Move core:** the inner band shrinks to a third of the half-extent, and the core inside it is tested first, so a small box keeps a Move area its handle squares used to cover entirely. From 42 px up the core ends exactly where the squares begin, so large boxes keep the old handle hits.

Callers: the passive Move box (press and hover) and the Free Transform session use `EdgesAndCorners`; Crop uses it too with Rotate mapped to None (off the box already rotates, crop-tool.md); artboard handles, marquee resize handles and the text double-click inside a session keep `HandlesOnly`. The path transform session (`path_transform_handle_at`, canvas_widget_vector_tools.cpp) has its own test with the same edge band (6 px outside, inside at most a sixth of the shorter side); outside it rotates everywhere, as before.

A grab beside a line must not jump it: the press records `transform_handle_grab_offset` (the document offset from the press to the grabbed side, along the moving axes, in the box frame) and the drag adds it to the pointer before snapping (`transform_drag_grab_offset_`, `crop_drag_grab_offset_`). A press exactly on a handle has a zero offset. Resize cursors follow the box's rotation (`set_transform_cursor_for_handle(handle, angle)` picks the nearest of the four resize shapes). Coverage: `ui_transform_zone_` (passive edges and corners, rotated session, tiny box, crop edges).

## Cursor

GitHub issue 170. The four-way move cursor shows only where a plain press drags something: `move_press_drags_at` mirrors the press handler (artboard label, selected artboard, the passive box interior on the pasteboard, Auto-Select off moving the selection from anywhere, then the Auto-Select hit through `topmost_move_layer_at` and the group target, counting only movable leaves). Elsewhere, where a press starts a layer selection rectangle (empty canvas, a position-locked Background, the pasteboard), `apply_move_tool_cursor` shows `move_pointer_cursor()` (tool_cursors.cpp): Patchy's own arrow with a four-way badge, hotspot on the tip, theme-exempt like the other tool cursors. Transform zones take their resize/rotate cursors first. Coverage: `ui_move_cursor_shows_drag_only_where_a_press_moves`.

## Layer outline

The hover outline, the rectangle pre-selection outlines and the outline of layers being dragged share `draw_move_layer_outline` (canvas_widget_render.cpp): light-blue 1 px dashes over a solid near-black line, aliased, on the first widget pixel outside the layer's rounded widget box, so it never covers the art and keeps an edge on light, dark and blue pixels at every zoom. The pair is theme-exempt like the marching ants. Coverage: `ui_move_layer_outline_reads_on_every_side_at_every_zoom`.

## Rectangle pre-selection

GitHub issue 171: while a selection rectangle is dragged, the targets releasing it would select carry the hover outline, drawn beside it in `paintEvent`. `collect_move_selection_candidates` walks the tree once when the rectangle starts (visible, unlocked leaves with a Move rect, each tagged with its target and unioned into a per-target outline); each pointer move then runs `move_selection_matches` (rect intersection against the document-clipped box, deduplicated in stack order) and repaints only the outlines that appear or disappear. Nothing proportional to layer pixels runs per move or per repaint: Move rects come from the `pixel_revision`-keyed alpha-bounds cache ([performance.md](performance.md)). Release re-collects and matches with the same functions, so the outline and the result cannot disagree. Escape and every other cancel clear the outlines with the box; the panel selection changes only on release.

Coverage: `ui_move_rectangle_outlines_layers_before_release`, `ui_move_auto_select_group_picks_outermost_group`, `ui_move_auto_select_target_persists_across_windows`, plus the distribute-button checks in `ui_layer_distribute_requires_three_units_and_spaces_evenly` and `ui_layer_arrange_menu_hosts_align_and_distribute`.
