# Arrow-tool selection

Split from [vector-commands.md](vector-commands.md): what a Path Select or Direct Select press picks and selects, marqueeing and moving a point or shape selection (Shift constraints, cross-layer Direct Select), and deleting the last shape of a layer.

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
selected (a marquee, then a drag on any of its lines); Shift+click adds a
segment's ends, a plain click [adds a point](vector-path-edits.md#direct-select-click-on-a-segment).
Test: `ui_direct_select_drags_marquee_selection_by_segment`. One-point drags
[weld](vector-path-edits.md#weld-while-dragging).

Shift during the drag constrains movement to the nearest horizontal,
vertical, or 45-degree axis (`constrain_drag_to_axes`), re-evaluated every
move against the total delta from the press (never latched). Anchor drags track
the total delta (`path_drag_applied_delta_`) and apply per-move increments. Shift pressed or released with a stationary cursor
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
