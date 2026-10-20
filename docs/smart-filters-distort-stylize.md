# Native Smart Filters: Distort and Stylize

Calibration record for the table-described native Smart Filters. Read it before
touching `src/core/smart_filter_schema.{hpp,cpp}` or
`src/filters/smart_filter_distort_stylize.{hpp,cpp}`. The shared stack, FEid
cache and authoring rules live in [smart-filters-native.md](smart-filters-native.md);
model and UI rules in [smart-objects.md](smart-objects.md). "PS" is Adobe
Photoshop 2026 (27.8), the ground truth; every fact below comes from its COM
captures unless noted.

## Model

- One schema per filter (`smart_filter_schemas()`): the `Fltr` class id and form,
  the integer `filterID`, the descriptor object name, the `Nm  ` entry name, and the
  fields in PS's key order. `SchemaSmartFilter::values` holds one integer per field
  (enums as the option index, booleans as 0/1). Import, authoring, leaf patching,
  render validation, the settings dialog and the Layers panel tooltip all read the
  table; adding a filter means one schema row, one kernel, and its fixtures.
- Import accepts an entry only when its class id, class id form and `filterID` all
  match a schema and every field is present with the schema's type and range; any
  miss keeps the entry unmodeled, so its stack stays preview-locked.
- Edits patch the known leaves of the existing `Fltr` object in place, so unknown
  keys, key order and id forms survive; untouched SoLd bytes re-emit verbatim.
- Ids are charIDs when four characters long and stringIDs otherwise.
- Fields Photoshop stores without a dialog control (random seeds) are `hidden`:
  kept verbatim, never shown.
- These filters have no Filter menu entry in Patchy yet: Patchy renders and edits
  imported entries but cannot add new ones.

## Filters

### Solarize (`Slrz`, filterID `Slrz`)

- No `Fltr` object: the entry is `Nm` "Solarize" (no ellipsis), `hasoptions` false,
  `filterID` 0x536c727a, then PS's trailing `comp`/`compInfo`.
- Straight RGB, per channel: values above 127 become 255 - v. Alpha and bounds are
  unchanged. Byte-exact on a 256x256 value ramp and on soft-alpha content.

### Find Edges (`FndE`, filterID `FndE`)

- No `Fltr` object, like Solarize (`Nm` "Find Edges").
- Byte-exact on noise, ramps and soft alpha. Per pixel and channel:
  1. Matte over white: `m = (c * a + 255 * (255 - a) + 127) / 255`.
  2. 3x3 Sobel on `m` (`gx` right column minus left, `gy` bottom row minus top, weights
     1-2-1). Positions outside the filter canvas repeat its edge; canvas positions
     outside the content are transparent, so they matte to white.
  3. Strength `s = (max * T[min * 256 / max]) >> 14` with `max`/`min` of `|gx|`, `|gy|`
     and `T[i] = round(16384 * sqrt(1 + (i / 256)^2))`: PS's fixed-point hypot, which
     reads up to one level below the true magnitude off the axes. Plain
     `floor(hypot)` matches only 97 % of noise pixels.
  4. Ink `k = min(255, s)`, un-matted from white:
     `out = 255 - min(255, (k * 255 + a / 2) / a)`; alpha 0 writes white.
- Alpha and bounds are unchanged.

### ZigZag (`ZgZg`, filterID `ZgZg`)

- Keys from PS's recorded action: `Amnt` integer (-100..100), `NmbR` integer ridges
  (0..20 accepted, dialog 1..20), `ZZTy` enum `ZZTy`: `ArnC` Around Center, `OtFC` Out
  From Center, `PndR` Pond Ripples. Entry `ZigZag...`.
- Runs over the whole filter canvas (W x H), not the content bounds: an off-center
  Smart Object distorts about the document center and can grow past its bounds.
- Inverse map per canvas pixel (x, y), integer coordinates: `u = (x - W/2) / (W/2)`,
  `v = (y - H/2) / (H/2)`, `r = |(u, v)|`; outside the ellipse (`r >= 1`) nothing moves.
  Profile `s = (1 - r)(1 - cos(2 pi ridges r))`.
  - Around Center: the source turns by `-Amnt * s` degrees in (u, v) space.
  - Pond Ripples and Out From Center render identically in PS 27.8: the source shifts by
    `Amnt * s` pixels along (+1, +1), each axis scaled by its half extent over the
    larger half extent (portrait and landscape probes).
- Source pixels are sampled bilinearly on premultiplied RGBA with the canvas edge
  repeated. PS's own samples jitter about the analytic position (up to 0.3 px on smooth
  fields, more where the field is steep; it likely supersamples), so this is a
  documented tolerance, not byte identity: mean position error 0.01-0.07 px on 16-bit
  coordinate ramps at moderate settings, and on the 64x48 fixtures a mean RGB difference
  of 1.3-2.3 levels (alpha 0.4-1.3), with isolated large differences on noise.

