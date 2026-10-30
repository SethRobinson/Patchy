# Vector commands and point-editing UI

The path point-editing surface (classifier, anchor tools, selection, hints,
context menu, path free transform) and the commands that operate on existing
shapes (Simplify Path, Combine Shapes, Ungroup Layers). The model, PSD
encodings, and Pen/Direct Select basics live in
[vector-tools.md](vector-tools.md); legal boundaries in
[legal-constraints.md](legal-constraints.md); the JavaScript contract in
[vector-automation.md](vector-automation.md) (scripts and the UI share
fitting, selection coverage, stroke sampling, and mask baking through
`src/ui/vector_operations.cpp`). Tests:
`tests/ui/vector_point_editing_tests.cpp`, `tests/ui/vector_commands_tests.cpp`.

Which tools show the target outline (`target_path_outline_tool_allows`): path tools always,
with their editing glyphs; otherwise only while a Paths-panel row is targeted, the work path,
saved paths and vector masks with any tool, but a shape layer's own path only under the shape
tools (Line, Rectangle, Ellipse, Polygon, Custom Shape) (GitHub issue 238;
`ui_shape_layer_outline_shows_only_under_path_and_shape_tools`). Under a
path tool the overlay also outlines every Layers-panel-selected shape layer with hollow anchors
(outline only under Path Select; `set_panel_selected_layer_ids`); only the target gets filled anchors, handles, edits.

Overlay glyphs (`draw_pen_overlay`, `draw_path_edit_overlay`, canvas_widget_vector_tools.cpp):
visibility comes from contrast, not size (GitHub issue 105). The accent is a saturated blue (`path_overlay_accent`, 30 136
255), every glyph is three-tone, and handle lines sit on a translucent dark halo
(`draw_handle_line`: 3.5 px black at 47 percent under a 1.6 px accent line). Every path outline
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
strips across workers above 32 K pixels, same bytes; `patchy_perf_tests gradientdrag` times both);
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

- Activation: a path tool (or Move or Crop) shows a one-sentence gesture
  hint in the status bar (`tool_activation_hint_source`,
  main_window_actions_tool_palette.cpp), other tools their name; the same
  tools get a second tooltip line (`tool_tooltip_detail_source`,
  `kActionTooltipDetailProperty`).
- Hover: `update_path_hover_hint` (Pen family) and
  `update_path_select_hover_hint` (Path/Direct Select) emit only on the
  transition INTO an actionable state, so other code's confirmations survive
  mouse motion. `set_tool` resets the memory.
- Every path-tool hint lives in the status bar (none in the options bar);
  the activation and hover hints name the Ctrl-drag gesture.
- Point count: `pathPointCountChip` (a permanent status-bar QLabel, so it
  survives `showMessage`) shows "%n points selected" for two or more anchors
  under Direct Select or the Pen's Ctrl latch (not Path Select).
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

## Pressing and picking with the arrows

GitHub issue 248. `handle_path_edit_press` tries, in order: a drawn handle knob
of the target (Direct Select), a target anchor (Path Select selects its whole
group and moves it), Direct Select's panel-selected
extra layers' anchors, a target segment, the extras' segments, then the pick:
`topmost_path_pick_layer_at` walks visible, editable shape layers top-down
(pixel layers never count) and takes the first whose anchor, outline or fill
(`shape_group_containing`, topmost group first) is under the pointer. A pick
other than the target activates that layer, clears the point selection and
replays the same press on it (guarded by `path_press_repicked_`), so one press
picks and drags; activation adds no undo entry, so the drag is the one step.
The work path and the last targeted saved path are picked too (`path_pick_at`;
[rules](vector-tools.md#live-components-and-the-properties-panel-github-521)).
Then, Path Select only, a press inside the target's fill selects that group
and drags it ("Move shape"); anything else starts the marquee, which first
clears the selection (blank canvas and pasteboard alike). A Path Select
marquee selects every target group it touches (`subpath_touches_rect`:
fill overlap for closed subpaths, outline crossing for open ones), anchors
inside or not; Direct Select's selects the anchors inside. A marquee does not
pick shapes on other layers.

Path Select draws anchor squares only for the selected shape: an unselected
target (and an unselected panel-selected extra) shows just its outline, so a
blank click visibly deselects. Direct Select keeps hollow anchors on the
active path. Switching from Path Select to Direct Select clears the point
selection (`set_tool`); switching to Path Select from
Direct Select or the Pen expands carried-over points to their whole groups
(`expand_path_selection_to_groups`), so Path Select never holds part of a shape.
A Path Select press on a group (anchor, outline or fill) goes through
`path_select_press_group`: a plain press keeps a selection that already holds the
group (dragging then moves every selected shape) and otherwise selects just it;
Shift adds the group, or deselects it when it is already selected (no drag).

The point selection is (subpath, anchor) indices into the target path, so it is
tied to the target's owner (`path_edit_target_owner_key`: vector mask, document
path, text area, shape layer or work path, with its id, recorded on every
`notify_path_selection_changed`). `drop_point_selection_if_target_changed` clears
it when the owner changes: on `activate_layer`, `set_layer_edit_target`,
`set_panel_selected_layer_ids` (every Layers-panel activation ends there through
`refresh_layer_controls`), and before every press, path key and Ctrl+T, so Delete
never removes another layer's points. Hover: a fill, or another
layer's shape, is `PathHoverTarget::Fill` (Path Select's select-and-move hint;
no Direct Select hint); under Path Select every target is the shape and the hint says so.
Tests: `ui_path_select_*` in tests/ui/vector_point_editing_tests.cpp,
`ui_direct_select_drags_points_of_unselected_shapes`,
`ui_direct_select_after_path_select_drags_one_point`,
`ui_point_selection_drops_when_the_active_layer_changes`.

