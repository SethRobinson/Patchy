# Scripting API changes: archive 2026-10-10 to 2026-10-19

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer entries
live there, older ones in
[scripting-api-changes-2026-10-06-to-09.md](scripting-api-changes-2026-10-06-to-09.md).
`app.apiVersion` stayed 1 throughout. Newest first.

2026-10-19 (additive plus behavioral): `.tif`/`.tiff` saves take `layers` (layered TIFF,
docs/tiff.md). `doc.saveAs` of a document with layers a flat file would lose now writes a
layered TIFF by default (the same composite plus Photoshop's layer data in tag 37724) instead
of a flattened copy; `layers: false` keeps the old flat file, and `exportAs`, `colorMode:
"cmyk"` and `--export` stay flat. `layers: true` with `colorMode: "cmyk"` throws. Opening a
TIFF with Photoshop layers (`app.open`) gives a layered document. Permanent identifier: the
key. Pinned by `ui_script_save_as_tiff_layers_option`.
2026-10-19 (additive): Brush Mode. `drawStrokes` strokes and `patchy.brushes` settings take
`mode` (Brush only, default `"normal"`): every blend mode id except `"pass-through"`, plus
`"behind"` and `"clear"`; Eraser and Mixer strokes reject any other value. `getCurrent`,
`resolve` and saved presets report it for the Brush. Permanent identifiers: the key, the two
new tokens, and the setting `tools/brushMode`. Pinned by
`ui_brush_mode_combo_lists_paint_modes_and_persists` and `ui_script_brush_mode_*`.
2026-10-19 (additive): right-column panels. `patchy.ui.expandPanel(name)` (the Window menu
entry's show: reopen, front tab, expand), `collapsePanel(name)`, `closePanel(name)` and
`panelState(name)` (`{open, front, expanded}`); unknown names throw. Permanent identifiers:
the four names and the panel names `layers`, `channels`, `paths`, `history`, `properties`,
`info`, `palette` (d.ts `PatchyPanelName`). Pinned by `ui_script_ui_side_panels_*` and
`ui_script_ui_expand_panel_in_cli_capture`.
2026-10-18 (behavioral): `patchy.ui.zoom` clamps to 0.01..12800 (was 5..12800), Photoshop's
range, as the view now zooms out to 0.01%. Pinned by `ui_script_ui_view_zoom`.
2026-10-18 (behavioral, Photoshop parity): `selection.selectAll()` selects the active
layer's artboard when that layer is on one (the whole canvas otherwise), and pixel paste
lands on the target artboard, centered on it (docs/artboards.md, "Feature interactions").
Pinned by `ui_artboard_feature_interactions_match_photoshop`.
2026-10-18 (additive plus behavioral): `.pdf` save options and `app.exportPdf` take `pages`
(`"artboards"`, the default, `"groups"`, `"canvas"`); `artboardPages` stays as its true/false
spelling. Exporting by groups now includes hidden top-level groups and crops a PDF import's
page folders to their own page size. Permanent identifiers: the key and its tokens. Pinned
by `ui_pdf_import_page_folders_export_back_by_groups`.
2026-10-18 (additive): artboards (docs/artboards.md). Documents gain `artboards` (top of
the stack first), `addArtboard(name, rect, {background, color})`, `artboardFromLayers(layers,
name?)`, `fitCanvasToArtboards()` (returns the `{x, y}` coordinate shift) and
`exportArtboards(folder, {format, prefix, artboards, overwrite})` (returns the written paths).
Layers gain `isArtboard`, `artboard` (`{x, y, width, height, background, color}` or null) and
`setArtboard(options | null)`. `app.exportPdf` and `.pdf` save options gain `artboardPages`
(default true: one page per artboard). The MCP document state adds `isArtboard` and
`artboard` (the rect) per layer. Permanent identifiers: every name above and the background
tokens `white`, `black`, `transparent`, `color`. Pinned by `ui_script_artboards_*`.
2026-10-18 (behavioral): on an artboard, `layer.bounds`, `x`, `y` and `moveTo` use the
artboard's rect, and a move carries the rect with the content; the canvas then grows to
hold every artboard. `doc.groupLayers` and `doc.moveLayers` refuse to put an artboard into
a group. Pinned by `ui_script_artboards_*`.
2026-10-16 (additive): AVIF. `app.open`, `doc.saveAs` and `doc.exportAs` handle `.avif`
(docs/avif.md). `PatchySaveOptions` gains `speed` (0 to 10) for `.avif`, which also takes
`quality` (0 to 100), `lossless` and `metadata`. Permanent identifiers: the `speed` key and
the `saveOptions/avifQuality`, `avifLossless`, `avifSpeed` settings. Pinned by
`ui_script_save_as_options_write_avif`.
2026-10-15 (behavioral): `app.open` of a CMYK JPEG or TIFF converts through the file's CMYK
profile (else the working one) instead of a profile-free conversion, and the document opens
with `doc.cmykProof` true when the working space has a profile. Pinned by
`ui_cmyk_files_open_in_cmyk_proof`.
2026-10-15 (additive): `doc.cmykProof` reads and sets Image > Mode > CMYK Proof
(docs/cmyk.md); setting true throws in palette mode or without a CMYK profile. Without a
`colorMode` option, `doc.saveAs`/`doc.exportAs` of a CMYK Proof document write `.jpg`, `.tif`
and flattened `.pdf` in CMYK. Command id `image.mode_cmyk_proof`. Permanent identifier: the
property. Pinned by `ui_cmyk_proof_mode_saves_cmyk`.
2026-10-15 (additive): JPEG XL. `app.open`, `doc.saveAs` and `doc.exportAs` handle `.jxl`
(docs/jxl.md). `PatchySaveOptions` gains `effort` (1 to 9) and `loopCount` (0 to 65535) for
`.jxl`, and `.jxl` also takes `quality` (1 to 100), `lossless`, `animate`, `frameDelayMs` and
`metadata`. Pinned by `ui_script_save_as_options_write_jxl`.
2026-10-15 (additive): `blackInkOnly: true` in `doc.saveAs`/`doc.exportAs` options, with
`colorMode: "cmyk"` only (throws otherwise), separates black, grays and black-ink colors to the
K plate alone. Permanent identifier: the key. Pinned by
`ui_script_export_cmyk_writes_jpeg_tiff_and_pdf`.
2026-10-14 (additive): CMYK export (docs/cmyk.md). `doc.saveAs`/`doc.exportAs` options take
`colorMode: "rgb" | "cmyk"` for `.jpg`, `.tif`/`.tiff` and flattened `.pdf`; `"cmyk"` separates
through the working CMYK profile and embeds it, and throws when the user chose no profile. `.tif`
now accepts this one key. Permanent identifiers: the key and both tokens. Pinned by
`ui_script_export_cmyk_writes_jpeg_tiff_and_pdf`.
2026-10-14 (additive): soft proof and gamut warning (docs/cmyk.md). `patchy.ui.proofColors`
and `patchy.ui.gamutWarning` read and set View > Proof Colors and View > Gamut Warning
(application-wide canvas views, never saved, off without a CMYK profile);
`app.colorInCmykGamut(color)` is the gamut test for one color. Command ids
`view.proof_colors` and `view.gamut_warning`. Pinned by `ui_proof_colors_and_gamut_warning_change_only_the_view`.
2026-10-14 (additive): the working CMYK space (docs/cmyk.md). `app.cmykProfile` reads
`{name, hasProfile, intent, blackPointCompensation}`; `app.colorToCmyk(color)` returns
`{c, m, y, k}` ink percentages and `app.cmykToColor(c, m, y, k)` the `"#rrggbb"` color, both
through the profile chosen in Preferences (an approximate formula when `hasProfile` is false).
Permanent identifiers: the intent tokens `perceptual`, `relative-colorimetric`, `saturation`
and `absolute-colorimetric`. Pinned by `ui_script_cmyk_conversions_use_working_space`.
2026-10-14 (behavioral, supersedes the entry below): `layer.applyGmic` changes every layer
`inputLayers` chooses that it may change, in one undo step; locked, text, shape and Smart
Object layers among them are only read. A filter made for one image (declared input
`active`) runs on each chosen layer alone at its own size, as if applied to each in turn, and
its further results become new layers below that layer in G'MIC's top-first order (above
it, in the same order, when it is named Background), where they used to go above and cover
it (Ball's sphere hidden under the original); it no longer sees the layers padded
onto a document-sized image, which turned a small layer canvas-sized for filters that fill
transparency. A filter made for several layers still gets them lined up on the document and,
when it combines them, replaces the layer it was called on. With `hidden` on a visible layer,
the hidden layers now change in place instead of arriving as new layers. It throws when none
of the chosen layers can be changed. Filters now receive each layer's name the way G'MIC
expects it, so a new layer is named after its source ("Layer 1 [green]", not "[unnamed]
[green]"). Pinned by `ui_gmic_filter_changes_every_chosen_layer` (which replaces the test
named below) and `ui_gmic_filter_adds_output_layers_in_one_undo_step`.
2026-10-14 (behavioral): `layer.applyGmic` with several input layers. A filter that returns
an image per layer it read now gives the layer its own result and drops the results for the
layers that were only read (they used to arrive as duplicate layers, and the layer took the
top layer's result). When the layer is not among the input (`hidden` on a visible layer) it
keeps its pixels and every result is a new layer. `visible` and `hidden` follow enclosing
folders. It now throws on text and shape layers, locked pixels, Quick Mask, an `inputLayers`
value that matches no layer, and a run that would not fit in memory. A filter that runs
longer than the inactivity limit is no longer stopped by it, and stopping the script now
stops the filter. Pinned at the time by
`ui_gmic_filter_changes_only_the_active_layer_whatever_it_reads` (since replaced, see 2026-10-15).
2026-10-13 (additive): G'MIC filters. `patchy.gmic.available` and `patchy.gmic.version`
describe the engine (absent from the web build and from builds with `PATCHY_ENABLE_GMIC` off), and
`patchy.gmic.list()` returns every offered filter as `{id, name, folder, inputLayers,
parameters}` with each parameter's `{name, type, default, minimum, maximum, choices}`.
`layer.applyGmic(filterId, values, {inputLayers})` runs one inside the selection as part of
the run's undo entry and returns the number of layers the filter added; `values` is an array
in list() order or an object keyed by parameter name. Permanent identifiers: the filter ids,
the parameter type tokens (`float`, `int`, `bool`, `choice`, `color`, `point`, `text`, `file`,
`folder`, `button`, `value`) and the `inputLayers` tokens (`active`, `activeAndBelow`,
`activeAndAbove`, `all`, `visible`, `hidden`). Pinned by `ui_gmic_script_api_lists_and_applies`.
2026-10-12 (behavioral): `layer.applyFilter` honors the selection like the Filter menu: only
selected pixels change and the layer keeps its bounds (it used to filter the whole layer).
A soft selection (a feather, an Object Selection edge) now blends `applyFilter` and
`layer.fill` by its coverage instead of applying at full strength wherever coverage is
nonzero. Pinned by `ui_destructive_edits_blend_by_soft_selection_coverage`.

2026-10-12 (additive, behavioral): Object Selection's High Quality model (EfficientViT-SAM
XL0, a one-time 234 MB desktop download). `doc.selection.objectSelectionModel` reads
`"standard"` or `"highQuality"` (permanent tokens): the model `objectSelect` runs, which is
High Quality once downloaded unless the tool's Model setting says Standard.
`app.downloadObjectSelectionModel()` downloads it when missing (status-bar progress, the
call waits), returns true once it is present, and throws on failure or in the web build.
`objectSelect({rect})` now encodes the box's surroundings at full resolution (zoom-to-box)
when the box is small against the image, like the tool; results change slightly, prompts and
options do not. Pinned by `ai_object_selection_zoomed_box_finds_the_disc`,
`ui_object_select_model_option_and_zoomed_box` and
`ui_select_subject_is_left_out_without_the_subject_model` (the model token).

2026-10-12 (behavioral): the web build has Object Selection. `automaticSelectionAvailable`
is true there and `objectSelect` works; the first call downloads the models (about 19 MB, then
cached by the browser), and the call waits in an event loop meanwhile, so it throws "The
document changed during automatic selection." if the document closed or changed size during
the wait. `subjectSelectionAvailable` stays false on the web. No automated pin (the UI suite
does not run on wasm); verified in Chrome, see [ai-selection.md](ai-selection.md).

2026-10-12 (additive, behavioral): `doc.selection.subjectSelectionAvailable`. Builds with
CMake's `PATCHY_ENABLE_SELECT_SUBJECT` off (the default; the subject model is held back pending
patent review) report it false, and `selectSubject`, `selectBackground` and
`layer.removeBackground` throw "Select Subject is not included in this build of Patchy";
`objectSelect` and `automaticSelectionAvailable` are unchanged. Pinned by
`ui_select_subject_is_left_out_without_the_subject_model`.

2026-10-11 (additive): File Info. `doc.fileInfo` (Description and IPTC fields), `doc.xmp`,
`doc.setFileInfo(fields)`, `doc.metadataEntries()`, `doc.stripMetadata({keepColorProfile})`,
`doc.removeLocation()`, `doc.removeEditHistory()`, and the `metadata` save option (`"all"`,
`"copyright"`, `"none"`) for `.jpg`, `.png`, `.tif`, `.webp` and `.gif` in `saveAs` and
`exportAs`. Saves now write the document's metadata. See [file-info.md](file-info.md).
Pinned by `ui_script_file_info_reads_edits_and_strips`.

2026-10-11 (additive): `sampleAllLayers` (default `true`) on `doc.selection.selectSubject`,
`selectBackground` and `layer.removeBackground(options?)`; `false` analyzes the active layer
rendered alone (any layer kind). `objectSelect({sampleAllLayers: false})` now accepts shape,
text and other non-pixel layers the same way instead of requiring a pixel layer. Pinned by
`ui_select_subject_follows_sample_all_layers`.

2026-10-11 (additive): `doc.selection.selectBackground({operation, feather, antialias})`
(Select > Select Background, the inverse of `selectSubject`) and `layer.removeBackground()`
(Layer > Layer Mask > Remove Background: the subject matte as the layer's mask, multiplied into
an existing mask). Both throw when the build has no automatic selection. Pinned by
`ui_script_select_background_and_remove_background`.

2026-10-11 (additive): automatic selection on `doc.selection`:
`selectSubject({operation, feather, antialias})` (Select > Subject),
`objectSelect({rect} | {point}, operation, sampleAllLayers, feather, antialias)` (the Object
Selection tool; exactly one of `rect` or `point`; `operation` has no `"intersect"`), and
`automaticSelectionAvailable`. Both calls throw when the build has no automatic selection.
See [ai-selection.md](ai-selection.md). Pinned by `ui_script_automatic_selection_selects_objects`.

2026-10-10 (behavioral correction): `patchy.io.writeTextFile(path, text)` writes through
a sibling temporary and a replacing rename (QSaveFile), so a failed write leaves the
previous file intact instead of a truncated one, and a write that cannot be committed
throws `Could not write <path>: <reason>` instead of returning silently. The Script
Manager's Save and Save As take the same path and keep the editor modified on failure.

2026-10-10 (behavioral correction): saving or exporting a 16/32-bit document to an
8-bit-only format (BMP, TGA, DDS, GIF, PCX, ICO/CUR, IFF, RTTEX, JXR, SVG, Aseprite, or any
export transform) writes an 8-bit copy instead of returning false; the document keeps its
depth. Pinned by `ui_script_deep_documents_save_to_8_bit_formats`.
