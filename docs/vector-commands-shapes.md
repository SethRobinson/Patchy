# Commands on existing shapes

Split from [vector-commands.md](vector-commands.md): Simplify Path, Combine Shapes, Ungroup Layers and Copy as SVG (menu entries, command ids, dialogs, settings, undo entries, script calls and tests).

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
one was set (a style counts when it has an enabled effect). Lock All on the folder refuses. One "Ungroup layers" undo
entry; the topmost released layer becomes active; the folder leaves the
session's collapsed set. Script: `layer.ungroup()` returns the released
layers top to bottom.

## Copy as SVG

See [svg.md](svg.md) (UI behavior): `Edit > Copy as SVG` puts the selected
layers on the clipboard as image/svg+xml and text; Paste reads it back in
place.
