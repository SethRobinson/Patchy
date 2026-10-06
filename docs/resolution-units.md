# Resolution (PPI) and measurement units

The model is Photoshop's: a document has one resolution, stored as pixels/inch in
`DocumentPrintSettings` (src/core/document.hpp, default 300 for new documents), and it is
pure metadata. Pixels are always the stored truth; physical units (in/cm/mm/pt) exist only
at UI surfaces as conversions through the PPI, done by the shared helpers in
`src/ui/measurement_units.{hpp,cpp}` (unit enum, px<->unit conversions, ruler tick steps,
persisted settings tokens; the tokens ride user settings, never rename them). Nothing
resamples pixels unless the user explicitly asks (Image Size with Resample on).

What consumes the document PPI: text point sizes (`text_size_ppi`, main_window_shared.cpp),
smart-object placement and Replace Contents (E2/E5 rules, docs/smart-objects.md), the print
dialog's physical layout (per-axis: horizontal_ppi for width, vertical_ppi for height, so
anisotropic scans print at true size), the Image Size / New Document dialogs, unit-mode
rulers, and the info panel's physical size line.

## Untagged imports open at 72 PPI, never a screen-derived value

Photoshop opens raster files that record no density at 72 PPI. Patchy does the same and
must NEVER fall back to `QImage::dotsPerMeter`'s constructor default, which is the SCREEN's
logical DPI (96 on Windows, varies with scaling) and indistinguishable from a real value.

- `src/formats/image_density_probe.{hpp,cpp}` (Qt-free) reads the actual container fields:
  PNG `pHYs` (meter unit only; unit 0 is aspect-only), JPEG EXIF tags 282/283/296 (both
  endians; EXIF wins over JFIF, the camera-file convention) else JFIF APP0 density units
  1/2. Aspect-only densities and EXIF ResolutionUnit "none" count as untagged.
- `apply_imported_image_density` (src/ui/image_document_io.cpp) applies the policy on
  every Qt-decoded open (including the registry Qt-fallback and smart-object child
  documents): probe hit -> exact values; PNG/JPEG without one -> 72; other containers
  (TIFF, WebP...) adopt the QImage density only when it differs from a fresh QImage's
  default (`explicit_qimage_density_ppi`), else 72.
- `smart_object_source_dpi` (smart_object_render.cpp) uses the same probe, so placing an
  untagged PNG scales like Photoshop (72), not like the screen (96).
