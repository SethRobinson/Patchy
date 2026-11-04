# Photoshop text render calibration

The Photoshop layout/measurement model for type layers: engine units, leading, tracking,
faux bold/italic, whole-pixel glyph folding, and the run-format columns. The session
machinery is in [text-tool.md](text-tool.md), the line-plan renderer contract in
[text-tool-layout.md](text-tool-layout.md).

Related documents:

- [text-render-calibration-pixel-grid.md](text-render-calibration-pixel-grid.md): fractional anchors, line-start and per-glyph rounding to whole pixels, glyph ink outside the advance box.
- [text-render-calibration-patchy-authored.md](text-render-calibration-patchy-authored.md): the layout metrics Patchy records for its own (Qt-natural) text and the TySh geometry the writer derives from them (first baseline, box baseline inset, auto-leading fraction).
- [text-render-calibration-vertical-rtl.md](text-render-calibration-vertical-rtl.md): vertical type (tategaki) and right-to-left paragraph direction.

## Photoshop text model (type layers)

Probe PSDs `photoshop-text-*.psd`. The rules apply when `kLayerMetadataTextLayoutMode == "photoshop"` (set on import of non-Patchy TySh):

- **Engine units are document pixels.** 24 pt UI at 300 dpi stores `/FontSize 100` with an identity transform; the transform does NOT carry DPI. UI pt = engine size x transform y-scale x 72/dpi.
- **The TySh transform maps text space to document pixels.** Vertical scale (`hypot(yx, yy)`) multiplies sizes and leading; the x/y ratio is a pure horizontal glyph stretch (free transform folds into the matrix, so xx != yy is common). Never average the two axes.
- **Style runs omit properties equal to their defaults**: a missing `/FontSize` is the paragraph style's (`/32`, [txt2.md](txt2.md)), else the normal sheet's, never a sibling's.
- **Leading is per-character; a line's baseline advance = the max effective leading among the ENTERED line's characters.** Fixed leading applies only with `/AutoLeading false`; otherwise the recorded `/Leading` is stale and the effective value is the paragraph auto-leading fraction (default 1.2) x FontSize, sub-pixel exact. It may be smaller than the em.
- **Point text anchors the FIRST baseline at the transform translation (tx, ty)**; justification decides whether tx is line start, middle, or end. No leading on the first line.
- **Box text puts the first baseline at box top + Ascent x size, rounded** (largest run on line 1; Ascent is the lowercase "d" height, "Tracking and alignment"). Leading does not move it.
- **Tracking = FontSize x tracking/1000 px per inter-glyph gap** (not after the last glyph), as absolute letter spacing; alignment: "Tracking and alignment".
- **VerticalScale/HorizontalScale scale glyphs only**; auto leading stays 1.2 x FontSize, unscaled.

Run format "patchy.text.runs" v3 adds double sizes, a leading column (number or `auto`), tracking, and H/V glyph scales; v4 appends the faux-bold flag, v5 the face/style name, v6 the faux-italic flag, v7 the rotated-Roman flag of vertical type; paragraph v3 appends the auto-leading fraction, v4 the direction, v5 the composer. Runs v8 adds baseline shift (engine pixels, positive raises glyphs) in column 15; both TySh and Txt2 preserve it. The snapped horizontal glyph renderer applies it before device rounding. Runs v9 adds the character-style tokens in column 16 ([text-character-styles.md](text-character-styles.md)). Every column is read by INDEX, so the version token rises only when a run needs the new column and existing files stay byte-identical. Patchy-authored text keeps v1/v2 and Qt-natural layout (the PS model is opt-in per layer, so Patchy PSDs reopen unchanged). Export writes `/AutoLeading false` for fixed leading (PS ignores it otherwise), non-zero `/Tracking`, non-1 `/HorizontalScale`/`/VerticalScale`.

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
both against Photoshop's rasters; both renderers put the raster at the line start rounded to a whole
pixel ([text-render-calibration-pixel-grid.md](text-render-calibration-pixel-grid.md)).

## Rendering, import and commit rules

