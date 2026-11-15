# Contours: the editor, the contour library and Photoshop .shc files

Split from [style-presets.md](style-presets.md) (GitHub issue 726): the contour pickers in the Layer Style dialog, the contour editor, the user's saved contours, and the Photoshop `.shc` codec. The descriptor form of a contour inside a PSD (`ShpC`) is in [psd-layer-styles.md](psd-layer-styles.md); how a contour shapes each effect is in [layer-effects-render.md](layer-effects-render.md).

## What has a contour

`StyleContour` (`src/core/layer.hpp`) is a name and a list of points (input, output, 0..255, with a corner flag); no points means Linear. Three places carry one: Bevel & Emboss's Gloss Contour, its Contour sub-option, and Drop Shadow's Contour. Satin, the glows and Inner Shadow have no contour in Patchy's model, so they have no picker.

`build_style_contour_lut` (`src/core/style_contour.hpp`) is the only evaluator: a 256-entry table, natural cubic through the points, corner points splitting the curve into separate runs, flat outside the first and last point. The compositor, the picker chips and the editor's graph all read that table, so the editor draws exactly what the effect renders. Never add a second evaluator for drawing.

## Pickers

Each picker is a combo box (`layerStyleBevelGlossContourCombo`, `layerStyleBevelContourCombo`, `layerStyleDropShadowContourCombo`) with two buttons beside it, named after the combo: `<combo>EditButton` (Edit…) and `<combo>LibraryButton` (Library, a menu). A press on the curve chip at the left of the combo opens the editor too (`ContourSwatchClickFilter`); the rest of the combo opens its list.

The list (`fill_contour_combo` in `src/ui/layer_style_dialog.cpp`) holds, in order:

- "Custom" (item data `custom`), only while the picker's retained custom curve is neither a built-in shape nor a saved contour. The dialog keeps one custom curve per picker; picking a listed contour never replaces it, so a curve read from a file or drawn in the editor can be picked again. A new custom curve (an edit, or another effect instance's curve) replaces it.
- The built-in shapes (`src/core/contour_presets.cpp`, item data the preset id).
- After a separator, the user's saved contours (item data `user:<storage id>`).

A picker shows a curve by shape (`style_contours_same_shape`: same points and corner flags, names ignored): a built-in match first, then a saved contour, then Custom. `load_contour_picker` is the one way a value is put into a picker; it rebuilds the list, so it also picks up library changes.

