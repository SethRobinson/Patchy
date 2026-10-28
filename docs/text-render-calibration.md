# Photoshop text render calibration

The Photoshop layout/measurement model for type layers: engine units, leading, tracking,
faux bold/italic, whole-pixel glyph folding, and the run-format columns. The session
machinery and the line-plan renderer contract are in [text-tool.md](text-tool.md).

## Photoshop text model (type layers)

Probe PSDs `photoshop-text-*.psd`. The rules apply when `kLayerMetadataTextLayoutMode == "photoshop"` (set on import of non-Patchy TySh):

- **Engine units are document pixels.** 24 pt UI at 300 dpi stores `/FontSize 100` with an identity transform; the transform does NOT carry DPI. UI pt = engine size x transform y-scale x 72/dpi.
- **The TySh transform maps text space to document pixels.** Vertical scale (`hypot(yx, yy)`) multiplies sizes and leading; the x/y ratio is a pure horizontal glyph stretch (free transform folds into the matrix, so xx != yy is common). Never average the two axes.
- **Style runs omit properties equal to their defaults**: a missing `/FontSize` is the paragraph style's (`/32`, [txt2.md](txt2.md)), else the normal sheet's, never a sibling's.
- **Leading is per-character; a line's baseline advance = the max effective leading among the ENTERED line's characters.** Fixed leading applies only with `/AutoLeading false`; otherwise the recorded `/Leading` is stale and the effective value is the paragraph auto-leading fraction (default 1.2) x FontSize, sub-pixel exact. It may be smaller than the em.
- **Point text anchors the FIRST baseline at the transform translation (tx, ty)**; justification decides whether tx is line start, middle, or end. No leading on the first line.
- **Box text puts the first baseline at box top + Ascent x size, rounded** (largest run on line 1; Ascent is the lowercase "d" height, "Tracking and alignment"). Leading does not move it.
- **Tracking = FontSize x tracking/1000 px per inter-glyph gap** (not after the last glyph), as absolute letter spacing; centred and right lines align on the width without a trailing gap.
- **VerticalScale/HorizontalScale scale glyphs only**; auto leading stays 1.2 x FontSize, unscaled.

Run format "patchy.text.runs" v3 adds double sizes, a leading column (number or `auto`), tracking, and H/V glyph scales; v4 appends the faux-bold flag, v5 the face/style name, v6 the faux-italic flag, v7 the rotated-Roman flag of vertical type; paragraph v3 appends the auto-leading fraction, v4 the direction. Runs v8 adds baseline shift (engine pixels, positive raises glyphs) in column 15; both TySh and Txt2 preserve it. The snapped horizontal glyph renderer applies it before device rounding. Every column is read by INDEX, so the version token rises only when a run needs the new column and existing files stay byte-identical. Patchy-authored text keeps v1/v2 and Qt-natural layout (the PS model is opt-in per layer, so Patchy PSDs reopen unchanged). Export writes `/AutoLeading false` for fixed leading (PS ignores it otherwise), non-zero `/Tracking`, non-1 `/HorizontalScale`/`/VerticalScale`.

## Faux bold is not the bold face

`/FauxBold` asks Photoshop to synthesize weight on the face the run already names. It is NOT
"use the family's bold face": folding it into the run's bold flag swaps in a different typeface.
On the Dungeon Scroll `Game_Screen.psd` headings, Georgia-Italic + faux bold measures 58px wide
in Photoshop's own raster while Georgia **Bold** Italic renders 63. Photoshop's Character panel
shows the same split, so clicking into such a layer must come up Italic, not Bold + Italic.

- `PsdTextStyleRun::faux_bold` carries it, `bold`/`italic` keep meaning the real face, and runs
  v4 serializes it in column 11. The Character panel's `textCharacterFauxBold` button edits it
  live per selection.
- Rendering: `apply_faux_bold_to_document` adds `kFauxBoldAdvanceEmFraction` (0.027 em) to
  every advance and strokes the outlines with a pen as wide, capped at `kFauxBoldMaxStrokePixels`
  (1 document px). Fit to PS 2026 captures (four faces, any weight, 12-200px, 300 ppi) in
  `local-test-fixtures/faux-bold-calibration`: ten H's grow 3.25 / 6.5 / 16.25 / 30.0px at
  12 / 24 / 62.5 / 120px. Pins: `ui_faux_bold_widening_matches_photoshop_captures` and
  `ui_layer_effects_psd_faux_bold_commit_matches_photoshop_if_available`.
