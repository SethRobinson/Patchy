# Scripting API changes: 2026-10-01 to 2026-10-05

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer
entries live there, older ones in
[scripting-api-changes-through-2026-09-30.md](scripting-api-changes-through-2026-09-30.md).
`app.apiVersion` stayed 1 throughout. Newest first.

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
