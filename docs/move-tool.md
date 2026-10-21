# Move tool: options bar, Auto-Select, transform box zones, cursor

The Move tool's options-bar row, what a click or selection rectangle picks, where its transform box takes a grab, and its cursor. Canvas click, drag and rectangle selection rules live in the "Move tool on the canvas" section of [layer-panel.md](layer-panel.md); snapping and Align/Distribute in [alignment.md](alignment.md), whose legal boundaries ([legal-constraints.md](legal-constraints.md), Alignment guides) bind everything here: the buttons only trigger the one-shot commands, nothing joins the drag path.

## Options-bar row

Order: Auto-Select (`moveAutoSelectCheck`, `tools/moveAutoSelect`), the Layer/Group combo (`moveAutoSelectTargetCombo`), Transform Controls, the transform type buttons (GitHub issues 225 and 299: Skew, Distort, Perspective and Warp, `moveTransformSkewButton` ... `moveWarpButton`, the same entries as Edit > Transform, so these start without a Free Transform first; [free-transform.md](free-transform.md)), Snap, a separator, the six Align buttons, a separator, the six feature Distribute buttons (`moveDistributeLeftButton` ... `moveDistributeBottomButton`, GitHub issue 172), then "..." (`moveAlignMoreButton`) holding Distribute Horizontal/Vertical Spacing and the Align To pair, then a separator and Auto-Nest Layers (GitHub issue 211; the Artboard bar has the same separator after Add Artboard). The Align and Distribute buttons mirror their Layer > Arrange QActions (icon, tooltip, enabled state; [alignment.md](alignment.md) explains why they are not default-action wrappers). Distribute glyphs come from `distribute_mode_icon` (action_icons.cpp): three bars on evenly spaced anchor lines, or three bars with two marked equal gaps for the spacing pair; the vertical modes are the horizontal glyphs transposed. The row must stay one line at 1366 px (`ui_options_bar_keeps_one_height_across_tools`).

## Auto-Select target (Layer / Group)

GitHub issue 168, Photoshop's Auto-Select dropdown. `MoveAutoSelectTarget` on the canvas; the combo writes `tools/moveAutoSelectTarget` (`kMoveAutoSelectTargetSettingsKey`) as `"layer"` or `"group"` on the user's change only, like `tools/moveAutoSelect` (tests flip the canvas directly and must not leak into later windows). Both tokens and the key are permanent; an unknown token reads as Layer. `load_tool_settings` reads it, the options-bar sync mirrors it under `QSignalBlocker`, and new sessions copy the combo's value.

- Group maps the hit leaf to its outermost group (`CanvasWidget::auto_select_target_layer`). An artboard is skipped: inside one, the outermost group within it is the target, and a direct child of the artboard is its own target. A top-level leaf is always its own target.
- Every path that selects by clicking reads it: a plain Auto-Select click and drag (the group is activated and its movable leaves move), Ctrl/Cmd+click (also with Auto-Select off, so the combo stays enabled), Shift+click toggles, Alt-drag duplication of the hit root, the hover outline (the union of the group's visible leaves' Move rects, `move_target_outline_bounds`), and selection rectangles. A press with Auto-Select off that drags the selection never maps anything.
- A group target counts as selected only when the group itself is in the selection, not through a selected child.
- An artboard's label, or with Auto-Select (or Ctrl) its empty area, is the target there (GitHub issue 259): the outline frames the artboard rect, and the press selects and drags the artboard. Details in [artboards.md](artboards.md).

## Transform box hit zones

GitHub issue 176. `CanvasWidget::transform_handle_at(..., TransformHitZones)` (canvas_widget_transform.cpp) reports the handle a press grabs. `HandlesOnly` is the original test: 14 px squares at the eight handles, the Rotate stem handle, inside = Move. `EdgesAndCorners` adds, in widget pixels of the box's own (rotated) frame:

- **Edge bands:** a press within 6 px outside a side's line, or up to half a handle square (7 px) inside it, resizes that side anywhere along it; where two bands meet it is the corner handle.
- **Corner rotate band:** outside the box and within 28.8 px of a corner (24 px grown 20 percent so it is easier to grab), a press rotates. Farther out is None.
- **Move core:** the inner band shrinks to a third of the half-extent, and the core inside it is tested first, so a small box keeps a Move area its handle squares used to cover entirely. From 42 px up the core ends exactly where the squares begin, so large boxes keep the old handle hits.

