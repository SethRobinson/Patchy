# Point, segment and component commands

GitHub #555. Commands that change the structure of an existing path (where its subpaths open,
join or split) and that act on whole components (shape groups). The point-editing surface they
plug into (context menu, tool roles) is in [vector-commands.md](vector-commands.md), the point
and shape selection in [vector-commands-selection.md](vector-commands-selection.md); the model
in [vector-tools.md](vector-tools.md). Geometry: `src/core/path_point_edits.{hpp,cpp}`;
canvas side: `src/ui/canvas_widget_path_edits.cpp`; MainWindow side:
`src/ui/main_window_path_components.cpp`. Tests: `path_break_*`, `path_join_*`,
`path_delete_segment_*`, `path_merge_points_*`, `path_components_*` in
`tests/core/vector_shape_tests.cpp`; `path_*` in `tests/core/path_edits_tests.cpp` (rendered
checks, a seeded sweep of every point command, Merge and Split Shapes); `tests/ui/vector_path_edits_tests.cpp`.

Classic bezier path editing (Illustrator 88 era, expired); no legal boundary applies beyond
[vector-tools.md](vector-tools.md#patents-and-trademarks-assessed-july-2026).

## Point and segment commands

Layer > Shape lists four commands (hotkey ids, no default keys), after Shape Appearance, Simplify
Path, the four combine commands, Merge Shapes and Split Shapes and before the component commands:
Break Path at Point (`path.break_at_point`, `pathBreakAtPointAction`), Join End Points
(`path.join_end_points`), Merge into One Point (`path.merge_points`) and Delete Segment
(`path.delete_segment`). The path context menu repeats them after Convert Point
(`pathMenuBreakAtPointAction`, `pathMenuJoinEndPointsAction`, `pathMenuMergePointsAction`,
`pathMenuDeleteSegmentAction`).

What they act on (`CanvasWidget::plan_path_point_command`): the menu's press first (an anchor for
Break, a segment for Merge and Delete Segment), else the Direct Select point selection of the
target path. Path Select's selection is whole shapes, so under it only the press counts. A
selected segment is one whose two end points are both selected, which is what a Direct Select
Shift+click on a segment selects (`path_segments_between_points`); there is no separate
segment selection.
A command that does not fit is disabled, never hidden (`refresh_path_edit_action_states`, run on
every selection change, tool change and Shape menu show); a hotkey on a stale state reports why
in the status bar. Not for text-area paths (a text area must stay one closed outline). Every
edit is one undo step through `apply_path_edit` and works on shape layers, vector masks, saved
paths and the work path; a shape layer left with no subpath is deleted in the same step
(`shape_layers_emptied_callback_`, as Delete does).

- **Break Path at Point**: a closed subpath opens at the anchor, which becomes both end points at
  the same place; an interior anchor of an open subpath splits it in two. With several points
  the path splits at each one into separate open pieces of the same shape. End points do not
  break. Each piece keeps the handles that shape its segments; the handle that drove the removed
  side collapses. The new end points stay selected, so Join End Points undoes it. Open pieces fill
  their own chord: breaking every point of a filled shape leaves two-point pieces with no area
  (the fill goes, a stroke stays), which is what open paths do everywhere.
- **Join End Points**: two selected end points join: the ends of one open subpath close it with a
  straight segment; ends of two open subpaths connect them into one, in the first one's place,
  shape group and op (the second is reversed as needed). End points closer than 0.01 px merge
  into one point instead of adding a zero-length segment. Any other selection closes every open
  subpath (three points or more) with a selected point (`close_open_subpaths`), so Shift+click two
  points, Break, Join splits a shape into two closed halves.
- **Merge into One Point**: two joinable end points become one point at their midpoint (the
  common "average and join"); otherwise a row of two or more neighbouring selected points of one
  subpath (cyclic on a closed one), or the segment under the press, collapses into one point at
  their average, keeping the incoming handle of the first and the outgoing handle of the last.
  Refused when the subpath would keep fewer than two points or the row is the whole loop.
