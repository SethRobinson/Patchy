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

Image Size (`request_image_size_settings`, main_window.cpp): canonical state is pixel
W/H + PPI. W/H unit combos (Percent/Pixels/Inches/Cm/Mm/Points) stay in step. Resample ON:
pixel/percent edits move pixels; physical edits set pixels = value x ppi; a resolution
edit keeps the PHYSICAL size (recomputes pixels) unless the units are pixel/percent, then
pixels hold. Resample OFF: pixels lock to the document's real dimensions (pending
resamples revert, as in Photoshop), pixel/percent units disable (auto-flip to Inches), and
W/H/Resolution tri-link (a physical edit re-derives the PPI). Applying with Resample off
is a metadata-only undo step ("Print resolution").

New Document: presets carry a resolution (physical paper presets 300; Clipboard, 1080p,
4K follow Photoshop's 72 screen convention; the default 1024x768 keeps Patchy's historical
300). One shared W/H unit combo converts through the Resolution spin; physical entry holds
its size when the resolution changes.

Print dialog (src/ui/print_dialog.cpp): print resolution is READ-ONLY, derived as document
PPI / scale (Photoshop semantics); editing resolution belongs to Image Size. Default scale
is 100% (actual size) unless that overflows the printable area, then "Scale to fit media"
pre-checks.

## Rulers and the units preference

`view/rulerUnits` (settings token px/in/cm/mm/pt/percent) is the app-wide ruler unit,
surfaced in Preferences > Grid and Guides and via right-click on a ruler (Photoshop's
gesture; CanvasWidget shows the menu and reports through
`set_ruler_unit_change_requested_callback`, MainWindow owns the preference and pushes it
to every canvas in `apply_canvas_aid_settings`). `CanvasWidget::draw_rulers` picks 1-2-5
tick steps in unit space via `ruler_tick_steps`; the Pixels unit reproduces the historical
pixel ruler exactly (subdivisions never go below one pixel). Horizontal ruler uses
horizontal_ppi, vertical uses vertical_ppi. Guides and the grid stay pixel-based. The doc
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

## Known limits / future work

Type unit preference (pt vs px for the text tool), Info-panel cursor/selection readouts in
ruler units, physical presets in Image Size's Fit To combo, and reading PCX header DPI
(unreliable in the wild; Photoshop ignores it too) are deliberately not implemented yet.
