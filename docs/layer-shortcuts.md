# Layer keyboard commands

Keys that change the selected layers without opening a panel: opacity digits, the transparency
lock, and blend modes. Their actions live on the main window (no menu-bar rows: the Layer menu's
row count is pinned). Hotkey ids are permanent ([ui-conventions.md](ui-conventions.md), Hotkeys).

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