- **Delete Segment**: removes the segment and leaves the path open there: a closed subpath opens
  (the run starts after the segment), an open one splits into two subpaths of the same group. A
  piece left with one point goes.

Pieces of one subpath stay adjacent in the same shape group, so the rasterizer's consecutive-run
grouping and the combine op are unchanged. Open subpaths still fill their implied chord. PSD
writes open subpaths as such (`ui_path_point_commands_edit_menus_and_undo` round-trips one).

## Component commands

A component is a shape group. Under Path Select or Direct Select (`path_edit_selected_groups`:
every group with a selected anchor):

- **Edit > Cut / Copy** (and the menu's Cut Shapes, Copy Shapes) copy the selected components
  instead of pixels or layers (`path_component_clipboard_active`). The payload is private to the
  app (`ClipboardPayload::path_components`, plus the source shape layer restricted to them in
  `path_components_layer` and its pattern tiles); the system clipboard is left alone. Cut removes
  them in one "Cut shapes" step.
- **Edit > Paste / Paste in Place** (and Paste Shapes) with copied components pastes them in place
  into the current path target (shape layer, vector mask, saved or work path), renumbered from
  `next_shape_group` with their ops kept, and selects them. A shape layer paints them with its own
  paint, as Photoshop does for a pasted path; a merged shape (parts) gets one new part with the
  copied paint. With no path target (a pixel layer active) Paste makes a new shape layer: a clone of
  the source layer with only those components, or the options-bar appearance for components copied
  from a path.
- **Color Selected Shapes...** (`path.shapes_color`) picks a solid fill for the selected components
  of a shape layer (`recolor_vector_shape_components`). The layer becomes a merged shape
  ([layer-merging.md](layer-merging.md#vector-representation-and-editing)): one part keeps the old
  paint for the rest, the selected components get a part of their own above it (a part whose
  painting components are all selected just changes color; selecting every one changes the
  layer fill, no parts). Subtract components that follow a selected one go into its part too, so
  holes stay holes. Refused (disabled, reason as tooltip) when the part combines components with
  Intersect or Exclude, when only cutters are selected, or on an inverted shape. PSD saves it as
  the merged-shape group of native shapes and reads it back as one layer.
- **Move / Copy Selected Shapes to New Layer** (`path.shapes_to_new_layer`,
  `path.shapes_copy_to_new_layer`) make a shape layer directly above the source: for a shape
  layer, a clone (styles, masks, paint parts) holding only the selected components; for a path or
  vector mask, the options-bar appearance. Move also removes them from the source (a source left
  empty is deleted). One undo step; the components stay selected on the new layer.

A new layer whose first component subtracts would paint the canvas around it, so its first
component becomes Add (`make_first_component_paint`); the rest keep their ops. The path context
menu lists the six component entries under Path Select and Direct Select
(`pathShapesCutAction` ... `pathShapesCopyToNewLayerAction`), before the Paths panel commands.

## Direct Select click on a segment

Under Direct Select (and Path Select's Ctrl swap; `direct_select_click_adds_point`), a segment of
the target path under the pointer shows `direct_select_add_cursor` (the arrow with a plus for the
anchor badge) and the hint "Click to add a point, drag to reshape, Shift+click to select the
segment". A plain press on a segment selects its two ends as before and records a pending add
(`path_segment_click_`); the drag waits for `QApplication::startDragDistance` and, once past it,
drops the add and drags the segment as before. Released before that, the click runs the Pen's
Add anchor edit (`apply_pen_hover_edit`, exact de Casteljau split, so the curve keeps its shape;
one "Add anchor" step) and selects the new point, ready to drag. Shift+click selects the
segment's two points without adding; Alt+click selects the whole subpath. Over a point or a
handle the cursor stays the plain Direct Select arrow. The Pen's Ctrl latch never adds (the Pen
adds through its own Auto Add/Delete click). Test: `ui_direct_select_click_on_segment_adds_a_point`.

## Weld while dragging

Dragging exactly one point with Direct Select (or the Pen's Ctrl latch) snaps it onto any other
point of the target path within `kWeldSnapPx` (8 screen pixels): the drag delta is corrected so
the point lands exactly there and the overlay rings the target (`kWeldRingRadiusPx`). What the
release does (`plan_path_weld`), with a second ring and a status hint while it is held (shown
after the edit, whose undo label also reaches the status bar):

- Two end points that `join_path_end_points` accepts (two open paths, or the ends of one with
  three points or more) join into one point: "Release to join the end points".
- The next or previous point of the same subpath (cyclic on a closed one, across its closing
  segment too) merges with it, as Merge into One Point does for a segment: one point at the
  snapped place, keeping the two outer handles, so a rectangle corner dropped on the next corner
  leaves a triangle: "Release to merge the points".
- Anything else only snaps and leaves two points at one place, since a path cannot branch; so do
  the two points of a two-point line and the corners of a triangle, which would leave nothing to
  fill: "Snapped to a point. Only end points or neighbouring points become one point."

The join or merge happens inside the drag's own undo entry (`weld_dragged_path_point`). Alt held
during the drag places the point freely (Alt at the press keeps its handle and subpath meanings).
Shapes on other layers, Path Select drags and multi-point drags never snap.
Tests: `ui_path_weld_on_drag_joins_end_points`, `ui_path_weld_on_drag_merges_neighbours`.

## Merge Shapes

Layer > Shape > Merge Shapes (`path.merge_shapes`, `pathMergeShapesAction`, after the four
combine commands), the path menu under Path Select and Direct Select, the Layers panel's Combine
Shapes submenu and a Merge Shapes button on the Path Select / Direct Select options bar
(`vectorMergeShapesButton`, a framed QPushButton after Combine so the bar keeps its 26 px row; the reporter's "Apply operation", as a one-click
command rather than a mode so the live ops stay the default). It acts on the selected components,
else every component of the target path; with no component picked and two or more combinable
shape layers selected, they first combine into the bottom one (`combine_shape_layers`, Add).

`merge_path_components` (core) takes the components alone, in path order, the first as the base,
and traces their combined fill with `vector_path_fill_contours` (vector_raster.cpp): the
stroker's boundary walk, flattened edges split at crossings and kept where the two sides fill
differently, on the 1/256 lattice, outer loops clockwise and holes counterclockwise. Each loop is
refit by `fit_closed_loop` (tolerance 0.25 px, corners at 45 degrees, near-straight runs snapped
to lines, 8 refinement passes), so rectangles keep exact straight corners and curves come back as
a few smooth anchors. The result is one component (holes as more subpaths of it, even-odd) in the
first one's place, group number and op; components that all subtract merge into one cutout and
keep subtracting. The render matches the live combine to the antialiasing when the merged
components start the path (a whole layer, or two shapes picked from it); a run in the middle is
combined on its own first. Shapes with no area left (Intersect of shapes that miss) leave no
subpath; a shape layer left empty is removed in the same step. A merged shape layer whose picked
components belong to different paint parts refuses ("Shapes with their own colors cannot merge
with others"). Live-shape annotations of the merged groups drop. One "Merge shapes" undo step;
the merged component stays selected. Deterministic: lattice coordinates, fixed-order loops,
`std::atan2` turn choice as the stroker already uses. Vector booleans are expired prior art
([vector-tools.md](vector-tools.md#patents-and-trademarks-assessed-july-2026)).

Only shapes that overlap are traced (#587); shapes apart still join the merged shape with their
points unchanged (the reporter's follow-up: separate shapes belong in the one shape, only their
points must not change). With two or more picked and none of them Intersect (Intersect clears
everything outside it, so it acts on shapes it misses), the picked components first group into
clusters whose own fills overlap or touch: each component's fill outline
(`vector_path_fill_contours` of it alone, as integers on the 1/256 lattice) is tested against
the others by bounds, then edge crossings or contact, then one loop inside the other's fill
(nested shapes count; a shape in another's hole does not). Each cluster of two or more is traced
as above; a component that meets none of the others is copied subpath for subpath (anchors and
handles exact). All of them become the one merged component in the first one's place, group
and op. The clusters' fills are apart, so their union under the component's even-odd fill is
the combined fill: each cluster combines from nothing, so a cluster's leading cutters (and a
lone cutter) cut nothing and go, unless every picked component cuts. Live-shape annotations of
every picked group drop. One picked component still bakes on its own (a self-crossing outline
cleans up). The merged shape stays selected.
Tests: `path_merge_components_*`, `ui_path_merge_shapes_bakes_the_combine`,
`ui_path_merge_shapes_keeps_separate_shape_points`.

## Split Shapes

Layer > Shape > Split Shapes (`path.split_shapes`, `pathSplitShapesAction`, after Merge Shapes),
the path menu under Path Select and Direct Select (after Merge Shapes) and a Split Shapes button
after Merge Shapes on the options bar (`vectorSplitShapesButton`) break each selected component,
else every component of the target path, into one component per outer outline
(`split_path_components`, #587). Each subpath is flattened (`flatten_subpath_polyline`, lattice
integers): subpaths whose outlines cross or touch stay together; otherwise one inside an odd
number of the component's other subpaths is a hole and goes with its deepest container, so a
ring keeps its hole and an island in that hole (depth 2) is a piece of its own with its own
holes. Anchors, handles and subpath order are kept; the first piece keeps the group number, the
rest take new numbers (`next_shape_group`) right after it, with the component's op. The pieces'
fills are apart, so the render does not change, except for Intersect components, which do not
split (each piece would clear the others). On a merged shape layer each new piece joins its
source's paint parts (`copy_vector_part_groups`); live-shape annotations of split groups drop.
Disabled (reason in the tooltip) when no selected component has two pieces; not for text areas.
One "Split shapes" undo step; the pieces stay selected. Merge Shapes of shapes apart, then Split
Shapes, gives back the original shapes. Search Commands describes it with its status tip.
Tests: `path_split_components_one_shape_per_outline`,
`ui_path_merge_shapes_keeps_separate_shape_points`.

## Aligning shapes inside a layer

Layer > Arrange > Align (and its options-bar buttons) on two or more components picked with Path
Select or Direct Select aligns them inside their layer or path (`align_selected_path_components`)
to their joint bounds, or the canvas with Align to Canvas; one "Align shapes" step, the shapes
stay selected, live shapes stay live. Distribute still works on layers only.
Test: `ui_path_align_shapes_inside_a_layer`.

## Spikes after Break Path at Point (open report)

Break Path at Point on several points must leave a filled shape without spikes (long thin wedges
along its edges, reported October 31, 2026). The reported steps (Rectangle tool, a corner dragged
into a quad, one, several and all points marquee-selected, Break) are pinned in
`ui_path_break_several_points_renders_cleanly`, fill only and with an 8 px miter stroke in every
alignment, checking the layer bounds and the canvas at 100% and 300% (Dynamic Vector Preview), and
in core with `path_break_at_points_renders_without_spikes` and the seeded sweep
`path_point_commands_sweep_renders_inside_the_shape` (60 random convex and curved shapes, every
point command, fill and three alignments): no spike appears. The wedge shape (widest at a vertex,
along an edge's extension) is what a miter join draws at a near-reversal; the stroker allows that
up to the miter limit (100, Photoshop's). The cause in the reported file is still open; the tests
guard every path the commands produce.

## Usage sweep (October 31, 2026)

Everyday shape and path tasks in the current build: how, steps, and the test that drives it.

| Task | How | Steps | Covered by |
|---|---|---|---|
| Combine shapes into one outline, then move or edit it | Draw, Shift-drag a second (Add), Merge Shapes button; drag with Path Select or edit points | 3 | `ui_path_merge_shapes_bakes_the_combine` |
| Cut a hole | Combine: Subtract, draw the hole (or pick a shape and choose Subtract); Merge Shapes bakes it | 2 to 3 | `path_merge_components_matches_the_live_combine` |
| Merge two shape layers into one outline | Select both in Layers, Merge Shapes (Layer > Shape or Combine Shapes submenu) | 2 | `ui_path_merge_shapes_bakes_the_combine` |
| Weld two open paths at a point | Direct Select drag an end onto the other end | 1 | `ui_path_weld_on_drag_joins_end_points` |
| Merge two neighbouring points into one | Direct Select drag a point onto the next one (a rectangle corner: a triangle) | 1 | `ui_path_weld_on_drag_merges_neighbours` |
| Fuse two shapes that touch | Merge Shapes (the shared edge goes) | 1 | `path_merge_components_matches_the_live_combine` |
| Split a shape into two closed halves | Click a point, Shift+click another, Break, Join End Points | 4 | `ui_path_split_shape_with_break_and_join` |
| Open a path or cut a segment out | Right-click the segment > Delete Segment (or Shift+click it, Delete Segment) | 1 to 2 | `ui_path_point_commands_edit_menus_and_undo` |
| Add a point on an edge | Direct Select click on the edge (the cursor shows a plus) | 1 | `ui_direct_select_click_on_segment_adds_a_point` |
| Close an open path | Click a point of it, Join End Points | 2 | `path_close_open_subpaths_splits_a_shape_in_two` |
| Extend an open path | Pen click on its end point, click on, Enter | 2+ | `ui_pen_click_on_an_open_end_continues_the_path` |
| Reshape point by point | Direct Select drag points and handles | 1 per edit | vector_point_editing_tests |
| Convert a corner or smooth point | Right-click > Convert Point, Alt+click with the Pen, or the Convert Point tool | 1 | `ui_path_context_menu_edits_anchors` |
| Collapse an edge to one point | Right-click the edge > Merge into One Point | 1 | `ui_path_point_commands_edit_menus_and_undo` |
| Duplicate one shape | Path Select Alt-drag | 1 | `ui_path_select_alt_drag_duplicates_*` |
| Align shapes inside one layer | Path Select click, Shift+click, Layer > Arrange > Align | 3 | `ui_path_align_shapes_inside_a_layer` |
| Recolor one shape | Path Select click, Color Selected Shapes (menu or right-click) | 2 | `ui_path_components_color_and_move_to_new_layer` |
| Move a shape to another layer | Path Select click, Ctrl+X, pick the layer, Ctrl+V (or Move Selected Shapes to New Layer) | 4 (2) | `ui_path_components_cut_copy_paste_round_trip` |
| Turn a path into a selection and back | Right-click > Make Selection (Ctrl+Enter); Paths panel > Make Work Path | 1 each | `ui_paths_panel_fill_stroke_and_make_selection` |
| Stroke a path with a brush | Right-click > Stroke Path | 1 | `ui_paths_panel_fill_stroke_and_make_selection` |
| Edit a vector mask | Target the mask, then every command above (Merge Shapes too) | as above | the shared target code; vector_mask_editing_tests |

Gaps left: Distribute for shapes inside a layer; Merge Shapes across differently colored parts of a
merged shape (refused with the reason); a knife that cuts a closed shape along a drawn line.

### Right-click menus

Under Path Select, Direct Select and the Pen family the path menu lists, by what is under the
pointer: Add Anchor Point (a segment), Delete Anchor Point and Convert Point (a point), Break,
Join, Merge into One Point and Delete Segment (the point or segment, else the selection), the
selection's Delete and Deselect, Free Transform, then under Path Select and Direct Select the
combine commands (with two or more shapes), Merge Shapes, Split Shapes, Cut/Copy/Paste Shapes,
Color Selected Shapes and Move/Copy to New Layer, then the Paths panel commands. Every disabled
entry names what it needs in its tooltip. With no path to edit (a pixel layer, nothing picked) the
canvas menu is the general one.
