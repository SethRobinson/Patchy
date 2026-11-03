# Export for Web

File > Export for Web... (`file.export_for_web`, Ctrl+Alt+Shift+S; action
`fileExportForWebAction`) writes a copy of the document in a web format from a dialog with live,
real encoded previews and sizes. It sits in the File menu itself, under Save As and Revert and right above the Export submenu,
not inside it (Seth, October 26, 2026, and again for GitHub issue 619: it is the export most
people use). Export > Flat Image is unchanged and still covers every format; it gave up the
Ctrl+Alt+Shift+S default when this command arrived (a user binding keeps working). "Save for Web"
is another product's command name, and the layout (a format list with live sizes, a split
divider) is Patchy's own.

Code: `src/ui/web_export.{hpp,cpp}` (settings, preview engine, panes, dialog),
`MainWindow::export_for_web` in `src/ui/main_window_files.cpp` (file name and write), the writer
hooks in `src/ui/image_document_io.cpp`.

## Formats and options

The list is JPEG, JPEG XL, WebP, AVIF, PNG and GIF (`web_export_formats()`; JPEG XL sits under
JPEG since GitHub issue 571, display order only: `WebExportFormat` stays append-only and the
tokens do not change; AVIF only where `avif::is_available()`, so not in the browser build). Each
format keeps its own settings.

Layout (GitHub issue 571): the settings column is as wide as its widest section needs once
polished (fields with their - and + buttons), between `ui_text_length(328)` and
`ui_text_length(410)`, and
`ui_web_export_settings_column_fits_every_language` fails when any label, check box, radio or
button in it is cut off, for any format, in any shipped language (so long explanations go in
tooltips: Progressive, Optimize coding, Include thumbnail). Full-width combos (Type, Resample,
Metadata) stop at `ui_text_length(300)` and may shrink below their longest entry
(`fit_combo_popup_to_entries` keeps the open list whole); the column's forms, the animation
timing rows included, use `WrapLongRows`, so a translated label too long for its row puts the
field under it. Every section is a form on one label column (`align_form_label_columns`, the
Animation group excepted), check boxes and notes in the field column under the fields. The
number fields (Quality, Speed, Effort, Colors, dither Amount, Width, Height, Scale) have the
shared - and + buttons and step as before (`webExportStepSpin`: right padding 30 px, as
Preferences > Interface scale); the Animation timing fields keep none (`webExportNoSpinButtons`).
AVIF Speed and JPEG XL Effort share one width.
The dialog opens at its minimum size the first time, then at the size it last closed with
(`exportForWeb/dialogSize`, bounded by the screen).

- Presets (top of the column): `webExportPresetCombo` with Save... (asks a name; the same name
  replaces) and Delete (user presets only). A `WebExportPreset` holds the format, that format's
  options, and the shared ones: Fill and its color, the size as a percent of the document's width
  (Pixel art: the factor), Resample, Trim, Metadata, Convert to sRGB; never another format's
  options, the compare mode, Show when done or play counts. The combo shows the preset the
  settings match (`web_export_preset_matches`: applying it changes nothing; the last picked one
  wins a tie), else the placeholder Custom. Built-in presets (`web_export_builtin_presets`: JPEG
  Very High 85, JPEG Medium 50, WebP High 70, PNG 256 Colors) are not stored and cannot be
  deleted. On a one-layer document a preset applies without animation.
