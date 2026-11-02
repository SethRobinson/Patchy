# Scripting API changes: archive 2026-10-20 to 2026-10-21

Entries of the permanent scripting API change log, moved here unchanged when
[scripting-api-changes.md](scripting-api-changes.md) reached its size limit. Newer entries
live there, older ones in
[scripting-api-changes-2026-10-10-to-19.md](scripting-api-changes-2026-10-10-to-19.md).
`app.apiVersion` stayed 1 throughout. Newest first.

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
