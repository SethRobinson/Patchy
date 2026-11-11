# Patchy-authored text geometry

Split from [text-render-calibration.md](text-render-calibration.md): the layout metrics Patchy records for its own (Qt-natural) type layers and the TySh geometry the PSD writer derives from them, so Photoshop's re-layout lands on Patchy's pixels.

## Patchy text re-renders where Patchy drew it

Patchy-authored (Qt-natural) text keeps its own layout, so the TySh has to tell Photoshop's
engine where Qt put the lines. PS 27.9 COM probes on `door_test.psd` (box text, Arial 268 px,
re-laid out through `textItem.contents`, ink bounds from the DOM) established the rules:

- **First baseline of box text = box top + OS/2 cap height x size**, measured on the same block
  with the font swapped to Arial (0.716 em), Times New Roman (0.662), Georgia (0.693) and
  Verdana (0.727); the "d" Ascent Photoshop gives its own blocks (Georgia 0.756) is 17 px off
  at 268 px. Qt's raster has it at winAscent (Arial 0.905, 47 px lower).
- **A non-zero `/BoxBounds` top moves the text by TWICE its value** (+47.5 -> +95 px; the
  frame bottom is irrelevant), so the frame is never moved that way.
- **The descriptor `bounds` top has no effect on layout** (-47.5 and +2.5 both render like 0),
  and `/StyleRunAlignment 2` (present in PS-authored style sheets, absent in Patchy's) changes
  nothing.
- Qt's line pitch is `QTextLine::height()` (ascent + descent + line gap, ~1.15 em for Arial)
  while Photoshop's auto leading is 1.2 x size, and the old point-text anchor was the ink
  BOTTOM.

The renderer records what it drew (`text_layout_metrics_for_plan`, stored by
`store_text_layout_metrics` at every site that gives a layer a text render, editor commits and
transform re-renders included) and the Qt-free writer turns it into geometry:

- `patchy.text.first_baseline`: the first line's baseline in plan units, below the text-local
  origin for point text (the transform origin, which is the raster's top row unless glyph ink
  overshoots the first line top, "Glyph ink outside the advance box" in
  [text-render-calibration-pixel-grid.md](text-render-calibration-pixel-grid.md)) and below the
  raster's top row for box text. `point_text_baseline_offset` anchors ty there; the ink-bottom
  scan stays the fallback for rasters no Patchy render measured. `patchy.text.raster_top`
  (point text: `local_rect.top()`) tells the writer whether ty is still that origin (the raster
  sits raster_top from it: anchor) or already the baseline (a reopened layer: leave it), never
  "ink starts above ty" (`psd_writer_point_text_grown_raster_still_anchors_baseline`).
- `patchy.text.anchor_x` (horizontal point text whose first line is centered or right-aligned):
  that line's justification point from the text-local origin (its middle or its end; logical
  alignment, so a right-to-left "left" is the end). Photoshop's anchor (the TySh translation, its
  click point) is the first baseline AT that point, while Patchy's origin is the line box's left
  edge, so the writer moves tx there in the same `translate_text_geometry_local` call as the
  baseline, through a rotated or scaled transform too. Without it a centered or right-aligned
  layer was saved with its anchor at its left edge and Photoshop's next layout jumped the text
  left by half or all of its width (Testy's text-save leg, November 2026). Left-aligned text
  records nothing, so its blocks keep their bytes. Warped point text gets the same column in
  its own branch (the translation by `warp->baseline`), with the warp box moved along.
- `patchy.text.file_anchor` ("x y", point text from a file): the anchor the FILE gave the layer,
  as a point of the text-local space the committed transform maps. `commit_text_editor` records
  it (`point_text_file_anchor_in_document` before the commit, `store_point_text_file_anchor`
  after) when the layer's transform was still the file's, or the layer already carried one, and
  the first paragraph's alignment did not change; every other render erases it
  (`store_text_layout_metrics`), and a move or free transform takes it along because it is
  text-local. The writer puts the anchor back on it exactly. The renderer's own measure
  (anchor_x, first_baseline) is only good to about a pixel, since a commit pins the new ink to
  the old ink on whole pixels, and Photoshop rounds the anchor before it draws: the psd-tools
  `clip-adjustment.psd` "a" (anchor 8.763, 21.192) went out as 21.859 and came back a pixel
  lower. The measured first baseline replaces the file's row only when they disagree by more
  than the pin's slack (2 px on the page; 1.5 text-local px or 6% of the size through the
  transform's scale if that is more), which means an edit really moved the
  line. A file Patchy saved before this fix has a centered or right-aligned anchor at the left
  edge; such a layer is recognized by its stored ink box (it starts at the anchor instead of
  straddling or ending at it) and gets the measured anchor instead.
- `patchy.text.file_box` ("w h", box text from a Photoshop file): the size of the box the file
  gave the layer. Photoshop's boxes are fractional (121.845 x 120.046) and Patchy's are whole
  pixels, a session sometimes a pixel taller. A commit records the file's size
  (`box_text_file_size`: the PSD `/BoxBounds`, or the key a previous commit left) while the
  committed box is within a pixel of it, and the writer puts it into the TySh `bounds`,
  `/BoxBounds` and the Txt2 frame contour. With whole pixels Photoshop wrapped the same lines
  but aligned a gradient stroke to a box one pixel taller (psd-tools `effect-stroke-gradient.psd`).
  Photoshop-layout layers only; a box the user resized is written as it is
  (`text_engine_box_keeps_the_files_fractional_size`).
- `patchy.text.box_baseline_inset` (Qt-natural box text only): Qt's first baseline minus
  (space before + the line's max `QFontMetricsF::capHeight`), rounded to 1/64 px (QFixed's grid,
  exact in binary). The writer moves the TRANSFORM ORIGIN down by it (`translate_text_geometry_local`),
  keeps `/BoxBounds [0 0 w h]` at the origin, and so writes the descriptor `bounds` with top =
  -inset; the raster-derived `boundingBox` and the document-space tail stay put on the page. The
  reader (`psd_layer_records.cpp`, Patchy-signed box blocks whose `bounds` top is negative)
  moves the origin back up by -top and shifts every rect along, storing the inset in the same
  key, so the frame Patchy edits reopens where it was, the GDI regeneration draws at the box top,
  and an unedited re-save writes the identical block. Photoshop-layout layers never carry it:
  their blocks stay byte-stable (their first-baseline model is Ascent, `photoshop_ascender_fraction`).
- `patchy.text.auto_leading` (Qt-natural only): the baseline advance (line 2 minus line 1 of
  the first paragraph, else the first line's height) over `dominant_text_run_size`, written as the
  paragraph `/AutoLeading`, written with at most six decimals (`engine_short_fraction`):
  Photoshop 27.9 rasterized a layer that said `/AutoLeading 1.11940298507` ("an error prevented
  them from being read") while `1.119402985` and `1.1194` were fine, the same token-length
  intolerance as the negative float `/Tracking`
  ([text-render-calibration.md](text-render-calibration.md#tracking-and-alignment)). Style runs keep `/AutoLeading true`; a fixed
  `/Leading` would read back as Photoshop provenance (`serialized_runs_have_photoshop_leading_signals`)
  and flip the layer into the Photoshop layout model on reopen. The fraction round-trips through
  the paragraph v3 column, which the model check ignores. One fraction per layer: mixed families
  on one line can still differ by a few px from line 2 on.

**The Photoshop layout mark.** A layer in the Photoshop layout model (`patchy.text.layout`
`photoshop`, every Photoshop import) that Patchy saves gets `/StyleRunAlignment 2` in its TySh
normal style sheet: Photoshop's own sheets carry it (its default, no effect on layout), Patchy's
never did, so on reopen a Patchy-signed block with it keeps the Photoshop model
(`text_patchy_photoshop_layout`). Without it a re-saved import with auto leading reopened Qt-natural
and the next edit drew Qt's line pitch (Amiri Quran at 14 px: 35 px instead of 16.8; GitHub issue
683, `psd_issue_683_photoshop_layout_mark_round_trips`,
`ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit`). Files saved before the mark
reopen Qt-natural as before.

**No advance calibration for Qt-natural text.** The edit session band-matches a fresh render
against the layer's raster only in the Photoshop layout model (`calibrated_box_text_metric_scale`,
the Photoshop-raster wrap fix). A Qt-natural raster is Patchy's own render, and when its box
overflows it holds fewer lines than the whole story, which the search read as a too-wide font and
squeezed the glyphs further on every re-edit (issue 683,
`ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs`).

`store_patchy_text_metadata` erases all seven keys so a render path without metrics never keeps a
stale inset. Pinned by `psd_writer_box_text_baseline_inset_moves_box_bounds`,
`psd_writer_point_text_anchor_follows_alignment`,
`ui_update_all_text_layers_keeps_photoshop_anchor_kerning_and_anti_aliasing`
(`test-fixtures/psd/photoshop-text-point-anchors.psd`, captured by
`scripts/dev/txt2/ps-capture-point-anchors.ps1`),
`psd_writer_point_text_first_baseline_beats_ink_bottom`, `psd_writer_qt_natural_auto_leading_fraction`
and `ui_text_commit_records_photoshop_baseline_metrics_and_round_trips_psd` (Arial 96 px, box +
point + two-line; writes `test-artifacts/text_baseline_check.psd` for the COM read-back).
**Acceptance (COM)**: Photoshop 27.9 re-laid out that artifact's three layers
within 1 px of Patchy's ink on every edge, and a headless re-save of the 268 px `door_test.psd`
re-rendered within 1 px of Patchy's rows (the original file had been 50 px up).
Older Patchy PSDs get the keys on open: `record_text_layout_metrics_for_reopened_text` lays each
kept Patchy-signed raster out again (fonts installed, horizontal, unwarped, Qt-natural) and stores
the metrics without touching pixels; a point layer still on the ink-bottom convention (transform
== the PSD's, stored boundingBox bottom 0) moves ty onto the real first baseline with its PSD
rects shifted along (`ui_reopened_old_convention_point_text_migrates_to_baseline_anchor`).