- Quality presets: a chevron drop-down against each Quality field of JPEG, JPEG XL, WebP and AVIF
  (`webExport<Format>QualityPresetsButton`/`...Menu`; `web_export_quality_presets`: Low 30,
  Medium 50, High 70, Very High 85, Maximum 100, Patchy's own list), the shared
  `make_spin_presets_button` that Preferences > Interface scale uses too. Picking one sets the
  quality; the preset the field holds is ticked when the list opens.

- JPEG: Quality 1-100 (default 80), Progressive (default on), Optimize coding (optimized Huffman
  tables, default on). JPEG has no alpha: the Transparency group (below) always fills.
- WebP: Quality 0-100 (75), Lossless; animated through the Animation group (below).
- AVIF: Quality 0-100 (80), Speed 0-10 (6, lower is smaller and slower), Lossless: Flat Image's
  `avif_*` options through the vendored libaom writer ([avif.md](avif.md); its codec rules apply).
- JPEG XL: Quality 1-100 (90), Effort 1-9 (7), Lossless ([jxl.md](jxl.md); animated frames are 8-bit).
- PNG: Colors Full color (lossless, transparency kept) or Palette: the GIF quantizer's Colors,
  Dither and Amount below, written as an indexed PNG with one fully transparent entry (tRNS) for
  pixels under 50% alpha (`ImageSaveOptions::png_palette`, `quantized_png8_image`). Animated, PNG
  is an APNG (`apng_animate`, the File > Export > Layers as Animated PNG writer,
  [animation.md](animation.md)) that keeps the .png extension and is always full color (8-bit
  RGBA): while it is ticked, Type and the palette controls are disabled with a note and
  `png_palette` is ignored. A still PNG is unchanged.
- GIF: Colors 2-256 (default 256), Dither None or Diffusion with Amount 0-100% (default None,
  100%), pixels under 50% alpha transparent (`gif::kQuantizeAlphaThreshold`). GIF and palette PNG
  show the colors the file uses as swatches (`WebExportResult::palette`, the decoded result's
  distinct colors).
- Animation (Seth, October 27, 2026: like the Layers as Animated dialogs): one group, shown for GIF,
  WebP, JPEG XL and PNG when the document has two or more layers and a visible one. It holds the
  format's own "Animation from visible layers" check and the old dialogs' timing rows from the
  shared helper `create_animation_timing_controls` (image_save_options_dialog): "Default frame
  delay:" (GIF in centiseconds, 2 decimals; the others 3 decimals up to `kMaxFrameDelayMs`; a layer
  name's `0.25s` token wins), Forever / Play count for WebP, JPEG XL and APNG (GIF loops forever,
  its explanation says so) and the dialogs' explanation. The delay is one value
  (`animation_frame_delay_ms`): editing it in one format's row updates the others. Play counts
  (`webp_loop_count`, `jxl_loop_count`, `apng_loop_count`) start from the document's metadata
  (`webp.loop_count`, `jxl.loop_count`, `apng.loop_count`) and are written back after an animated
  export, as the old exports do; they are never persisted in settings.
