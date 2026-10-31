# Point, segment and component commands

GitHub #555. Commands that change the structure of an existing path (where its subpaths open,
join or split) and that act on whole components (shape groups). The point-editing surface they
plug into (selection, context menu, tool roles) is in [vector-commands.md](vector-commands.md);
the model in [vector-tools.md](vector-tools.md). Geometry: `src/core/path_point_edits.{hpp,cpp}`;
canvas side: `src/ui/canvas_widget_path_edits.cpp`; MainWindow side:
`src/ui/main_window_path_components.cpp`. Tests: `path_break_*`, `path_join_*`,
`path_delete_segment_*`, `path_merge_points_*`, `path_components_*` in
`tests/core/vector_shape_tests.cpp`; `tests/ui/vector_path_edits_tests.cpp`.

Classic bezier path editing (Illustrator 88 era, expired); no legal boundary applies beyond
[vector-tools.md](vector-tools.md#patents-and-trademarks-assessed-july-2026).

## Point and segment commands

Layer > Shape lists four commands (hotkey ids, no default keys): Break Path at Point
(`path.break_at_point`, `pathBreakAtPointAction`), Join End Points (`path.join_end_points`),
Merge into One Point (`path.merge_points`) and Delete Segment (`path.delete_segment`). The path
context menu repeats them after Convert Point (`pathMenuBreakAtPointAction`,
`pathMenuJoinEndPointsAction`, `pathMenuMergePointsAction`, `pathMenuDeleteSegmentAction`).

What they act on (`CanvasWidget::plan_path_point_command`): the menu's press first (an anchor for
Break, a segment for Merge and Delete Segment), else the Direct Select point selection of the
target path. Path Select's selection is whole shapes, so under it only the press counts. A
selected segment is one whose two end points are both selected, which is what a Direct Select
segment click selects (`path_segments_between_points`); there is no separate segment selection.
A command that does not fit is disabled, never hidden (`refresh_path_edit_action_states`, run on
every selection change, tool change and Shape menu show); a hotkey on a stale state reports why
in the status bar. Not for text-area paths (a text area must stay one closed outline). Every
edit is one undo step through `apply_path_edit` and works on shape layers, vector masks, saved
paths and the work path; a shape layer left with no subpath is deleted in the same step
(`shape_layers_emptied_callback_`, as Delete does).

- **Break Path at Point**: a closed subpath opens at the anchor, which becomes both end points at
  the same place; an interior anchor of an open subpath splits it in two. End points do not
  break. Each piece keeps the handles that shape its segments; the handle that drove the removed
  side collapses. Both new end points stay selected, so Join End Points undoes it.
- **Join End Points** (two selected end points): the ends of one open subpath close it with a
  straight segment; ends of two open subpaths connect them into one, in the first one's place,
  shape group and op (the second is reversed as needed). End points closer than 0.01 px merge
  into one point instead of adding a zero-length segment.
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

Not here yet: applying Add / Subtract / Intersect / Exclude to the geometry itself (a curve
boolean that bakes the live ops into one outline).
