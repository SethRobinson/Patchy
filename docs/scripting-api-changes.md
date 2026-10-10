# Scripting API compatibility

`app.apiVersion` is 1. Bump it only for breaking API changes. Every addition and
behavioral change is recorded here, newest first, in the same change that makes it; the
entries are a permanent record. Entries through 2026-09-11 (the July 2026 API, MCP
attachment, palettes, vector automation, brushes, Slow mode and Pause) are in
[scripting-api-changes-through-2026-09-11.md](scripting-api-changes-through-2026-09-11.md).
Every entry below is API 1.

2026-10-10 (behavioral correction): `patchy.io.writeTextFile(path, text)` writes through
a sibling temporary and a replacing rename (QSaveFile), so a failed write leaves the
previous file intact instead of a truncated one, and a write that cannot be committed
throws `Could not write <path>: <reason>` instead of returning silently. The Script
Manager's Save and Save As take the same path and keep the editor modified on failure.

2026-10-10 (behavioral correction): saving or exporting a 16/32-bit document to an
8-bit-only format (BMP, TGA, DDS, GIF, PCX, ICO/CUR, IFF, RTTEX, JXR, SVG, Aseprite, or any
export transform) writes an 8-bit copy instead of returning false; the document keeps its
depth. Pinned by `ui_script_deep_documents_save_to_8_bit_formats`.

2026-10-09 (additive): `patchy.scripts` (`PatchyScripts` in patchy.d.ts): `userFolder`,
`bundledFolder`, `list()` / `rescan()` (every script with its effective `hotkey`,
`defaultHotkey` and `commandId`), `install(relativePath, source, {hotkey})` (writes below the
user folder, rescans, binds the Preferences override), `setHotkey` / `getHotkey`. The File >
Scripts entries gain the Script Manager's right-click menu. Pinned by
`ui_script_library_installs_and_binds_hotkey` and `ui_scripts_menu_context_menu_offers_script_actions`.

2026-10-09 (additive): `doc.saveAs(path, options?)` and `doc.exportAs(path, options?)`
take the format's save options (`PatchySaveOptions` in patchy.d.ts: JPEG/WebP/JXR/RTTEX
`quality`, WebP/JXR/PDF `lossless`, DDS `compression` and `mipmaps`, ICO/CUR `sizes`,
`resample` and `hotspot`, BMP `encoding`, `paletteMode` and `palettePath`, RTTEX `encoding`,
`powerOfTwo`, `forceSquare`, `forceAlpha` and `compress`, the exportPdf keys, GIF `animate` and
`frameDelayMs`). Unspecified keys follow the user's Save Options defaults; an unknown key, a
wrong type, an out-of-range value, or a key for another extension throws before anything is
written. A scripted save no longer rewrites the user's persisted saveOptions defaults, and
scripted saves skip the saved-channels and Aseprite fill-opacity prompts (the script decided).
Pinned by `ui_script_save_as_options_write_dds_and_jpeg` and `ui_script_save_as_options_are_strict`.

2026-10-09 (behavioral correction): `doc.exportAs` is a copy export. It used to be
identical to `saveAs`, so a flat document exported as .dds became the .dds session; now the
document's path, title and modified state never change, whatever the format. Scripts that
relied on exportAs retargeting the document should call saveAs.

2026-10-09 (additive): per-script hotkeys. A `// @hotkey Ctrl+Alt+D` header line is a
script's default shortcut; Preferences > Hotkeys lists scripts under "Scripts" and its
override wins. The command id is `script.` plus the percent-encoded relative path
(`script.Utilities%2Fquick-export-dds.js`), listed by `app.commandIds()`; `app.runCommand`
refuses those ids because one script runs at a time. New bundled example
`Utilities/quick-export-dds.js`. See [scripting.md](scripting.md) "Script hotkeys".

2026-10-09 (additive): `addTextLayer(text, {area})` creates area text from one
closed `PatchyVectorPath` in document coordinates. `area` and `box` are mutually
exclusive. `layer.textArea` reads a detached boundary snapshot or `null`; assigning
a boundary reflows through the text session, and assigning `null` converts it to
box text. Source shapes remain independent. See [area-text.md](area-text.md).

2026-10-09 (behavioral): 16 and 32-bit editing is on by default (`PATCHY_DEEP_EDITING=0`
turns it off). A 16 or 32-bit file opened with `app.open` keeps its depth (`doc.bitDepth`
reports it; with the gate off it still decodes to 8 bits) and `doc.convertBitDepth` works
without opting in; `app.newDocument` stays 8-bit. `layer.applyPlugin` runs on 16-bit layers
through an 8-bit copy whose change folds back at depth, and still throws for a 32-bit
layer. On a 32-bit document the `layer.blendMode` setter throws for the modes Photoshop
refuses at 32 bits (Color Burn, Linear Burn, Screen, Color Dodge, Overlay, the Light modes,
Hard Mix, Exclusion), and `convertBitDepth(32)` sets layers in those modes to Normal. The
web build throws for `convertBitDepth(32)`. See [high-bit-depth.md](high-bit-depth.md);
pinned by `ui_script_bit_depth_and_deep_filters`.