The Library menu: New Contour… (saves the picker's current curve under a name; the picker then shows the saved contour), Rename Contour… and Delete Contour (enabled while a saved contour is picked), Load Contours… and Save Contours… (`.shc` files; Save asks which saved contours to write through `request_contours_to_save`). Every picker's value is read before the library changes and shown again after, so a deleted contour's curve stays on its effect as Custom. With no library handed to the dialog (`contour_library` null) the Library button is disabled and Edit still works.

## The editor

`request_contour_edit` (`src/ui/contour_editor_dialog.*`, dialog `contourEditorDialog`) is a modal child of the Layer Style dialog. Left, the square graph (`ContourGraphWidget`, `contourEditorGraph`: input left to right, output bottom to top, corner points drawn as squares and smooth points as circles); right, Input and Output fields for the selected point, the Corner box, Delete Point and Reset (back to the curve the dialog opened with).

- A click on empty graph adds a point there and selects it; a drag moves a point; dragging it more than 24 px off the graph removes it (it comes back if the pointer returns before the release); Delete or Backspace removes the selected point; the arrow keys nudge it (Shift: ten levels); Page Up and Page Down select the neighbours.
- Point rules live in `src/core/contour_editing.hpp`: points stay in input order at least one level apart, a contour has 2 to 16 points (`kMaxContourPoints`; Photoshop 2027 loads and saves 16), and a point the user places or moves lands on whole levels. Points nobody touched keep the exact values a file gave them.
- Every edit calls the `changed` callback; the Layer Style dialog shows the curve in the picker and previews it on the canvas through `emit_preview` (the dialog's Preview box applies as for any other control). OK returns the caller's own contour when the curve is unchanged, otherwise the points under the name "Custom". Cancel returns nothing and the Layer Style dialog puts back the picker, its retained custom curve and, with several layers selected, the batch edits made while the editor was open.
- The graph's drag coalesces its moves (`coalesce_widget_drag_moves`): each move re-renders the layer's effects. `ui_contour_graph_drag_coalesces_queued_moves` is its queued-moves test.
- Colors are the Curves graph's theme roles (`curves_graph_*`); the picker chip (`contour_preset_icon`) paints from `field_bg_large`, `field_border` and `icon_ink` each time it is drawn.

## Contour library

`ContourLibrary` (`src/ui/contour_library.*`, `MainWindow::contour_library()`, built when the Layer Style dialog first opens) keeps the user's saved contours in `<settings dir>/contours/`: one single-contour `.shc` file per entry, named by a storage UUID, the name inside the file. The folder name and the one-contour-per-file layout are persisted contracts. Entries hold whole levels, as a `.shc` file does (a fractional point rounds when it is saved). Edits apply at once with no undo, like the other preset libraries; writes go through `QSaveFile`. There are no shipped entries and no folders; the list sorts by name.

`import_shc` appends a file's contours and skips one the library already holds under the same name with the same curve; `export_shc` writes the chosen entries in list order.

## Photoshop .shc files

`psd/shc_io.cpp` (`read_shc`, `write_shc`), worked out from files Photoshop 2027 wrote (November 2026) and never from another reader's source. Big-endian:

- `8BFS`, u16 version 1, u32 contour count.
- Per contour: u32 2; u32 UTF-16 unit count and the name (terminator included, no padding); u16 2; u16 point count; the points as u16 pairs, output first, then input, 0..255; one byte per point, 1 = smooth and 0 = corner; eight zero bytes.

Photoshop names its own presets with a localization key (`$$$/Contours/Defaults/Cone=Cone`); the reader keeps the text after `=` (`photoshop_zstring_display_text`) and Patchy writes plain names. The point pairs match the `Vrtc`/`Hrzn` doubles of the same preset in a PSD's `ShpC` descriptor. The flag byte is read as `Cnty` (true = smooth) from the shapes of Photoshop's own set (Rounded Steps and Sawtooth carry 0 exactly on their sharp points) and from the all-smooth presets, whose bytes are all 1 and whose PSD descriptors carry no `Cnty` key at all.

The reader caps the file at 8 MiB, a name at 4096 units and a contour at 256 points, clamps values past 255, skips a contour with fewer than two points with a warning, and keeps the contours before a damaged tail. The writer rounds to whole levels first, so two points a fraction apart cannot land on one input level.

Checks against Photoshop 2027 (COM, Action Manager; the throwaway contours were deleted again, 12 contours before and after):

- Loading a file in this layout with three contours (mixed corners, 16 points, a name outside ASCII) appended them without a prompt, and saving Photoshop's set wrote those three records byte for byte as they were written. The same held for a file `write_shc` itself produced (`test-artifacts/patchy-contours.shc`, written by `shc_write_then_read_round_trips_names_points_and_corners`).
- `set` on the application's `ShpC` property with a file path saves the contour set (`null` = path, `T   ` = the property reference) or loads one (`null` = the reference, `T   ` = path, `Appe` true to append); `Dlt ` on an index reference deletes one. Photoshop refuses `Mk  ` and `get` for contours, so a contour cannot be made or read back over COM except through a file.
- `local-test-fixtures/shc/photoshop-2027-contours-with-probes.shc` is that saved set (local only: its first twelve contours are Photoshop's own presets).

## PSD

Nothing changed in the PSD writer: an edited curve is a custom `ShpC` (name, `Crv ` list with `Cnty` on every point), which the writer already produced for curves read from files. `psd_edited_contours_with_corner_and_smooth_points_round_trip` saves an edited curve on all three contours and reads the same points back, and writes `test-artifacts/psd_edited_contours_round_trip.psd`, which Photoshop 2027 opens without a prompt (`scripts\dev\photoshop-open-check.ps1`, November 2026) and whose three curves it returns through Action Manager with the same points and `Cnty` flags (it drops the name "Custom" and keeps any other).

## Tests

- Core (`tests/core/contour_tests.cpp`): `contour_editing_model_adds_moves_removes_and_toggles_corners`, `shc_write_then_read_round_trips_names_points_and_corners` (pins the byte layout), `shc_reader_rejects_damaged_files` (every truncation, random damage), `shc_reader_reads_photoshop_contours_if_available`, `psd_edited_contours_with_corner_and_smooth_points_round_trip`.
- UI (`tests/ui/contour_editor_tests.cpp`): `ui_contour_editor_adds_moves_deletes_and_toggles_corners`, `ui_contour_editor_cancel_and_untouched_curve_keep_the_original`, `ui_contour_graph_drag_coalesces_queued_moves`, `ui_layer_style_contour_editor_previews_and_cancel_restores`, `ui_layer_style_contour_library_new_rename_delete_load_save`, `ui_contour_library_persists_and_reads_unicode_paths`.
- The dialog sweep (`ui_offscreen_dialog_sweep_is_themed_in_both_schemes`) captures the editor and the Save Contours chooser.