### Wave (`Wave`, filterID `Wave`)

- Keys from PS's recorded actions: `Wvtp` enum `Wvtp` (`WvSn` sine, `WvTr` triangle,
  `WvSq` square), integers `NmbG` generators, `WLMn`/`WLMx` wavelength, `AmMn`/`AmMx`
  amplitude, `SclH`/`SclV` scale percent, `UndA` enum `UndA` (`WrpA` wrap around,
  `RptE` repeat edge pixels), `RndS` random seed (kept verbatim, no dialog control).
  Entry `Wave...`.
- Measured (16-bit coordinate ramps; 256x256, 128x192 and 192x144 canvases): the
  horizontal shift of a pixel depends only on its row and the vertical shift only on its
  column; one generator has period `2 * wavelength` px and amplitude
  `0.78125 * amplitude` px whatever the canvas size; generators add (three equal waves
  reach about twice one wave); a scale of 0 % removes that axis; the seed changes only
  the phases; triangle and square keep the period and amplitude.
- PS draws each generator's wavelength, amplitude and phases from its own random
  generator, which probes cannot reproduce. Patchy draws them from the stored seed with
  splitmix64 (wavelength and amplitude uniform integers in the ranges, two uniform
  phases), so its pattern has PS's units and character but is not PS's pattern. The
  fixture test checks the render is repeatable; `smart_filter_wave_keeps_photoshop_units`
  pins the units. Wrap Around wraps sample positions over the filter canvas; Repeat Edge
  Pixels clamps them to it.

### Ripple (`Rple`, filterID `Rple`)

- Keys from PS's recorded actions: `Amnt` integer (-999..999), `RplS` enum `RplS`
  (`Sml `, `Mdm `, `Lrg `). Entry `Ripple...`. Deterministic: two runs match exactly.
- Measured on 16-bit coordinate ramps: the shift is one fixed noise-like field per size
  times `Amnt / 100` exactly (50, 100, 200 and -100 at Medium project onto one field
  within 0.025 px). Each axis's shift varies fast along that axis and slowly across it:
  zero crossings every 3.5 (Small), 9-11 (Medium) and 20 (Large) px along the axis and
  about 10.5 px across; spread (standard deviation at Amount 100) 0.57, 0.83 and
  2.46 px. The field is broadband and not periodic within 256 px, so it cannot be
  reproduced from probes.
- Patchy renders its own smooth value noise (splitmix64 lattice hashed from document
  coordinates, smoothstep interpolation) with those cells and spreads, linear in
  Amount: PS's character, not PS's pattern. `smart_filter_ripple_amount_zero_is_identity`
  pins the linear scale at 0.

### Shear (`Shr `, filterID `Shr `)

- Keys from PS's recorded actions: `ShrP` list of `Pnt ` objects (double `Hrzn`,
  `Vrtc`), `UndA` enum `UndA` (`WrpA`, `RptE`), integers `ShrS`, `ShrE` (PS writes 0
  and the last point index). Entry `Shear...`. Patchy has no curve editor: the curve,
  `ShrS` and `ShrE` are hidden fields kept verbatim (the list is rewritten only if the
  points change); the dialog edits Undefined Areas and blending.
- Points live in a 128-unit grid. Row y reads a natural cubic spline (zero second
  derivative at both ends, the Curves solver) through the points as a function of
  `Vrtc` at `y * 128 / (H - 1)`; the row moves right by `spline * H / 128` pixels: PS
  scales both axes by the canvas height (portrait probe). Rows outside the first and
  last point hold their values. Wrap Around wraps over the canvas width; Repeat Edge
  Pixels clamps. Bilinear sampling fits better than cubic.
- Calibration: on 16-bit coordinate ramps the shift is within 0.06 px for a line and a
  three-point bend and 0.6 px for a steep four-point curve (functional, not parametric,
  spline). Open item: on the 8-bit 64x48 fixture PS's rows sit up to 0.7 px off that
  model (best fit there is `(y + 0.5) * 128 / H`), so the fixture pins a loose mean
  tolerance (RGB 8, alpha 4.5 levels; actual 7.1 and 3.8, mostly in its noise block).

## Fixtures

`test-fixtures/psd/photoshop-smart-filter-<name>.psd`, self-authored through COM from a
64x48 RGBA probe (gradients, a noise block, soft alpha edges and a soft hole). "Layer 1"
is the filtered Smart Object; "_source" is a hidden copy of its unfiltered pixels, so
`psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render` renders the
stack from `_source` and compares Photoshop's baked layer (byte-exact for Solarize and
Find Edges, a documented mean tolerance for ZigZag, repeatability only for Wave and Ripple,
whose patterns are Patchy's own). Capture scripts write an empty descriptor only for filters without settings;
every other filter passes PS's full recorded key list ([photoshop-com.md](photoshop-com.md)).