- Applied in `build_text_render_document`, not when the runs are parsed, so it follows later
  colour and size edits and the raster pass and caret layout (sharing that function) see
  identical advances. Apply it BEFORE the final `setTextWidth`: the widened advances have to be in
  place while the lines are laid out.
- Export writes `/FauxBold` from `faux_bold` alone. The run's real weight already rides in the
  font name `font_index_for_run` resolves (`Arial-BoldMT`, not Arial + FauxBold); writing both
  made Photoshop embolden an already-bold face.
## Faux italic is a shear, not a font

`/FauxItalic` splits from `run.italic` exactly as faux bold splits from `run.bold`: it slants the
run's OWN face and must not resolve to the family's real Italic, which is a different typeface.
It rides `PsdTextStyleRun::faux_italic` and runs v6 column 13, the Character panel edits it
(`textCharacterFauxItalic`), and export writes `/FauxItalic` from that flag alone.

Rendering it cannot go through QFont: measured on Arial, `QFont::setStyle(QFont::StyleOblique)`
resolves to the family's REAL Italic face (`QFontInfo::styleName()` returns "Italic", identical
ink). The renderer shears the glyphs about the line's baseline instead (`faux_italic_shear`,
`kFauxItalicSlant` = tan 12 degrees), leaving advances alone as Photoshop does and growing only
the raster's right bleed.

- **The shear is per RUN** (issue 466): `format_needs_glyph_run_draw` sends a faux italic line
  through `draw_line_glyphs_pixel_aligned`, which shears only that fragment; `QTextLine::draw`
  and `drawContents` never see faux italic. Decoration bars stay upright.
- `ui_faux_italic_shears_the_rendered_glyphs` pins it on "HH" (vertical stems only): upright ink
  starts at the same column top and bottom, sheared ink ~8px further right at the top of a 64px cap.
  `ui_faux_italic_on_a_selection_slants_only_the_selected_characters` pins the per-run case.

## Glyph sizes fold only to whole pixels

Qt rasterizes glyphs at whole pixel sizes only: `QFont::setPixelSize` takes an int, and a
fractional `setPointSizeF` quantizes to the same whole pixel (measured: 16.2px and 16px report
an identical advance). So `render_text_pixels_with_local_rect` folds a transform's vertical scale
into the glyph sizes only as far as the nearest whole pixel and leaves the remainder in
`document_transform`, which the rasterizer applies exactly because these lines are drawn THROUGH
the matrix rather than resampled after the fact. `dominant_text_run_size` picks the size that
lands exactly (the largest run, vertical glyph scale included).

Folding the whole scale would round some layers off Photoshop's size (Dungeon Scroll: 18 x 0.9
= 16.2 becomes 16, 14.44444 x 0.9 = 13.0 does not); the caret also lays out at the raw size.

