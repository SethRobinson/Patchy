# Text anti-alias modes

How Photoshop draws each type layer `/AntiAlias` mode (0 None, 1 Crisp, 2 Strong, 3 Smooth,
4 Sharp, 6 Windows LCD, 7 Windows; `kTextAntiAlias*` in core/layer_metadata.hpp) and what
Patchy's renderer does with it. The mode itself round-trips through the TySh and the options bar
(`psd_text_anti_alias_modes_save_and_reopen_as_photoshop_writes`); the general render rules
(unhinted glyphs, whole-pixel sizes, per-glyph x rounding) are in
[text-render-calibration.md](text-render-calibration.md).

## Photoshop captures

PS 2026 COM captures in `local-test-fixtures/psd/ps2026_text_antialias/` (local only):
`capture_antialias.jsx` writes `aa_arial.psd` (ArialMT at 8 to 72 px) and `aa_georgia.psd`
(Georgia at 9 to 48 px), one point-text layer per mode and size (`<mode>_<size>`, black
"Hamburgefonts" on transparency, anchor x 10 on whole pixels, `manifest.jsonl` has the anchors);
`capture_pin.jsx` writes `aa_pin.psd` (Arial "Strong12/Smooth12/Strong30/Smooth30 Hamburgefonts").
`run_capture.ps1` runs either under the desk's Photoshop lock. Each layer's stored raster is
Photoshop's render.

Ink (sum of alpha) relative to Smooth at the same size:

| Mode | Arial 9 / 12 / 16 / 24 / 48 / 72 px | Georgia 9 / 12 / 16 / 24 / 48 px |
|---|---|---|
| Crisp | 0.99 / 0.99 / 0.99 / 1.00 / 1.00 / 1.00 | 1.00 / 0.99 / 0.99 / 0.99 / 1.00 |
| Strong | 1.37 / 1.23 / 1.13 / 1.09 / 1.06 / 1.06 | 1.62 / 1.37 / 1.21 / 1.16 / 1.08 |
| Sharp | 1.42 / 1.24 / 1.01 / 1.02 / 1.06 / 1.06 | 1.89 / 1.59 / 1.11 / 1.00 / 1.03 |

- **Coverage is quantized.** Smooth and Strong use 16 steps (alpha 16k, then 255), Crisp and Sharp
  15 (alpha 17k): a 16-sample coverage, not exact area. Patchy keeps 8-bit area coverage; the
  difference is at most half a step.
- **Crisp is Smooth.** Same glyph shapes, ink within 1%; only the step size differs. Patchy renders
  it as Smooth.
- **Strong is Smooth plus an outline stroke.** Every "H" stem from 12 to 72 px is 0.25 px wider
  than Smooth's and the crossbar 0.25 px taller, at every size; filling the unhinted outline and
  stroking it with a pen 0.2 to 0.3 px wide (document pixels) is also the best per-pixel fit of
  whole words in both fonts. The pen does not scale with the size, which is why the gain falls
  from +23% at 12 px to +6% at 48 px. Advances do not change. Edges sit on quarter pixels (the
  4 x 4 samples), so a grow under one sample row often does not show: the "H" cap top and
  baseline stay put.
- **Sharp is hinted.** Stems snap to whole pixels (the 12 px Arial "H" stems are full 255 columns)
  and glyph shapes change (the "a" bowl moves a row), so the gain is large at small sizes and
  irregular above 16 px. Not modelled: it needs Photoshop's hinter, and engine hinting differs per
  platform. Patchy renders it as Smooth.

## Patchy's rendering

- Every antialiased mode renders unhinted (`QFont::PreferNoHinting`,
  `configure_text_font_smoothing`); 0/None keeps `NoAntialias` + full hinting and a 50% alpha
  threshold (`render_text_pixels_with_local_rect`).
- **Strong**: `build_text_render_plan` sets `TextRenderPlan::strong_stroke` to
  `kStrongAntiAliasStrokePixels` (0.25, text_render.hpp) over the square root of the residual
  transform's determinant, so the pen is 0.25 DOCUMENT pixels at any layer scale. Every draw path
  then fills and strokes glyph paths (`glyph_outline_pen`: a faux-bold outline widens by the pen,
  otherwise the pen alone in the run colour, round joins): the pixel-aligned Photoshop-layout
  lines, Qt-natural lines (point text that would use `drawContents` takes line items instead, so
  placement is unchanged) and vertical cells. Advances and caret layout are untouched. The
  editable PDF export (`draw_text_layer_to_painter`) clears the pen so its glyphs stay real text.
- **Strong drops alpha below 32** (`kStrongAntiAliasMinimumAlpha`, coverage under 1/8, which no
  4 x 4 sample sees) in `render_text_pixels_with_local_rect`. Without it the stroke's faint graze
  added a new top row or left column, and the imported-text placement, which pins the render's
  ink corner to Photoshop's (`anchored_text_transform_for_pixels`), moved the whole layer a
  pixel (aa_pin "Strong30": pixel difference 0.62 instead of 0.14).
- Pinned by `ui_psd_strong_anti_alias_commit_keeps_photoshop_weight_if_available` (offscreen,
  Windows): an unchanged edit of each `aa_pin.psd` layer keeps Photoshop's ink within 8%, Strong
  over Smooth at least 1.12 at 12 px and 1.02 at 30 px, and Strong's per-pixel difference
  (sum |Patchy - Photoshop| over Photoshop's ink) under the unstroked render's. Measured:

  | Layer | Ink vs Photoshop, before / after | Pixel difference, before / after |
  |---|---|---|
  | aa_pin Strong12 | 0.78 / 0.93 | 0.30 / 0.25 |
  | aa_pin Strong30 | 0.89 / 0.96 | 0.16 / 0.14 |
  | psd-tools clip-adjustment.psd ("a", 12 px) | 0.75 / 0.90 | 0.41 / 0.41 |
  | psd-tools layers/type-layer.psd ("A", 30 px) | 0.88 / 0.95 | 0.12 / 0.06 |

  clip-adjustment's "a" keeps a half-pixel x offset from Photoshop that predates Strong.
