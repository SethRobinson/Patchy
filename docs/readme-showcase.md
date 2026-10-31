# Rebuilding the README showcase

The README uses a hero image and eleven feature captures. The [full gallery](screenshots.md)
keeps the other examples. Every capture has a script or a `shot_readme_*` scene;
do not replace one with a manually arranged desktop screenshot.

From the repository root on Windows:

```powershell
# Build the screenshot targets and regenerate the regular gallery.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-readme-screenshots.ps1

# Include the locally installed commercial KPT fixture.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-readme-screenshots.ps1 -IncludeLegacyPlugins

# Rebuild one capture using the current opt-dev binaries.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/make-readme-screenshots.ps1 -SkipBuild -Scene long_shadow
```

Six of these captures (`smart_filters`, `layer_styles`, `vector_tools`, `warp_text`,
`palette_mode`, `brush_tips`) are also the Linux AppStream screenshots, pinned by tag in
`packaging/linux/com.rtsoft.patchy.metainfo.xml`. After regenerating any of them, run
`python scripts/release/check-metainfo.py --version <next release> --fix` so the pins move
to the release that will ship them ([packaging/linux/README.md](../packaging/linux/README.md)).

The driver accepts image base names or full `shot_readme_*` names. `-Scene hero`
regenerates `smart_filters.png`, which the README uses directly. Missing local
fixtures are reported as skips during a full run; an explicitly selected script
scene fails if its fixture is missing. KPT runs only with `-IncludeLegacyPlugins`
or `-Scene plugin_kpt5`. It requires the Windows 32-bit legacy helper.