## Moving a selection

Direct Select (or the Pen's Ctrl latch) drags every selected anchor when the
press lands on a selected anchor OR on a segment whose two ends are already
selected (a marquee, then a drag on any of its lines); Shift on a segment
adds it, an unselected segment replaces the selection with its two ends.
Test: `ui_direct_select_drags_marquee_selection_by_segment`.

Shift held during the drag constrains movement to the nearest horizontal,
vertical, or 45-degree axis (`constrain_drag_to_axes`), re-evaluated every
move against the total delta from the press (never latched); the points follow
the mouse's projection onto that axis. Anchor drags track the total delta
(`path_drag_applied_delta_`) and apply per-move increments
(`update_path_edit_drag`). Shift pressed or released with a stationary cursor
replays the drag at the last raw pointer position. Shift at press keeps its
additive meaning. Handle drags and the Pen's in-session Ctrl anchor drag stay
unconstrained.

The marquee drag takes Space and Shift like the selection marquee: Space held
repositions the whole rect (its keyRelease branch deliberately skips
`set_tool`, which would drop the Pen's Ctrl latch mid-drag), and Shift during
the drag squares the rect (`constrain_marquee_current`, re-applied on Shift
press/release with a stationary cursor via `path_marquee_raw_current_`).
Shift at press keeps its additive-selection meaning.

Cross-layer Direct Select: with several shape layers selected in the Layers
panel, clicks, marquees, drags, nudges, Delete, and Escape span all of them.
Point keys on non-target layers live in `extra_selected_anchors_` (a per-layer
map beside `path_selected_anchors_`); eligibility per layer is the
`combine_shape_candidates` lock triple via `extra_edit_shape_layer`. Writes go
through `write_shape_layer_path` (the shared shape re-bake), armed by `arm_path_edit_undo` so N layers coalesce
into the one document-wide undo snapshot per gesture. The count chip totals
both structures. Deliberately primary-target-only: bezier handle editing, the
Combine op box, and Free Transform Points (`path_transform_subset_` snapshots
one path). Panel deselection of a layer drops its point selection
(`set_panel_selected_layer_ids`); document swaps clear both structures.

## Path context menu

Right-click under a path tool: the press records `context_press_pos_` (the
right button never pans); a release within `startDragDistance` opens
`canvasPathContextMenu` (`show_path_context_menu`, public for tests, reached
through `show_canvas_context_menu`, which falls back to the shared canvas menu
when the path menu has no target), otherwise the gesture opened nothing.
Every entry is always listed and enabled by context (object names
`pathMenu*Action`): Add Anchor Point (over a segment), Delete Anchor Point
and Convert Point (over an anchor), Delete Selected Points and Deselect
Points (with a selection; Delete Selected Shapes and Deselect Shapes under Path
Select, whose selection is whole shapes; it is the Delete key's
`delete_selected_path_anchors`, so a shape layer it empties is deleted too, see
[Deleting the last shape](#deleting-the-last-shape)), then Free Transform Points (Direct Select with a
selection) or Free Transform Path, enabled for Path/Direct Select only (the
Pen family sees it disabled and keeps falling through to the layer
transform). Then the Paths panel's QActions (`set_path_context_actions_callback`,
issue 396): Make Selection | Fill, Stroke | Simplify | Duplicate, Delete Path.
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

## Deleting the last shape

GitHub issue 315: `delete_selected_path_anchors` (Delete/Backspace under Path
Select, Direct Select or the Pen's Ctrl latch, and the menu's Delete Selected
Shapes/Points) collects every shape layer, primary target or cross-layer extra,
whose path it left with no subpath, and hands the ids to
`set_shape_layers_emptied_callback`. MainWindow removes them through
`remove_layers_in_open_undo_step` (delete_layers without its own snapshot), inside
the path edit's undo entry, so one Undo restores each layer and its path. Only
a vector shape path counts: a vector mask, a saved path or the work path stays
empty (Reveal All coverage). The deletion activates a pixel layer when no
shape layer is next, so with no path target at all a Path/Direct Select press
(and its hover) still picks the shape layer under the pointer
(`path_pick_at`) instead of refusing. Tests: `ui_path_select_delete_last_shape_removes_the_layer`,
`ui_path_select_delete_one_of_two_shapes_keeps_the_layer`,
`ui_path_menu_delete_last_shape_removes_the_layer`,
`ui_direct_select_edits_points_across_selected_layers`.

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

## Simplify Path

`Layer > Shape > Simplify Path...` (id `path.simplify`, `pathSimplifyAction`,
also in the Paths panel context menu) refits the path the pen and path tools
target (`path_edit_target_path`: vector mask, targeted Paths-panel row, active
shape layer, else the work path). Core: `simplify_vector_path`
(`src/core/path_simplify`) flattens each subpath with
`flatten_subpath_polyline` (the rasterizer's own lattice polyline) and refits
it through `fit_closed_loop` / `fit_open_polyline` (`core/path_fit`, the
per-run Schneider fit with Douglas-Peucker corners; no global smoothness
solve, the legal boundary). A subpath keeps its anchors when the refit is
empty or not smaller ("never worse"); ops, groups, the closed flag, and the
path-level fill fields survive; changed groups drop their live-shape
parameters (keyShapeInvalidated). The dialog (`simplifyPathDialog`:
`simplifyPathToleranceSpin` 0.1..20 px, `simplifyPathCornerSpin` 10..170
degrees, `simplifyPathSnapLinesCheck`, readout `simplifyPathAnchorsLabel`
"Anchors: N -> M"; settings `paths/simplifyTolerance`,
`paths/simplifyCornerAngle`, `paths/simplifySnapCurvesToLines`) previews live
through `CanvasWidget::replace_path_edit_target`, the un-armed half of
`apply_path_edit`, under the preview edit lock; cancel restores the snapshot
of the OWNING object (whole `Layer` or `DocumentPath`, so a saved path keeps
its verbatim PSD bytes), accept restores then re-applies after one
"Simplify path" undo entry. Removing nothing, the readout says whether a
larger Tolerance would help and OK adds no undo entry
(`ui_simplify_path_from_canvas_menu_previews_and_explains_no_change`). Non-modal (`run_non_modal_dialog`): the commit
re-validates the document and target first. Script: `layer.simplifyPath({tolerance, cornerAngle,
snapCurvesToLines})` -> `{anchorsBefore, anchorsAfter}`. Tests: `path_simplify_*` in
`tests/core/vector_shape_tests.cpp`.

## Combine Shapes

`Layer > Shape > Unite Shapes / Subtract Front Shape / Intersect Shapes /
Exclude Overlapping Shapes` (ids `layer.combine_unite`,
`layer.combine_subtract`, `layer.combine_intersect`, `layer.combine_exclude`)
merge the Layers-panel selection (`combine_shape_candidates`,
`src/core/shape_combine`): two or more unlocked, editable shape layers with
paths, all siblings of one parent; folders expand to nothing (root ids only).
Enable state follows the selection (`refresh_combine_shapes_action_states`,
also on pure multi-selection and path-selection changes); context menus and
path components: [vector-tools.md](vector-tools.md#live-components-and-the-properties-panel-github-521). Semantics are the renderer's
sequential combine: the BOTTOM-most layer is the base and keeps its id, name,
appearance, styles, masks, origination, and its groups' own ops; every group
of each front layer is appended in stacking order with the chosen op (Add /
Subtract / Intersect / Xor), groups renumbered from `next_shape_group()`.
Front styles, masks, and origination vanish with the layers. A compound
front (a donut) therefore combines group by group, which is also
Photoshop's per-component model; the Path Select Combine box retunes ops
afterwards. One undo entry per command ("Unite shapes", "Subtract front
shape", "Intersect shapes", "Exclude overlapping shapes"). Script:
`doc.combineShapes([layers], "unite" | "subtract" | "intersect" | "exclude")`.
Tests: `tests/core/vector_raster_tests.cpp` (`combine_shape_layers_truth_table`,
`combine_shape_candidates_refuses_mixed_parents_locks_and_fill_layers`),
`tests/ui/vector_commands_tests.cpp`.

## Ungroup Layers

`Layer > New > Ungroup Layers` (id `layer.ungroup`, default Ctrl+Shift+G;
`layerUngroupAction`; also in the Layers panel context menu when the active
layer is a folder) releases every selected folder's layers into its parent
at the folder's position, composite order unchanged (`ungroup_layer`,
`src/core/layer_tree`). New Folder (`layer.new_folder`) is Ctrl+G, so the
pair matches Photoshop. Photoshop's rule applies: the folder's own opacity,
blend mode, masks, and style are dropped, and the status line says so when
one was set. Lock All on the folder refuses. One "Ungroup layers" undo
entry; the topmost released layer becomes active; the folder leaves the
session's collapsed set. Script: `layer.ungroup()` returns the released
layers top to bottom.

## Copy as SVG

See [svg.md](svg.md) (UI behavior): `Edit > Copy as SVG` puts the selected
layers on the clipboard as image/svg+xml and text; Paste reads it back in
place.

## Path free transform

Ctrl+T with Path Select or Direct Select active (and a targetable path)
starts a PATH transform session instead of the layer one: a rotated-box
overlay over the path (the bezier hull, handles included), over the
Direct Select anchor subset (Photoshop's Free Transform Points), or over the
shapes Path Select has selected (whole groups; `ui_path_select_transform_moves_only_selected_shapes`), with the
usual move/scale/rotate, arrow nudges, Enter/Esc, tool-switch commit,
document-switch cancel. Ctrl+T during a session is a no-op. The commit is
ONE `apply_path_edit` undo entry ("Transform path") routed to the active
target (panel path, vector mask, shape layer with live annotations dropped,
or work path), then re-rasterizes. Lives in canvas_widget_vector_tools.cpp
(`path_transform_*`), separate from the pixel session. `begin_path_transform`
has two callers: `MainWindow::transform_active_layer_dialog` (Ctrl+T, tried
before the layer transform; the layer position lock does not apply to path
geometry) and the path context menu. Never call it from
`begin_free_transform`, whose internal callers stay layer-only. Corner-handle
aspect locking and Shift share the pixel session's rules and predicate
([tools.md](tools.md)). Hover cursors are Free Transform's own
(`apply_path_transform_cursor` over `set_transform_cursor_for_handle` and
`transform_rotate_cursor_at`: resize turned with the box, move inside, each
corner's rotate cursor off the box; no stem handle), and a drag keeps the
grabbed zone's cursor (`ui_path_transform_shows_free_transform_cursors`).

Distort gestures (GitHub issue 248) are Free Transform's ([free-transform.md](free-transform.md)):
Ctrl+drag a corner distorts it, Ctrl+drag a side moves it, Ctrl+Shift+drag a side
slides it (skew), Ctrl+Alt+Shift+drag a corner applies perspective, and Alt, read
live, mirrors any of them about the center. The session keeps the pixel session's
model: the rect-plus-angle box (`path_transform_affine`, the same map as
`path_transform_map_point`) plus four corner offsets in the original box's frame
(`path_transform_corner_offsets_`, TL, TR, BR, BL of the original box;
`path_transform_corner_index` finds the box corner under an on-screen handle, so
flips work). The gesture, the offset update, the convex-quad guard, the distorted
handle directions and the gesture cursor are the pixel session's own code, shared
as `CanvasWidget` statics (`transform_drag_kind_for_mode`,
`transform_distorted_offsets`, `transform_quad_winding`,
`transform_quad_handle_direction`, `apply_transform_gesture_cursor`); never fork
them. Once distorted, the preview and commit map every anchor and handle (of the
Free Transform Points subset when there is one) through the homography onto the
corner quad, the map a distorted Free Transform gives a shape layer, and the hit
zones, box and handles follow the quad (`path_transform_handle_corners`). An
undistorted session keeps the affine path byte for byte.

Live fill: a shape layer or vector-mask target re-rasterizes with the preview
path on every drag frame, nudge and distort step (`refresh_path_transform_live_fill`
through the un-armed `replace_path_edit_target`, the Simplify Path preview's
route; one layer's re-raster per change, the Direct Select drag cost). The
session snapshots the owning layer at Ctrl+T; Esc, a document switch and every
other cancel restore it (`restore_path_transform_owner`), but only while the
layer still holds the session's last write, so a history restore wins. Commit
restores first, then applies the result as the one armed "Transform path" entry.
Undo during the session cancels it (MainWindow::undo) and Redo does nothing, so
neither reaches the history behind the un-armed writes. Work paths, saved paths
and text-area paths have no fill and keep the outline preview. Coverage:
`ui_path_transform_fill_follows_the_box_live`. The canvas key filter
refreshes the cursor when Ctrl, Alt or Shift changes over the box. Coverage:
`ui_path_transform_ctrl_drag_distorts_corner_and_side`,
`ui_path_transform_ctrl_alt_drag_is_symmetric`,
`ui_path_transform_ctrl_shift_drag_skews_side`,
`ui_path_transform_ctrl_alt_shift_drag_is_perspective`,
`ui_path_transform_distort_never_folds_the_quad`,
`ui_path_transform_modifiers_show_gesture_cursors`.
