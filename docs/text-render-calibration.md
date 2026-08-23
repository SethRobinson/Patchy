# Photoshop text render calibration

The Photoshop layout/measurement model for type layers: engine units, leading, tracking,
faux bold/italic, whole-pixel glyph folding, and the run-format columns. Split from
[text-tool.md](text-tool.md), which owns the inline-editor session machinery; the
line-plan renderer contract also lives there.

## Photoshop text model (type layers)

Probe PSDs `photoshop-text-*.psd`. The rules apply when `kLayerMetadataTextLayoutMode == "photoshop"` (set on import of non-Patchy TySh):

- **Engine units are document pixels.** 24 pt UI at 300 dpi stores `/FontSize 100` with an identity transform; the transform does NOT carry DPI. UI pt = engine size x transform y-scale x 72/dpi.
- **The TySh transform maps text space to document pixels.** Vertical scale (`hypot(yx, yy)`) multiplies sizes and leading; the x/y ratio is a pure horizontal glyph stretch (free transform folds into the matrix, so xx != yy is common). Never average the two axes.
- **Style runs omit properties equal to the ResourceDict normal style sheet**; a run without `/FontSize` uses the sheet's default (usually 12.0), never a sibling run's value.
- **Leading is per-character; a line's baseline advance = the max effective leading among the ENTERED line's characters.** Fixed leading applies only with `/AutoLeading false`; otherwise the recorded `/Leading` is stale and the effective value is the paragraph auto-leading fraction (default 1.2) x FontSize, sub-pixel exact. It may be smaller than the em.
- **Point text anchors the FIRST baseline at the transform translation (tx, ty)**; justification decides whether tx is line start, middle, or end. No leading on the first line.
- **Box text puts the first baseline at box top + OS/2 sTypoAscender x size** (largest run on line 1; capHeight and hhea/winAscent are wrong), read via QRawFont (`typographic_ascent_fraction`). Leading does not move it.
- **Tracking = FontSize x tracking/1000 px per inter-glyph gap** (not after the last glyph), as absolute letter spacing.
- **VerticalScale/HorizontalScale scale glyphs only**; auto leading stays 1.2 x FontSize, unscaled.

Run format "patchy.text.runs" v3 adds double sizes, a leading column (number or `auto`), tracking, and H/V glyph scales; v4 appends the faux-bold flag, v5 the face/style name, v6 the faux-italic flag; paragraph v3 appends the auto-leading fraction. Every column is read by INDEX, so the version token rises only when a run needs the new column and existing files stay byte-identical. Patchy-authored text keeps v1/v2 and Qt-natural layout (the PS model is opt-in per layer, so Patchy PSDs reopen unchanged). Export writes `/AutoLeading false` for fixed leading (PS ignores it otherwise), non-zero `/Tracking`, non-1 `/HorizontalScale`/`/VerticalScale`.

## Faux bold is not the bold face

`/FauxBold` asks Photoshop to synthesize weight on the face the run already names. It is NOT
"use the family's bold face": folding it into the run's bold flag swaps in a different typeface.
On the Dungeon Scroll `Game_Screen.psd` headings, Georgia-Italic + faux bold measures 58px wide
in Photoshop's own raster while Georgia **Bold** Italic renders 63. Photoshop's Character panel
shows the same split, so clicking into such a layer must come up Italic, not Bold + Italic.

- `PsdTextStyleRun::faux_bold` carries it, `bold`/`italic` keep meaning the real face, and runs
  v4 serializes it in column 11. The Character panel's `textCharacterFauxBold` checkbox edits it
  live per selection.
- Rendering: `apply_faux_bold_to_document` strokes the glyph outlines with a pen
  `kFauxBoldEmFraction` (0.03) of the em wide and adds the same amount to every advance, as
  Photoshop does (its faux bold pushes the glyph out on both sides and pays for it in the
  advance). Calibrated on Georgia-Italic at 12px against Photoshop's rasters: "Dungeon:" 58px and
  "Fights Left:" 69px both land exactly anywhere in 0.025-0.030.
- Applied in `build_text_render_document`, not when the runs are parsed, so it follows later
  colour and size edits and the raster pass and caret layout (which share that function) see
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
ink). The renderer shears the drawn line about its own baseline instead (`faux_italic_shear`,
`kFauxItalicSlant` = tan 12 degrees), leaving advances alone as Photoshop does and growing only
the raster's right bleed.