Callers: the passive Move box (press and hover) and the Free Transform session use `EdgesAndCorners`; Crop uses it too with Rotate mapped to None (off the box already rotates, crop-tool.md); artboard handles, marquee resize handles and the text double-click inside a session keep `HandlesOnly`. The path transform session (`path_transform_handle_at`, canvas_widget_vector_tools.cpp) has its own test with the same edge band (6 px outside, inside at most a sixth of the shorter side); outside it rotates everywhere, as before.

A grab beside a line must not jump it: the press records `transform_handle_grab_offset` (the document offset from the press to the grabbed side, along the moving axes, in the box frame) and the drag adds it to the pointer before snapping (`transform_drag_grab_offset_`, `crop_drag_grab_offset_`). A press exactly on a handle has a zero offset. Resize cursors follow the box's rotation (`set_transform_cursor_for_handle(handle, angle)` picks the nearest of the four resize shapes). A corner band shows that corner's rotate cursor: `corner_rotate_cursor(direction)` (tool_cursors.cpp, Patchy's own quarter arc with two heads, four orientations) picked by `transform_rotate_cursor_at` from the side of the box the pointer is on, the corner's outward direction turned by the box angle; the stem handle keeps `rotate_cursor()`. Coverage: `ui_transform_zone_` (passive edges and corners, rotated session, tiny box, crop edges, per-corner cursors and the band reach).

## Cursor

GitHub issue 170. The four-way move cursor shows only where a plain press drags something: `move_press_drags_at` mirrors the press handler (artboard label, selected artboard, the passive box interior on the pasteboard, Auto-Select off moving the selection from anywhere, then the Auto-Select hit through `topmost_move_layer_at` and the group target, counting only movable leaves). Elsewhere, where a press starts a layer selection rectangle (empty canvas, a position-locked Background, the pasteboard), `apply_move_tool_cursor` shows `move_pointer_cursor()` (tool_cursors.cpp): Patchy's own arrow with a four-way badge, hotspot on the tip, theme-exempt like the other tool cursors. Transform zones take their resize/rotate cursors first. Coverage: `ui_move_cursor_shows_drag_only_where_a_press_moves`.

## Type tool Ctrl+drag

GitHub issue 216. With the Type tool, Ctrl is a temporary Move tool for text, read from each event's modifiers. Without a session, a Ctrl+press on a text layer (`topmost_text_layer_at`) activates it and starts the ordinary Move drag (`CanvasWidget::begin_text_layer_move`: one Move undo step, snapping and the Shift axis lock included); the Type tool stays active and no session opens. Ctrl elsewhere changes nothing. With a session open, a Ctrl+press on the edited text (editor viewport or transformed overlay) commits it, drags the committed layer, and on release reopens the session with the old caret and selection (`MainWindow::handle_text_ctrl_move_press`, `resume_text_editing`, shared with Warp Text); the canvas grabs the mouse because the commit removes the widget that took the press. A Ctrl+press on another text layer commits the session like any click off and moves that layer. Only presses are intercepted, so Ctrl shortcuts (Ctrl+A, Ctrl+Z, Ctrl+B) keep working in the editor. The four-way cursor shows while Ctrl is held over a text layer or the editor and during the drag; elsewhere the I-beam stays. Coverage: `ui_text_ctrl_drag_moves_text_without_switching_tools`, `ui_text_ctrl_drag_in_session_moves_and_resumes_typing`.

## Layer outline

The hover outline, the rectangle pre-selection outlines and the outline of layers being dragged share `draw_move_layer_outline` (canvas_widget_render.cpp): light-blue 1 px dashes over a solid near-black line, aliased, on the first widget pixel outside the layer's rounded widget box, so it never covers the art and keeps an edge on light, dark and blue pixels at every zoom. The pair is theme-exempt like the marching ants. Coverage: `ui_move_layer_outline_reads_on_every_side_at_every_zoom`.

## Rectangle pre-selection

GitHub issue 171: while a selection rectangle is dragged, the targets releasing it would select carry the hover outline, drawn beside it in `paintEvent`. `collect_move_selection_candidates` walks the tree once when the rectangle starts (visible, unlocked leaves with a Move rect, each tagged with its target and unioned into a per-target outline); each pointer move then runs `move_selection_matches` (rect intersection against the document-clipped box, deduplicated in stack order) and repaints only the outlines that appear or disappear. Nothing proportional to layer pixels runs per move or per repaint: Move rects come from the `pixel_revision`-keyed alpha-bounds cache ([performance.md](performance.md)). Release re-collects and matches with the same functions, so the outline and the result cannot disagree. Escape and every other cancel clear the outlines with the box; the panel selection changes only on release.

Coverage: `ui_move_rectangle_outlines_layers_before_release`, `ui_move_auto_select_group_picks_outermost_group`, `ui_move_auto_select_target_persists_across_windows`, plus the distribute-button checks in `ui_layer_distribute_requires_three_units_and_spaces_evenly` and `ui_layer_arrange_menu_hosts_align_and_distribute`.

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

## Layer selection from the canvas

Moved here from layer-panel.md; the panel-side selection rules stay there ([layer-panel.md](layer-panel.md#click-selection)).

Ctrl+click (Command on macOS) selects just the clicked layer and Shift+click toggles it, with Auto-Select on or off (#73, Ctrl too); a held Ctrl shows the hover outline with Auto-Select off. Alt-drag duplicates the dragged selection roots on the first drag frame and moves the copies, one "Duplicate layer" step, never on a bare Alt+click (#69). Auto-Select persists (`tools/moveAutoSelect`). With Auto-Select on, a plain click selects only the clicked leaf (a selected member or folder included); collapse waits for release so dragging a selected member still moves the whole set. Shift-drag adds an unselected target before moving the enlarged selection, keeps a selected one, and constrains movement to an axis. Ctrl-drag always draws a layer-selection rectangle, over artwork and with Auto-Select off. With Auto-Select on, dragging empty space also draws a rectangle, the pasteboard outside the selected transform box included; a position-locked Background is empty space. "Empty space" means outside every movable layer's Move rect (`move_layer_outline_bounds`: the opaque raster extent, or a text layer's frame): `CanvasWidget::topmost_move_layer_at` runs three passes (Photoshop parity): a visible pixel under the point, a selected layer whose rect contains it, the topmost such layer, so a press on a transparent pixel inside an outline grabs it. The hover outline and the lock status message share the lookup; the right-click layer menu lists only layers with real pixels under the pointer, and the pasteboard picks like the canvas: a layer outside the document is outlined, grabbed and listed there (`ui_move_tool_outlines_and_moves_off_canvas_layer`). Ctrl bypasses passive transform handles; rulers, guides, panning and transform sessions keep priority. Pinned by `ui_move_tool_grabs_transparent_pixel_inside_layer_rect`, `ui_move_tool_prefers_selected_layer_rect_over_topmost_rect`.

With Auto-Select off, a plain drag anywhere in the document workspace moves the selected movable layers, even with their pixels and transform box entirely offscreen. Passive controls never consume the first drag outside their box. With Auto-Select on and Transform Controls enabled, the selected box's off-canvas interior moves the selected set (folders and multiple layers included) before the empty-space rectangle path; its Move cursor and resize/rotate handles stay usable outside the document. Ctrl-drag, guides, panning, locks and active transform sessions keep their priority. Manual moves preserve off-canvas coordinates. Tests: `ui_move_auto_select_off_recovers_offscreen_layer_from_anywhere`, `ui_move_auto_select_on_drags_off_canvas_box_interior`, `ui_move_off_canvas`.

Rectangle intent and Shift-add mode latch at press; a plain rectangle replaces the selection, Shift adds. Matching (cached Move outline bounds, clipped to the document) previews while dragging and reruns on release ([move-tool.md](move-tool.md)). Any positive overlap selects an eligible leaf, occluded leaves and children of collapsed folders included; hidden, zero-opacity, position-locked and non-movable layers are skipped, group restrictions inherited. The active layer stays active while selected; otherwise the topmost match becomes active. Toggling the last selected layer keeps it. With Auto-Select on (or Ctrl held), a plain click on empty space or on the pasteboard that never becomes a rectangle, and an in-document rectangle that matches nothing, both deselect every layer (`CanvasWidget::request_layer_deselection`); Shift-clicks and Shift-rectangles keep the selection, and a position-locked Background counts as empty space, so clicking it deselects. A rectangle drawn entirely outside the document keeps the selection: matching is clipped to the document, so the box may enclose off-canvas artwork the matcher cannot see. Escape, focus loss, tool/document changes and edit locking discard pending gestures. Layer selection changes neither pixel selections nor history.
