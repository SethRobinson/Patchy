# 16/32-bit compositor and fixture corpus

Split from [high-bit-depth.md](high-bit-depth.md): the deep fixture corpus that scores 16/32-bit documents against Photoshop (the Photoshop facts it pinned, the baseline and the current scores) and Phase 3, the deep compositor with the Photoshop 2026 rules the corpus pinned.

## The deep fixture corpus

`python scripts\dev\deep\make_deep_fixtures.py` builds 46 single-feature scenes
(plain ramp, all 27 blend modes, opacity/fill, alpha ramp, gradient layer mask,
clipping, pass-through group, nine adjustment layers, solid and gradient fill layers,
smart object, drop shadow plus stroke, saved alpha channel) with Photoshop COM, each
directly at 8, 16 and 32 bits, into `local-test-fixtures/deep/<scene>/<depth>/`
(gitignored): the PSD, Photoshop's flatten as `render.png` (8-bit sRGB),
`render16.png` (16-bit documents) and `render32.tif` (32-bit documents: uncompressed
little-endian float, linear, no profile). `manifest.json` records each build.
`python scripts\dev\deep\score_patchy.py --label <name>` opens every document in a
Patchy build, exports a render and a resave, and scores them (8-bit metrics, 16-bit
precision, saved depth) into `build/test-output/deep-score/<name>/summary.json`. `--exe`
defaults to `build\release\patchy.exe`, which only releases refresh: pass
`build\opt-dev\patchy.exe` to score a development build.

Photoshop facts the corpus pinned (Photoshop 27.10):

- A 32-bit document takes descriptor and `SolidColor` values as LINEAR light on the
  0..255 scale (250 is stored as 0.980); the generator linearizes scene colors there so
  every depth shows the same picture. Gradients interpolate in linear light at 32 bits,
  so 32-bit midtones legitimately differ from 8/16-bit ones.
- Not available at 32 bits: the Color Burn, Linear Burn, Screen, Color Dodge, Overlay,
  Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix and Exclusion
  blend modes, and the Invert, Posterize and Threshold adjustment layers. Everything
  else in the corpus builds at 32 bits (Dissolve, Lighter/Darker Color, Subtract and
  Divide included).

Baseline (Patchy 089007f7 release build, `baseline-089007f7`), mean over documents:

| Depth | Docs | Byte match | Perceptual | 16-bit precise | Saved at depth |
|---|---|---|---|---|---|
| 8 | 46 | 0.9964 | 0.9963 | - | 46 |
| 16 | 46 | 0.9959 | 0.9973 | 0.2979 | 0 |
| 32 | 31 | 0.4794 | 0.6611 | - | 0 |

The 32-bit row is the cost of compositing converted 8-bit sRGB layers where Photoshop
composites linear light: opacity, masks, gradients, adjustments and most blend modes
come out wrong. The psd-tools corpus barely shows it because its 32-bit files are simple.

The scorer inherits the environment, and `--deep` only forces `PATCHY_DEEP_EDITING=1`:
with the gate on by default, a run without it is deep too, so reproduce the 8-bit
baseline with `PATCHY_DEEP_EDITING=0` set. With the deep compositor (gate on): 8-bit byte 0.9993
(Levels' toe, below), 16-bit byte 0.9996 and 16-bit precise 0.9778, 32-bit byte
0.9995 and perceptual 1.0; every document keeps its depth. Remaining 16-bit precision
gaps: the gradient fill (interpolation within two 16-bit steps), the effects scene (the
same 148 pixels the 8-bit render misses: outside-stroke corners and the shadow beside
them; its byte match is 0.9819 against the baseline's 0.9822, two pixels) and
Exposure (98%).

## Phase 3: Deep compositor

3. **Deep compositor** (compositor done; `tests/core/deep_compositor_tests.cpp`).
   `render/layer_compositor.hpp` is templated on the target's color type
   (`render/composite_color.hpp`: `target_color_t`, `DeepRgb` floats on the deep scale);
   the 8-bit instantiation is the historical code, byte for byte. Deep blend math is
   `core/blend_math_deep`, deep adjustments `core/adjustment_deep` (continuous
   transfers, no 256-entry LUTs; Curves as splines). Effects and gradient colors stay
   8-bit parameters, widened per target (`deep_from_byte`); gradient fill layers render
   a deep raster (`deep_gradient_fill_raster`, linear-light interpolation at 32 bits).
   Encoded targets clamp to 0..255 on store; Linear targets keep any finite value,
   negatives included. `Compositor::flatten_rgba_deep` feeds the display
   (`render_document_rect` narrows) and PNG/TIFF export (16-bit RGBA64). Photoshop
   2026 rules the corpus pinned:
   - Levels above gamma 1 is not the plain power near black: below
     t = 2^(-g - 1/(g-1)) it is a cubic Hermite toe leaving black at slope 2^g and
     meeting x^(1/g) in value and slope at t (`levels_gamma_curve`, all depths; ramps
     at gamma 1.2 to 9.99 match within 3/65535 up to gamma 3).
   - 32-bit Levels maps linear values: (v - black)/(white - black) through a signed
     power, unclamped (negative below the black point). 32-bit Curves applies to the
     linear value times 255.
   - 16-bit Posterize buckets floor(v16 * levels / 65536).
   - Deep Hue/Saturation (`hue_saturation_transfer`): the lightness percent is exact,
     the hue rotation unrounded (h * 4.25 wheel steps), and saturation uses its own
     multipliers (`kDeepSaturationScale`, probed per percent; +20 is 318/256 where 8
     bits use 1.2473). At 32 bits there is no gamut limit and no clamp.
   - 32-bit Luminosity is the PDF SetLum without ClipColor (negatives survive).
   The eyedropper picks deep documents from the deep render; layer thumbnails read
   any depth (`display_rgba8_at`), and mask and saved-channel previews (Layers and
   Channels thumbnails, the mask view) read gray at any depth through `coverage8_at`
   (#540, `ui_flat_alpha_mask_thumbnail_reads_deep_masks`). Large deep renders and flattens split into strips
   under the 8-bit rules (`PATCHY_RENDER_SINGLE_THREADED` included). The recovery
   store writes the same PSB writer and reopens through the normal loader, so it keeps
   depth whenever the gate is on. Testy's Patchy driver splits a 16-bit PNG export into
   `render16.png` (the precision metric) and an 8-bit `render.png`
   (`analyze.split_deep_png`). A deep target caches one `DeepAdjuster` per adjustment
   pass, keyed by `adjustment_pass_serial` as well as the settings address: stacked
   adjustment layers reuse one stack slot for their settings.