2026-10-09 (additive): `doc.bitDepth` (8, 16 or 32) and `doc.convertBitDepth(bits)`
(Image > Mode's conversion; throws for other values, while 16 and 32-bit editing is off,
or on an Indexed document). On 16 and 32-bit documents `getPixels` and `setPixels` stay
RGBA8 and convert at the boundary, `fill` and `fillRect` write at the document's depth,
and `applyFilter` runs at depth, throwing for a filter a 32-bit document does not offer.
See [high-bit-depth.md](high-bit-depth.md); pinned by `ui_script_bit_depth_and_deep_filters`.

2026-10-07 (additive plus behavioral correction): `doc.resizeImage(width, height, {method})`
takes a resampling method id: `"automatic"` (default), `"nearest"`, `"bilinear"`, `"bicubic"`,
`"bicubicSmoother"`, `"bicubicSharper"`; an unknown id throws. A resize without a method is
Automatic (Bicubic Sharper for a reduction, Bicubic Smoother for an enlargement) instead of
the old fixed bilinear, 16-bit and float documents are filtered instead of nearest-sampled,
reductions average their footprint, and alpha interpolates premultiplied. The Image Size
dialog now honors its method combo and remembers the choice. See
[resampling.md](resampling.md); pinned by `ui_script_resize_image_method_option` and the
`resample_*` core tests.

2026-10-06 (behavioral): `ui.zoom` reads and writes the view zoom in percent, where
100 is one document pixel per device pixel (the status-box number). On a HiDPI or scaled
display the value therefore differs from the logical widget scale by the device pixel
ratio; at ratio 1 nothing changes. Matches Photoshop's 100% (GitHub issue 75).

2026-10-05 (additive): `layer.rerenderSmartObject()` renders an embedded smart
object again from the file it stores, for every layer sharing that source, and returns
the number of layers re-rendered. A smart object opened from a PSD shows the pixels
saved in the file until it is transformed or its contents change. Throws for a linked
smart object (`updateSmartObject()` is the call for those), a plain layer, a locked one,
or contents that cannot be decoded. Pinned by `ui_script_rerender_smart_object_from_embedded_file`.

2026-10-05 (additive): `layer.rerenderText()` renders a text layer again from its
stored text, fonts and formatting, changing none of them. A type layer opened from a PSD
shows the pixels saved in the file until it is edited; this replaces them with Patchy's
own render (what Testy uses to score Patchy's text engine instead of Photoshop's cached
pixels). Throws on a layer that is not text or is locked. Pinned by
`ui_script_rerender_text_replaces_stored_pixels`.

2026-10-03 (additive): `doc.mergeLayers(layers, {singleVector: true,
effectsFrom?: layer})` explicitly combines selected vectors at the bottommost
source's stack position. It removes individual layer effects unless `effectsFrom`
selects one source stack to apply to the combined silhouette. Fills, vector strokes
and curves remain editable. Protected or incompatible inputs throw before mutation.
See [layer-merging.md](layer-merging.md); pinned by `ui_layer_merge_single_vector_*`.

2026-10-02 (behavioral correction): text font warnings recognize compact family
spellings such as `LiberationSans` as the same font as `Liberation Sans`. Creating or
editing text with that spelling no longer reports a missing font when the requested
face renders it. Pinned by `ui_text_name_table_names_resolve_to_the_registered_face`.

2026-10-02 (additive): `doc.exportAnimatedWebp(path, options?)` writes visible
top-level layers as animation, with millisecond timing, finite or infinite play counts,
quality and lossless options. It preserves the source document path and dirty state.
Defaults and validation are in `scripts/bundled/patchy.d.ts`; ordinary `saveAs` and
`exportAs` WebP output stays flat. MCP uses the same method via `execute_script`.

2026-10-01 (behavioral correction): `doc.resizeImage` re-renders every editable embedded
and every resolvable linked smart object from its source (vector files at the new scale),
matching Image > Image Size; before, the script and MCP resize kept the resampled
previews, and linked placements stayed resampled in every path. A linked file that is
missing or cannot be decoded keeps the resampled preview without throwing, and
`getSmartObject().missing` still reports it. Pinned by
`ui_script_smart_object_image_size_rerenders_linked_and_embedded`,
`ui_script_smart_object_linked_raster_rerenders_from_full_resolution` and
`ui_script_smart_object_missing_linked_file_keeps_preview_on_image_size`.

