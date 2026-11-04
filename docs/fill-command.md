# Edit > Fill

Edit > Fill... (Shift+Backspace, Shift+F5; action `editFillAction`) opens the Fill dialog (`src/ui/fill_dialog.cpp`, objectName `patchyFillDialog`, issues 530 and 575). `MainWindow::edit_fill_dialog` (main_window_layer_ops.cpp) refuses a read-only channel or an unfillable layer before showing it, as Stroke Selection does. The Fill tool (Paint Bucket) and the plain Fill command are in [tools.md](tools.md#fill-tool-paint-bucket-and-the-fill-command).

## Dialog

Two group boxes, **Fill** and **Blending**, each a `QFormLayout` (`AllNonFixedFieldsGrow`) whose label column `align_form_label_columns` gives one width, so every field starts at one x; the two checkboxes sit in the field column with no label (issue 575's follow-up). OK is as wide as Cancel in every language (`equalize_dialog_button_widths`).

- **Contents:** (`editFillContentsCombo`): Foreground Color, Background Color, Color..., a separator, Black, 50% Gray, White, a separator, Content-Aware. The combo uses `AdjustToContents` and a minimum width of its own size hint, so the longest entry in the current language is never cropped (issue 575). Color... (from the list or a click on the swatch `editFillColorSwatch`) opens the color picker at once; cancelling it goes back to the previous choice. The swatch's `fillColor` property holds the Color... choice, so automation can set it without the picker. The swatch hides (keeping its room) while Content-Aware is chosen.
- **Blending**: Mode: (`editFillModeCombo`, the paint modes including Behind and Clear from `core/paint_mode.hpp`, which change only alpha; this dialog alone adds a separator above Behind, the Brush bar's shared `add_paint_mode_items` list keeps none), Opacity: 1..100 (`editFillOpacitySlider` / `editFillOpacitySpin`, an `add_dialog_slider_spin_row`), Preserve Transparency (`editFillPreserveTransparencyCheck`).
- Color contents go through a `FillCommandBlend` into `fill_active_layer_with_color`; non-Normal Modes use `EditOptions::fill_paint_mode`. The Foreground and Background choices carry the paint intensity of their color. No History contents.

## Settings

Saved on OK, all permanent identifiers: `tools/editFillContents` (tokens `foreground`, `background`, `color`, `black`, `gray50`, `white`, `contentAware`), `tools/editFillColor`, `tools/editFillMode` (the script paint-mode id), `tools/editFillOpacity`, `tools/editFillPreserveTransparency`, `tools/editFillContentAwareNewLayer`.

## Content-Aware

- Offered only when there is a selection and the target is a layer's own pixels (not a mask, a document channel, a Smart Filter mask or Quick Mask): otherwise the entry is disabled with a tooltip, and a remembered Content-Aware opens as Foreground Color.
- While it is chosen Preserve Transparency is disabled (the fill comes from the surrounding pixels) and Add to New Layer (`editFillContentAwareNewLayerCheck`) is enabled, only then. Mode and Opacity (with their labels) are enabled only while Add to New Layer is ticked, and then offer only layer blend modes: Behind and Clear are disabled, and a chosen one switches to Normal.
- `MainWindow::content_aware_fill_selection` runs Remove Object's dialog-free path (`CanvasWidget::remove_object_in_selection`, [healing-remove-object.md](healing-remove-object.md#remove-object)) at the dialog's defaults: attempt 0 (the best-match fill), tone match 0, feather 0, under a wait cursor and the canvas progress overlay. It runs with `record_history = false`, then puts the original layer back, pushes ONE "Content-Aware Fill" history entry and writes the result. It is the same engine, so the legal boundary of Remove Object applies unchanged ([legal-constraints-filters.md](legal-constraints-filters.md)): never give this entry its own algorithm.
- **Add to New Layer**: the layer keeps its pixels; Layer Via Copy's `copy_pixels_from_layer` copies the filled layer's pixels under the selection (selection coverage as alpha) onto a new pixel layer named "Content-Aware Fill" directly above it, which becomes active. Outside the selection the new layer is transparent. The new layer takes Mode as its blend mode (Normal where `blend_mode_supported_at_depth` refuses it) and Opacity as its layer opacity.
- The selection stays put.

## Coverage

`ui_edit_fill_dialog_fills_contents_with_mode_and_opacity` (every color choice, Mode, Opacity, Preserve Transparency, no selection, Cancel, the shortcuts), `ui_edit_fill_dialog_ok_matches_cancel_width` (OK as wide as Cancel in English and German; artifacts `issue_575_fill_dialog_buttons_*`) and `ui_edit_fill_dialog_content_aware_fills_selection` (the box titles, form labels, checkboxes in the field column and one field x; the list order and separators, the Mode separator above Behind, Content-Aware disabled without a selection, the slider pair, Mode, Opacity, Preserve Transparency, Behind/Clear and Add to New Layer following Content-Aware, an in-place fill of issue #23's banner changing only the selection as one undo step, Add to New Layer leaving the layer alone and its new layer transparent outside the selection, in Multiply at 60%; artifacts `issue_575_*`). The dialog is in `ui_offscreen_dialog_sweep_is_themed_in_both_schemes` (`edit_fill`).
