# Select > Color Range

Select > Color Range... (GitHub issue 709; `selectColorRangeAction`, hotkey id `select.color_range`, no default key) selects pixels by their own color: colors sampled from the image, one of six hue bands, or the highlights, midtones or shadows. The result is a soft 8-bit selection, so a background cut out with it has no jagged rim. The binding limits are the "Color Range" entry of [legal-constraints-selection.md](legal-constraints-selection.md); read it before adding a range, a sampling gesture or a preview.

| | |
| --- | --- |
| Core | `src/core/color_range.{hpp,cpp}`: `ColorRangeClassifier`, `color_range_mask` |
| Dialog | `src/ui/color_range_dialog.{hpp,cpp}`: `request_color_range_settings`, `color_range_mask_image`, `color_range_sample_at` |
| Command | `MainWindow::color_range_dialog` (`src/ui/main_window_color_range.cpp`), commit through `CanvasWidget::apply_selection_mask` |
| Script | `doc.selection.selectColorRange(options)` (`src/ui/script_color_range.cpp`) |
| Settings | `tools/colorRangeFuzziness`, `tools/colorRangeSampleAllLayers` (the last accepted values) |

## The mask

`ColorRangeClassifier` turns one display-encoded RGBA pixel into a coverage byte with integer math only (an integer square root, integer hue and luma), so every toolchain gives the same bytes; `color_range_is_deterministic_and_tokens_are_stable` pins a digest of every mode. Nothing looks at a pixel's neighbours or position.

- **Sampled Colors.** For each sample, coverage by the Euclidean RGB distance to it: full up to half the Fuzziness, falling linearly to nothing at the Fuzziness (0 selects exact matches only; the maximum, 200, fades between distances 100 and 200). Added samples combine by maximum, and the result is scaled by 255 minus the strongest subtract sample's coverage. Each sample stays one point with this one linear ramp; samples are never merged, fitted or clustered. At most 32 samples.
- **Reds, Yellows, Greens, Cyans, Blues, Magentas.** The pixel's hue on the integer hexcone (1536 steps a turn) against the band's 60 degree sector: full inside it, fading over Fuzziness x 30 / 200 degrees past each side, so at the maximum a band ends at its neighbours' centers. The result is scaled by the pixel's chroma (max minus min, full from 128 up): grays, black and white are never selected.
- **Highlights, Midtones, Shadows.** The pixel's luma (77 R + 150 G + 29 B, over 256) against the upper, middle or lower third of the range, fading over Fuzziness x 85 / 200 levels past it. Fuzziness 0 gives hard thirds.
- Every mode scales by the pixel's alpha (a clear pixel has no color). Invert flips the result last, so it selects clear pixels.

`color_range_mask` takes an RGB or RGBA `PixelBuffer` at any bit depth. 16 and 32-bit pixels are read as their display-encoded 8-bit colors (`display_rgba8_at`), so a 32-bit linear image is compared the way it looks and a highlight above 1.0 is a highlight. The UI never hands it a deep buffer: it samples the same RGBA8888 pictures the Magic Wand and Object Selection do (`CanvasWidget::automatic_selection_source`: the settled composite with Sample All Layers, otherwise the active layer rendered alone), which already cover 16 and 32-bit, CMYK proof and indexed documents.

Mode tokens (`color_range_mode_token`: `sampled`, `reds`, `yellows`, `greens`, `cyans`, `blues`, `magentas`, `highlights`, `midtones`, `shadows`) are permanent script identifiers; the enum is append-only.

## The dialog

Patchy's own layout: the preview on the left, the settings on the right, OK and Cancel below. `patchyColorRangeDialog` runs through `run_non_modal_dialog` under the preview edit lock, so the canvas can be zoomed and panned and takes the sampling clicks, and nothing else edits the document meanwhile.

- **Select** (`colorRangeModeCombo`): the ten modes. Only Sampled Colors uses samples; the sampling controls and the canvas picker are off for the others.
- **Fuzziness** (`colorRangeFuzzinessSlider`, `colorRangeFuzzinessSpin`): 0 to 200, default 40, remembered.
- **Sample** (`colorRangePickButton`, `colorRangeAddButton`, `colorRangeSubtractButton`): what a click does. Pick starts over from the clicked color, Add adds it, Subtract adds a subtract sample. Shift and Alt choose Add and Subtract for one click. A click samples the mean of the 3 x 3 square around it (`wand_seed_color`, the Magic Wand's averaging); a click on a clear pixel samples nothing. The first sample is the foreground color.
- **Colors** (`colorRangeSampleStrip`, `colorRangeClearSamplesButton`): one chip per sample, newest last, a bar across a subtract sample; Clear removes them all.
- **Result** (`colorRangeResultCombo`): New Selection, Add to, Subtract from or Intersect with the selection there is.
- **Sample All Layers** (`colorRangeSampleAllLayersCheck`, remembered, on by default): the merged image, or the active layer alone. **Invert** (`colorRangeInvertCheck`).
- **Preview** (`colorRangePreview`): the mask in black and white. It is the mask of a copy of the sampled picture no larger than 300 px on its longer side, rebuilt once per setting change or sample and only drawn in `paintEvent`. A click on it samples the document point under it. For automation it carries `selectedPixels` and `maskRevision` properties; the strip carries `sampleCount`.

Canvas clicks arrive through the canvas's dialog-owned read gesture (`set_transient_read_interaction`, the Curves eyedroppers' route) with the eyedropper cursor. One press is one sample: a drag adds nothing. Escape pressed on the canvas cancels the dialog (the canvas is a sibling window, so the key would not reach it otherwise). Nothing is drawn on the canvas and the document's selection is untouched until OK.

OK computes the mask of the full-size picture once and commits it with `CanvasWidget::apply_selection_mask`: the plain boolean with the existing selection (the selection tools' Replace, Add, Subtract and Intersect), exactly as computed, with no tool Feather or Anti-alias applied, as one "Color Range" selection history step. Cancel leaves the selection as it was. The action is refused in Quick Mask (`patchy.quickMaskBlocked`) and during a pointer drag.

## Script

`doc.selection.selectColorRange({range, colors, subtractColors, fuzziness, invert, operation, sampleAllLayers})` runs the same classifier on the merged document (or the active layer with `sampleAllLayers: false`) and combines the mask with the selection through `combine_selection_coverage`, as `selectSubject` does. `range: "sampled"` (the default) needs at least one entry in `colors`; unknown keys, ranges, colors and out-of-range values throw before anything changes. Reference: `scripts/bundled/patchy.d.ts`, `scripts/bundled/scripting-guide.md`.

## Tests

- Core (`tests/core/color_range_tests.cpp`): `color_range_sampled_fuzziness_zero_and_max`, `color_range_sampled_edge_is_soft_and_monotonic`, `color_range_samples_add_and_subtract`, `color_range_hue_bands_select_their_sector`, `color_range_tones_split_the_luma_range`, `color_range_alpha_scales_and_invert_flips`, `color_range_mask_reads_every_depth_and_layout`, `color_range_is_deterministic_and_tokens_are_stable`.
- UI (`tests/ui/color_range_tests.cpp`): `ui_script_select_color_range_selects_by_color` (every operation, the ranges, the soft edge, 16 and 32-bit documents, bad options), `ui_color_range_dialog_samples_previews_and_commits` (canvas and preview clicks, Shift and Alt, one rebuild per change and none per repaint, no selection before OK, one undo step, Cancel, Result), `ui_color_range_cuts_out_a_plain_background` (the use the issue asks for), and the dialog in `ui_offscreen_dialog_sweep_is_themed_in_both_schemes`.