- Registry formats: BMP maps zero pels-per-meter to 72 (bmp_document_io.cpp); the WIC HEIF
  reader treats WIC's exactly-96x96 "no density" default as untagged -> 72; formats whose
  containers have no density concept (ico/tga/aseprite/pcx/ilbm) are stamped 72 in
  `load_document_from_path`. Clipboard documents are 72 (Photoshop's Clipboard preset).
- Qt's PNG/JPEG/TIFF writers always embed the density from `apply_document_resolution`,
  so Patchy-saved flat images are tagged and round-trip exactly.

## Dialog semantics (Photoshop link rules)

Canvas Size preserves off-canvas layer pixels and raster masks by default, including
hidden layers and layers inside groups. The anchor translates their bounds without
resampling. Background layers fill newly exposed canvas with the extension color while
retaining existing pixels. "Also crop each actual layer to the canvas area" explicitly
enables the destructive layer/mask crop, even when the canvas dimensions are unchanged.
"Also delete layers that end up fully off the canvas" (`canvasSizeDeleteOffCanvasCheck`,
core `remove_layers_outside_canvas`) removes every non-group layer whose bounds miss the
new canvas entirely and any group that empties as a result; layers without bounds
(adjustments, never-painted layers) stay, and the status line reports the count. The
delete runs against the frame BEFORE the resize (`remove_layers_outside_canvas(doc, frame)`):
the layer crop rewrites every pixel layer to canvas-sized bounds, so afterwards nothing
tests as off the canvas. "Delete cropped pixels and layers too"
(`canvasSizeDeleteCroppedCheck`, above the pair) is their one-click form: toggling it sets
both, and it mirrors them, checked only while both are. All three start unchecked on every
opening and are never persisted. All modes are
undoable. Document alpha/spot channels remain canvas-sized; editable vector paths, text
transforms and Smart Object placements continue to follow the anchor translation.
The resize itself is core `resize_canvas_to_frame(doc, frame, ...)`: the dialog turns its
reference frame (the canvas, or the selection below), anchor and target size into the new
canvas rect in current coordinates through `canvas_resize_frame`, and the anchor overload
`resize_canvas_and_layers` is that same path with the canvas as the frame.
Image > Crop to Selection (Advanced) (`imageCropToSelectionAdvancedAction`, hotkey id
`image.crop_to_selection_advanced`, no default) opens this dialog titled "Crop to Selection
(Advanced)" with the rectangular selection as the reference frame (the Crop tool's pending
box while that tool is active; see [crop-tool.md](crop-tool.md)): W/H prefill to the
selection size, Current Size still shows the document, an unchanged accept crops exactly to
the selection, and an edit grows or shrinks that rect about the chosen anchor point
(Relative and Percent stay relative to the document size, as labeled). The link keeps the
selection's aspect. Coverage: `ui_canvas_size_dialog_deletes_off_canvas_layers`,
`ui_crop_to_selection_advanced_prefills_canvas_size_dialog`, core
`document_canvas_resize_to_frame_translates_by_its_origin`,
`document_remove_layers_outside_canvas`, and for both checkboxes together
`ui_crop_to_selection_advanced_crops_and_deletes_off_canvas_layers` and core
`document_remove_layers_outside_frame_before_cropping_resize`.
Units (`request_canvas_size_settings`, main_window_document_dialogs.cpp): the state is
the absolute target size in pixels; the W/H fields show it converted through the
document PPI in the unit the two linked combos select (Percent/Pixels/Inches/Cm/Mm/Points).
Percent is relative to the current size per axis. Relative mode shows the change in that
unit (negative allowed) with the range mapped so the pixel result stays 1..30000, and the
Current Size lines follow the unit. `ui_canvas_size_dialog_units_convert_through_resolution`.
A link button beside the W/H fields (`canvasSizeLinkButton`, off by default; Photoshop's
Canvas Size has none) constrains proportions: an edit on one axis derives the other from
the document's current aspect ratio in absolute pixels, so Relative mode links the
resulting sizes rather than the two deltas, and turning the link on makes the pair
proportional from the width at once. `ui_canvas_size_dialog_link_keeps_aspect_ratio`.
The unit is remembered across openings (below); Relative, the link and the crop checkbox
are not.

Image Size (`request_image_size_settings`, main_window_document_dialogs.cpp): canonical
state is pixel W/H + PPI. W/H unit combos (Percent/Pixels/Inches/Cm/Mm/Points) stay in step. Resample ON:
pixel/percent edits move pixels; physical edits set pixels = value x ppi; a resolution
edit keeps the PHYSICAL size (recomputes pixels) unless the units are pixel/percent, then
pixels hold. Resample OFF: pixels lock to the document's real dimensions (pending
resamples revert, as in Photoshop), pixel/percent units disable (auto-flip to Inches), and
W/H/Resolution tri-link (a physical edit re-derives the PPI). Applying with Resample off
is a metadata-only undo step ("Print resolution").

