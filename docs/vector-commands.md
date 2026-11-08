# Vector commands and point-editing UI

The path point-editing surface (classifier, anchor tools, selection, hints,
context menu, path free transform) and the commands that operate on existing
shapes (Simplify Path, Combine Shapes, Ungroup Layers). The model and
Pen/Direct Select basics live in [vector-tools.md](vector-tools.md), the PSD
encodings in [vector-psd-encodings.md](vector-psd-encodings.md); legal boundaries in
[legal-constraints.md](legal-constraints.md); the JavaScript contract in
[vector-automation.md](vector-automation.md) (scripts and the UI share
fitting, selection coverage, stroke sampling, and mask baking through
`src/ui/vector_operations.cpp`). Structural point edits and component commands:
[vector-path-edits.md](vector-path-edits.md). Tests:
`tests/ui/vector_point_editing_tests.cpp`, `tests/ui/vector_commands_tests.cpp`.

Related documents:

- [vector-commands-selection.md](vector-commands-selection.md): what a Path Select or Direct Select press picks and selects, marquees, moving a selection (Shift constraints, cross-layer Direct Select), and deleting the last shape.
- [vector-commands-path-transform.md](vector-commands-path-transform.md): Ctrl+T on a path (Free Transform Path and Points), its distort gestures and the live fill.
- [vector-commands-shapes.md](vector-commands-shapes.md): Simplify Path, Combine Shapes, Ungroup Layers and Copy as SVG.

Which tools show the target outline (`target_path_outline_tool_allows`): path tools always,
with their editing glyphs; otherwise only while a Paths-panel row is targeted, the work path,
saved paths and vector masks with any tool, but a shape layer's own path only under the shape
tools (Line, Rectangle, Ellipse, Polygon, Custom Shape) (GitHub issue 238;
`ui_shape_layer_outline_shows_only_under_path_and_shape_tools`). Under a
path tool the overlay also outlines every Layers-panel-selected shape layer with hollow anchors
(outline only under Path Select; `set_panel_selected_layer_ids`); only the target gets filled anchors, handles, edits.

