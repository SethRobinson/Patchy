# New layers: the New Layer dialog and the footer's modifier clicks

The Layers panel itself (rows, footer, menus) is in [layer-panel.md](layer-panel.md).

## New Layer dialog

Layer > New > New Layer... (`layerNewWithDialogAction`, id `layer.new_with_dialog`, Ctrl+Shift+N; #275) runs `request_new_layer_settings` (`src/ui/new_layer_dialog.cpp`): Name, Clip to the layer below, Color (the color-label actions' names and swatches), Mode, Fill, and Opacity. `add_layer(settings)` applies them in one undo step. New Layer (`layer.new`, no default key since #275; Ctrl+Alt+Shift+N stays Open from Clipboard's) and the footer button add a layer at once.

- The clipping option is enabled only when the new layer would have a clip base (`effective_clip_base`). It sits under Name in the form's field column (`addRow(nullptr, widget)`), lined up with the fields rather than the labels (#410).
- The Fill row under Mode (`newLayerNeutralFillRow`) holds a "Neutral color" checkbox (`newLayerNeutralFillCheck`), a swatch of the color (`newLayerNeutralFillSwatch`) and its name (`newLayerNeutralFillName`). A mode without one hides those and shows "This mode has no neutral color" (`newLayerNeutralFillNote`) as ordinary text, never a disabled checkbox: dimmed text is easy to miss, and Normal (the default) has no neutral color. The row keeps one height either way.
- `neutral_fill_for_blend_mode` gives the fill's color: white for Darken, Multiply, Color Burn, Linear Burn, Darker Color and Divide; black for Lighten, Screen, Color Dodge, Linear Dodge (Add), Lighter Color, Difference, Exclusion and Subtract; 50% gray for Overlay, Soft Light, Hard Light, Vivid Light, Linear Light and Pin Light. Every other mode has none. Hard Mix has none: it thresholds the image at 50% gray. A checked box applies only while the chosen mode has a color.
- The fill is part of the New layer step, a fully opaque layer at the document's depth: 50% gray is 128 in 8 bits, the encoded midpoint in 16 bits and 0.5 linear in 32 bits, where most modes blend linear light ([high-bit-depth.md](high-bit-depth.md)).

Tests: `ui_new_layer_dialog_*`.

## Footer modifier clicks

- Alt-click on the footer's New Layer button (`layerNewButton`) opens the New Layer dialog (#408): `add_layer_with_dialog`.
- Ctrl-click (Command-click on macOS) on New Layer adds the layer below the active one (#467): `add_layer(nullptr, true)`, through `insert_layer_below_anchor` (the lowest selected layer without an active one). Under a Background it goes above as usual; under a clipped layer it is clipped too, so the clipping group keeps its base. Alt wins over Ctrl. The tooltip names both modifier clicks through `resolve_modifier_names` (a retranslation callback).
- Alt-click on Add Layer Mask (`layerAddMaskButton`) calls `add_layer_mask(true)` (#400): a hide-all mask (full-canvas 0, default color 0), or with a selection a mask that hides it (the selection coverage inverted, default color 255). One undo step, like the plain click's reveal-all and reveal-selection masks.
- Ctrl-click (Command-click on macOS: `Qt::ControlModifier`) on Add Layer Mask runs Layer > Vector Mask > Reveal All instead (#447): `add_vector_mask(hide_all, false)`, where Alt also held makes it Hide All. The button's enabled state stays `can_add_layer_mask`'s. Its tooltip names both modifier clicks through `resolve_modifier_names`, so it is set by a retranslation callback rather than bound.
- `clicked` carries no modifiers, so the buttons record them off their own mouse events (`record_click_modifiers`/`take_click_modifiers`, `src/ui/click_modifier_recorder.hpp`, shared with the folder disclosure arrow). A keyboard or programmatic `click()` is a plain click.

Tests: `ui_new_layer_button_alt_click_opens_new_layer_dialog`, `ui_new_layer_button_ctrl_click_adds_below`, `ui_layer_add_mask_button_alt_click_hides_all_or_selection`, `ui_layer_add_mask_button_ctrl_click_adds_vector_mask`.

## Optional footer buttons

Five footer buttons start hidden (#470, #539; `kLayerFooterButtonIdsHiddenByDefault`, stored when shown: [tool-palette.md](tool-palette.md#customizing-the-palette)), so the footer keeps its one row. Alt wins over Shift; they are document widgets, and each command reports its own refusals.

- `layerMoveUpButton` / `layerMoveDownButton`: Move Layer Up / Down (`move_active_layer`); Shift-click Bring to Front / Send to Back (`arrange_selected_layers_to_end`, core `arrange_layers_to_end`: the top or bottom of the layer's own folder, never under a Background, which is left out of the selection); Alt-click `move_selected_layers_out_of_folder` (core `move_layers_out_of_folder`: one level out, just above or below the folder; a layer at the root stays, with a status message).
- `layerMergeDownButton`: Merge Down; Shift-click Merge Visible; Alt-click Merge Visible to New Layer (Photoshop's Ctrl+Alt+Shift+E).
- `layerMergeVisibleButton` (#539): triggers Layer > Merge > Merge Visible (`layerMergeVisibleLayersAction`); not a document widget: it mirrors the action's enabled state through `QAction::changed` (`refresh_merge_visible_action_state`). Icon `merge-visible.svg`.
- `layerRasterizeButton` (#539 follow-up): a `QToolButton` in `InstantPopup` mode like `layerStylesButton`: a click opens `layerRasterizeButtonMenu`, the layer context menu's Rasterize entries as shared actions (Rasterize `rasterize.svg`, Rasterize (including layer style) `rasterize-layer-style.svg`, a separator, Trace Image to Shapes... `trace-image.svg`). The menu bar and row menu hide those icons (`hide_menu_action_icons`); this menu turns them on while it is open and restores them on close. The button is on while any entry is (Trace Image keeps it on over a pixel layer). Seth, October 31, 2026: Merge Down and Merge Visible stay plain buttons; a corner mark promising a menu that opened only on a hold read as broken.
- Layer > Arrange > Bring to Front (`layer.bring_to_front`, Shift+Ctrl+]) and Send to Back (`layer.send_to_back`, Shift+Ctrl+[) run the same command (#468). Ctrl+] and Ctrl+[ stay Image > Rotate, so Move Layer Up / Down keep no default key.

Tests: `ui_layer_footer_move_and_merge_buttons_follow_modifiers`, `ui_layer_footer_merge_visible_and_rasterize_buttons`, `ui_layer_footer_rasterize_button_opens_its_menu`, `ui_layer_footer_optional_buttons_start_hidden`, `ui_layer_arrange_bring_to_front_and_send_to_back`, core `arrange_layers_to_end_moves_within_each_folder`, `move_layers_out_of_folder_lands_beside_the_folder`.
