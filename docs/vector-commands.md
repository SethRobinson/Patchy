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
  under about 70 characters so the flow layout never wraps.

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
same reason (a group has no path).