- **Text renders UNHINTED**: every antialiased `/AntiAlias` mode gets `QFont::PreferNoHinting` (`configure_text_font_smoothing`); 0/None keeps `NoAntialias` + full hinting. Strong adds an outline stroke; per-mode captures in [text-anti-alias.md](text-anti-alias.md).
- **Imported type layers keep Photoshop's raster until edited** (`should_regenerate_imported_text_preview`, psd_text_write.cpp): a missing font never changes appearance on open. Rasters are kept even under big effects; regenerate only when the stored preview is visibly NOT any run's declared fill color (baked-in effect pixels would corrupt the live outer-effect contour), or when the type block is Patchy-authored. Editing a kept raster warns before substituting fonts; `--append-text` substitutes silently. **Continuing past that warning really substitutes**: `substituted_text_family` (what `QFontInfo` resolves the missing family to, then the UI font, then the original when nothing installed can draw the text) moves the session's base family and `substitute_missing_document_font_families` every run, blank paragraphs' block char formats included. Otherwise the commit stores the missing name back over a raster drawn in the substitute and the layer stays badged. Only the editable PDF export re-lays-out a kept raster without an edit (text on the raster's ink; [pdf-editable-export.md](pdf-editable-export.md)).
- **A type layer with no raster** (writer-made: ag-psd's `auto-lead/expected.psd`, issue 272) lays out at open
  (`lay_out_raster_less_imported_text`): line 1's baseline on ty, its justification point on tx, through the TySh
  transform, kept as `psd_raster_preview` so a save writes it under the file's own type block. PS 2026 relays out
  it and Patchy's re-save alike (ink x 0..50, y -8..0). Vertical, warped or empty layouts keep the placeholder.
- **Black/Heavy faces (weight >= 800, DirectWrite or font database)** keep their face in the stored name (`family + " " + face`, "Arial Black"; rules in [font-resolution.md](font-resolution.md)) so the real face renders (family+bold renders Bold, ~15% narrower); the bold flag stays set for fallback. Never feed such a name raw to the font combo: `QFont("Arial Black")` resolves to Tahoma; use `text_font_combo_font_for_family`.
- **Rotated point-text anchoring**: committed placement pins the TEXT-SPACE anchor (justification fraction along the reading axis, first-line side on the stack axis), never a fixed document corner; the CS-era document-bounds fallback pins the corresponding fractional point of the source ink box.
- **Scaled BOX text**: runs and box dims (`patchy.text.box_width/height`, from `/BoxBounds`) are engine units, but a PSD-frame edit session works in DOCUMENT space; the render call's `layout_scale` folds the transform's vertical scale into glyph sizes WITHOUT scaling box dims, and commit stores frame dims divided back to raw units so runs, box and transform stay one coordinate system.
- Committing a transformed point-text layer re-renders CRISP through the aligned transform even when the font is substituted (never a blurry resample). The first re-edit after conversion settles placement by a few pixels; later cycles are identical.
- Known gaps: LeadingType 1 (Japanese top-to-top), VerticalScale x auto leading under a folded transform; box-text RE-edits resample when the residual still has a linear part (rotation, aspect): Free Transform and Image Size fold a uniform scale into the size and frame dims, so those re-edits commit crisp, while the commit-time crisp path stays point-text only.

## Tracking and alignment

PS 2026 COM sweeps (`scripts/dev/text-tracking/`, captures `test-fixtures/psd/photoshop-text-track-*.psd`):
Arial "TEXT" and "A", point/box, left/centre/right, tracking -80 to +200.

- **Alignment uses the width without the trailing gap**, at any tracking (a lone centred "A"
  never moves): `photoshop_text_layout_plan` moves each centred or right left-to-right line by
  its share of the tracking Qt adds after the last glyph (point text: against its widest line).
- **The box lays out at the frame width rounded up, ignoring matrix noise**
  (`psd_text_frame_extent_pixels`; psd-tools `type-layer.psd`'s xx 1.0000000000000002 made 32 px 33).
- **Box first baseline = round(box top + Ascent x size)**, halves up, Ascent being the top of
  the lowercase "d" (`photoshop_ascender_fraction`; typo ascender only without a "d"): Arial 48
  at top 40.10 lands on 74 and 40.15 on 75 ("d" 0.7158, typo 0.728). Calibri (0.678 vs typo
  0.750), Segoe UI, Times, Verdana, Georgia, Courier New and Tahoma at 30 to 72 px all fit "d".
  Baseline shift moves it exactly. Patchy-written blocks get cap height
  ([text-render-calibration-patchy-authored.md](text-render-calibration-patchy-authored.md)).
- **World-Ready paragraphs align the last line as if its last glyph kerned with a space** (Arial
  30 px "A" -1.655 px; "TEXT" 121.80, not 122.66): Txt2 paragraph `/35` 1 or 2, [txt2.md](txt2.md).
- **`/Tracking` is written as an integer.** Photoshop's engine re-lays out a layer with a
  negative float tracking (`-305.000000`) as "the result would be too big", every edit failing;
  `-305` and positive floats work
  (`psd_vertical_tracking_bug_file_resaves_with_integer_tracking_if_available`).
- Gap: a fractional box top can land a row off. Pinned by
  `ui_psd_centered_text_commit_rounds_line_start_like_photoshop` and `_if_available` tests.