| Image | Recipe | Input |
| --- | --- | --- |
| `smart_filters.png` (hero) | `shot_readme_smart_filters` | `local-test-fixtures/psd/akiko_cycling_okinawa.jpg`, grouped captions, clipping, a Levels adjustment, and color controls |
| `layer_styles.png` | `shot_readme_layer_styles` | `local-test-fixtures/psd/ipad_main_v04.psd`, live layer styles and gradient color controls |
| `plugin_kpt5.png` | `scripts/dev/readme-shots/plugin-kpt5.js` | Licensed KPT 5 fixture and the existing CC0 city photograph |
| `plugin_dialog.png` | `scripts/dev/readme-shots/plugin-dialog.js` | `local-test-fixtures/photoshop-plugins/mehdi` (Mehdi's Absolute Color, not committed; skipped when absent) and the CC0 city photograph |
| `object_select.png` | `scripts/dev/readme-shots/object-select.js` | `test-fixtures/readme/havana_vintage_car_cc0.jpg` (CC0), cropped in the script; needs the High Quality model (the script downloads it when missing) |
| `artboards.png` | `scripts/dev/readme-shots/artboards.js`, with isolated Light settings | `test-fixtures/readme/havana_vintage_car_cc0.jpg` (CC0), placed as a smart object on each artboard; generated type and vector shapes |
| `layered_import.png` | `scripts/dev/readme-shots/layered-import.js` | Existing local `psd/mow_master.psd` and `af-spike/corpus/tips.af`; local CC0 corpus files `kra-corpus/gdquest-dan.kra` (GDquest) and `xcf-corpus/opengameart/gas-giant-rings.xcf`; committed `test-fixtures/pdn/demo.pdn`; generated ORA, layered TIFF, and Aseprite files |
| `smart_objects.png` (gallery only) | `shot_readme_smart_objects` | Local PSD fixtures used by the existing scene |
| `vector_tools.png` | `shot_readme_vector_tools` | Generated vector artwork with gradient fill, rounded corners, and a dashed pattern stroke |
| `warp_text.png` | `shot_readme_warp_text` | Generated editable type; Japanese-capable font required |
| `tilt_shift.png` | `shot_readme_tilt_shift` | `test-fixtures/readme/san_francisco_cityscape_cc0.jpg` |
| `gmic.png` | `shot_readme_gmic` | `test-fixtures/readme/havana_vintage_car_cc0.jpg` (CC0), cropped around the car; needs a build with the G'MIC engine |
| `web_export.png` | `shot_readme_web_export` | `test-fixtures/readme/san_francisco_cityscape_cc0.jpg`, graded into eight frame layers in the scene |
| `flame.png` | `shot_readme_flame` | Generated night backdrop and Arial Black lettering; the letters' outline is a saved path |
| `perspective_planes.png` (gallery only) | `shot_readme_perspective_planes` | `test-fixtures/readme/brick_wall_new_brunswick_cc0.jpg` (CC0), with posters made from the San Francisco, Havana and sunset images |
| `long_shadow.png` (gallery only) | `shot_readme_long_shadow` | Generated editable type and live continuous shadows; no external artwork |

Offscreen recipes live in `tests/ui/readme_screenshot_tests_*.cpp`. They create
real Patchy documents, arrange the UI, and capture the widgets. The long-shadow
scene opens the actual Layer Style dialog and checks its Continuous setting.
Its typography uses a live continuous shadow, not a painted imitation.

The hero shows rulers and keeps the native three-filter stack and shared mask on the photograph.
The mask protects the cyclist's face and applies the blur stack to the surroundings.
The color picker shows its wheel and palette, while a real Levels adjustment layer
is open for editing with its histogram and live preview. Neutral Levels settings
preserve the photograph; the scene checks the face pixels against the source.
An expanded Ride notes group contains two editable text layers, a rounded caption
card, and a color wash clipped to that card. The vector scene combines pen paths
and anchor editing with gradient paint, a dashed pattern stroke, and rounded corners.

The separate Levels, brush-dynamics, shape-appearance, and Affinity images are retired
from the gallery. Their existing scenes remain regression coverage; the driver
does not publish those artifacts. Brush dynamics are described with brush tips.
Long Shadow and Smart Objects remain in the gallery; the README's feature cards use
Layer Styles and Object Selection instead. Ten feature cards sit in pairs;
the eight-artboard Layered Imports capture spans the final row so its labels read.

The Object Selection scene crops the Havana street photo to its lower half and runs
`doc.selection.objectSelect` with one loose box around the vintage car, on the High
Quality model (zoom-to-box applies). The car must come out exact, wheels and mirrors
included and the street excluded; check the capture after a model or engine change.
One-pixel marching ants vanish at thumbnail size, so the script inverts the selection,
fills a gray Saturation-mode layer and a 45% black layer through it, and inverts back
(fill layers rather than a filter on the photo, so the photo layer stays untouched; a
Desaturate menu command run from the script did not apply). The status bar shows "Running script", as in the KPT shot.

The Artboards scene builds a fictional car-weekend campaign as four artboards, one per
background type: Story (white), Banner (black), Post (custom cream) and Icon (transparent,
so the checkerboard shows only inside it). Each photo is larger than its artboard and
vector-masked, so the artboards visibly clip it. The Post is selected with the Artboard tool
(handles, "+" buttons, options bar) and `patchy.ui.expandPanel('properties')` shows its
X, Y, W, H and background. Check that no text overlaps after a font or layout change.

The Artboards capture runs with `ColorScheme = 'light'`
in the driver's scene definition. The driver writes only that run's isolated
settings; it never changes the user's theme.

The Layered Imports scene imports eight actual files through Files as Layers and
places each imported group inside its own artboard. A 4 x 2 grid (native editor
formats on top: PSD, Affinity, Krita, GIMP; interchange formats below)
uses 640 x 480 boards: an 80-pixel format label above a 640 x 400 artwork area.
Above the grid, a 312-pixel introduction (room for the artboard titles at the fit zoom) uses loose editable text and shapes:
"Why settle for only .psd?", the import explanation, a mint accent, and a
"8 FORMATS / 1 FILE" badge (the count comes from the format list). It is part of the document, not a screenshot overlay.
The imported layer stacks remain editable, unlocked and scaled to fit without
flattening; the original files are never modified.
An imported leaf's offset locates the original canvas, since Files as Layers
centers content bounds, including off-canvas content. A vector mask on the
imported group preserves that source canvas crop; labels sit outside the mask.
PSD/PSB uses the existing `local-test-fixtures/psd/mow_master.psd` game artwork;
Affinity uses `local-test-fixtures/af-spike/corpus/tips.af`, the old Affinity
showcase's game tips. Krita uses GDquest's CC0 `gdquest-dan.kra` character (nested
groups and an Inherit Alpha group) and GIMP the CC0 OpenGameArt
`gas-giant-rings.xcf` (6692 x 3000, six layers), both from the local import corpora.
Those local sources remain untracked; a missing source
skips the scene in a full run and fails an explicitly selected run.

The other boards use the existing Paint.NET-saved `test-fixtures/pdn/demo.pdn`
landscape and three script-authored sources: an OpenRaster city poster using
the existing CC0 photograph, a layered TIFF abstract print design with text and
shapes, and a 64 x 40 Aseprite space scene with separate stars, planet, and ship
layers. The script writes those formats in its scratch folder, then imports
them through the same reader as the fixtures. ORA lettering becomes pixels;
the TIFF keeps text and shapes; Aseprite uses nearest-neighbor enlargement.

The scene checks each import has multiple children and the result has exactly
one top-level artboard per format plus the introduction group. Label strips are added
after import. The PDN fixture's
`Xor checks` calibration overlay is hidden but retained. Only the
Layers/Channels/Paths dock stays open. Generated sources and logs remain under
`build/opt-dev/readme-shot-layered_import`.

The G'MIC scene opens the real Filter Library (scripts cannot open it) with the right half
of the street photo selected, picks Rodilius with Lighter strokes, waits for the full-quality
preview to land on the layer, and checks that the left half is untouched, so the canvas shows
the original beside the result. The photo stays under the 1.5 megapixel draft threshold, so
the first preview is the final one. The right dock is widened under the dialog so the whole
photo stays in view beside it. Builds without the engine report a skip.

The Export for Web scene (`tests/ui/readme_screenshot_tests_showcase.cpp`, with Perspective
Planes) builds an eight-layer day-to-blue-hour time-lapse from the San Francisco photo (graded,
a slow push in, lit windows at night; the last layer's name holds it for a second), presets GIF
at 16 colors with diffusion (few enough that the dither shows in the split) and Split compare through the exportForWeb settings (restored
afterward), waits for all eight decoded frames and pauses on frame 4.

The Flame scene (same file) paints FIRE in charred, letter-spaced Arial Black (the scene skips without the
font) on a generated night backdrop, saves the glyph outline as the FIRE outline path, selects it in the Paths
panel and hides its canvas outline (View > Show > Target Path, not persisted). The real Flame dialog runs
Campfire with Follow Path, Straight Up and short, fine tongues (length 8, flicker 65) with a dim glow: longer
flames or Away from the Path fill the strokes and the word stops reading. It waits for the live canvas
preview, checks that the far corner stays unburned and that Cancel restores the layer, and draws the dialog's
caption with `readme_captioned_dialog`, since the native frame is not grabbed.

The Perspective Planes scene drives the real workspace on the CC0 brick wall photo, scaled to
1400 x 1050. Its one plane is built from the photo's own lines: the top and bottom edges run to
the vanishing point of the right window sill and the sign's lower edge, the sides to that of
the drainpipe and the sign's left edge, so the grid follows the brick courses. Three posters
(San Francisco, Havana, Sunset) are pasted at one scale and spaced from the plane's width; the
first two are placed and the last is left floating. Keep the plane at a gentle angle: a
steeply foreshortened plane samples a coarse mip level and blurs the art (a first version on
the Havana street's right-hand wall did). Use real CC0 photos here, never generated ones.

The text showcase combines a live Warp Text preview with three warped words,
a single boxed paragraph containing bold, italic, colored, and mixed-font runs plus the
Character panel's underline, strikethrough, superscript, subscript, small caps and all caps,
a tracked heading, and two Japanese vertical columns. The Character and Paragraph panels
open before Warp Text; each runs a nested non-modal loop (`run_non_modal_dialog`), so the
scene opens each window from a timer inside the previous one's loop. Their native title
bars are not part of a widget grab, so `readme_captioned_dialog` draws the dark caption
(icon, title, close glyph) the desktop shows. The sample reads
"Let's play with letters / Let's enjoy color." Every sample is an editable
type layer. The scene registers its Latin and Japanese test fonts and reports
a skip if no Japanese family is available; it never publishes missing-glyph boxes.

The vector and SVG scenes select actual path anchors for their editing overlays.
A drag that starts over another shape's fill can pick and move that shape, so these
scenes check the active layer and selected-anchor count before capturing.

The KPT recipe uses `local-test-fixtures/photoshop-plugins/kpt5/KPT5_Files/KPT5`.
Keep the commercial binaries, presets, manuals, and generated state gitignored.
Follow the fixture scan and first-run requirements in [plugins.md](plugins.md).
The driver copies the fixture and a portable Patchy host into its task-owned
scratch directory, isolating both Patchy's settings and KPT's writable state.
An installed copy of KPT cannot override that fixture through duplicate IDs.

The KPT recipe pins its virtual screen to 1024 x 768, allows five seconds for the
preview to settle, then uses the runner's targeted `30,30` acceptance click.
No global cursor movement or keyboard input is involved.

The KPT picture combines two direct window captures: `patchy.ui.captureWindow`
for Patchy and `layer.applyPlugin(..., {dialog: true, captureDialog: ...})` for
the framed plug-in. The driver scales and positions the plug-in capture over
Patchy. This is a presentation layout, not a claim about physical window
positions. It never captures the desktop or crops a desktop screenshot.

Raw captures and script logs stay under `build/opt-dev/readme-shot-*`. A failed
script run leaves the last published image intact. The driver uses isolated
settings, a separate process, pinned 96-DPI geometry, and bounded plug-in windows.
Only task-owned processes may be stopped after a timeout. Never close a user's
documents or attach this capture workflow to an unrelated running instance.

Capture conventions, fixture requirements, and the separate Mehdi plug-in recipe are in
"README screenshots: capture conventions" below. The photograph's source and
license are recorded in [NOTICE-THIRD-PARTY.md](../NOTICE-THIRD-PARTY.md).

## README screenshots: capture conventions

`scripts\make-readme-screenshots.ps1` regenerates `docs/images/screenshots/`. The current
showcase, per-image inputs, and regeneration commands are listed in
[readme-showcase.md](readme-showcase.md). KPT is an explicit driver scene
(`-Scene plugin_kpt5`, or `-IncludeLegacyPlugins` for a full run) using an isolated
portable host and fixture copy; it combines direct Patchy and plug-in window captures.
Two pipelines:

- **Script-driven scenes** (`scripts/dev/readme-shots/*.js`, listed in the driver's
  `$jsScenes` table): a fresh unattended `patchy.exe --run-script` run stages the UI with the
  `patchy.ui` staging APIs on the REAL windows platform, so every installed font renders
  (offscreen enumerates none). The driver pins DPI (`QT_ENABLE_HIGHDPI_SCALING=0`,
  `QT_FONT_DPI=96`), sets `PATCHY_NO_SINGLE_INSTANCE=1`, and isolates settings with
  `PATCHY_SETTINGS_DIR`, so a run never touches the user's Patchy state. The window shows
  on screen for a few seconds per scene.
- **Offscreen test scenes** (everything not yet migrated): the `shot_readme_*` scenes in
  `patchy_ui_visual_tests` run offscreen and their artifacts are copied out. New scenes should
  be authored as scripts in the first pipeline; the remaining test scenes migrate over time and
  stay in the suite as regression smoke tests either way (the driver skips copying a test
  artifact whose scene has a script-driven owner). Scenes that picture a modal dialog the
  scripting surface cannot open stay here permanently: `shot_readme_image_trace` captures the
  Trace Image to Shapes dialog, which `layer.traceToShapes` bypasses and `app.runCommand`
  would block a script on; `shot_readme_long_shadow` opens the Layer Style dialog
  beside generated editable text with a live continuous shadow.

`scripts/dev/readme-shots/plugin-dialog.js` (driver scene `plugin_dialog`) produces
`docs/images/screenshots/plugin_dialog.png`, the dialog of a third-party 8bf plug-in
(Mehdi's Absolute Color from `local-test-fixtures/photoshop-plugins/mehdi`) captured through
`layer.applyPlugin(id, {captureDialog})` on Windows. The plug-in is not committed, so the
driver skips the scene with a `[SKIP]` line where that folder is missing; its window-targeted
capture and automatic dialog answers are authorized Patchy automation, not desktop control.

Both pipelines round the corners of the window they captured, because DWM rounds Patchy's
frameless windows in the compositor and a `QWidget::grab()` is therefore square. The offscreen
side does it in `save_readme_shot` and `draw_readme_overlay` (which also rounds the shadow it
fakes under a composited dialog); the driver does it to the script-driven PNGs in
`Set-RoundedWindowCorners`. Both use 8 px, `DWMWCP_ROUND`'s radius at 96 DPI, and leave the
corner pixels transparent so a shot reads as a window on a light or dark page. Change the
radius in both places or the two pipelines drift.