- Transparency (formats with alpha): Keep transparent or Fill with a color on one row (two rows
  when the language's labels do not fit the column), the Export section's group through the
  shared helper `add_transparency_choice` with `one_row`
  (`webExportKeepTransparencyRadio`, `webExportFillTransparencyRadio`,
  `webExportBackgroundColorSwatch`). The color is one value (`jpeg_background`, kept under its old
  key). JPEG keeps Keep transparent in sight, disabled, with Fill with selected
  (`keep_shown_when_forced`; the user's choice for the formats with alpha is held aside,
  `transparency_choice_fills`), then an info mark (`webExportJpegBackgroundInfo`, a themed glyph)
  whose tooltip says JPEG has no transparency. The Export section's forced fill is unchanged.
  GIF and palette PNG show "Pixels under 50% opacity become transparent."
  (`webExportTransparencyCutNote`) here, disabled while Fill is on. Fill
  (`export_fill_transparent`) applies to the still result, every animated frame
  (`for_each_animation_export_frame`) and so the preview; the Original pane stays unfilled.
- Image size: a form: "Size:" (width, the proportion link, height and one "px"; the fields have
  no unit suffix, typed units still convert; tooltips name them), "Scale:" with "of W x H",
  "Resample:": Smooth (the export resize,
  `export_width`/`export_height`) or Pixel art (whole-pixel `export_scale` 1x-16x, the size fields
  then follow the scale); Trim transparent edges. The size is per document, never persisted.
- Metadata (Seth, October 27, 2026): None (default), Basic, Copyright and Contact Info, All, each
  with a tooltip (`add_export_metadata_choices`, shared with the Export section). Basic is the
  Preferences "Saved metadata: Basic" save stamp alone, for the exported file ([file-info.md](file-info.md),
  "Writing"). "Include thumbnail" (JPEG only, off) works with every choice: with None the JPEG
  gets one APP1 holding only IFD0's resolution and the IFD1 thumbnail
  (`jpeg_exif_thumbnail_without_metadata`). Save and Save As are unchanged.
- The settings column is a `QScrollArea` (`webExportControlsScroll`) under
  `install_wheel_scroll_guard`: a wheel over an unfocused combo box, spin box or slider scrolls
  the column; a focused one takes the wheel. The preview keeps its wheel zoom.
- Color: an sRGB document (every RGB document after open, [rgb-profiles.md](rgb-profiles.md))
  writes no profile and the row says so. A document that still carries another RGB profile gets
  "Convert to sRGB (from <profile>)", on by default; unchecked, the file keeps that profile.
- 16 and 32-bit documents export 8-bit here (`document_for_8bit_writer` once per dialog).
- Not offered: CMYK and the non-web formats (Export > Flat Image).

`jpeg_progressive` and `jpeg_optimize` default to false in `ImageSaveOptions`, so Save, Save As,
Export Flat Image and the byte canaries keep their bytes; `configure_image_writer` sets the Qt
writer options only when asked for.

### Quantizer

`palette_colors`, `palette_dither`, `palette_dither_amount` map to `QuantizeOptions`
(`formats/document_flatten.hpp`): `indexed_rgba8_quantized` picks the colors as before (the exact
colors when they fit, else the deterministic median cut; the transparent slot counts toward the
total) and, with diffusion, first runs the palette mode's integer Floyd-Steinberg
(`apply_palette_to_pixels`, serpentine, error in 1/16 units) on a copy with the error scaled by
`amount / 100` (truncating). The defaults (256 colors, no diffusion) are the old call exactly, so
`gif_encoder_bytes_are_stable` and the GIF writer's bytes do not change; `gif::write(document,
options)` and the animated GIF frames take the same options. Palette-mode documents keep their own
palette and ignore them. Pinned by `gif_quantize_*` (core).

## Preview engine

`WebExportPreviewEngine` has two lanes, each with one worker at a time: lane 0 encodes the
selected format, lane 1 the other rows one after another (fast formats first). A slow AVIF or JPEG
XL encode in lane 1 therefore never delays the selected format, and the UI thread only keeps
books. `request()` replaces both queues and bumps the generation; a queued job whose generation
is stale when its worker starts is dropped unrun, and one already running finishes and is cached
by signature. The narrowed source and the prepared document are made under a mutex once and then
shared read-only by both lanes. AVIF and JPEG XL sizes are only computed in the background after
their row has been selected once in that dialog (until then the row says "select to encode"): on
a large photo libaom and libjxl take seconds, and encoding them for every quality change of
another format would keep a core busy for nothing. Each job:

1. Narrows the source to 8 bits once per dialog.
2. Prepares the resized, trimmed, sRGB-converted stand-in (`export_transformed_document`), cached
   by its geometry signature, and its display image (`flat_export_qimage`, ARGB32 premultiplied).
3. Encodes through `encode_flat_image_bytes` (the real `write_flat_image_file` into a private
   temporary folder) with `without_export_transforms` options plus JPEG's background fill, or from
   the whole document for an animated WebP.
4. Decodes the bytes (`decode_web_export_bytes`): Qt's readers for JPEG, WebP, PNG and GIF,
   `avif::read_avif` and `jxl::read_jxl` for the other two (an animation shows frame 1).

Results are cached by signature (format, geometry, metadata and the format's options; at most 12),
so equal settings never encode twice and switching back is instant. The dialog shows a result only
when its signature matches the current settings; until then the last image stays up with
"encoding...", and a row whose settings changed keeps its last size, in italics with " ...",
until the new size lands. Settings changes restart a 200 ms debounce. Save reuses the current result's bytes
(`write_encoded_image_file`, a QSaveFile); while the current settings are still encoding, Save
waits for them. With no current result, `export_for_web` encodes on the save worker.

Panes (`WebExportPane`, a ZoomableImagePreview) draw only cached images: below 100% each image's
smooth reduction is built once per size on a worker (`ScaledImageCache`). Each pane draws a
frame (`group_box_border`) in every view. View modes (the "View:" buttons, in this order): Split
(a divider at a fraction of the pane width, original left, result right, dragged with the
mouse), Side by side (a second pane; `ZoomableImagePreview::view_state`/`set_view_state` keep
zoom and pan in step across every showing pane), 4-up (GitHub issue 571; token `quad`) and
Result only. 4-up shows the
original top left, the result top right, and below them the selected format at 50% and 25% of
its quality (`web_export_reduced_settings`: rounded, clamped to the format's minimum, so 60 gives
30 and 15; GIF and palette PNG halve and quarter Colors). Each tile's tag names its quality and
size; a format with nothing to reduce (full-color PNG, lossless) shows a note in the lower tiles.
The two extra jobs go to the second lane right after the selected format's job, only while 4-up
is on, so AVIF and JPEG XL stay off the UI thread like every encode; a tile of an animated result
shows frame 1. The panes share one minimum area (300 x 420) in every view, split between them,
so switching views never resizes the dialog. One row under the preview (`webExportViewRow`,
never wrapped; its width is the dialog's minimum): "View:" and the view buttons, then "Zoom:",
the amount as a plain percent (`webExportZoomLabel`, sized for "1600%" plus room), - and + of
one width, 100% and Fit at the right edge; Fit is checkable and checked while the preview fits.
The view, zoom and frame-stepper buttons are framed push buttons with no explicit minimum width,
so none is ever narrower than its text; Play/Pause is as wide as the wider of its two texts.
`ui_web_export_preview_buttons_fit_at_minimum_size` checks every button and label under the
preview at the minimum size in every language, still and animated. The busy spinner
(`webExportBusySpinner`, test probe `webExportBusy`) turns only while the shown result, and in
4-up its two tiles, is still encoding; background rows never keep it turning. `ScaledImageCache` keeps up to 64 reductions per size by image,
so an animation's frames are reduced once each and then reused as it loops.

### Animation playback

An animated result (GIF, WebP, JPEG XL or PNG with "Animation from visible layers" and 2+ frames) plays
in the preview (Seth, October 27, 2026):

- The worker decodes every frame of the encoded bytes (`decode_web_export_animation`): Qt's GIF
  reader with `nextImageDelay`, `webp::decode_animation`, `apng::read_animated_png` (composited
  frames, millisecond delays), and `jxl::read_jxl` (frame layers, delays
  from their names), so palette, dither, lossy artifacts and the written delays (layer-name times
  included) are what plays. The source frames come from `for_each_animation_export_frame`, the
  same frame list and per-frame transforms the animated writers encode (visible top-level layers,
  top first; [animation.md](animation.md)). Both are cached as 8-bit display images in
  `WebExportResult::frames`/`original_frames` with `frame_delays_ms`; the source is already
  narrowed to 8 bits, so no deep image reaches the painter.
- Memory: both sides together stay under `kWebExportAnimationCacheBytes` (384 MB); above it the
  preview keeps the first frames and the info line says so (`frames_total` is the file's count).
- The dialog keeps the playing result until another animated result for the selected format
  replaces it, so playback continues on the last frames while a new encode runs; a stale
  generation's frames are never shown (results are matched by signature as before).
- A single-shot timer (`webExportPlaybackTimer`) shows the next cached frame and restarts with
  that frame's delay (under 20 ms plays at 100 ms, as browsers do); it never decodes or
  composites. It stops on a still format, on Pause, at the end without Loop, and on close
  (`QDialog::finished`).
- Controls (`webExportPlaybackControls`, shown only then): Play/Pause (playing by default),
  previous and next frame (which pause), "Frame 2 of 4 (0.50 s)", Loop (on). Zoom, pan, the
  divider and the compare modes keep working while it plays; the info line adds the frame count
  and the time per loop. Test probes on `webExportPreview`: `webExportFrameIndex`,
  `webExportFrameCount`, `webExportPlaying`.

## Settings (permanent identifiers)

`exportForWeb/format` (`jpeg`, `webp`, `png`), `exportForWeb/jpegQuality`, `jpegProgressive`,
`jpegOptimize`, `jpegBackground` (`#rrggbb`), `webpQuality`, `webpLossless`, `webpAnimate`,
`metadata` (`none`, `basic`, `copyright`, `all`), `thumbnail`, `resample` (`smooth`, `pixelArt`), `trim`,
`convertToSrgb`, `compare` (`split`, `sideBySide`, `result`, `quad` for 4-up), `revealWhenDone`, `pngColors`
(`full`, `palette`), `pngPaletteColors`, `pngDither` and `gifDither` (`none`, `diffusion`),
`pngDitherAmount`, `pngAnimate`, `pngFrameDelayMs`, `gifColors`, `gifDitherAmount`, `gifAnimate`, `gifFrameDelayMs`, `avifQuality`, `avifSpeed`, `avifLossless`,
`jxlQuality`, `jxlEffort`, `jxlLossless`, `jxlAnimate`, `jxlFrameDelayMs`, `fillTransparent`, all
under `exportForWeb/`. The default frame delay is the animated exports' `saveOptions/animationFrameDelayMs`
(GIF's `saveOptions/gifFrameDelayCs` written beside it); the three `*FrameDelayMs` keys are still
written with it and read only when the shared key was never set. Fill has its own key, not
`saveOptions/exportFillTransparent`, so Export for Web's choice never changes Save As exports.
Saved when the dialog is accepted, except `compare` and `dialogSize` (the window size, a QSize),
which are saved however it closes. `presets` is a compact JSON array of user presets, each an
object keyed like the settings above (`name`, `format`, that format's keys, `frameDelayMs` for the
animating formats, `fillTransparent`, `jpegBackground`, `metadata`, `resample`, `scale` as a
percent, `trim`, `convertToSrgb`); the keys are persisted identifiers. Reset returns every option
to its first-use default and keeps the compare mode and reveal choice.

## Scripting

`doc.exportAs(path, options)` and `doc.saveAs` take `progressive`, `optimizeCoding` and
`thumbnail` for `.jpg`/`.jpeg`, and `colors` (2..256), `dither` (`"none"`, `"diffusion"`) and
`ditherAmount` (0..100) for `.gif` and for `.png` with `pngColors: "palette"` (`"full"` is the
default; the palette keys without it throw). `.png` also takes `animate`, `frameDelayMs` and
`loopCount`, the animation keys `.jxl` takes (an APNG; `animate` with `pngColors: "palette"`
throws). Strict, like every save option
([scripting-api-changes.md](scripting-api-changes.md)). `metadata` was already there.

`doc.encodeForWeb(format, options)` encodes without a file and returns `{bytes, width, height}`
(`bytes` an ArrayBuffer). `format` is a persisted token (`"jpeg"`, `"webp"`, `"png"`, `"gif"`,
`"avif"`, `"jxl"`; `"avif"` throws where AVIF is not built); `options` are exactly the `exportAs`
keys of that format, parsed by the same strict reader, starting from the same defaults, and the
bytes are what `exportAs` would write for an RGB document (a CMYK Proof document still needs
`colorMode: "cmyk"` for CMYK). `width`/`height` come from decoding the result.

## Tests

Core: `gif_quantize_*`, `gif_encoder_bytes_are_stable`. UI: `ui_web_export_defaults_and_options`,
`ui_web_export_palette_png_and_gif_read_back`, `ui_script_export_as_palette_keys`,
`ui_web_export_avif_and_jxl_read_back`, `ui_web_export_slow_encode_never_blocks_the_selected_format`,
`ui_script_encode_for_web`, `ui_script_export_as_png_animation`,
`ui_web_export_still_png_is_unchanged_by_animation`,
`ui_web_export_jpeg_progressive_and_optimize_read_back`,
`ui_web_export_engine_drops_stale_requests`, `ui_web_export_animation_frames_read_back`,
`ui_web_export_play_count_and_fill_read_back`,
`ui_web_export_wheel_over_unfocused_control_scrolls_the_settings`,
`ui_web_export_metadata_choices_and_thumbnail_with_none` (captures `web_export_metadata_choices`),
`ui_web_export_animation_stale_frames_are_discarded`,
`ui_web_export_format_order_quality_steps_and_presets`,
`ui_web_export_dialog_keeps_size_and_compare_mode_through_cancel`,
`ui_web_export_quad_compare_and_presets_in_the_dialog` (captures `web_export_presets`,
`web_export_png_transparency` and `web_export_quad`),
`ui_web_export_settings_column_fits_every_language`, `ui_web_export_preview_buttons_fit_at_minimum_size`,
`ui_web_export_view_row_order_and_zoom_controls`, `ui_web_export_view_modes_keep_the_dialog_size`,
`ui_web_export_jpeg_keeps_the_transparency_choice` (captures `web_export_jpeg_settings`),
`ui_web_export_spinner_stops_when_the_shown_results_arrive`
(captures `web_export_minimum_size` and `web_export_minimum_size_animated`),
`ui_web_export_animation_plays_in_the_preview`
(captures `web_export_gif_animation`, `web_export_png_animation` and `web_export_transparency_fill` from `test-fixtures/psd/web-export-animation.psd`, the #198
Animation Preview document made by Patchy), `ui_web_export_dialog_previews_and_saves` (captures
`web_export_jpeg_split`, `web_export_png_palette`, `web_export_gif_side_by_side`,
`web_export_avif` and `web_export_jxl`), `ui_web_export_metadata_and_thumbnail_choices`,
`ui_unicode_export_for_web_writes_encoded_bytes`, `ui_script_export_as_jpeg_coding_options`, the
dialog sweep, `ui_file_export_menu_actions_registered` and `ui_photoshop_shortcuts_are_registered`.
