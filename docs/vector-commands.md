# Vector commands and point-editing UI

Feature reference for the path point-editing surface added in August 2026
and the vector commands that operate on existing shapes. The model, PSD
encodings, and the Pen/Direct Select basics live in
[vector-tools.md](vector-tools.md); legal boundaries in
[legal-constraints.md](legal-constraints.md). Tests:
`tests/ui/vector_point_editing_tests.cpp`.

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
Pen plus the three anchor tools everywhere the Pen's handlers dispatch;
`path_edit_tool_active()` includes them so anchors draw and the selection
keys work.

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
switch. The flyout button is `penToolButton` (menu `penToolMenu`); P
re-selects the Pen and swaps it back onto the button.

## Selection keys under the Pen family

`handle_path_edit_key` lets the Pen family own Delete/Backspace (remove the
Ctrl-selected anchors) and Escape (clear the selection) once anchors are
selected; arrows stay with the app hotkeys. `CanvasWidget::event` accepts
the Delete/Backspace ShortcutOverride for every path tool with a selection
so `layer.clear` does not fire. Starting a Pen session clears a leftover
selection.

## Hints

- Activation: picking a path tool shows a one-sentence gesture hint in the
  status bar (`tool_activation_hint_source`, tool palette TU); other tools
  keep showing their name. Tooltips for the same tools are two lines
  (`tool_tooltip_source`, bound through `bind_tooltip`).
- Hover: `update_path_hover_hint` (Pen family: add/delete/convert/close) and
  `update_path_select_hover_hint` (Path/Direct Select: anchor, handle,
  segment) emit through the status callback only on the transition INTO an
  actionable state and never on the way back, so confirmations shown by
  other code survive mouse motion. `set_tool` resets the memory.
- Options bar: `pathToolHintLabel` (registered for the six path tools, not
  part of the appearance widgets that hide without a shape layer) shows a
  short reminder per tool from `path_tool_hint_source`
  (main_window_vector.cpp, `refresh_path_tool_hint_label`); keep the texts
  under about 70 characters so the flow layout never wraps. Under Direct
  Select with two or more anchors selected the same label shows "%n points
  selected" instead, refreshed through
  `set_path_selection_changed_callback` (fired from every mutation of
  `path_selected_anchors_`: clicks, marquees, deletes, prunes, deselects).

## Moving a selection

Direct Select (or the Pen's Ctrl latch) drags every selected anchor when the
press lands on a selected anchor OR on a segment whose two ends are already
selected (a marquee, then a drag on any of its lines); Shift on a segment
adds it, an unselected segment replaces the selection with its two ends.
Pinned by `ui_direct_select_drags_marquee_selection_by_segment`.

Shift held during the drag constrains movement to the nearest horizontal,
vertical, or 45-degree axis (Photoshop's vertex constraint,
`constrain_drag_to_axes`): the constraint is re-evaluated every move against
the total delta from the press point, never latched, and the points follow
the mouse's projection onto the snapped axis. Anchor drags therefore track a
total-delta ledger (`path_drag_applied_delta_`) while still applying
per-move increments through `apply_path_edit` (`update_path_edit_drag`,
split out of `handle_path_edit_move`). Pressing or releasing Shift with a
stationary cursor replays the drag at the last raw pointer position from
keyPressEvent/keyReleaseEvent. Shift at press keeps its additive-selection
meaning; only move/key state during the drag drives the constraint. Handle
drags and the Pen's in-session Ctrl anchor drag stay unconstrained.

## Path context menu

Right-click under a path tool: the press records `path_context_press_pos_`
and starts the universal pan; a release within `startDragDistance` opens
`canvasPathContextMenu` (`show_path_context_menu`, public for tests),
otherwise the gesture was a pan. Entries (object names `pathMenu*Action`):
Add Anchor Point (over a segment), Delete Anchor Point and Convert Point
(over an anchor), Delete Selected Points and Deselect Points (with a
selection), Free Transform Points (Direct Select with a selection) or Free
Transform Path (Path/Direct Select only; the Pen family sees it disabled
and keeps falling through to the layer transform). The menu uses the RAW
hit, so it works with Auto Add/Delete off and under the anchor tools. No
menu with no target path, during a Pen session, or in a path transform.

## Paths panel retargeting

`refresh_paths_panel` remembers the active layer of the previous refresh;
when it changes to a layer whose transient row is a shape or vector-mask
path, a targeted work/saved-path row is dropped so
`path_edit_target_path` resolves to the new layer (Photoshop retargets on
layer selection). The memory resets on a document switch so reused layer
ids never look like a change. Explicit row clicks within one layer still
stick. Trace Image to Shapes activates the frontmost traced shape for the
same reason (a group has no path). The same refresh repaints the canvas on a
layer change: a pure activation composites nothing, and the overlay (drawn
from the active layer) otherwise stayed stale until an unrelated edit (Seth,
August 2026: anchors appeared only after toggling Stroke;
`ui_layer_row_click_shows_anchors_for_shape_layer` counts paint events).

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
"Simplify path" undo entry. The dialog is non-modal; the commit re-validates
the document identity and the target's existence first. Script:
`layer.simplifyPath({tolerance, cornerAngle, snapCurvesToLines})`.

## Combine Shapes

`Layer > Shape > Unite Shapes / Subtract Front Shape / Intersect Shapes /
Exclude Overlapping Shapes` (ids `layer.combine_unite`,
`layer.combine_subtract`, `layer.combine_intersect`, `layer.combine_exclude`)
merge the Layers-panel selection (`combine_shape_candidates`,
`src/core/shape_combine`): two or more editable shape layers with paths, all
siblings of one parent; folders expand to nothing (root ids only). Enable
state follows the selection (`refresh_combine_shapes_action_states`, also on
pure multi-selection changes). Semantics are the renderer's sequential
combine: the BOTTOM-most layer is the base and keeps its id, name,
appearance, styles, masks, origination, and its groups' own ops; every group
of each front layer is appended in stacking order with the chosen op (Add /
Subtract / Intersect / Xor), groups renumbered from `next_shape_group()`.
Front styles, masks, and origination vanish with the layers. A compound
front (a donut) therefore combines group by group, which is also
Photoshop's per-component model; the Path Select Combine box retunes ops
afterwards. One undo entry per command ("Unite shapes", "Subtract front
shape", "Intersect shapes", "Exclude overlapping shapes"). Script:
`doc.combineShapes([layers], "unite" | "subtract" | "intersect" | "exclude")`.
Tests: `tests/core/vector_shape_tests.cpp` (fitter, simplify),
`tests/core/vector_raster_tests.cpp` (combine truth table, refusals),
`tests/ui/vector_commands_tests.cpp`.

## Ungroup Layers

`Layer > New > Ungroup Layers` (id `layer.ungroup`, default Ctrl+Shift+G;
`layerUngroupAction`; also in the Layers panel context menu when the active
layer is a folder) releases every selected folder's layers into its parent
at the folder's position, composite order unchanged (`ungroup_layer`,
`src/core/layer_tree`). New Folder gained Photoshop's Ctrl+G at the same
time, so the pair matches muscle memory. Photoshop's rule applies: the
folder's own opacity, blend mode, masks, and style are dropped, and the
status line says so when one was set. Lock All on the folder refuses. One
"Ungroup layers" undo entry; the topmost released layer becomes active; the
folder leaves the session's collapsed set. Script: `layer.ungroup()` returns
the released layers top to bottom.

## Copy as SVG

See [svg.md](svg.md) (UI behavior): `Edit > Copy as SVG` puts the selected
layers on the clipboard as image/svg+xml and text; Paste reads it back in
place.
