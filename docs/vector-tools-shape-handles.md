# Vector tools: handles and drag points on the shape tools (GitHub #690, #716)

Split from [vector-tools.md](vector-tools.md). What a shape tool draws on the active shape so it can be edited
without leaving the tool: the eight-handle box (resize, and rotate from just outside a corner) in Shape, Path and
Pixels modes, and the two drag points of a live Polygon and Line. Legal rules:
[legal-constraints.md](legal-constraints.md#shape-drag-points); record:
[patent-research-shape-handles.md](patent-research-shape-handles.md).

## Resize handles on the shape tools

With a shape tool active in Shape mode and an editable shape layer active (just
drawn, or picked in the Layers panel), the canvas draws the box the W / H fields
describe (`shape_resize_box_rect`: the path's bounds, or those of the components
Path Select picked) with the Move tool's transform controls, without the rotate
stem: the dashed frame (`draw_transform_frame`) under the shape's path outline,
which it coincides with on a rectangle, and the eight squares
(`draw_transform_handle_squares`) over it.
`transform_handle_at(..., TransformHitZones::ResizeHandles)` takes a press only on
a handle square (14 px; the nearest one where squares overlap). Every other press,
inside the box included, draws a new shape; Ctrl stays the temporary Path Select.
Shift on a corner keeps the proportions (the pointer's travel projected onto the
box diagonal, the Move box's rule), Alt resizes about the center, the grabbed side
moves on whole pixels through the snap targets (the layer's own edges excluded,
`free_transform_snap_exclude_ids`), a handle stops 1 px before its opposite side
(a mirrored shape would stop being live), and Esc cancels.

The canvas side (canvas_widget_shape_resize.cpp) follows the Gradient Fill
handles: `ShapeResizePhase` Preview / Commit / Cancel through
`set_shape_resize_callback`, with the box at the press and the box now.
`MainWindow::handle_shape_resize_edit` keeps the layer as it was at the press
(`run_shape_handle_edit`, shared with the drag points below), scales that copy for
every preview (`transform_path_component_target`, no history, the W / H fields
following) and commits through `apply_path_component_geometry`, the W / H fields'
own path: one "Shape size" undo step, and live shapes stay live. The box hides
while a shape is dragged out, with a mask targeted, for a hidden, locked or
position-locked layer, with several layers selected, and whenever the shape's path
outline is hidden (a dismissed path row, View > Show > Target Path, View > Extras).
In Path mode it rides a path instead ("The box in Path mode" below), in Pixels mode
the pending shape ("A pending pixel shape").

An uneven scale (a side handle, or one W / H field) would stretch a rounded
rectangle's corner arcs into a curve its live parameters no longer generate.
`transform_path_component_target` rebuilds such a rectangle from its scaled
parameters (`keep_rectangles_round`; the radii follow the mean factor, as in
`scale_live_shape_params`), so the corners stay round and Radius keeps editing
it. A Line resized unevenly is stretched like any path and stops being a live line
(a line now shows its end points instead of the box, so this takes a W / H edit).

The handles are the same plain bounding-box resize as the Move tool's transform
controls, not a live-shape gizmo: a drag changes the box and nothing else. No
handle edits a corner radius, a side count or a star inset; those stay in the
options bar and the Properties panel
([Patents and trademarks](vector-tools.md#patents-and-trademarks-assessed-july-2026)).

## The box in Path mode

In Path mode (GitHub #716) the box shows on the components Path Select picked on the
work path or on the active saved path (`shape_resize_document_path`), under every shape
tool, with the hide cases of the Shape mode box. A shape tool's Path-mode drag leaves
the component it drew picked (`add_subpaths_to_work_path`, shape tools only: the Pen's
paths stay unpicked), so the box and the bar's Width / Height act on it at once. There
are no drag points here: a polygon or a line path takes the box, and a box with no area
(a hairline) shows none. The callback carries a `ShapeBoxEdit` (the layer or the path,
the box before and now, or an angle). `MainWindow::handle_shape_box_edit` runs a path's
drag through `run_path_handle_edit`, the layer bookkeeping with a copy of the
`DocumentPath` (path and live parameters) from the press: previews without history, one
"Shape size" step at the release through `apply_path_component_geometry`, Esc restores.
Live shapes on the path stay live as on a layer. Ctrl stays the temporary Path Select.

## Rotating from outside a corner

In all three modes a press just outside a corner of the box turns the shape about the
box's center (GitHub #716). The zone is the Move box's:
`TransformHitZones::ResizeHandlesAndRotate` is the eight squares plus the
EdgesAndCorners rotate band (`kCornerRotateReach`, 28.8 px from a corner, outside the
box); further out a press still draws a new shape (or paints a pending one). The cursor
is the corner rotate cursor of the Move box and Free Transform
(`transform_rotate_cursor_at`). The drag is the pointer's turn about the center since the
press, in steps of 15 degrees with Shift (Free Transform's step); the frame and squares
draw turned while it lasts. Nothing else is drawn: no stem, arc, pivot mark or readout
([legal](legal-constraints.md#shape-drag-points)).

- **Shape and Path modes.** `rotate_path_component_target` turns the layer (whole: Free
  Transform's rotate through `transform_layer_vector_data`, so gradients, patterns and a
  vector mask turn too) or the picked components; one "Rotate" undo step
  (`apply_path_component_rotation`), Esc cancels. After the release the box is the
  turned shape's upright bounds again.
- **Live shapes.** `rotate_live_shape_params` (core) keeps a shape live when its
  parameters can say the turn: a line and a regular polygon at any angle, and a
  rectangle (its corner radii go round), an ellipse or a stretched polygon at a multiple
  of 90 degrees (`rotation_cos_sin` is exact there, so the box stays on whole pixels).
  The component is regenerated from the turned parameters. Any other turn leaves a plain
  path of the same points and drops the annotation (Photoshop's keyShapeInvalidated):
  Radius and the Properties rows stop editing it, the box, the point tools and Shape
  Appearance still do.
- **Pixels mode.** The pending shape keeps the angle (`PendingPixelShape::angle`) and its
  box stays on the turned shape, so the handles then resize it along its own sides (the
  pointer is turned back about the box's center, the opposite side stays where it is on
  the canvas) and a press inside the turned box moves it. `pending_pixel_shape_path`
  turns the finished path; a turned shape is painted from that path by
  `draw_shape_path_pixels`, the shape layer's renderer, which is also the preview's. An
  upright shape keeps its painters and bytes.

## A pending pixel shape

A shape drawn in Pixels mode that is not painted yet ([tools.md](tools.md#pending-pixel-shapes),
GitHub issue 716) shows the same eight-handle box and uses the same drag
(`ShapeResizeDrag::pixel_shape`, no layer and no callback: the drag sets the pending shape's
box). Rectangle, Ellipse, Custom Shape and Polygon all take the box: a pending polygon keeps no
drag points, and a stretched box scales it. Two differences from a shape layer's box: a press
inside the box moves the shape (`TransformHandle::Move`; a shape layer's inside draws the next
shape and Ctrl moves it), and a press outside it paints the shape and goes on to draw the
next. The move is a whole-shape drag: the pointer's travel on whole pixels through
`snap_whole_shape_drag` with the box as the one source rect, so both edges and the center of the
box are the snap sources on each axis and the Move tool's alignment guide shows
([legal](legal-constraints.md#shape-drag-points): never a source chosen from where the box was
grabbed; a turned shape's source rect is its upright bounds). The guides clear at the release and
on Esc. Esc mid-drag puts the box and its angle back; a second Esc drops the shape.

## Drag points on a live Polygon and Line

A live Polygon and a live Line show two drag points in place of the box: the two
points of the drag that drew them (canvas_widget_shape_points.cpp,
`shape_live_points`). Rectangle, Ellipse and Custom Shape keep the box: their two
drawing points are corners it already has.

- **Polygon** (a star too): its center and its first vertex. Dragging the vertex
  sets the radius and `polygon_angle` exactly as the Polygon tool's center-out drag
  does (`set_live_polygon_first_vertex`, the math of `commit_polygon_drag`), so the
  polygon turns and resizes about its center. Dragging the center moves it by the
  pointer's travel on whole pixels and snaps it like a whole-shape drag (below).
  Sides and Star inset never change.
- **Line**: its two ends. Dragging one moves that end and leaves the other
  (`set_live_line_point`); the weight and the arrowheads stay and the box is
  refitted to the hull of the regenerated line, as when a line is drawn.

Which shape gets them: exactly one component (the layer's only one, or the only
one Path Select picked) that `resolve_live_shape_group` still resolves, and for a
polygon a square box (`live_polygon_is_regular`: what the drawing drag makes). The
box stays for a polygon stretched through W / H or the Create Polygon dialog's
W x H, for a polygon with an edited point, for a line a W / H edit stretched, and
for a layer of several components with none or several picked.

Rules shared with the box: the points show wherever the box would
(`shape_resize_layer` holds every hide case) and under every shape tool. A press
takes a point within 8 px (the nearer one, a tie to the second point); every other
press, inside the shape included, draws a new shape; Ctrl stays the temporary Path
Select; a click without travel changes nothing; Esc cancels. The vertex and a
line's end go through `snapped_document_point_f` like the second point of the
drawing drag (whole pixels, the snap targets, their own layer's edges excluded).
Shift and Alt do nothing: the drawing drags of Polygon and Line have no modifier
rule to repeat. A point dropped under a pixel from the other one is ignored and the
shape stays as it last was.

The center point moves the whole polygon, so it snaps as a
[whole-shape drag](alignment.md#whole-shape-drags) does (GitHub #695, follow-up of
2026-11-13). `update_shape_point_drag` rounds the pointer's travel to whole pixels
and hands it to `snap_whole_shape_drag`, the step a Ctrl drag of the shape uses,
with one source rect: the polygon's bounds at the press (`shape_group_snap_bounds`,
the tight bounds of its curves). Both edges and the center of those bounds, on each
axis, are compared with guides, the grid, the canvas and other layers (its own layer
left out), and the Move tool's alignment guide shows. An axis that snaps lands
exactly on its line; an axis that does not keeps the whole-pixel travel. View > Snap
turns it off. The sources are always all of those features, never one chosen from
the grabbed point and never the center alone
([legal](legal-constraints.md#shape-drag-points)). The guides clear at the release,
on Esc and when the document is swapped under the drag.

`ShapePointCallback` carries the component's new `LiveShapeParams` through the
box's Preview / Commit / Cancel phases. `MainWindow::handle_shape_point_edit` runs
them through `run_shape_handle_edit` and regenerates the component with
`replace_shape_layer_live_component` (`replace_live_shape_group`, never a stretch
of the path), so the shape stays live: previews without history, one "Edit shape"
undo step at the release, the W / H fields following.

Drawing (`draw_shape_points`): two discs in the Gradient Fill handle roles
(`canvas_gradient_handle_ink` / `_fill` / `_active`), placed through
`widget_position_f` outside the upright overlay frame, so they follow zoom and view
rotation. A polygon's center carries a dot. Nothing else is drawn: no stem, arc or
readout.

PSD: the writers are unchanged. A turned polygon is the plain path the reader
recognizes at any angle (`recognize_live_polygon`, GitHub #643); a re-aimed line
writes its Line origination like one whose ends were typed into Shape Appearance.

Tests: tests/ui/shape_resize_handles_tests.cpp (`ui_shape_tool_`) for the box and
the points, `ui_shape_tool_path_mode_box_resizes_the_drawn_path` and
`ui_shape_tool_rotate_band_turns_*` among them;
`ui_pending_pixel_shape_turns_by_the_band_outside_a_corner` and
`ui_pending_pixel_shape_turned_pixels_match_a_turned_shape_layer` for Pixels mode;
`ui_shape_drag_snap_polygon_center_point_*` (tests/ui/alignment_tests.cpp)
for the center point's snapping; `live_shape_drag_points_edit_polygon_and_line_parameters`
and `live_shape_rotation_keeps_what_its_parameters_can_say` (core) for the parameter
helpers and the PSD round trip.