The fold runs for every Photoshop-layout render, scaling transform or not: a run's FontSize x
VerticalScale is fractional on its own (issue 20: 1086.61 x 0.93 = 1010.55), and the width has
the same problem one level up. HorizontalScale / VerticalScale becomes `QFont::setStretch`, a
whole percent (0.95 / 0.93 = 102.15% renders at 102%, a pixel narrow at the far edge of a
1080 px word). `dominant_run_width_residual` measures the dominant run's real advance ratio
(what the stretch delivered, on whatever engine) against the ratio the runs ask for (times the
session's `patchy.textMetricScale`, which is wanted narrowing, not a shortfall) and
pre-scales the matrix's local x axis by the shortfall. Both residuals are render-only: the stored
transform and the TySh never carry them, and a layer whose sizes and ratio are already whole
renders byte-identically. `ui_dungeon_scroll_psd_text_commit_keeps_placement_if_available` pins
both against Photoshop's rasters; both renderers put the raster at the anchor rounded to a whole
pixel (next section).

- **Text renders UNHINTED**: every antialiased `/AntiAlias` mode gets `QFont::PreferNoHinting` (`configure_text_font_smoothing`); 0/None keeps `NoAntialias` + full hinting. Strong adds an outline stroke; per-mode captures in [text-anti-alias.md](text-anti-alias.md).
- **Imported type layers keep Photoshop's raster until edited** (`should_regenerate_imported_text_preview`, psd_text_write.cpp): a missing font never changes appearance on open. Rasters are kept even under big effects; regenerate only when the stored preview is visibly NOT any run's declared fill color (baked-in effect pixels would corrupt the live outer-effect contour), or when the type block is Patchy-authored. Editing a kept raster warns before substituting fonts; `--append-text` substitutes silently. **Continuing past that warning really substitutes**: `substituted_text_family` (what `QFontInfo` resolves the missing family to, then the UI font, then the original when nothing installed can draw the text) moves the session's base family and `substitute_missing_document_font_families` every run, blank paragraphs' block char formats included. Otherwise the commit stores the missing name back over a raster drawn in the substitute and the layer stays badged. Only the editable PDF export re-lays-out a kept raster without an edit (text on the raster's ink; [pdf.md](pdf.md)).
- **A type layer with no raster** (writer-made: ag-psd's `auto-lead/expected.psd`, issue 272) lays out at open
  (`lay_out_raster_less_imported_text`): line 1's baseline on ty, its justification point on tx, through the TySh
  transform, kept as `psd_raster_preview` so a save writes it under the file's own type block. PS 2026 relays out
  it and Patchy's re-save alike (ink x 0..50, y -8..0). Vertical, warped or empty layouts keep the placeholder.
- **Black/Heavy faces (weight >= 800, DirectWrite or font database)** keep their face in the stored name (`family + " " + face`, "Arial Black"; rules in [font-resolution.md](font-resolution.md)) so the real face renders (family+bold renders Bold, ~15% narrower); the bold flag stays set for fallback. Never feed such a name raw to the font combo: `QFont("Arial Black")` resolves to Tahoma; use `text_font_combo_font_for_family`.
- **Rotated point-text anchoring**: committed placement pins the TEXT-SPACE anchor (justification fraction along the reading axis, first-line side on the stack axis), never a fixed document corner; the CS-era document-bounds fallback pins the corresponding fractional point of the source ink box.
- **Scaled BOX text**: runs and box dims (`patchy.text.box_width/height`, from `/BoxBounds`) are engine units, but a PSD-frame edit session works in DOCUMENT space; the render call's `layout_scale` folds the transform's vertical scale into glyph sizes WITHOUT scaling box dims, and commit stores frame dims divided back to raw units so runs, box and transform stay one coordinate system.
- Committing a transformed point-text layer re-renders CRISP through the aligned transform even when the font is substituted (never a blurry resample). The first re-edit after conversion settles placement by a few pixels; later cycles are identical.
- Known gaps: LeadingType 1 (Japanese top-to-top), VerticalScale x auto leading under a folded transform; box-text RE-edits resample when the residual still has a linear part (rotation, aspect): Free Transform and Image Size fold a uniform scale into the size and frame dims, so those re-edits commit crisp, while the commit-time crisp path stays point-text only.

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
  BOTTOM (one descent low for descenders, lines low for multi-line text).

The renderer records what it drew (`text_layout_metrics_for_plan`, stored by
`store_text_layout_metrics` at every site that gives a layer a text render, editor commits and
transform re-renders included) and the Qt-free writer turns it into geometry:

- `patchy.text.first_baseline`: the first line's baseline in plan units, below the text-local
  origin for point text (the transform origin, which is the raster's top row unless glyph ink
  overshoots the first line top, "Glyph ink outside the advance box" below) and below the
  raster's top row for box text. `point_text_baseline_offset` anchors ty there; the ink-bottom
  scan stays the fallback for rasters no Patchy render measured. `patchy.text.raster_top`
  (point text: `local_rect.top()`) tells the writer whether ty is still that origin (the raster
  sits raster_top from it: anchor) or already the baseline (a reopened layer: leave it); the
  old "ink starts above ty" test read a grown buffer as anchored and wrote the CoreText "M"
  of issue 20 an ascent high (`psd_writer_point_text_grown_raster_still_anchors_baseline`).
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
  intolerance as the negative float `/Tracking`. Style runs keep `/AutoLeading true`; a fixed
  `/Leading` would read back as Photoshop provenance (`serialized_runs_have_photoshop_leading_signals`)
  and flip the layer into the Photoshop layout model on reopen. The fraction round-trips through
  the paragraph v3 column, which the model check ignores. One fraction per layer: mixed families
  on one line can still differ by a few px from line 2 on.

`store_patchy_text_metadata` erases all three so a render path without metrics never keeps a
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

## Vertical type (tategaki)

Photoshop 2026 captures: `local-test-fixtures/psd/ps2026_vtext/` (`capture_vtext.jsx`, PSD + PNG +
`manifest.jsonl` with DOM bounds; `dump_tysh.py` prints a TySh). Four are committed as
`test-fixtures/psd/photoshop-text-vertical-{point,box,rotated-roman}.psd` and
`photoshop-text-rtl-hebrew.psd` (corpus digests pinned). Pinned by
`ui_vertical_text_matches_photoshop_capture` (ink within 1 px of PS's 67x92 point raster; skips
without MS Gothic) and `psd_vertical_*captures*` in tests/core.

- **Every glyph is upright, Latin included, by default**: "Hello" stacks H, e, l, l, o. Photoshop's
  "Standard Vertical Roman Alignment" lies Roman glyphs on their side instead: per run,
  `/BaselineDirection` 1 = upright (what PS writes by default), 2 = rotated; a run WITHOUT the
  key re-lays out rotated, so every vertical run Patchy writes carries it. Patchy: runs v7
  column 14 (`2` = rotated, written only then), `kTextRotatedRomanFormatProperty`, the
  Character panel's "Rotate Latin (vertical text)"; a rotated cell advances by the glyph's
  horizontal width and is drawn turned 90 degrees clockwise with its ascent+descent box centred
  on the axis; CJK clusters stay upright either way. Cell pitch = FontSize x
  VerticalScale (MS Gothic and Arial both advance 32 px per glyph at 32 px); whitespace advances
  by its horizontal width (a 32 px Arial space is 8.89 px of column, so "Hello World" spans
  10 x 32 + 8.89). Tracking adds FontSize x tracking/1000 after every cell except a column's
  last (three cells at +200 = 96 + 2 x 6.4).
- **Glyph placement in the cell**: centred on the column axis horizontally; the font's
  ascent + descent box centred vertically, baseline = cell top + (em - (asc + desc)) / 2 + asc.
  MS Gothic's box is exactly 1 em (ink 3 px inside the cell top), Arial's is 1.117 em (caps
  4-5 px below the cell top, ~0.84 em baseline).
- **Columns advance left by the entered column's max effective leading** (auto 1.2 x 32 =
  38.4: second column left edge at -54.4; fixed 48: at -64), the horizontal per-line rule
  transposed.
- **Anchors** (`bounds` in the TySh, transform = the click): point text x in [-em/2, em/2]
  around the first column's axis; y in [0, h] for left (top), [-h/2, h/2] for center,
  [-h, 0] for right (bottom). Box text: transform at the frame's top-left, `/BoxBounds`
  [0 0 w h], the first column against the frame's RIGHT edge, wrapping by whole cells at the
  frame height (7 cells of 32 in a 250 px frame), overflow columns hidden.
- **Patchy raster = cell union + bleed** `vertical_text_bleed_for_size(size)` = ceil(0.25 x
  base size) on every side (psd/psd_text_runs.hpp, shared by the renderer and the Qt-free PSD
  writer), so `text_geometry_for_layer` recovers the anchor from the raster rect alone:
  tx = right - bleed - em/2, ty = top + bleed + fraction x (height - 2 x bleed).
- **`/Tracking` is written as an integer.** Photoshop's engine re-lays out a layer with a
  negative float tracking (`-305.000000`) as "the result would be too big", every edit failing;
  `-305` and positive floats work
  (`psd_vertical_tracking_bug_file_resaves_with_integer_tracking_if_available`).

## Tracking and alignment

PS 2026 COM sweep (`scripts/dev/text-tracking/`; six captures in `test-fixtures/psd/
photoshop-text-track-*.psd`): "TEXT", Arial 48 px, point/box, left/centre/right, tracking -80 to +200.

- **Alignment uses the width without the trailing gap.** Qt's letter spacing follows the last
  glyph too, so `photoshop_text_layout_plan` moves each centred or right left-to-right line by
  its share of that tracking (point text: against its widest line); others never move.
- **Box first baseline = round(box top + Ascent x size)**, halves up, Ascent being the top of
  the lowercase "d" (`photoshop_ascender_fraction`; typo ascender only without a "d"): Arial 48
  at top 40.10 lands on 74 and 40.15 on 75 ("d" 0.7158, typo 0.728). Calibri (0.678 vs typo
  0.750), Segoe UI, Times, Verdana, Georgia, Courier New and Tahoma at 30 to 72 px all fit "d".
  Baseline shift moves it by exactly its value. Patchy-written blocks get cap height (above).
- Gaps: Qt's untracked "TEXT" advance is 122.66 px to Photoshop's 121.80 (right lines can sit a
  column left); a fractional box top can land a row off. Pinned by
  `ui_psd_centered_text_commit_rounds_line_start_like_photoshop` and two `_if_available` tests.

## Pixel grid and fractional anchors

PS 27.9 COM captures: "Hg", Arial 48 px, Sharp, placed at x or y 100.0 / 100.3 /
100.5 / 100.7, plus 10-degree rotated and 150% scaled variants; two are committed as
`test-fixtures/psd/photoshop-text-anchor-{whole,half}.psd` (x 100.0 and 100.5).

- **The TySh keeps the fractional anchor** (tx 100.3/100.5/100.7 round-trip exactly). Patchy keeps it too:
  `patchy.text.transform` serializes at 17 significant digits (`serialize_layer_affine_transform`),
  `committed_text_transform` leaves tx/ty alone when their rounding already equals the committed
  document point, and integer moves add to them.
- **Photoshop rasterizes with each LINE START rounded to a whole pixel, halves up**
  (`snap_to_pixel_grid`, core/pixel_grid.hpp: floor(v + 0.5)). The line start is the anchor minus
  the justification offset in document space, so left-aligned x 100.3 renders like 100.0 and
  100.5 like 100.7; y likewise, rotated and scaled layers included. Centred and right text does
  NOT round the anchor: the sweep (`local-test-fixtures/psd/ps2026_text_anchor_just/`,
  `capture_anchor_just.jsx`: left/centre/right x 100.0/.3/.5/.7 x scale 100%/90%, plus Bookman
  "Pause" replicas of the Dungeon Scroll buttons) puts the centred "Hg" (advance box +-30.68) at
  column 72 for 100.0 but 73 for 100.3, scaled 90% at 75 then 76, right-justified at 42 for all
  four. Every case fits round(anchor + line offset) + left side bearing, none round(anchor).
  Three places apply it. `build_text_render_plan` snaps dy and, for an axis-aligned transform,
  keeps the fractional line start for the per-glyph rounding below (`round_line_starts` moves
  whole lines only where glyphs are not aligned individually; rotated, sheared, vertical and
  drawContents renders keep the anchor snap). The session override for an imported layer's
  re-render puts Patchy's line start on Photoshop's WITH its fraction: from the TySh advance box
  when the file has one (`psd_point_text_local_bounds_transform_for_pixels`), else from
  Photoshop's ink column less this render's side bearing plus the anchor fraction recorded at
  session entry (`kTextEditorSourceLineStartFractionProperty`; `anchored_text_transform_for_pixels`
  truncates the justification width correction to whole pixels;
  `psd_point_text_document_bounds_transform_for_pixels` estimates the fraction for CS-era scaled
  layers). A fractional x translation commits through the crisp re-render
  (`render_crisp_transformed_text_for_editor`), never a half-pixel resample. The editor's
  document point (`set_text_editor_transform_override`, `rendered_text_bounds_for_editor`,
  session entry) rounds the same way. Pinned by `ui_text_transform_rerender_rounds_anchor_like_photoshop`,
  `ui_box_text_edit_keeps_fractional_anchor`, `psd_text_anchor_captures_keep_fractional_transform`
  and `ui_psd_centered_text_commit_rounds_line_start_like_photoshop`.
- **Glyph ink outside the advance box is kept, and the buffer grows around the origin.**
  Qt's line rect is the advance box; a negative left side bearing (script and italic faces: the
  Balmoral LET "é" starts 9 px left of the pen at 1011 px), ink past the last advance, or a swash
  above Qt's ascent lies outside it, and Photoshop rasterizes all of it. `build_text_render_plan`
  measures the real ink per line (`line_glyph_ink_rect`: `QRawFont::boundingRect` per glyph,
  widened by any stretch because the DirectWrite engine's boxes ignore it, plus half the faux-bold
  stroke, the faux-italic descender lean, and on macOS 1 px of slack because CoreText's boxes
  are a pixel tighter than its raster) and `grow_point_text_rect_to_glyph_ink` extends
  `local_rect` by 2 px past whichever edge the ink exceeds; ink that fits leaves the rect and
  every pinned raster untouched. Local (0, 0) stays the line start / first line top, so the
  transform still names the pen and the buffer starts at transform + `local_rect.topLeft()`
  (`rendered_text_bounds_for_editor`, the resample offset in commit and preview, the SVG and
  Affinity placements); the PSD pins convert buffer-space ink to local space before comparing
  (clipping the overhang slides the text by the bearing on an unchanged apply, issue 20). Pinned by
  `ui_point_text_render_keeps_glyph_overhang` (Arial Italic "jf": transform at the pen, re-entry
  byte-identical, TySh tx at the pen) and, with the reporter's file and font in the local
  fixtures, `ui_la_methode_psd_text_commit_keeps_glyph_overhang_if_available` (ink within 1 px of
  Photoshop's, clear buffer margins, TySh at Photoshop's pen), plus zoom and edit independence
  and the live preview's placement (`..._is_zoom_and_edit_independent_if_available`,
  `..._preview_sits_on_photoshop_ink_if_available`). Known gap: box text keeps its 2 px `kHorizontalBleed` and still clips an
  overhanging first or last glyph; its frame-origin contract differs and is untouched.