2026-10-01 (behavioral correction): text font warnings name their cause, and the text
setters log them too. A font that is installed but has no glyph for any character of the text
(the bundled Noto Naskh Arabic asked for Latin text) now logs `addTextLayer: font has no
glyphs for this text, rendered with a fallback: <family>`. It used to log `font not
available`, which still appears for a font that is not installed. A layer with both problems
logs both lines. `layer.text`, `setTextRuns`, `textAlign`, `textParagraph`, `textOrientation`
and `textDirection` used to say nothing; they now log the same two lines under their own name
(`layer.text: font not available, ...`). That includes the case where the edit replaced a
missing font with a substitute, so `textFont` no longer names the font the layer was created
with. Runs that give every character an installed font log nothing. These are console lines,
never dialogs. Pinned by `ui_script_text_font_without_glyphs_warns_with_the_real_cause` and
`ui_script_text_setters_warn_about_fonts`.

2026-10-01 (behavioral correction): `layer.moveTo` and the `x` / `y` setters move a
layer the way the Move tool does. They used to shift only the pixel bounds and the mask
bounds, leaving the placement data behind: a shape kept its old path and geometry and
snapped back at its next re-render, a smart object kept its old quad, and a text layer kept
its old transform, so Photoshop laid it out again at the creation anchor on the first edit.
Now the shape model, smart-object quads, text transform, preserved vector-mask data and a
linked raster or vector mask travel with the layer and with every layer inside a moved
group, and a smart object with Smart Filters re-renders at the new place. One visible
difference: an unlinked mask now stays where it is (it used to move with the layer).
Pinned by `ui_script_move_*`; the Photoshop check is
`scripts\dev\photoshop-text-move-check.ps1`. See [scripting.md](scripting.md).

2026-09-30 (additive): smart objects. `doc.addSmartObject(path, {linked?, x?, y?, width?,
height?, scale?, name?})` places a file as an embedded or linked smart-object layer
(the core behind File > Place Embedded and the new File > Place Linked); a linked
placement of a file the document already links shares that source. Layers expose
`isSmartObject`, `getSmartObject()` (`{linked, fileName, path, relativePath, missing,
changed, sourceId, width, height, resolution, quad}` or null) and `updateSmartObject()`
(Update Smart Object Content for every layer sharing the source; returns the count).
See [smart-object-editing.md](smart-object-editing.md).

2026-09-28 (additive): `patchy.plugins.folder` (the plug-ins folder next to the
application, created with its README on read; "" off Windows) and the `captureDialog`
option of `layer.applyPlugin` (a PNG of the plug-in's own dialog while it is up). See
[plugins.md](plugins.md).

2026-09-28 (additive): legacy Photoshop plug-ins. `patchy.plugins` (`list()`, `rescan()`,
`folders` get/set) exposes the `.8bf` filters found in the plug-in folders, and
`layer.applyPlugin(id, {dialog?})` runs one on a pixel layer inside the selection as one
undoable edit (`{dialog: false}` skips the plug-in's own dialog; unattended runs never show
it). Windows only; elsewhere every entry lists as unsupported and `applyPlugin` throws.
See [plugins.md](plugins.md).

2026-09-27 (additive): paragraph metrics. Text layers expose `textParagraph` (read/write:
`{firstLineIndent, startIndent, endIndent, spaceBefore, spaceAfter}` in document pixels;
reading gives the first paragraph, setting merges the given fields into every paragraph), and
`doc.addTextLayer` takes the same object as its `paragraph` option. See
[text-tool.md](text-tool.md).

2026-09-26 (additive): rich text. `doc.addTextLayer` accepts an array of runs (`{text, font?,
size?, bold?, italic?, color?}`) in place of the string, so one layer mixes faces, sizes and
colors; options gain `box` (`{width, height}`: a wrapping paragraph text box with x/y as its
top-left corner) and `align`. Text layers expose `textRuns` (the stored runs), `textBox`
(`{width, height}` or null), `textAlign` (read/write) and `setTextRuns(runs)`, which retypes
the layer with formatted runs on top of the first character's formatting. See
[text-tool.md](text-tool.md).

2026-09-26 (behavioral fixes): `doc.addTextLayer` renders exactly the face its options name.
The session seeded its face from the options bar's style picker, so with the bar parked on a
Semibold or Black layer every scripted layer in a family offering that face took it,
whatever `font`, `bold` and `italic` said. The requested `size` is now committed exactly
at every canvas zoom, and an unchanged `layer.text` re-edit keeps the size (the whole-pixel
editor font divided by a low zoom used to shift it by a pixel or two). On Windows `font`
also accepts a face's full name or PostScript name ("Futura Extra Black BT",
"FuturaBT-ExtraBlack") for the face the database lists as family + style. See
[text-tool.md](text-tool.md).

