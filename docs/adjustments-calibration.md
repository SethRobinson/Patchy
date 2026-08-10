# Adjustment calibration vs Photoshop

Calibration record for adjustment-layer and auto-adjustment math: Brightness/Contrast (legacy and modern), Curves, the auto adjustments, and Hue/Saturation. Read this before touching src/core/adjustment_layer.* or src/filters/auto_levels_math.*.
Conventions: "PS" = Adobe Photoshop 2026/27.8, the installed ground truth; every rule is pinned by PS COM captures unless noted. Fixtures named `photoshop-*` live in `test-fixtures/psd/`; `local-test-fixtures/` is machine-local. The COM workflow lives in [ps-compat.md](ps-compat.md).

## Brightness/Contrast legacy calibration (July 2026)

Nine 256-gray-ramp captures pin `brightness_contrast_channel_value` (core/adjustment_layer.cpp):

- c = 0: `clamp(v + b)`.
- 0 < c < 100: brightness folds into the INPUT: `clamp(lround((v + b - 127.5) * 100/(100-c) + 127.5))`. The slope is `100/(100-c)`, NOT the destructive filter's `(100+c)/100`.
- c = 100: hard threshold `(v + b) >= 127 ? 255 : 0`.
- c < 0: contrast compresses FIRST, brightness adds to the OUTPUT: `clamp(lround((v - 127.5) * (100+c)/100 + 127.5) + b)`.