New Document: presets carry a resolution (physical paper presets 300; the screen presets,
the default 1024x768 included, and Clipboard follow Photoshop's 72 screen convention). One
shared W/H unit combo (px/in/cm/mm) converts through the Resolution spin; physical entry
holds its size when the resolution changes.

Remembered dialog units (Photoshop's dialog memory, issue 53): each dialog's W/H unit,
and the Image Size / New Document resolution unit, persist on accept only (Cancel writes
nothing) through `remembered_dialog_unit` / `remember_dialog_unit` and
`remembered_resolution_unit_index` / `remember_resolution_unit` (measurement_units.hpp).
With no stored unit the combo seeds from `view/rulerUnits`; a token the combo cannot
show falls back to Pixels (New Document has no `pt` or `percent`). The keys are settings
tokens and compatibility contracts: `newDocument/lastUnit`,
`newDocument/lastResolutionUnit`, `imageSize/lastUnit`, `imageSize/lastResolutionUnit`,
`canvasSize/lastUnit` (resolution units are `in`/`cm`). An Image Size accept with
Resample off remembers the Inches the dialog forced. Tests:
`ui_new_document_dialog_remembers_unit`, `ui_image_size_dialog_remembers_units`,
`ui_canvas_size_dialog_remembers_unit`; the UI suite clears the three groups at startup
and the auto-accept helpers select Pixels before typing pixel values.

Print dialog (src/ui/print_dialog.cpp): print resolution is READ-ONLY, derived as document
PPI / scale (Photoshop semantics); editing resolution belongs to Image Size. Default scale
is 100% (actual size) unless that overflows the printable area, then "Scale to fit media"
pre-checks. "Print Using System Dialog..." hands off to the OS print dialog (Chrome-style),
prints on accept with Patchy's position, scale, and crop-mark settings, and adopts the
printer, paper, orientation, and copies chosen there; cancelling returns to Patchy's dialog.
The OS dialog shows no preview for Win32 apps on Windows 11 (see [platform.md](platform.md));
Patchy's own dialog keeps the preview pane. The Paper group (September 2026, issue 19) sets
the sheet without a printer driver: a size combo (`print_page_size_choices`: Letter, Legal,
Tabloid, Executive, A3 to A6, B4, B5, Custom), orientation, and custom width/height spins in
the units combo's unit, enabled only for Custom and pre-filled with the sheet on screen.
Custom sheets are `QPageSize::ExactMatch` points (`custom_page_size_points`), never snapped
to a named size; `page_layout_with_size` keeps the margins and drops them when the sheet
cannot hold them. Page Setup and the system dialog still edit the same layout and the
controls mirror what they return (Custom when the sheet is not listed). Save PDF writes the
custom sheet through `QPrinter::PdfFormat` with no driver involved. The accepted layout
persists in the `print` settings group (`load_stored_print_page_layout` /
`store_print_page_layout`; Letter when nothing is stored) and seeds `print_page_layout_`
at startup.

## Rulers and the units preference

`view/rulerUnits` (settings token px/in/cm/mm/pt/percent) is the app-wide ruler unit,
surfaced as Default units in Preferences > Units & Grids and via right-click on a ruler (Photoshop's
gesture; CanvasWidget shows the menu and reports through
`set_ruler_unit_change_requested_callback`, MainWindow owns the preference and pushes it
to every canvas in `apply_canvas_aid_settings`). `CanvasWidget::draw_rulers` picks 1-2-5
tick steps in unit space via `ruler_tick_steps`; the Pixels unit reproduces the historical
pixel ruler exactly (subdivisions never go below one pixel). Horizontal ruler uses
horizontal_ppi, vertical uses vertical_ppi. Guides and the grid stay pixel-based; a guide drag's position readout reads in the ruler unit ([tools.md](tools.md)). The doc
info line shows the physical size in the ruler unit (inches while the unit is px/percent).

## PSD resource 1005 (verified against Photoshop 2026)

hRes/vRes are ALWAYS pixels/inch (fixed 16.16); the four unit fields are display-only.
Ground truth (July 2026 COM probe): a 144 PPI file byte-patched to hResUnit=2 (px/cm)
still opens in Photoshop at resolution 144, and toggling Photoshop's ruler units between
saves does not change the resource at all (PS 2026 writes 1/1/1/1). The old reader
multiplied by 2.54 for unit 2 and misread px/cm-display files; do not reintroduce that.
The four unit fields are captured into `DocumentPrintSettings` and written back on save
(defaults of 1 reproduce the historical bytes, so the writer canaries hold). Pinned by
`psd_resolution_resource_units_are_display_only`.

## Typed units in numeric fields

`UnitSpinBox` / `UnitIntSpinBox` (`src/ui/unit_spin_box.{hpp,cpp}`) accept a unit token after
the number, Photoshop-style, and convert into the field's native unit; the display always
stays native. `parse_unit_entry` reads the number with the widget locale (group separators
rejected, C locale fallback so "1.5" works under a comma locale) and maps the token:
`px`/`pixel(s)`, `in`/`inch(es)`/`"`, `cm`, `mm`, `pt`/`point(s)`, `%`/`percent`/`pct`,
`deg`/`degree(s)`/the degree sign, plus the localized suffixes. `convert_unit_entry` goes
through pixels using `measurement_unit_to_pixels` / `pixels_to_measurement_unit` with the
`UnitConversionContext` the field's provider supplies: `ppi` (the document's, via
`text_size_ppi`; 300 with no document) and `percent_reference_pixels`, what 100% means
for that field. Conventions: the transform X/Y fields (native px) take percent of the
document width/height; the W/H fields (native percent) take pixels relative to the
session's original extent (`TransformControlsState::original_size`); text size (native
pt) takes px through the PPI; a degree field accepts only degrees; a percent typed into a
field with no basis is refused. Converted values clamp to the range; plain numbers keep
the stock spin-box typing rules.