2026-09-26 (additive): `app.listFonts()` returns every family the text engine can use as
`{family, styles, writingSystems}` objects sorted by family, loading the installed
fonts first under `--headless` on Windows.

2026-09-26 (behavioral fix plus additive): `doc.addTextLayer`'s `font` option now takes
effect. The script path set the family on the editor's character format only, while the
commit read the session's family, so every script-made text layer rendered in the options
bar's current font. A family that is not installed now logs a console warning naming it,
and text layers expose a read-only `layer.textFont` (the stored family name, `""` for
other layers). See [text-tool.md](text-tool.md).

2026-09-25 (additive): `patchy.recovery` exposes the automatic document recovery store:
`enabled` and `intervalMinutes` (the Preferences values), `directory`, `writeNow()`,
`listFiles()`, `listOrphaned()`, `recoverAll()`, and `discardOrphaned()`. See
[document-recovery.md](document-recovery.md).

2026-09-25 (additive): `doc.importFilesAsLayers(paths)` adds image files as layers directly
above the active layer, bottom to top in argument order (the core behind File > Import >
Files as Layers, the Layers-panel file drop, and Paste with copied files). A multi-layer
file becomes a folder named after it; an unreadable file throws without adding anything.
See [import.md](import.md).

2026-09-25 (behavioral fix): the `layer.text` setter replaces the text the way retyping it
in the editor does, so the new text keeps the first character's run formatting (exact
fractional size, Character-panel glyph scales, leading, tracking, faux styles). It used to
delete the text first and re-insert at the session's fallback font, so an imported
Photoshop layer with a 0.93 vertical glyph scale re-rendered 7.5% taller than the same
layer applied interactively. See [text-tool.md](text-tool.md).

2026-09-24 (additive): `layer.removeObject(options?)` gains `toneMatch` (0..100, default
0, the raw exemplar fill), `feather` (px, default 0; softens the fill's edge
outward), and, for the content-aware method, `attempt` as the variation number (0 = the
best-match fill, each n > 0 a different reproducible fill, the dialog's Reroll). The
result gains `attempt`. Existing calls are unchanged. See [healing.md](healing.md).

2026-09-22 (additive plus behavioral): `app.exportPdf` options gain `imageQuality`
(`"lossless"`, `"high"`, `"medium"`, `"low"`; an unknown id throws), which wins over
`lossless`. Image pages now go through Patchy's own PDF writer: `lossless: false` means
JPEG quality 90 (it was Qt's fixed 94), and gray pages are written as one channel in every
mode. The default stays lossless. `keepOriginalImageData` (default true) writes a page that
was imported from a PDF as one image, and has not visibly changed since, with that image's
original bytes. Pinned by `ui_script_export_pdf_writes_pages`.

2026-09-22 (additive): `doc.alignLayers(edge, options?)` and `doc.distributeLayers(mode,
options?)` run Layer > Arrange > Align / Distribute (`edge` ids `left`, `hcenter`, `right`,
`top`, `vcenter`, `bottom`; Distribute adds `hspacing`, `vspacing`; options `layers` and,
for Align, `alignTo: "selection" | "canvas"`). Both return the number of layers moved and
ride the run's single undo entry. See [alignment.md](alignment.md).

2026-09-22 (additive): `layer.removeObject(options?)` runs Edit > Remove Object on the
document selection. `{method}` is `"contentAware"` (default, the exhaustive exemplar
fill) or `"nearestEdge"` (the selection form of Spot Healing, where `{attempt}` picks
the source candidate); the call returns `{method, patches, source, sourceCount}`. The
layer must be the active layer. See [healing.md](healing.md).

2026-09-21 (additive): `PatchyShapeState.feather` (px) and `.density` (0..100), plus
`layer.updateShape({feather, density})`: Photoshop's vector-mask Feather / Density on a
shape layer's own path, the same convention as `setVectorMask`. Pinned by
`ui_script_shape_feather_and_density`.

2026-09-21 (additive): `app.exportPdf(documents, path, options?)` writes a multi-page PDF
with one page per document (`lossless`, `editableLayers`, `missingFontsAsImages`
options), the same writer as File > Export Multi-Page PDF. See [pdf.md](pdf.md).

2026-09-20 (additive): vertical type and paragraph direction. `doc.addTextLayer` takes
`orientation` (`"horizontal"` | `"vertical"`) and `direction` (`"auto"` | `"ltr"` |
`"rtl"`); text layers expose `textOrientation` and `textDirection` (read/write, a write
re-renders through the same hidden session as `text`). Invalid values throw. See
[text-tool.md](text-tool.md).
