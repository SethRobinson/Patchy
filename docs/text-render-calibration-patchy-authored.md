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

`store_patchy_text_metadata` erases all four keys so a render path without metrics never keeps a
stale inset. Pinned by `psd_writer_box_text_baseline_inset_moves_box_bounds`,
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