- **Each glyph's x rounds too.** Photoshop rounds EACH glyph's absolute x position, not just the
  line start: at x 100.5 the "H" moved one column while the "g" (100.5 + 34.67 = 135.17 -> 135,
  the same column as 134.67) stayed, and the centered/right "Hg" right edge moves at .7 while the
  left edge does not. Qt places glyphs at fractional advances, so a Photoshop-layout line through
  an axis-aligned transform is drawn run by run (`draw_line_glyphs_pixel_aligned`, gated by the
  plan's `pixel_align_glyphs`): every glyph origin is mapped to document space, snapped with
  `snap_to_pixel_grid`, and mapped back through the residual scale before `drawGlyphRun`, per
  fragment for colour, with faux bold filling and stroking glyph paths as the vertical plan does.
  **Glyph images are drawn from the unstretched face through an explicit x scale** equal to the
  advance ratio the engine delivered: FreeType (the offscreen suite) stretches images and
  advances for a stretched QFont, DirectWrite (a Windows window) stretches the advances only,
  so a stretched glyph drawn through the QFont is narrow on screen while every offscreen pin
  passes. Rotated or
  sheared transforms and the drawContents fallback keep `QTextLine::draw` (their stretch is
  still engine-dependent, a known gap; so is vertical type). Pinned by
  `ui_psd_left_text_commit_rounds_each_glyph_like_photoshop` on the whole/half fixtures: the
  re-render keeps Photoshop's half-alpha edge columns exactly, left edge +1 and right edge +0
  between the pair.
- Box text keeps a fractional `/BoxBounds` in PS (100.6 x 80.3); `patchy.text.box_width/height`
  round it (`extract_type_tool_text_box`). The frame origin rounds like a point anchor.
- **TySh encoding**: descriptor `Ornt` enum `Vrtc`; engine data `/WritingDirection 2` in
  both the Shapes and Lines dictionaries and `/Procession 1` (horizontal: 0, 0). Nothing else
  in the engine data differs between a vertical and a horizontal save.
- **OpenType `vert`** rides on the render document's default font (`QFont::setFeature`; Qt 6.8
  char formats cannot carry features), so fonts with the table get their vertical brackets and
  long-vowel marks.
