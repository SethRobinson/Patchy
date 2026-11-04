# Path transform

Split from [vector-commands.md](vector-commands.md): Ctrl+T on a path under Path Select or Direct Select (Free Transform Path and Free Transform Points), its distort gestures, and the live fill.

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
([tools-transform-modifiers.md](tools-transform-modifiers.md)). Hover cursors are Free Transform's own
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
