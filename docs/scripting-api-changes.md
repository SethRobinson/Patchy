# Scripting API compatibility

`app.apiVersion` is 1. Bump it only for breaking API changes. Every addition and
behavioral change is recorded here, newest first, in the same change that makes it; the
entries are a permanent record. Entries through 2026-09-30 (the July 2026 API, MCP
attachment, palettes, vector automation, brushes, Slow mode and Pause, text direction,
Remove Object, Align/Distribute and smart objects) are in
[scripting-api-changes-through-2026-09-30.md](scripting-api-changes-through-2026-09-30.md),
those of 2026-10-01 to 2026-10-05 in
[scripting-api-changes-2026-10-01-to-05.md](scripting-api-changes-2026-10-01-to-05.md), and
those of 2026-10-06 to 2026-10-09 in
[scripting-api-changes-2026-10-06-to-09.md](scripting-api-changes-2026-10-06-to-09.md), and
those of 2026-10-10 to 2026-10-19 in
[scripting-api-changes-2026-10-10-to-19.md](scripting-api-changes-2026-10-10-to-19.md).
Every entry below is API 1.

2026-10-26 (additive): `doc.trim` takes `tolerance` (0..255, default 0 = the exact match it always
made; GitHub issue 297): Transparent Pixels then cuts alpha up to it, a corner color every pixel whose
channels (alpha included) are all within it of the corner, in 8-bit units at every depth. Out of range
or not a number throws. Permanent identifiers: the option key `tolerance` and settings key
`trim/tolerance`. Pinned by `ui_script_trim` and `ui_trim_tolerance_cuts_near_matches`.
2026-10-25 (additive): `layer.applyCameraRawFilter(settings)` runs Filter > Camera Raw Filter (GitHub
issue 392, [camera-raw.md](camera-raw.md#camera-raw-filter)) on an RGB pixel layer at its depth, limited
to the selection like `applyFilter`. Permanent setting names: `temperature`, `tint` (-100..100,
relative), `exposure` (-5..5 EV), `contrast`, `highlights`, `shadows`, `saturation`, `vibrance`
(-100..100); missing ones are neutral, out-of-range numbers clamp, unknown names or non-numbers throw.
Pinned by `ui_camera_raw_filter_script_and_selection`.
2026-10-25 (additive): blend mode ids `soft-light-svg`, `additive-subtractive`, `arc-tangent`,
`geometric-mean`, `allanon`, `parallel`, `hard-overlay`, `interpolation` and `easy-dodge` (Krita's
modes, for .kra files, [kra.md](kra.md)), appended in that order after GIMP's to the permanent list
for `layer.blendMode` and stroke `mode`. 8 and 16-bit documents only; a PSD save writes them as
`normal` or bakes them. Pinned by `ui_script_krita_blend_mode_ids`.
2026-10-25 (additive): blend mode ids `grain-extract`, `grain-merge`, `hsv-hue`, `hsv-saturation`,
`hsl-color`, `hsv-value`, `lch-hue`, `lch-chroma`, `lch-color`, `lch-lightness`, `luminance`,
`luma-darken-only`, `luma-lighten-only`, `linear-multiply`, `linear-addition`, `linear-subtract` and
`linear-divide` (GIMP's modes, for .xcf files, [xcf.md](xcf.md)), appended in that order to the
permanent list for `layer.blendMode` and stroke `mode`. 8 and 16-bit documents only; a PSD save writes
them as `normal` or bakes them. Pinned by `ui_script_gimp_blend_mode_ids`.
2026-10-25 (additive): `app.open` and the other path-taking calls open GIMP `.xcf` files (also
`.xcf.gz`, `.xcf.bz2`, `.xcf.xz`, `.xcfgz`, `.xcfbz2`, `.xcfxz`) as layered documents, read-only like
`.pdn`; `doc.importNotes` lists what was approximated ([xcf.md](xcf.md)).
2026-10-25 (behavioral): `layer.transform` on a pixel layer while the document has a selection
transforms only the selected pixels, as Free Transform now does (GitHub issue 399,
[free-transform.md](free-transform.md#transforming-the-selected-pixels-github-issue-399)): the box
is the selection bounds clipped to the layer's pixels, the transformed pixels land back on the
layer, and the selection follows them. Without a selection, and on text, shape and smart object
layers, it transforms the whole layer as before. Pinned by
`ui_free_transform_selection_on_background_and_script`.
2026-10-25 (additive): save option `unsupportedBlendModes` for `.psd`, `.psb`, layered `.tif`/`.tiff`
and `.aseprite`/`.ase` (GitHub issue 379, [blend-modes.md](blend-modes.md#layered-saves)): `"normal"`
(the default, as before) writes layers in blend modes the format cannot store as Normal; `"bake"`
rebuilds each as a Normal layer that keeps the look. Scripted saves never show the Save prompt that
asks this. The key and its tokens are permanent identifiers. Pinned by
`ui_script_save_unsupported_blend_modes_option`.
2026-10-25 (additive): `doc.importNotes` (read only, string array): the notes the file's reader left
when the document opened (dropped or approximated features, the import notes the status bar shows),
empty for new documents. Permanent identifier: the property name. Pinned by
`ui_script_import_notes_list_reader_notices`.

2026-10-24 (additive): `doc.saveAs` and `doc.exportAs` write OpenRaster for a `.ora` path
([ora.md](ora.md)): the layer tree stays, and what the format cannot hold is baked into layer pixels
with no prompt (a scripted save answers it, like the Aseprite Fill Opacity one). `saveAs` points the
document at the file, as for `.psd`. The extension token `ora` is a permanent identifier. No save
options apply to it. Pinned by
`ora_round_trip_keeps_layers_and_pixels` and `ui_ora_save_warns_about_baked_content_and_stays_on_ora`.
2026-10-24 (additive): blend mode ids `reflect`, `glow`, `negation` and `xor` (Paint.NET's modes,
GitHub issue 379), appended to the permanent list for `layer.blendMode` and stroke `mode`. 8 and
16-bit documents only; a PSD save writes them as `normal`. Pinned by
`ui_script_paint_dot_net_blend_mode_ids`.

2026-10-24 (behavioral): `layer.redEye` uses the revised Red Eye method ([red-eye.md](red-eye.md)): the
area is grown with hysteresis and its rim and glint halo corrected by redness, and Darken Amount keeps
a quarter of the brightness at 100 instead of going black (Darken 50 keeps about 60%). Options and
result keys are unchanged. Pinned by `ui_script_red_eye_corrects_pupil`.

2026-10-24 (additive): the Color panel (Window > Color). `patchy.ui.expandPanel`,
`collapsePanel`, `closePanel` and `panelState` take the panel name `color` (d.ts
`PatchyPanelName`), a permanent identifier mapped to the panel id `colorDock`. Pinned by
`ui_color_panel_window_menu_and_script_name`.

2026-10-24 (additive): `doc.exportAnimatedPng(path, options?)` writes the visible top-level layers
as an animated PNG (APNG), like `exportAnimatedWebp`: options `frameDelayMs` and `loopCount`, a
`.png` or `.apng` path, document path and modified state unchanged. The loop count is kept in
document metadata `apng.loop_count` (also set when an animated PNG opens). Permanent identifiers:
the method and option names, the metadata key and command id `file.export_animated_png`. Pinned by
`ui_script_apng_animation_export_preserves_document_and_validates_options`.
2026-10-24 (additive): `app.newDocument(width, height, options?)` takes `name` (the tab title and
default Save As name, through the New Document dialog's file-name rule; throws when nothing usable is
left) and `colorMode` (`"rgb"` default, or `"cmykProof"`; throws for other values and without a CMYK
profile). Both are checked before the document is created. Permanent identifiers: the option keys and
the `colorMode` tokens ([new-document.md](new-document.md)). Pinned by `ui_script_new_document_options`.
2026-10-24 (additive): `layer.redEye({x, y, width?, height?, pupilSize?, darkenAmount?})`, the Red
Eye tool ([healing.md](healing.md#red-eye)): a click at `{x, y}` or a box, Pupil Size and Darken
Amount 1..100 (default 50), returns `{pupilPixels, x, y, width, height}`, throws when no red is found.
Permanent identifiers: the method and key names, hotkey command id `tools.red_eye` and the settings
keys `tools/redEyePupilSize`, `tools/redEyeDarkenAmount`. Pinned by `ui_script_red_eye_corrects_pupil`.

2026-10-23 (additive): `doc.colorProfile` (the embedded RGB profile's name, `"sRGB IEC61966-2.1"`
when untagged) and `doc.convertToProfile(profile, options?)`, Edit > Convert to Profile: `profile`
is `"sRGB"` or an RGB profile file path, options `intent` (the rendering intent tokens of
`RenderingIntent`) and `blackPointCompensation`. Throws for an unusable profile, in Indexed mode, in
CMYK Proof, and for a 32-bit document with a table-based profile. Opening a file in another RGB
profile converts it to sRGB unless the `imports/convertRgbProfilesToSrgb` preference is off
([rgb-profiles.md](rgb-profiles.md)). Permanent identifiers: the property and method names, the option
keys, command id `edit.convert_to_profile` and the settings keys `imports/convertRgbProfilesToSrgb`,
`color/convertProfileDestination`, `color/convertProfileIntent`, `color/convertProfileBlackPoint`.
Pinned by `ui_script_convert_to_profile`.

2026-10-23 (additive, behavioral): Saved metadata ([file-info.md](file-info.md#saved-metadata-preference)).
`app.saveMetadataPreferences` (`{mode, name, copyright, email, website, askBeforeAdding}`) and
`app.setSaveMetadataPreferences({...})`. With mode `"none"`, PSD and PSB saves write no save stamp;
with `"personal"` and `askBeforeAdding: false`, `saveAs` adds the name, copyright and contact to
files that carry metadata and do not name the user (never on `exportAs`). Permanent identifiers:
the property, method and key names, the tokens `none`, `basic`, `personal`, and the settings keys
`saveMetadata/autoAdd`, `saveMetadata/authorName`, `saveMetadata/copyrightNotice`,
`saveMetadata/contactEmail`, `saveMetadata/contactWebsite`, `saveMetadata/askBeforeAdding`. Pinned
by `ui_save_metadata_preferences_page_persists`.

2026-10-23 (additive, behavioral): Preferences > Watermarks ([watermarks.md](watermarks.md#preferences)).
`app.watermarkPreferences` (`{text, addOnSave, checkOnOpen}`) and `app.setWatermarkPreferences({...})`.
With `addOnSave: "always"`, `saveAs` and `exportAs` to PNG, JPEG, WebP, BMP, flat TIFF, JPEG XL and
AVIF write a TrustMark-marked file; `"ask"` never asks a script and adds nothing. Permanent
identifiers: the property, method and key names, the tokens `never`, `ask`, `always`, and settings
keys `watermarks/payloadText`, `watermarks/addOnSave`, `watermarks/checkOnOpen`. Pinned by
`ui_watermark_preferences_page_persists`.

2026-10-23 (additive): TrustMark watermarks ([watermarks.md](watermarks.md)). `doc.scanForWatermarks()`
gains `trustmark: {available, marks: [{variant, schema, text, id, bits, correctedBits, confidence}]}`;
`doc.addInvisibleWatermark({text?, id?, schema?, strength?})` embeds a TrustMark payload in the active
layer and returns `{schema, text, id}`; `app.downloadWatermarkModels()` and
`app.watermarkModelsAvailable`. Permanent identifiers: the method, property and key names, the variant
ids `Q`, `P`, `C`, the schema ids `BCH_SUPER`, `BCH_5`, `BCH_4`, `BCH_3`, and command id
`image.add_invisible_watermark`. Pinned by `ui_script_trustmark_round_trip`.

2026-10-23 (additive): `doc.scanForWatermarks().checks`, one row per kind checked
([watermarks.md](watermarks.md#scan-results)): `{id, name, status, reason, detail}`, plus `match`,
`mark` or `declarations`. The other fields are unchanged. Permanent identifiers: the key names, the
ids `stableDiffusion1`, `stableDiffusion2`, `stableDiffusionXL`, `trustmarkQ`, `trustmarkP`,
`trustmarkC`, `c2paDeclared`, the statuses `found`, `notFound`, `notChecked` and the reasons
`imageTooSmall`, `modelsNotDownloaded`, `notSupported`, `failed`. Pinned by
`ui_script_scan_for_watermarks` and `ui_watermark_scan_report_rows`.

2026-10-23 (additive): `doc.scanForWatermarks()`, File Info's Scan for Watermarks: returns
`{scanned, matches: [{id, name, bits, differingBits, redBlueSwapped}]}` for the open invisible
watermarks of Stable Diffusion 1.x, 2.x and SDXL found in the composite (`scanned` is false under
256 x 256 pixels). `metadataEntries()` rows gain the privacy class `"watermark"` for watermarks
declared in C2PA manifests. Permanent identifiers: the method name, every key, the match ids
`stableDiffusion1`, `stableDiffusion2`, `stableDiffusionXL` and the privacy id `watermark`. Pinned
by `ui_script_scan_for_watermarks`. See [file-info.md](file-info.md#watermarks).

2026-10-23 (additive): `app.displayInfo()` lists every screen (primary first) with its geometry
and HDR capability, read fresh per call: `hdrQueried`, `hdrEnabled`, `headroom`,
`potentialHeadroom`, and on Windows `sdrWhiteNits`, `peakNits`, `fullFrameNits`, `minNits` (null
when unknown), plus `remoteSession` (docs/platform.md, site inventory). Permanent identifiers:
the method name and every key. Pinned by `ui_script_display_info_reports_screens`.
2026-10-22 (additive): `doc.perspectiveCrop(corners, options?)`, the Perspective Crop tool's
commit (GitHub issue 328): four corners as `[x, y]` or `{x, y}` in result order (top left, top
right, bottom right, bottom left) are rectified onto `width` x `height` (default: the corners'
mean edge lengths) on every layer, mask and channel; `resolution` sets the pixels per inch. A
folded quad, bad options or a document with artboards throw. Permanent identifiers: the method
name, the keys `width`, `height`, `resolution`, command id `tools.perspective_crop`, `tools.cycle.crop`
and settings key `tools/perspectiveCropShowGrid`. Pinned by `ui_script_document_perspective_crop`.
2026-10-22 (additive): `doc.trim({basedOn, top, bottom, left, right})`, Image > Trim in one
call (GitHub issue 297): `basedOn` is `"transparent"` (default), `"topLeft"` or `"bottomRight"`,
the sides default to true; returns the kept rect in the old canvas coordinates or null when
nothing was trimmed; throws for a document with artboards or an unknown `basedOn`. Permanent
identifiers: the method name, the option keys and the three `basedOn` ids (also the settings
value of `trim/basedOn`), and command id `image.trim`. Pinned by `ui_script_trim`.

2026-10-21 (behavioral): `layer.drawStrokes` and `layer.strokePath` accept 16 and 32-bit pixel
layers (they refused anything but 8 bits); the strokes run through the canvas brush, which paints
at the layer's depth. The refusal now reads "Strokes require an unlocked pixel layer."
`doc.flatten()` flattens 16 and 32-bit documents at their depth (it threw). `doc.renderPreview`
and the connector's previews render 16 and 32-bit documents (the RGBA8 flatten threw on deep
layers). Pinned by `ui_deep_documents_match_8_bit_after_every_operation`.

2026-10-21 (additive): `patchy.ui.previewExposure` reads and sets the active 32-bit
document's preview exposure in stops (the status bar's Exposure box, docs/high-bit-depth.md
Phase 7), so automation can bring HDR highlights into view before a window capture.
Permanent identifier: the property. Pinned by `ui_script_preview_exposure_drives_the_32_bit_view`.
2026-10-20 (behavioral): `drawStrokes` strokes and `patchy.brushes` settings accept `opacity`
and `flow` of 0 (was 1..100): a 0% stroke paints nothing, the options bar's new minimum for
Opacity and Flow (GitHub issue 253; Photoshop stops both at 1%). Pinned by
`ui_brush_opacity_and_flow_accept_zero_and_paint_nothing`.
2026-10-20 (additive): `patchy.ui.pixelGrid` reads and sets View > Pixel Grid, the saved
application-wide view preference for lines between pixels at 600% zoom and up (GitHub issue
239). Permanent identifiers: the property, command id `view.pixel_grid` and settings key
`view/pixelGrid`. Pinned by `ui_pixel_grid_toggle_persists_and_scripts`.
2026-10-20 (additive): `layer.transform({matrix} | {quad}, interpolation?)`, Edit > Free
Transform in one call through the interactive commit (GitHub issue 225): `matrix` maps the
layer's transform box, `quad` places its corners (layer order TL, TR, BR, BL), so skew,
distort and perspective are scriptable; smart objects keep a perspective placement, type
layers throw for a non-parallelogram quad, a crossed or concave quad throws (pinned by
`ui_script_layer_transform_refuses_folded_quad`), a pending transform throws. `interpolation`
takes the existing resample method ids. Permanent identifiers: the method name and the
keys `matrix`, `quad`, `interpolation`. Pinned by `ui_script_layer_transform_quad_and_matrix`
and `ui_script_layer_transform_refusals`.
2026-10-20 (additive): `layer.colorLabel`, the Layers panel color label (GitHub issue 244):
`"none"`, `"red"`, `"orange"`, `"yellow"`, `"green"`, `"blue"`, `"violet"`, `"gray"` (d.ts
`PatchyLayerColorLabel`). Setting another value throws; an index Patchy does not know reads as
`"none"`. PSD saves write it as Photoshop's `lclr` block. Permanent identifiers: the property
and its eight values. Pinned by `ui_script_layer_color_label`.