The hybrid order (brightness inside for positive contrast, outside for negative) is required by the interaction captures; residual +/-1 (PS's own LUT has sub-LSB noise). Do not "unify" this with the destructive `patchy.filters.brightness_contrast` in either direction: the destructive output is byte-pinned, and the adjustment layer must render PS's math because it round-trips as a native legacy `brit` record.

## Modern Brightness/Contrast (July 2026)

The default (useLegacy=false) algorithm is fully modeled, recovered from 300 16-bit ramp captures (PS computes in its 15-bit 0..32768 space), 150 contrast captures, and a 451-curve 8-bit sweep (captures/scripts in `local-test-fixtures/ps-bc-captures/`; implementation with exact constants in core/adjustment_layer.cpp):

- `result = contrast(brightness(v))` on the unit interval, one final rounding per depth (compose the real-valued curves, not byte LUTs).
- 0 < b <= 100: gain ray `sigma = 2^(b/110)` until output 0.5, then one cubic Hermite to (1, 1) with end slope `tau = max(0.1, 1/(1 + 12*(sigma-1)))`, clamped to 1 (legitimate overshoot for b > 88, where the tau floor engages).
- b > 100: `f_b = f_{b-100} o f_100`. b < 0: the exact functional inverse of `f_{-b}`, evaluated by fixed 64-step bisection (deterministic; NOT a mirrored construction).
- Contrast: `beta = 1 - 0.0076*c`; lower half `y = (2-2*beta)*u^2 + beta*u`, upper half mirrored through (0.5, 0.5). Exact for c in -50..100.
- The stored `means` value is inert at render time (dialog Auto metadata); Patchy preserves imported values, writes 127 otherwise. `sigma` multiplies out the literal 110th root of 2 (1.006321233202252) per step instead of calling `exp2`, for determinism.
- Validation: all 450 15-bit captures within 1 LSB15; 449 of 452 8-bit curves byte-exact, the rest within 1/255.

## Curves 2.0 engine and editor

- `CurvesAdjustment`: independent ordered control points for Composite RGB, R, G, B; 2-19 points, values 0-255, duplicate inputs resolve deterministically, first/last input may move inward; outside them the LUT clamps to that point's output.
- `build_curve_lut` is the ONLY point-curve interpolation path: natural cubic through the points, zero second derivative at both endpoints, clamped outside movable endpoints, rounded to nearest byte. `build_curves_lut` applies component tables first, then Composite. All Curves consumers (destructive, adjustment render, thumbnails, editor graph) use these LUTs; ramp calibration matched all 3,072 measured bytes exactly.
- Old Patchy 3-value data loads as Composite anchors at 0/128/255; `plAD` v4 may carry the `CRV2` v1 tail (bounded to 324 bytes; malformed tails fall back to the anchors). Editable saves migrate to native `curv` and OMIT `plAD` (PS warns on a second private adjustment block); malformed native `curv` stays on the opaque Pixel path retaining both raw blocks. Levels and Hue/Saturation followed as the last `plAD` writers (native `levl`/`hue2` carry every modeled value); the Levels dialog's channel tab persists only when importing legacy files (fresh saves reset to composite, matching PS).
- Native `curv` shape: zero map-mode byte, version 1, big-endian u32 changed-channel bitmap (despite Adobe's table saying two bytes), implicit point records, indexed `Crv ` version 4 extension, padded to four bytes. Channel ids/bits: Composite=0, R=1, G=2, B=3; points are output then input; identity = bitmap/count zero, 20-byte payload. Patchy writes that shape, keeps untouched payloads byte-for-byte, regenerates only after a modeled edit; native blocks are authoritative over `plAD`. COM probes round-trip Patchy files as editable `LayerKind.CURVES`.
- Editor: four-channel histogram, one box-averaged sample per 2x2 block over the full image (integer means over opaque pixels only, all-transparent blocks skipped). The light 2x2 kernel fills codes missing from quantized sources like PS's cache-level-2 histograms; a larger kernel over-concentrates noisy channels into a few towering bins that crush the rest of the linear display (observed on the blue channel of a warm-lit 24MP photo). The composite is the sum of the per-channel counts, and the display height is `sqrt(count / max_bin)`: PS's Histogram panel is linear but its Curves/Levels dialogs compress with a square root (fitted August 2026 against dialog renders of a known histogram with a 43x-mean spike at bin 0; linear crushed the tail, log overfilled it). Histogram columns draw unantialiased and inset 1px from the graph frame so a clipping spike in bin 0 or 255 stays visible instead of vanishing under the border. Dialog Auto consumes the same averaged histograms, and the Levels dialog shares the sampler over the active layer. Re-editing an unclipped layer samples the layer-tree prefix below it (Auto sees the input); Auto is disabled for clipped adjustments. `.acv` Load accepts version 4 counted RGB, legacy version 1 bitmaps, and the indexed `Crv ` extension; Save writes PS's five-curve RGB shape with the trailing identity compatibility curve.

## Auto adjustments calibration (August 2026)

Six self-authored ramp/outlier fixtures through PS COM (`autoLevels()`, `autoContrast()`, the `Lvls` event) vs Patchy's `auto_levels_math`:

- **Auto Tone (per-channel) byte-exact on five of six fixtures**; PS leaves a constant channel untouched, matching Patchy's degenerate-scan identity rule.
- **Auto Contrast is merged-histogram composite semantics** (color relationships preserved), within maxAbs 2 of Patchy's prediction.
- **PS's effective clip thresholds exceed 0.1% on small images** (a 0.18%-per-end probe still clipped; bounded below 0.39%); not chased. Patchy keeps `max(1, samples/1000)` so menu commands and the Levels dialog Auto agree internally.
- **Auto Color is not flag-invocable**: `Lvls` with `AuCo` reproduces monochromatic-contrast output; ScriptListener records menu Auto Color as explicit computed values. Patchy's midtone snap follows PS's documented Auto Options defaults (midtones 128 neutral, shadows 0, highlights 255); the snap formulation is Patchy's own design. Photoshop-like, not pixel identity.

Capture scripts are session-scratch; conclusions are pinned by the core auto-adjustment tests.

## Hue/Saturation calibration (July 2026)

Calibrated with byte-patched `hue2` probes (patch the header triple, PS flatten to BMP, fit each stage by exact interval arithmetic). Shared helpers and the measured tables (`photoshop_lightness_value`, the 1530-step wheel, `photoshop_hsl_reconstruct`, `kColorizeSaturationScale`, `kColorizeHueInterp`, `kMasterSaturationScale`) live in src/core/adjustment_layer.cpp.

### Colorize

- hue2 header = version(=2) u16, colorize u8, pad u8, colorize h/s/l i16 x3, master h/s/l i16 x3; hue stored -180..180 (raw 203 also accepted); colorize-off files still carry (0, 25, 0). Fresh layers append six default hextant band records plus an undocumented 36-byte trailer of (k*60, 100, 50) triples; Patchy writes that exact template (`kPhotoshopHueSaturationDefaultTail`) and patch-in-place preserves imported bands/trailer byte-identically.
- Pipeline (`apply_colorize`): `li = (max+min)>>1`; lightness blends toward white/black and ROUNDS; `delta = min(li, 255-li) * kColorizeSaturationScale[s]` (PS's percent conversion sits ~0.05-0.2% BELOW s/100, hence a per-percent table); `q = li + floor(delta + 0.5)` but `p = li - floor(delta)` (the round/truncate asymmetry reproduces PS's alternating 2l/2l+1 channel sums); `mid = p + floor((q-p) * kColorizeHueInterp[hue]/255 + 0.5)`. `kColorizeHueInterp` is a measured per-degree table (adjacent degrees quantize together, values pin near sector boundaries, sinusoidal deviation up to ~5/255, sectors NOT symmetric; no closed form, all 360 degrees probed).
- Accuracy: 99.62% of 1.04M probed pixels byte-exact, residuals +/-1 (worst +/-2 on 0.16%). Master sliders are ignored by PS while colorize is on. The 'HStr' action path renders identically to the file path. Fixture `photoshop-hue-saturation-colorize.psd`.

### Hue/Saturation master (colorize off)

Calibrated from a 256x284 probe canvas (gray ramp, the full 32,640-entry lightness/chroma grid, three hue strips) under 763 byte-patched slider triples; harness `local-test-fixtures/hue-sat-master-probe/`.

- Stage order (`apply_hue_saturation`): LIGHTNESS runs FIRST and PER CHANNEL, then integer HSL read (`light = (max+min)>>1`), saturation scales the half-chroma, hue rotates the wheel position, shared reconstruction rebuilds the triple.
- Lightness is BYTE-EXACT: PS quantizes the percent to a byte first (`step = |lightness| * 255 / 100` truncated), then rounds the scale toward 0 or 255. The plain `|lightness|/100` form drifts by 1 on 188 of 201 percents (they agree on multiples of 20, which is why colorize probes at 0/+-40 never saw it). Colorize shares this helper.
- Saturation multiplies the half-chroma via `kMasterSaturationScale` (201 entries, indexed delta + 100), fitted by maximum-agreement interval overlap. NEUTRALS ARE NEVER TINTED (`max == min` returns early). No closed form reproduces the table: -100 is exactly 0, 0 exactly 1, -50 exactly 0.5 and +50 exactly 2.0, but +40/+60 sit measurably BELOW `1/(1 - s/100)`, and +100 is 128, not unbounded (PS leaves a chroma-1 midtone at half saturation). Refuted candidates, do not re-derive: `1 + s/100`, `1/(1 - s/100)`, `1/(1 - kColorizeSaturationScale[s])`, `(100+s)/(100-s)`, any HSV-saturation variant.
- The saturation limit clamps the RECONSTRUCTED BYTES, never the incoming chroma: `half_chroma = min(half * ratio, max(min(light, 255-light), half))`. Because `light` floors, normalized saturation legitimately exceeds 1 for `{min == 0, max odd}` and `{max == 255, min even}`; clamping at 1 there breaks the identity, and dropping the `max(base, ...)` limit overshoots saturated dark pixels by up to 17/255.
- Hue rotates by a WHOLE number of wheel steps, `floor(degrees * 4.25 + 0.5)`, not continuous `4.25 * degrees`. Do NOT reuse `kColorizeHueInterp` here: degree-indexed, deliberately non-monotone; inverting it breaks the shift-zero identity.
- Identity: all sliders zero is a BYTE-EXACT identity over the whole RGB cube (bought by the asymmetric q/p rounding); PS's own zero-slider render wobbles by 1 on about half the wheel. Patchy is deliberately the cleaner.
- Accuracy: no probed pixel off by more than 2/255 (61% byte-exact, 97% within 1). Fixture `photoshop-hue-saturation-master.psd` + `.bmp`.
- Destructive Image > Adjustments > Hue/Saturation routes through the same core math (`apply_hue_saturation_to_pixels`, filter_workflows.cpp), bit-identical; Affinity `HsRA` import maps onto this model.

### Per-hue-range bands

Each of the six band records = four i16 range stops in wheel order + an i16 h/s/l triple.

- Weight: a trapezoid over the four stops (0 outside, ramps between outer and inner stops, 1 between the inner pair), linear in degrees, wrapping past 360 (reds default 315/345/15/45); `outer == inner` = hard step.
- Selection is by the pixel's ORIGINAL hue; a master hue rotation does not move it; neutral pixels are never touched.
- Hue: rotations ADD, converted to wheel steps PER BAND (`floor(weight * degrees * 4.25 + 0.5)`), then added to the master rotation.
- Saturation: each band contributes `weight * (kMasterSaturationScale[percent] - 1)`; contributions SUM; `1 + sum` multiplies the master ratio. A full-weight band equals the master slider exactly. Do not multiply per-band ratios or sum percents into one lookup (weight 0.5 at +50 measures ratio 1.5, not `table[+25]`).
- Lightness is NOT the master blend: positive collapses chroma toward the max channel (`min' = min + (max-min) * L/100`), negative toward the min channel (`max' = max + (min-max) * |L|/100`). Overlapping bands SUM weighted percents and apply once. Band lightness runs BEFORE master lightness.
- Accuracy: plateaus within 2/255; feather RAMPS carry up to 7/255 (the reds ramp-in runs a constant 6 wheel steps behind PS, unexplained; ramp-out within 1). Fixture `photoshop-hue-saturation-bands.psd`/`.bmp`: max 7, mean 0.13, 98% within 2, pinned at those bounds.
- Round trip: the six band records are written from the model (header plus 84 bytes); only the undocumented 36-byte trailer stays patch-in-place, so an unedited layer resaves byte-identically.

