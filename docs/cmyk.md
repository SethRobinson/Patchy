# CMYK numbers and the working CMYK space

Read this before touching the color picker's C/M/Y/K fields, the Info panel's CMYK line,
CMYK swatch import, the Preferences CMYK group, or `CmykWorkingSpace`. It covers GitHub
issue 90, stages 1 to 3. How imported CMYK files are read, composited and saved is a separate
subject: [psd-native-color.md](psd-native-color.md), [file-formats.md](file-formats.md)
and [blend-modes.md](blend-modes.md).

## What it is, and what it is not

Patchy edits in sRGB. The working CMYK space gives single colors a CMYK reading and turns
typed ink percentages into an RGB color. Nothing here changes document storage or
rendering: pixels are always RGB, also in CMYK Proof mode ("CMYK Proof mode" below).

The same space drives two views of the canvas, View > Proof Colors and View > Gamut
Warning ("The proof view" below). They change what is painted, never the document.

It also drives CMYK export of JPEG, TIFF and flattened PDF ("CMYK export" below), and
layered CMYK PSD and PSB saves in CMYK Proof mode ("CMYK Proof PSD saves" below).

Not built: CMYK forms of adjustment layers, layer styles, shape and fill layers, Blend If
and non-Normal blend modes (each makes a CMYK Proof PSD save RGB), a true CMYK document
mode, opening a CMYK PDF
into CMYK Proof (Qt PDF renders pages to RGB), Assign or Convert to Profile, a separate Proof
Setup dialog (the proof uses the working space from Preferences).

Limits to keep stating plainly in user-facing text:

- Ink numbers do not survive storage. RGB cannot hold every ink mix, so 100 percent black
  typed in the picker becomes one RGB color, and that color reads back as a four-ink mix.
  For black text, CMYK export's "Black and grays in black ink only" option restores K-only
  black (below).
- Numbers differ slightly from Photoshop's for the same profile (Little CMS against Adobe's
  engine; the measured difference is in [ps-compat.md](ps-compat.md)).

## The space

`CmykWorkingSpace` (`src/color/color_management.hpp`) is immutable and thread-safe:

- `from_icc_profile` / `from_profile_file`: a CMYK ICC profile, a `RenderingIntent` and
  black point compensation. Conversions run through Little CMS, sRGB on the RGB side,
  16-bit integers on the ink side, built with `cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE` (one
  color at a time needs no precalculated device link, and startup stays cheap).
- `formula()`: no profile. Black is the ink common to all three channels and the rest is
  scaled; the inverse is `(1 - ink) * (1 - black)`. Exact for every 8-bit color in both
  directions, and only a rough guide to print.

`working_cmyk_space()` is the process-wide instance and is never null.
`MainWindow`'s constructor installs the stored choice; Preferences replaces it on OK and
calls `refresh_color_picker_cmyk_fields()`.

Percentages are a print shop's: 0 is no ink, 100 is full ink (`CmykPercent`). The PSD
storage convention (255 = no ink) used by `CmykToRgbTransform` and `InkSpace` stays inside
the import code.

## Which profile

The choice is stored under `color/cmykProfile`:

| Stored value | Meaning |
|---|---|
| empty (default) | Automatic |
| `formula` | no profile |
| a path | that profile file; a missing or unusable file falls back to Automatic |

Automatic (`cmyk_working_space_for`, `src/ui/app_settings.cpp`) is U.S. Web Coated (SWOP)
v2 where Adobe software installed it (`automatic_cmyk_profile_path()`, the same profile
untagged CMYK imports use), otherwise the bundled ISO Coated v2 profile. It never picks an
operating system profile, so every machine without Adobe software, the Flatpak and the
browser show the same numbers.

`installed_cmyk_profiles()` lists the CMYK profiles in the operating system's and Adobe's
profile folders for the Preferences combo (bounded walk, header check first). Desktop
builds also offer "Choose Profile File..."; the web build does not, because a picked file
would not outlive the page.

`color/cmykIntent` holds a `rendering_intent_token` (`perceptual`,
`relative-colorimetric`, `saturation`, `absolute-colorimetric`; default relative
colorimetric) and `color/cmykBlackPointCompensation` a bool (default true). The three keys,
the tokens and the `formula` value are compatibility contracts.

### The bundled profile