Every px-native field that converts typed units needs a context provider carrying the
document PPI (`MainWindow::document_unit_context_provider(horizontal)` for live fields, a
`DocumentFieldUnits` snapshot through `document_field_context` for modal dialogs). A field
without one converts at 300 PPI, which is wrong on every other document; until issue 53
the feather, corner radius, magnetic lasso width, pattern offset, tolerance and grid
spacing fields all did that. `ui_feather_field_typed_unit_uses_document_ppi`.

A switchable field (`set_display_unit_switchable`) adopts a typed unit as its display unit,
Photoshop-style: `value()` stays native, `textFromValue` converts for display, a plain
number is then read in the shown unit, and a right-click lists the units
(`display_unit_changed` fires for every switch and lets the linked transform W/H pair
follow each other; a menu pick goes through `pick_display_unit`, which also emits
`display_unit_picked`). Leaving the
native unit widens `decimals()` to `measurement_unit_decimals` (never narrower than the
field's own) and sets `singleStep()` to one shown unit converted to native
(`measurement_unit_single_step`: 1 for px/mm/pt/%, 0.1 cm, 0.01 in), so arrow keys and a
scrubby-label drag ([ui-conventions.md](ui-conventions.md)) move a sensible amount in
inches; returning to the native unit restores both. The decimals change is silent (no
`valueChanged`), so a shape W/H handler never resizes the shape for a readout change.
`refresh_display_metrics` re-derives the step when the PPI changes. Set a field's native
decimals and step before it shows another unit, never while one is shown.

### Dimension fields follow the ruler unit (issue 53)

`view/rulerUnits` is also the display unit of the pixel-native position and size fields,
Photoshop's Units & Rulers model: the transform X/Y fields, the shape W/H readouts (options
bar and Properties panel), the vector stroke width and line weight (both fractional px so
a 0.5 mm hairline survives), the New Guide position, and the Shape Appearance and Create
Shape dialogs' X/Y/W/H, line start/end, line weight and stroke width. The transform W/H
pair stays percent-native. MainWindow enrolls its live fields (and the New Guide position
spin for that dialog's lifetime) with `register_ruler_unit_field`;
`apply_ruler_unit_to_fields` (Preferences OK, the ruler right-click, a field's unit menu,
startup) resets them all to the preference; `refresh_ruler_unit_field_metrics` runs from
`refresh_document_info` because the PPI is per document. A unit picked from any field's
right-click menu is Photoshop's Units & Rulers change: `display_unit_picked` routes to
`set_ruler_unit_preference`, so the rulers and every field follow and `view/rulerUnits`
is saved. A typed unit token ("2 in") only switches that field for the session, until the
next preference change; routing it through the preference would flip every ruler because
one stroke width was typed in inches. Dialogs receive a `DocumentFieldUnits` snapshot
(`document_field_units()`, whose `on_unit_picked` is the preference setter) and call
`apply_document_field_units`; `link_field_unit_picks` makes a pick on one dialog field
show on the dialog's others, since the app-wide re-apply covers only enrolled live
fields. `measurement_unit_for` / `spin_unit_for` bridge the two enums. A Percent ruler
unit shows percent of the document extent on position/size fields and falls back to the
native text on thicknesses (no basis). `ui_shape_appearance_unit_pick_sets_ruler_unit`,
`unit_spin_box_context_menu_pick_emits_picked`.

Deliberately still px: selection Feather, corner radii, pattern offsets, layer-style
sizes and distances, brush and Liquify sizes, tolerances. They are raster parameters
Photoshop keeps in px, most are integer fields, and they accept "0.5 mm" at the document
PPI. Coverage: the `unit_spin_box` UI test group, `ui_transform_fields_accept_unit_tokens`,
`ui_shape_size_fields_follow_ruler_unit`.

## Known limits / future work

Type unit preference (pt vs px for the text tool), Info-panel cursor/selection readouts in
ruler units, remembering Image Size's Resample state and Canvas Size's Relative checkbox
(Photoshop does; Seth chose units only, September 2026), physical presets in Image Size's
Fit To combo, and reading PCX header DPI (unreliable in the wild; Photoshop ignores it
too) are deliberately not implemented yet.