- **The shear is per LINE, not per run.** `QTextLine::draw` draws a whole line, so
  `line_is_entirely_faux_italic` gates it and a line whose runs disagree stays upright. Per-run
  would mean redrawing through `QTextLine::glyphRuns()` + `QPainter::drawGlyphRun`, reapplying
  colour, the faux-bold outline and selection per run, and moving every pinned pixel baseline in
  the suite. Known gap; faux italic is layer-level across the corpus.
- `ui_faux_italic_shears_the_rendered_glyphs` pins it on "HH" (vertical stems only): upright ink
  starts at the same column top and bottom, sheared ink ~8px further right at the top of a 64px cap.

## Glyph sizes fold only to whole pixels

Qt rasterizes glyphs at whole pixel sizes only: `QFont::setPixelSize` takes an int, and a
fractional `setPointSizeF` quantizes to the same whole pixel (measured -- 16.2px and 16px report
an identical advance). So `render_text_pixels_with_local_rect` folds a transform's vertical scale
into the glyph sizes only as far as the nearest whole pixel and leaves the remainder in
`document_transform`, which the rasterizer applies exactly because these lines are drawn THROUGH
the matrix rather than resampled after the fact. `dominant_text_run_size` picks the size that
lands exactly (the largest run, vertical glyph scale included).

Folding the whole scale rounds the text off Photoshop's size, and only for some layers: in the
Dungeon Scroll repro, 18 x 0.9 = 16.2 became 16 (~1.2% narrow -- "Jumble"/"Submit word" lost
1-2px and shifted on edit) while 14.44444 x 0.9 = 13.0 was already whole and never moved
("Quit"/"Pause"). Leaving the remainder in the matrix also agrees with the caret, which lays out
at the raw size and applies the full transform through the overlay.
`ui_dungeon_scroll_psd_text_commit_keeps_placement_if_available` pins both against Photoshop's
rasters. What is left is a pixel of grid phase: Photoshop's anchors sit at fractional document
positions (tx 267.35, ty 305.4) and both rasters land on the whole-pixel grid, so an edited layer
can still settle 1px off in x or y.

- **Text renders UNHINTED**: PS never runs TrueType hinting; every antialiased `/AntiAlias` mode maps to `QFont::PreferNoHinting` (`configure_text_font_smoothing`); mode 0/None keeps `NoAntialias` + full hinting, which fattens stems on small-print-era fonts and shifts advances into collisions.
- **Imported type layers keep Photoshop's raster until edited** (`should_regenerate_imported_text_preview`, psd_text_write.cpp): a missing font never changes appearance on open. Rasters are kept even under big effects; regenerate only when the stored preview is visibly NOT any run's declared fill color (baked-in effect pixels would corrupt the live outer-effect contour), or when the type block is Patchy-authored. Editing a kept raster warns before substituting fonts; `--append-text` substitutes silently. **Continuing past that warning really substitutes**: `substituted_text_family` (what `QFontInfo` resolves the missing family to, then the UI font, then the original when nothing installed can draw the text) moves the session's base family and `substitute_missing_document_font_families` every run, blank paragraphs' block char formats included. Otherwise the commit stores the missing name back over a raster drawn in the substitute and the layer stays badged. The editable PDF export is the one reader that re-lays-out a kept raster without an edit (real text placed on the raster's ink, missing fonts substituted unless asked for pixels; see [pdf.md](pdf.md)).
- **Black/Heavy faces (weight >= 800, DirectWrite or font database)** resolve to their FULL face name so the family+style matcher finds the real face (family+bold renders Bold, ~15% narrower); the bold flag stays set for fallback. Never feed such a name raw to the font combo: `QFont("Arial Black")` resolves to Tahoma; use `text_font_combo_font_for_family`.
- **Rotated point-text anchoring**: committed placement pins the TEXT-SPACE anchor (justification fraction along the reading axis, first-line side on the stack axis), never a fixed document corner; the CS-era document-bounds fallback pins the corresponding fractional point of the source ink box.
- **Scaled BOX text**: runs and box dims (`patchy.text.box_width/height`, from `/BoxBounds`) are engine units, but a PSD-frame edit session works in DOCUMENT space; the render call's `layout_scale` folds the transform's vertical scale into glyph sizes WITHOUT scaling box dims, and commit stores frame dims divided back to raw units so runs, box and transform stay one coordinate system.
- Committing a transformed point-text layer re-renders CRISP through the aligned transform even when the font is substituted (resampling delivers the same glyphs blurry). The first re-edit after conversion settles placement by a few pixels; later cycles are identical.
- Known gaps: LeadingType 1 (Japanese top-to-top), per-run BaselineShift, VerticalScale x auto leading under a folded transform; box-text RE-edits resample through the transform (the crisp path is point-text only).