- **Acceptance (COM, September 2026)**: Photoshop 2026 opened the Patchy-authored
  `test-artifacts/vertical_text_check.psd` (written by
  `ui_vertical_text_recommit_keeps_origin_and_round_trips_psd`, dialogs suppressed), read the
  layer back as `orientation:vertical` with bounds [-54.4, -32, 16, 32] (its own convention for
  centred two-column text), and a forced type re-render landed the two columns within 4 px of
  Patchy's ink. Photoshop's reading of an authored paragraph direction is unverified.

## Paragraph direction (right-to-left)

`patchy.text.paragraph_runs` v4 appends column 9 (`auto`/`ltr`/`rtl`), written only when a
paragraph carries an explicit direction. Photoshop keeps its `directionType` OUTSIDE the TySh
(captures `rtl_hebrew_dir_rtl.psd` and `_ltr.psd` differ only in bounds; the DOM reads it back
from the document-level Txt2 resource). Patchy writes it in both places: the authored Txt2
paragraph sheet's `/33` (1 = RTL, else 0; [txt2.md](txt2.md)) and, only for an explicit
direction, the Middle Eastern composer's `/ParagraphDirection` TySh paragraph key (1 = RTL,
0 = LTR), which the reader maps back into the v4 column. Whether Photoshop honours either on a
Patchy-authored file is unverified. Photoshop's default engine already reorders Hebrew and shapes
Arabic (captures `rtl_mixed.png`, `rtl_arabic_left.png`), as Qt does.
