# Scripting API changes: archive 2026-10-22 to 2026-10-23

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer entries
live there, older ones in
[scripting-api-changes-2026-10-20-to-21.md](scripting-api-changes-2026-10-20-to-21.md).
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