`third_party/icc/basiccolor/ISOcoated_v2_bas.ICC` ("ISO Coated v2 (basICColor)", FOGRA39L,
ICC v2 CMYK printer class, 1,052,608 bytes, SHA-256
`7cdd1a643a1703fe62c30cd0cd2aba0fb01ae8d5a1ac985c8f8964ff73e4fae0`), unmodified, with its
licence file beside it. Both are compiled in through `src/ui/color_profiles.qrc`
(`:/icc/...`), so no package or installer lists them and the browser build has them too.
`patchy_color` is Qt-free: `bundled_cmyk_profile()` in the UI layer reads the resource and
hands the bytes to `CmykWorkingSpace::from_icc_profile`. Never edit or re-save the file; a
replacement is a new file with its own notice entry.

Licence record (October 14, 2026):

- zlib/libpng, Copyright (c) 2007-2010 basICColor GmbH. Announced by basICColor's managing
  director on the OpenICC list on September 1, 2010
  (https://lists.freedesktop.org/archives/openicc/2010q3/002218.html, naming "the default
  CMYK profiles" on colormanagement.org); the same text is the `COPYING` and
  `LICENSE-ZLIB-bICC` of OpenICC's `icc-profiles-basiccolor-printing2009-1.2.0.tar.gz`
  (SourceForge project `openicc`, SHA-256
  `9dfe89f08c934ad4b5ec9d3eda1f1cccb959168ba1ca873a9790deb5236d61d4`), which is where the
  file came from. The original colormanagement.org download link is dead.
- The profile's own copyright tag still reads "Copyright (c) 2006-2007 Color Solutions, All
  Rights Reserved": the profiling tool's stamp, and Color Solutions is the same company.
  The licence grant is the announcement and the package files.
- Rejected: Adobe's profiles (barred by [legal-constraints.md](legal-constraints.md)), and
  the ECI offset and IDEAlliance SWOP/GRACoL profiles, which allow use and embedding but
  restrict sale and need the owner's permission for inclusion in an installer. No freely
  licensed SWOP profile was found.

## Where the numbers appear

- **Color picker** (`src/ui/color_panel.cpp`): four integer spin boxes,
  `patchyColorCyanSpin`, `patchyColorMagentaSpin`, `patchyColorYellowSpin`,
  `patchyColorBlackSpin`. The picker keeps the typed values (`cmyk_`) together with the
  color they produced (`cmyk_color_`) and recomputes them only when the current color stops
  being that color or the working space changes. Recomputing on every sync would change a
  value under the cursor. The tooltip names the active profile.
- **Picker gamut warning** (`patchyColorGamutWarningButton`, under the preview): shown while
  the current color is outside the working space's gamut, filled with the color it prints as
  (`CmykProofTransform::printed`); a click takes that color. Hidden under the formula. The
  test comes from `cmyk_gamut_check()` (`src/ui/app_settings.hpp`), one proof transform per
  working space shared with `app.colorInCmykGamut`.
- **Info panel** (`MainWindow::update_canvas_info`): a `CMYK:` line under the RGB line.
- **Swatch files** (`src/formats/palette_io.cpp`): CMYK entries in `.aco` (space 2) and
  `.ase` (`CMYK` blocks) convert through the working space instead of being skipped. Lab
  and HSB entries are still skipped.
- **Preferences > Application > CMYK**: `preferencesCmykProfileCombo`,
  `preferencesCmykIntentCombo`, `preferencesCmykBlackPointCheck`. `preferencesCmykSourceInfo`
  states that only the bundled profile ships and the rest of the list was found on the
  machine, and the Automatic entry names its source ("included with Patchy" when its bytes
  are the bundled profile, else "found on this computer"). Keep both: the list shows
  third-party profiles Patchy may not redistribute.
- **Scripting**: `app.cmykProfile`, `app.colorToCmyk`, `app.cmykToColor`.

The eyedropper's status message still reports RGB and hex only.

## The proof view

Read the binding boundary in [legal-constraints.md](legal-constraints.md) ("Soft proof and
gamut warning") before extending this.

`CmykProofTransform` (`src/color/color_management.hpp`) is built from the working space and
two flags, and is null when the space has no profile:

- **Simulate colors**: an explicit four-profile chain (sRGB, profile, profile, sRGB), to the
  profile by the working intent and back relative colorimetric with black point
  compensation, as a precalculated device link, so whole rows are cheap. Neither paper
  white nor the ink's black is simulated (Photoshop's default proof).
  `cmsCreateProofingTransform` would turn compensation off on the way back and lift every
  shadow. Calibration (Photoshop 2026, ISO Coated v2): `#00a0e0` separates to 75/20/0/0,
  which Photoshop converts back to 8,159,218 relative colorimetric with compensation
  (34,161,219 without; rich black 3,2,2 against 32,32,32). PDFium (Qt PDF) converts back
  perceptually, 0,159,222. `ui_script_export_cmyk_writes_jpeg_tiff_and_pdf` pins both.
- **Mark out of gamut**: a 33-step-per-channel table, sampled once, of Little CMS's gamut
  test (a relative colorimetric round trip within 5 dE, `GamutSampler`) against the color
  after the separation's black point compensation. `cmsFLAGS_GAMUTCHECK` compares before
  compensation and marks pure black and every deep shadow, though the separation prints
  them as the press's black; with compensation off they clip and are marked. Marked pixels become `kGamutWarningColor`, a flat mid gray. The gray depicts the
  artwork's state, like the transparency checkerboard, so it does not follow the color
  scheme. The gamut edge is approximate by design.

`apply` works in place on straight-alpha RGBA8 rows, judges gamut on the original colors,
and leaves alpha alone.

`MainWindow::refresh_proof_display` rebuilds the transform when either View toggle or the
working space changes and hands it to every canvas (`apply_canvas_aid_settings` covers new
ones). The toggles are application-wide, have no default shortcut (command ids
`view.proof_colors`, `view.gamut_warning`), and are not remembered across runs: a forgotten
proof view would make every document look dull on the next start. With the formula they
refuse and say why.

On the canvas the view is applied at paint time, never to `render_cache_`:

- `CanvasWidget::proof_display_image(image)` returns a proofed copy of an image the paint
  path is about to draw, from a small cache keyed by `QImage::cacheKey` (least recently
  used first, bounded in count and bytes, the newest entry always kept). `paintEvent`'s
  `draw_scaled_image` and `draw_document_patch`, the move proxy and commit-hold draws, and
  the tiling ghosts go through it, so the composite, its mips, the Move, Free Transform and
  Warp drag bases and their patches are all proofed. At zoom below 100 percent only the
  displayed mip is proofed.
- A miss costs one pass over that image, once per content change, never per repaint.
  `patch_render_cache_patches` patches the proofed copy of the full composite alongside
  the cache and re-keys it, so a brush stroke under the view costs its own rect.
- Because `render_cache_` is untouched, the eyedropper, the Info panel, thumbnails, script
  previews, saves and exports all see the document's colors.
- Not proofed: the Dynamic Vector Preview overlay, the Patch tool's drag ghost, the Curves
  clipping preview (a diagnostic image), and mask or channel overlays.
- Memory: the view holds up to one more copy of what the canvas displays (512 MiB bound,
  128 MiB in the browser).

Scripting: `patchy.ui.proofColors`, `patchy.ui.gamutWarning`, `app.colorInCmykGamut`.

### Ink plates in the Channels panel

While Proof Colors is on, `MainWindow::refresh_channel_panel` lists Cyan, Magenta, Yellow and
Black after Blue (`ChannelPanel::RowKind::Cyan..Black`, targets
`LayerEditTarget::ComponentCyan..ComponentBlack`, both session-only). Selecting one shows the
composite over white paper separated through the working space, plate drawn as gray
`255 - ink` (dark means ink, Photoshop's convention). `refresh_mask_display_image` separates
once per render-cache content and working space (`plate_inks_`, keyed by
`QImage::cacheKey`), so switching plates is cheap; the buffer is dropped when the view leaves
the plates. Plates are read-only through `CanvasWidget::is_component_preview`. Ctrl-click
loads where a plate prints no ink as a selection. Turning the proof off removes the rows and
returns a plate view to the composite (`refresh_proof_display`). The plates are the generic
working-profile separation of the RGB composite, not an imported CMYK file's own planes, and
ignore the export's black-ink-only option.

## CMYK export

File > Export offers a Color row (`exportColorCombo`, RGB or CMYK with the profile's name)
for JPEG, TIFF and PDF, remembered per format under `saveOptions/exportColorJpeg`,
`saveOptions/exportColorTiff` and `saveOptions/exportColorPdf` (`rgb`/`cmyk`). Save and Save As
never offer it (the file is a print deliverable); for them CMYK Proof mode (below) decides the color form. The
CMYK entry is disabled under the formula, and PDF's editable-layers mode forces RGB (Qt's PDF
engine writes RGB only). Scripts pass `colorMode: "cmyk"`.

`ImageSaveOptions::export_cmyk` reaches `write_flat_image_file`, which refuses it for any
other format and throws when the working space has no profile:

- The document is narrowed to 8 bits, composited over white paper, and separated by
  `CmykWorkingSpace::separate_rgba8` (one optimized Little CMS transform with the working
  intent and black point compensation; 255 = full ink).
- **JPEG and TIFF**: Qt 6.8+ writes `QImage::Format_CMYK8888` itself, JPEG Adobe-style
  (APP14, inverted inks) and TIFF as separated (Photometric 5). The working profile replaces
  any RGB profile in the metadata the file embeds.
- **Black and grays in black ink only** (`exportBlackInkOnlyCheck`, off by default like
  Photoshop, enabled only with CMYK, remembered per format under
  `saveOptions/exportBlackInkOnlyJpeg|Tiff|Pdf`; scripts pass `blackInkOnly: true`, refused
  without `colorMode: "cmyk"`): `SeparationOptions::black_ink_only`. `separate_rgba8` samples
  what black ink alone prints at all 256 levels; a neutral pixel, or one within
  `kBlackInkOnlyTolerance` per channel of that curve (a color typed as K-only in the picker,
  and its antialiased blends toward paper), separates to 0/0/0/K with K from the nearest green
  (lower level on a tie). Everything else separates through the profile. Rich black text
  shows color fringes when the plates are slightly out of register, which is why print shops
  ask for this.
- **PDF** (flattened pages): `PdfExportOptions::cmyk_space`. Every page is written CMYK, gray
  pages included, as a four-component ICCBased image whose profile stream names
  `/Alternate /DeviceCMYK`. Lossless pages are Flate; lossy pages are Qt's CMYK JPEG with
  `/Decode [1 0 1 0 1 0 1 0]` for its inverted inks. Soft masks stay as they are, and an
  imported page's original image data is never carried over. The catalog carries an
  `/OutputIntents` entry (`pdf::ImageWriter::set_output_intent`: subtype `GTS_PDFX`,
  identifier `Custom`, `/Info` the profile's description, `/DestOutputProfile` the same
  profile object the pages use) so preflight tools know the print condition. No
  `GTS_PDFXVersion` is written, so the file makes no PDF/X conformance claim.

## CMYK Proof mode

Image > Mode > CMYK Proof (`imageModeCmykProofAction`, command id `image.mode_cmyk_proof`,
exclusive with RGB Color and Indexed) is `DocumentSession::cmyk_proof`: per document,
session-only, never written to a file, not an undo step.

- **View.** `refresh_proof_display` gives a CMYK Proof canvas `document_proof_transform_`,
  which always simulates colors (with the gamut warning when View > Gamut Warning is on;
  the View > Proof Colors transform itself when that is on). Other canvases keep the View
  transform. The Channels panel lists the ink plates whenever the active canvas's transform
  simulates colors.
- **Entering and leaving.** `set_session_cmyk_proof` refuses palette mode and the formula.
  From Indexed the menu runs Convert to RGB Color first; choosing Indexed or RGB Color leaves
  CMYK Proof. A working space without a profile (Preferences) turns every CMYK Proof
  document back to RGB with a status error.
- **Saving.** In `save_document_to_path`, unless `ImageSaveOptions::export_color_explicit`
  (set by the Export dialog's Color row and by a script's `colorMode`), the mode sets
  `export_cmyk` for every format `cmyk_form_available` accepts (JPEG, TIFF, flattened PDF)
  and RGB for the rest; black ink only comes from the per-format
  `saveOptions/exportBlackInkOnly*` key. Interactive saves to a CMYK form show
  `cmykProofSaveDialog` (profile, intent, paper white, reopened as RGB, the black-ink box)
  until "Don't show this again" sets `saveOptions/cmykProofSaveNotice` false. Interactive
  saves and Save As to other formats (PSD, PNG, editable-layer PDF) ask
  `cmykProofRgbSaveMessageBox` once per session (`cmyk_proof_rgb_save_accepted`); exports
  and scripted saves never prompt. A PSD or PSB save sets `ImageSaveOptions::psd_cmyk_proof`
  and writes CMYK with its layers ("CMYK Proof PSD saves" below); it asks only when
  `psd::DocumentIo::layered_save_color_mode` says the file will be RGB, and the question
  names its `rgb_reason`. The status line says "Saved ... in CMYK" whenever the file is CMYK.
- **Opening.** Qt hands a CMYK JPEG or TIFF over as `Format_CMYK8888`.
  `srgb_image_from_cmyk` (`main_window_files.cpp`) converts it through the embedded CMYK
  profile, else the working profile, relative colorimetric with black point compensation
  (Photoshop's way back), instead of Qt's profile-free conversion, and adds an import note,
  plus a second one when the file's profile is not the working one (saves use the working
  profile). `OpenDocumentResult::cmyk_source` then puts the session in CMYK Proof
  (`open_cmyk_source_in_proof_mode`, on Open, background multi-file Open and Reopen), so a
  plain Save writes CMYK again. Without any profile the file opens through Qt in RGB mode.
  A CMYK PSD or PSB (`psd.color_mode` "CMYK", layered or flat, GitHub issue 220) sets
  `cmyk_source` too. The PSD reader converts through the embedded profile (else the
  untagged fallback) and notes it; the open adds a note when that profile differs from the
  working one, which the proof view and CMYK JPEG, TIFF and PDF saves use, while a PSD save
  keeps the file's own profile. Its CMYK Proof note names PSD saves only when
  `cmyk_proof_psd_save_color_mode` says CMYK at open (an adjustment layer, for one, needs
  RGB); otherwise it gets the JPEG/TIFF wording. A second note says every time that CMYK Proof
  is an emulation (Patchy edits in RGB and converts through the profile, so colors and ink
  numbers can differ slightly from a true CMYK editor). An interactive open, single file or
  Open Folder, also shows that in `cmykProofOpenNoticeDialog` (`show_cmyk_proof_open_notice`)
  until its "Don't show this again" sets `imports/cmykProofOpenNotice` false; unattended and
  scripted opens never show it, and the UI suite turns it off by default (tests/ui/main.cpp).
- **Painting.** An imported 8-bit CMYK PSD composites on its inks in CMYK Proof and RGB
  mode alike, so its cost per brush dab does not depend on the mode: the dab separates only
  its own rect and the profile's transforms are built once per file
  ([blend-modes.md](blend-modes.md#imported-cmyk-documents), with the regression test).
- **Scripting.** `doc.cmykProof` (read/write; refusals throw).

## CMYK Proof PSD saves

`psd::WriteOptions::cmyk_proof` (the working space; the UI passes it for a CMYK Proof
document) makes `write_layered_rgb8` write an 8 or 16-bit CMYK PSD or PSB with its layers.
`layered_save_color_plan` decides once for the writer and for
`DocumentIo::layered_save_color_mode`, which the save question and the open note use, so
the answer always matches the file:

1. An imported file whose content is unchanged keeps its own mode, as without CMYK Proof
   ([psd-native-color.md](psd-native-color.md)).
2. Otherwise CMYK, unless a refusal below applies. The profile is the imported CMYK file's
   own (an untagged file stays untagged and separates through the same assumed profile as
   its preview), else the working profile, embedded as resource 1039 in place of any RGB
   profile. The working space's intent and black point compensation apply (relative
   colorimetric with compensation by default, as JPEG/TIFF export).
3. Layers of an imported CMYK file that `native_layer_unchanged` accepts write their own
   planes and blocks byte for byte, while the depth, profile resource and document profile
   still match the import. Every other raster, text and Smart Object record separates its
   RGB pixels (`separate_for_cmyk_proof`); alpha, masks, opacity and names pass through, and
   RGB identity blending ranges are dropped (none means the same in CMYK). The merged image
   is the separation of Patchy's own composite. Smart Filter caches convert to CMYK
   (`native_filter_effects_cache`) in 8-bit documents; a 16-bit document keeps only caches
   its CMYK source already had (Photoshop showed a converted 8-bit cache with two plates
   off), else the save refuses.

Refusals, each a `rgb_reason` string the save question shows: adjustment layers, layer
styles, shape and fill layers, Blend If or channel restrictions, blend modes other than
Normal and Pass Through (on layers not kept natively), 32-bit documents, too many saved
channels for four inks, Smart Filter caches that cannot convert, and a profile Little CMS
cannot use. `WriteOptions::preserve_source_color_mode = false` still forces RGB. Saves outside
CMYK Proof are unchanged.

Photoshop acceptance (Photoshop 2026, October 19, 2026, ISO Coated v2 working profile;
`scripts\dev\photoshop-flatten-check.ps1` opens each file with a dialog watcher and saves
Photoshop's flatten, `scripts\dev\psd-merged-diff.py` compares it with the file's merged
image, in 8-bit ink steps):

- Opened with no dialog, PSD and PSB, 8 and 16 bits: RGB-born layered documents, edited
  CMYK files, Patchy-authored and Photoshop text (RGB EngineData colors are accepted in a
  CMYK file), Smart Objects, and Smart Filters in 8-bit documents. Files of every refused kind also
  opened clean; they are refused for fidelity, not acceptance.
- Opaque raster pixels, Smart Objects and Smart Filter previews flatten identically (0).
  Translucent pixels differ (RGB-born test stack: up to 16, mean under 1) because
  Photoshop blends in ink, as it does after its own mode conversion; text differs only on
  antialiased edges (up to 67 on black, 0.4 percent of pixels). The CMYK Proof view shows
  the separation of the RGB composite, so it can differ from the saved file there by as much.
- Refused, measured with the refusal bypassed: adjustment layers act on the inks (master
  Levels and Curves mean 9 steps, Invert, Threshold and Hue/Saturation 40 to 230); styles,
  shapes and fills are rebuilt from RGB descriptor colors (means 2 to 13, patterns 64);
  Blend If ranges name RGB channels (mean 74); non-Normal modes mix inks (Difference up to
  178, Multiply mean 2).

## Tests

Core (`tests/core/palette_tests.cpp`): `cmyk_working_space_formula_converts_both_ways`,
`cmyk_working_space_reads_profile_from_unicode_path` (the bundled profile), `palette_io_converts_cmyk_swatches_through_working_space`,
`cmyk_proof_transform_simulates_colors_and_marks_gamut`,
`cmyk_working_space_separates_rows_for_export`,
`cmyk_black_ink_only_keeps_black_and_grays_on_the_black_plate`. CMYK Proof PSD saves
(`tests/core/psd_cmyk_proof_tests.cpp`): RGB-born and edited CMYK documents at 8 and 16 bits,
PSD and PSB, text and Smart Objects, every refusal, and the Photoshop acceptance files it
writes under `test-artifacts/cmyk-proof`.

UI: `ui_color_picker_cmyk_fields_follow_working_space`,
`ui_color_picker_warns_when_a_color_cannot_print`,
`ui_channels_panel_shows_cmyk_plates_under_proof_colors` (`tests/ui/channels_panel_tests.cpp`),
`ui_preferences_cmyk_settings_persist_and_apply`
(`tests/ui/pickers_notices_hotkeys_tests.cpp`) and
`ui_script_cmyk_conversions_use_working_space` and
`ui_proof_colors_and_gamut_warning_change_only_the_view`,
`ui_script_export_cmyk_writes_jpeg_tiff_and_pdf`, `ui_cmyk_proof_mode_saves_cmyk`, `ui_cmyk_files_open_in_cmyk_proof`,
`ui_cmyk_psd_opens_in_cmyk_proof`, `ui_cmyk_proof_saves_layered_psd_in_cmyk`,
`ui_cmyk_open_explains_cmyk_proof_emulation` and the local-fixture
`ui_issue_220_photoshop_cmyk_psd_opens_in_cmyk_proof_if_available` (`tests/ui/scripting_tests_color_formats.cpp`), and
`ui_export_dialog_offers_cmyk_for_print_formats` (`tests/ui/flat_image_format_tests.cpp`). UI tests
that assert ink numbers store the `formula` choice first: Automatic is Adobe's profile on a
machine that has it.
