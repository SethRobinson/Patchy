# Vector tools: handles and drag points on the shape tools (GitHub #690)

Split from [vector-tools.md](vector-tools.md). What a shape tool draws on the active shape so it can be edited
without leaving the tool: the eight-handle box, and the two drag points of a live Polygon and Line. Legal rules:
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
while a shape is dragged out, in Path and Pixels modes, with a mask targeted, for a
hidden, locked or position-locked layer, with several layers selected, and whenever
the shape's path outline is hidden (a dismissed path row, View > Show > Target Path,
View > Extras).

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

## Drag points on a live Polygon and Line

A live Polygon and a live Line show two drag points in place of the box: the two
points of the drag that drew them (canvas_widget_shape_points.cpp,
`shape_live_points`). Rectangle, Ellipse and Custom Shape keep the box: their two
drawing points are corners it already has.

- **Polygon** (a star too): its center and its first vertex. Dragging the vertex
  sets the radius and `polygon_angle` exactly as the Polygon tool's center-out drag
  does (`set_live_polygon_first_vertex`, the math of `commit_polygon_drag`), so the
  polygon turns and resizes about its center. Dragging the center moves it by the
  pointer's travel on whole pixels. Sides and Star inset never change.
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
The center point does not snap to guides, the grid or other layers
([legal](legal-constraints.md#shape-drag-points)). Shift and Alt do nothing: the
drawing drags of Polygon and Line have no modifier rule to repeat. A point dropped
under a pixel from the other one is ignored and the shape stays as it last was.

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
the points; `live_shape_drag_points_edit_polygon_and_line_parameters` (core) for the
parameter helpers and the PSD round trip.
