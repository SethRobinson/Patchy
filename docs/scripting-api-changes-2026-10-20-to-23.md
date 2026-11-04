# Scripting API changes: archive 2026-10-20 to 2026-10-23

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer entries
live there, older ones in
[scripting-api-changes-2026-10-01-to-19.md](scripting-api-changes-2026-10-01-to-19.md).
`app.apiVersion` stayed 1 throughout. Newest first.

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

2026-10-23 (additive, behavioral): Saved metadata ([file-info-save-stamp.md](file-info-save-stamp.md#saved-metadata-preference)).
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
by `ui_script_scan_for_watermarks`. See [file-info-watermarks.md](file-info-watermarks.md#watermarks).

2026-10-23 (additive): `app.displayInfo()` lists every screen (primary first) with its geometry
and HDR capability, read fresh per call: `hdrQueried`, `hdrEnabled`, `headroom`,
`potentialHeadroom`, and on Windows `sdrWhiteNits`, `peakNits`, `fullFrameNits`, `minNits` (null
when unknown), plus `remoteSession` (docs/platform-sites.md, site inventory). Permanent identifiers:
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