Overlay glyphs (`draw_pen_overlay`, `draw_path_edit_overlay`, canvas_widget_vector_tools.cpp):
visibility comes from contrast, not size (GitHub issue 105). The accent is a saturated blue (`path_overlay_accent`), every glyph is three-tone, and handle lines sit on a translucent dark halo
(`draw_handle_line`). Every path outline
and the Pen's preview segment are a 1-device-pixel accent hairline with no halo
(`draw_path_outline`, Photoshop's path display), shifted half a device pixel so an edge on whole
pixels covers one row (GitHub issue 238; `ui_path_overlay_outline_is_hairline`). Anchors are 8 px
squares with a dark 1 px edge: white with an accent inner ring when unselected (Photoshop's
hollow anchor), accent with a white inner ring when selected. Handle knobs are 8 px white
discs with the dark edge and an accent dot, and grab from 9 px (`kHandleHitRadiusPx`; a press
nearer the anchor stays the anchor's; `ui_path_handle_knobs_read_and_grab`). Knobs are
grabbed by Direct Select alone (and the Pen's Ctrl latch, which is Direct Select). Direct
Select and the Pen family draw the knobs of selected points; the Pen keeps them after the
button and Ctrl come up, so a Ctrl-drag can find them, but a plain Pen click never grabs one
(`ui_pen_ctrl_selected_point_keeps_its_knobs`). Path Select draws and grabs none
(`ui_path_tools_keep_handle_knobs_to_direct_select`; see "Tool roles"). Point and handle drags
re-render a shape layer's fill live (`apply_path_edit`; `ui_direct_select_drags_redraw_the_fill_live`), once per
batch of queued mouse moves at the latest pointer position, and the release applies a move still queued
(`flush_pending_path_edit_drag`, `ui_path_select_drag_coalesces_queued_moves`, GitHub #314; the 8-bit gradient paint in `paint_coverage` runs in row
strips across workers on large fills, same bytes; `patchy_perf_tests gradientdrag` times both);
a step that moves every anchor of a shape layer (Path Select dragging all its shapes) shifts the
existing raster by whole pixels instead of re-baking, and `finish_path_drag_rasters` re-bakes once at
release, cancel or tool switch (GitHub #678, `ui_path_select_shape_drag_shifts_raster_and_rebakes_once`,
`patchy_perf_tests shapedrag`);
tests that set up handles go through `replace_path_edit_target` (`set_target_path_through_edits`),
never `set_vector_shape`, or captures show the old fill under the new outline. `kGlyphReach` (8 px) is how far a glyph reaches
past its point; every repaint region around path points pads by it. The tests'
`accent_overlay_near` probes 3 px around a point for the accent, which the inner ring and the
knob dot satisfy.

## One classifier, three consumers

`CanvasWidget::pen_hover_hit_raw` says what is under the pointer on the
target path (anchor, segment, the session's first anchor) and
`filter_pen_hit` narrows it by `pen_edit_mode()`: Auto for the Pen with
Auto Add/Delete on, DrawOnly with it off, AddOnly/DeleteOnly/ConvertOnly
for the anchor tools. `pen_hover_hit` is the filtered result and drives the
cursor badge (`apply_pen_cursor`, which returns the badge it showed), the
click editor (`apply_pen_hover_edit`, shared by the Pen click, the anchor
tools, and the context menu; undo labels "Add anchor", "Delete anchor",
"Convert point"), and the hover hint. `pen_family_tool_active()` covers the
Pen and the three anchor tools wherever the Pen's handlers dispatch;
`path_edit_tool_active()` includes them (anchors draw, selection keys work).

## Auto Add/Delete (Pen options bar)

`penAutoAddDeleteCheck`, setting `tools/penAutoAddDelete` (default on,
mirrored in `current_pen_auto_add_delete_`, applied to every canvas like
the vector tool mode). Off, a Pen click on the target path starts a new
subpath instead of editing it (Photoshop's behavior); the badge shows the
plain crosshair. The anchor tools and the context menu ignore it.

## Pen Tools flyout: Add Anchor Point, Delete Anchor Point, Convert Point

`CanvasTool::AddAnchor/DeleteAnchor/ConvertPoint` (append-only enum values;
hotkey ids `tools.add_anchor`, `tools.delete_anchor`, `tools.convert_point`,
no default keys; actions `toolAddAnchorAction` etc.; icons
`tool-add-anchor`, `tool-delete-anchor`, `tool-convert-point`). Each shows a
fixed badge, performs its one edit on a hit, and does nothing on a miss:
never a session, never a close. Ctrl still latches Direct Select. Switching
from a live Pen session to an anchor tool commits the path like any tool
switch. Flyout button `penToolButton` (menu `penToolMenu`); P re-selects
the Pen and puts it back on the button.

## Selection keys under the Pen family

`handle_path_edit_key` gives the Pen family Delete/Backspace (remove the
Ctrl-selected anchors) and Escape (deselect) once anchors are selected;
arrows stay with the app hotkeys. `CanvasWidget::event` accepts
the Delete/Backspace ShortcutOverride for every path tool with a selection
so `layer.clear` does not fire. Starting a Pen session clears a leftover
selection.

## Hints

- Activation: each path tool shows a one-sentence gesture hint in the status
  bar (`tool_activation_hint_source`, main_window_actions_tool_palette.cpp; a
  tool without one shows its name) and a second tooltip line
  (`tool_tooltip_detail_source`, `kActionTooltipDetailProperty`).
- Hover: `update_path_hover_hint` (Pen family) and
  `update_path_select_hover_hint` (Path/Direct Select) emit only on the
  transition INTO an actionable state, so other code's confirmations survive
  mouse motion. `set_tool` resets the memory.
- Every path-tool hint lives in the status bar (none in the options bar);
  the Pen's activation hint and the Pen family's add and delete hover hints
  name the Ctrl-drag gesture.
- Point count: `pathPointCountChip` (a permanent status-bar QLabel, so it
  survives `showMessage`) shows "%n points selected" for two or more anchors
  under Direct Select or the Pen family, whose Ctrl latch selects points (not
  Path Select).
  `refresh_path_point_count_chip` (main_window_vector.cpp) runs from
  `set_path_selection_changed_callback` (every change to
  `path_selected_anchors_`) and every options-bar refresh.

## Tool roles

Each tool owns one job (GitHub issue 248).

| Tool | Owns | Never |
|---|---|---|
| Pen | creating and extending paths; its Ctrl latch is Direct Select; shows the knobs of selected points (grabbed only through the latch) | moving whole shapes |
| Path Select (black arrow) | selecting and moving whole shapes (groups): filled anchors, no knobs | point, handle or segment edits |
| Direct Select (white arrow) | every point, handle and segment edit; hollow anchors on the active path | moving a shape it did not select point by point |

An anchor, outline or fill press under Path Select moves the whole group; the knob hit test
(`path_handle_at`) and handle hover hint are Direct Select's only.

Modifiers. Ctrl swaps the two arrows while held (`arrow_swap_active_`, read by
`path_edit_tool()` like the Pen's `pen_temp_direct_select_` latch): set from the press
for the whole gesture, from hover moves and from Ctrl key presses through the canvas key
filter, never mid-drag, reset by `set_tool` and by Ctrl+T (whose Ctrl is the shortcut's;
`begin_path_transform` reads the real `tool_`). The selection follows the tool that
presses (`path_selection_by_path_select_`): a Direct Select press drops Path Select's
whole-shape selection first, a Path Select press expands Direct Select's points. Path
Select Alt-drag duplicates: the first drag move copies every selected group into new
groups of the same path (`next_shape_group`) and drags the copies, one "Duplicate shape"
undo entry with the drag (`path_drag_duplicate_pending_`). On a merged shape layer each copy
joins its source group's paint parts (`copy_vector_part_groups`), else it shows only an
outline. Direct Select Alt+click on a
point or segment selects its whole subpath; Alt at a handle press breaks a smooth pair
(`path_drag_break_pair_`, the anchor turns corner and only that handle moves). A plain
handle drag on a smooth anchor keeps the pair colinear: the opposite handle turns with it
and keeps its own length.

Cursors (`tool_cursors.cpp`, Patchy's own art, hotspot on the tip, theme-exempt):
`path_select_cursor()` is a solid dark arrow, `direct_select_cursor()` a light one with
a hollow anchor badge; `apply_arrow_cursor` shows `path_edit_tool()`'s, so Ctrl flips it
and the Pen's Ctrl latch shows Direct Select's. Coverage: `ui_direct_select_alt_click_selects_the_whole_subpath`,
`ui_direct_select_handle_drag_keeps_the_pair_colinear_and_alt_breaks_it`,
`ui_ctrl_swaps_the_arrow_tools`, `ui_path_select_alt_drag_duplicates_*`,
`ui_path_select_tools_show_their_own_arrow_cursors`.

Shape tools (issues 312, 313). Ctrl is a temporary Path Select: `shape_ctrl_path_select_`
follows Ctrl while idle (hover, key filter: cursor, overlay); a Ctrl press latches
`shape_temp_path_select_` to release, so `path_edit_tool()` reports PathSelect (Ctrl+Alt-drag
duplicates the shape into the same layer like Path Select's Alt-drag). Shift at a
vector drag's press sets `shape_press_adds_to_shape_` for that commit:
`effective_vector_combine_index()` reads Add (any kind joins the active shape layer; none: a
new layer); the Combine combo is never written. Tests:
`ui_shape_tool_ctrl_acts_as_path_select`, `ui_shape_tool_ctrl_alt_drag_duplicates_a_filled_shape`,
`ui_shape_tool_shift_press_adds_to_shape_layer`.

## Pen: extending and ending paths

GitHub issue 248. Shift keeps a new point on a 45-degree line from the last one and a
dragged handle on a 45-degree direction from its anchor (`constrain_drag_to_axes`, the
Direct Select vertex constraint). A Pen click (no session, no Alt) on an end point of an
open subpath of the target continues that subpath (`pen_open_endpoint_at`, before the
Auto Add/Delete edit, so it never deletes the point): the session starts from its anchors,
run backwards when continuing from the first one so it always grows at its last anchor
(`PenContinuation`), and commit (Enter, Ctrl+click away, closing on the other end, a tool
switch) replaces that subpath in place in its original direction, one "Extend path" undo
entry, skipped if the target or subpath changed meanwhile or nothing was added. The hover shows `PenHoverAction::Continue` (a solid square badge, hint "Click to
continue the path from this end point"). Mid-session, Ctrl+click away from its points
ends the path open (`handle_pen_ctrl_press`). Coverage:
`ui_pen_shift_keeps_45_degrees_and_ctrl_click_ends_the_path`,
`ui_pen_click_on_an_open_end_continues_the_path`.

## Path context menu

Right-click under a path tool: the press records `context_press_pos_` (the
right button never pans); a release within `startDragDistance` opens
`canvasPathContextMenu` (`show_path_context_menu`, public for tests, reached
through `show_canvas_context_menu`, which falls back to the shared canvas menu
when the path menu has no target), otherwise the gesture opened nothing.
Every entry is listed and enabled by context (object names
`pathMenu*Action`): Add Anchor Point (over a segment), Delete Anchor Point
and Convert Point (over an anchor), Break, Join, Merge and Delete Segment
([vector-path-edits.md](vector-path-edits.md)), Delete Selected Points and Deselect
Points (with a selection; Delete Selected Shapes and Deselect Shapes under Path
Select, whose selection is whole shapes; it is the Delete key's
`delete_selected_path_anchors`, so a shape layer it empties is deleted too, see
[Deleting the last shape](vector-commands-selection.md#deleting-the-last-shape)), then Free Transform Points (Direct Select with a
selection) or Free Transform Path, enabled for Path/Direct Select only (the
Pen family sees it disabled and keeps falling through to the layer
transform). Then the component commands and the Paths panel's QActions
(`set_path_context_actions_callback`, issue 396): Make Selection | Fill, Stroke | Simplify | Duplicate, Delete Path.
They act on the panel's row, so the callback first runs
`target_canvas_path_in_paths_panel` (no row targeted: target the edited path's
row); `set_active_document_path` keeps the point selection when the edited path
stays the same (`ui_path_context_menu_offers_paths_panel_commands`). A command
that would only refuse is disabled with its refusal as tooltip
(`paths_panel_command_refusals`, `kActionDisabledReasonProperty`), refreshed by
`refresh_layer_controls` and before either menu opens
(`ui_path_menu_greys_out_commands_that_would_refuse`). The menu uses the RAW hit,
so it works with Auto Add/Delete off and under the anchor tools. No menu with
no target path, during a Pen session, or in a path transform.

## Paths panel retargeting

`refresh_paths_panel` remembers the active layer of the previous refresh
(`paths_panel_last_active_layer_`); when it changes to a layer whose
transient row is a shape or vector-mask path, a targeted work/saved-path row
is dropped so `path_edit_target_path` resolves to the new layer (Photoshop
retargets on layer selection). The memory resets on a document switch (layer ids are reused). Explicit row clicks within one
layer still stick. Trace Image to Shapes activates the frontmost traced
shape for the same reason (a group has no path). The same refresh repaints
the canvas on a layer change: a pure activation composites nothing, and the
overlay (drawn from the active layer) would otherwise stay stale until an
unrelated edit (`ui_layer_row_click_shows_anchors_for_shape_layer` counts
paint events).
