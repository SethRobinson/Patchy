# Layer keyboard commands

Keys that change the selected layers without opening a panel: opacity digits, the transparency
lock, blend modes, selection stepping, visibility and the Lock Layers dialog. Most actions live on
the main window only: the Layer menu's row count is pinned at 26 (`ui_main_window_renders_color_controls`).
Hotkey ids are permanent ([ui-conventions.md](ui-conventions.md), Hotkeys).

## Opacity digits (#527)

With a tool that has no opacity of its own, the bare digit keys on the canvas set the selected
layers' opacity, as Photoshop does. The tools are listed in `tool_sets_layer_opacity_with_digit_keys`
(canvas_widget_events.cpp): Move, Artboard, the marquees, lassos, Magic Wand, Quick Select, Object
Selection, Pick, Ruler, Type (while not editing), Hand, Zoom, the Pen and path tools, Polygon,
Custom Shape, Patch and Red Eye. Tools in `tool_supports_opacity_digit_keys` keep the digits for
their own Opacity or Flow ([brushes.md](brushes.md)). Fill, Dodge, Burn, Sponge, Blur, Sharpen,
Mixer Brush and Spot Healing ignore them (their own amounts may take the keys later), and so do
the crop tools. No tool takes them during a pointer drag (`pointer_gesture_active`) or a Free
Transform, Warp or path transform, whose commit owns the next history entry.

The value follows `CanvasWidget::handle_opacity_digit_key`'s rules: 1 to 9 are 10 to 90%, 0 is
100%, a second digit within 800 ms refines the first ("0" then "5" is 5%, "00" is 100%). Shift
with a digit does nothing here. `MainWindow::set_layer_opacity_from_digit_keys` sets the value
through the Layers panel's Opacity field (its pending edit, so the field shows it) and holds the
edit open with `layer_opacity_digit_finish_timer_` (1 s, longer than the pair window) instead of
the field's 250 ms idle pause: a run of digits, a two-digit entry included, is one undo entry.
A value every selected layer already has writes nothing. Bare digits cannot be bound as hotkeys
(`is_reserved_binding_key`), so no command competes. Pin: `ui_layer_opacity_digit_keys_with_move_tool`.

## Lock Transparent Pixels (#529)

`layerLockTransparentPixelsAction` (`layer.lock_transparent_pixels`, default "/") is checkable and
also stands in the layer context menu's Lock submenu, where it shows its key. A press locks the
selected layers unless every one is already locked, then unlocks them, through
`set_active_layer_lock_flag` (one undo entry, like the panel button). That function reselects a
multi-selection after its row rebuild (which keeps only the active row), so a second press, or a
panel lock button, acts on the same layers. `refresh_layer_controls` keeps the action's check
state on the selection. Pin: `ui_lock_transparent_pixels_command_toggles_selected_layers`.

## Blend mode keys (#531)

One action per mode (`layer.blend_mode.<mode>`, object names `layerBlend<Mode>Action`), default
Shift+Alt+letter (Shift+Option on macOS): Normal N, Dissolve I, Darken K, Multiply M, Color Burn
B, Linear Burn A, Lighten G, Screen S, Color Dodge D, Linear Dodge W, Overlay O, Soft Light F,
Hard Light H, Vivid Light V, Linear Light J, Pin Light Z, Hard Mix L, Difference E, Exclusion X,
Hue U, Saturation T, Color C, Luminosity Y. Darker Color, Lighter Color, Subtract and Divide have
no key, as in Photoshop. `set_blend_mode_from_shortcut` acts on the same target as Next / Previous
Blend Mode (#476, `blend_mode_shortcut_tool_combo`): the Brush or Healing Brush Mode list while that
tool is active ([brushes.md](brushes.md)), else the selected layers through the Layers panel's list
(disabled rows, such as modes a 32-bit document cannot use, are ignored; a selection whose active
layer already has the mode still brings the others along). A mode the target lacks is ignored.
On macOS the inline text editor takes Option(+Shift)+letter as text (main_window.cpp
ShortcutOverride), since those keys type characters there. Pin:
`ui_blend_mode_shortcuts_set_the_layer_or_tool_mode`.

## Layer Below / Layer Above (#541)

Select > Layer Below (`select.layer_below`, Alt+[) and Layer Above (`select.layer_above`, Alt+])
move the selection one row down or up the Layers panel (`MainWindow::step_layer_selection`), so
a collapsed group is one row and the name filter's hidden rows are skipped. They stop at the
ends. With no active layer, Layer Below takes the top row and Layer Above the bottom one.
Window-only `select.add_layer_below` / `select.add_layer_above` (Alt+Shift+[ and ]) add the row
past the selection's lowest or highest row and make it active. Option on macOS; there the inline
text editor keeps Option+key as text (main_window.cpp ShortcutOverride). Pin:
`ui_layer_select_below_above_steps_rows`.

## Hide Layers / Show Layers (#546)

`layerToggleVisibilityAction` (`layer.toggle_visibility`, Layer menu; Ctrl+, on Windows and
Linux, no macOS default since Cmd+, is Preferences there) hides the selected layers when any of
them shows, else shows them all (`toggle_selected_layers_visibility`). Like the eye it is
outside history but marks the document modified. `refresh_layer_visibility_action_text` (from
`refresh_layer_controls` and the Layer menu's aboutToShow) names it Hide Layers or Show Layers.
Pin: `ui_layer_hide_show_layers_toggles_selection`.

## Lock Layers dialog (#544)

Layer > Lock Layers... (`layerLockLayersAction`, `layer.lock_layers`, Ctrl+/; "/" alone stays
Lock Transparent Pixels) runs `request_layer_locks` (src/ui/layer_lock_dialog.cpp,
`layerLocksDialog`): All above the indented Transparent Pixels, Image Pixels and Position, then
Prevent Auto-Nesting. `layer_lock_choices_for` prefills each box from the selection; a lock
only some layers have starts partly checked, and a click turns a box only on or off. Mixed boxes
left alone keep each layer's own state (`apply_layer_lock_choices`). OK writes one "Lock layer"
undo entry (none when nothing changes) and keeps a multi-selection selected
(`MainWindow::show_layer_locks_dialog`). Pins: `ui_layer_lock_layers_dialog_applies_and_undoes`,
`ui_layer_lock_choices_*`, and the dialog sweep.
