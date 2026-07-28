# Blend modes

Everything a new blend mode touches, and the calibrated rounding rules. `BlendMode` (core/layer.hpp) is append-only: the enum rides combo item data, casts, and file maps keyed on the existing order - only append (see the universal invariants in `AGENTS.md`).

## Adding a mode: the full checklist

Adding a blend mode means updating ALL of:

- `blend_math.cpp` — the pixel math.
- `blend_mode_ui.cpp` — the name switch AND the `kBlendModes` array. Display order is decoupled from enum order via combo item data; insert the new mode at its Photoshop menu position. `BlendModeMenu::Filter` drops the modes a recipe or Smart Filter step cannot execute.
- The three PSD maps: the 4-char blend key map (whose read direction also carries the CS-era descriptor charID aliases like 'Drkn') AND the lfx2 stringID map, in BOTH read and write directions (lfx2 blend modes are written as full stringIDs, never 4-char codes — see [ps-compat.md](ps-compat.md)).
- The Aseprite map in both directions.
- `script_api.cpp` — `kBlendModeIds` is INDEXED BY THE ENUM ORDINAL, so its size literal has to grow with the enum and the new id appends at the end. Scripts hard-code these strings.
- `svg_io_internal.hpp` — `blend_mode_css` has a `default:` returning the empty string, which is what raises the SVG rasterization barrier. Nothing to do unless CSS can express the mode.
- `af_document_io.cpp` — read-only Affinity id map. Unmapped ids already fall back to Normal plus a notice, so leave an id alone unless a real Affinity file confirms it.
- `recipe_blend_mode_supported` (`filters/filter_registry.hpp`) — decide explicitly whether recipes and Smart Filters can execute the mode. This used to be an ordinal range ending at `Divide`, duplicated in `filter_look_library.cpp`, and went stale the moment the enum grew: the combos offered Vivid Light and friends while the guard silently rejected them.
- `translations/patchy_ja.ts` — the display name.

Most of the exhaustive `switch (mode)` maps are caught by `-Wswitch` because they have no `default:`. The ones that are NOT: the `if`-chains in `blend_mode_from_key` and `blend_mode_from_descriptor_enum`, the `kBlendModes` array, and `kBlendModeIds`. Check those four by hand.

## Calibrated math rules

- Non-separable modes (Hue/Saturation/Color/Luminosity) use the PDF-spec set_lum/set_sat algorithm.
- Exclusion rounds the s*d/255 product BEFORE doubling; Divide rounds to nearest. Both verified against Photoshop and Aseprite.
- Color Burn and Color Dodge round their quotient to NEAREST half-up (the
  `(2a+b)/(2b)` form, same as Aseprite's DIV_UN8), and the 0/0 division corner
  follows the destination: Burn returns 255 at d=255 even when s=0, Dodge
  returns 0 at d=0 even when s=255 (the special-case ORDER matters — checking
  s first was the one residual mismatch). Calibrated July 2026, bit-exact on
  the full 256x256 Photoshop 2026 captures (they used to floor, off by one on
  ~16k entries each); pinned by
  `blend_math_color_burn_dodge_match_photoshop_captures`. The special-Fill
  256-scale Burn/Dodge kernels in `composite_special_fill_rgb` are calibrated
  separately and were deliberately left unchanged.
- `aseprite_blend_modes_match_aseprite_render` pins the Aseprite-parity set in-suite.

## Group compositing: Pass Through, isolation, and group opacity

Calibrated against a Photoshop 2026 COM-authored fixture (July 2026,
`test-fixtures/psd/photoshop-group-opacity.psd/.bmp`, pinned within 1/255 by
`psd_photoshop_group_opacity_fixture_matches_render`). The group branch lives
in `composite_layer` (src/render/layer_compositor.hpp); the UI renderer routes
every blend-if, masked, non-pass-through, or faded group through it
(src/ui/image_document_io.cpp), which also covers Merge Group and Merge Down.

- **Pass Through group opacity is a post-composite fade.** Children composite
  at full strength against the true backdrop (child blend modes and interior
  adjustments keep meeting the layers below), then the whole result
  interpolates back toward the pre-group backdrop by the group opacity: the
  PDF non-isolated-group alpha formula. One fade applies to the composite, so
  overlapping children never double-fade. Implemented as `CompositeSnapshot` +
  `fade_toward_snapshot` + a `store_color` overwrite primitive (source-over
  cannot reduce coverage). The fade covers the full clip because an interior
  adjustment with unlimited bounds can touch backdrop pixels outside the
  children's render bounds. A group raster/vector mask still attenuates each
  child contribution in place; the fade applies once on top.
- **Non-pass-through groups isolate.** Children composite into a transparent
  `IsolatedClipGroupTarget` (bounded by the children's render bounds when no
  overrides are active) and the merged result meets the backdrop with the
  group's blend mode, opacity, and mask: a Multiply child inside a Normal
  group no longer sees the backdrop. Groups default to `BlendMode::Normal` in
  core, so tests that want Photoshop's default folder behavior must set
  `BlendMode::PassThrough` explicitly (the UI folder command and PSD import
  do).
- **Blend-if groups always isolate** (calibrated separately; see the blend-if
  group tests). A Pass Through group WITH blend-if keeps that path, where
  opacity is an alpha multiply on the merged result.
- **Every compositor target must implement `store_color`** (direct overwrite
  of straight color + coverage) alongside `composite_color`/`sample_color`:
  Rgb8PixelBufferTarget, QImageCompositeTarget, the two BMP render targets,
  Rgba8FlattenTarget, IsolatedClipGroupTarget, and the forwarding
  GroupMaskedTarget.
- Photoshop Knockout (shallow/deep) is not modeled. The single-pixel merged
  sampler `compose_layer_pixel` (src/ui/canvas_widget_render.cpp) still
  ignores group opacity and child blend modes (pre-existing approximation).

## July 2026 modes (Vivid/Linear Light, Hard Mix, Darker/Lighter Color)

Calibrated against full 256x256 Photoshop 2026 flatten captures (crossed gray
gradients per mode, COM-scripted; `blend_math_new_modes_match_photoshop_captures`
pins sampled triples in-suite). The capture tables and the COM script that
regenerates them live machine-local in `local-test-fixtures/ps-blend-captures/`.
The pinned kernels, all bit-exact on the capture except where noted:

- **Vivid Light** is NOT the textbook burn(2s)/dodge(2s-255): the burn half
  doubles the source as round(s*255/128) and rounds its ramp half DOWN; the
  dodge half doubles as round((s-128)*255/127) and rounds half UP.
- **Linear Light** is d + 2s - 256 (not the textbook -255), clamped.
- **Hard Mix** thresholds the TEXTBOOK floor-rounded vivid light at >127, not
  Photoshop's own vivid-light kernel (yes, really - both pinned by capture).
- **Darker/Lighter Color** compare rounded 0.3/0.59/0.11 luma and keep the
  destination on ties. Gray inputs are exact; on color inputs a handful of
  exact half-luma boundary pixels (21 of 131072 captured) differ because
  Photoshop's float evaluation splits halves inconsistently. Deliberate: the
  integer rule is toolchain-deterministic.
- These five are absent from Aseprite's format: the .ase writer marks them
  lossy (Normal), like LinearBurn/PinLight.
- **Special-Fill for the three light modes** (calibrated July 2026 against
  256x256 Photoshop 2026 flatten captures at Fill 1/10/25/40/49/50/51/60/75/90/99
  percent, bit-exact on every capture; `blend_mode_has_special_fill` now covers
  all eight of Photoshop's special modes). With `fb = lround(fill * 255)`:
  - **Fill 0 is identity for all three** (Photoshop skips the invisible
    layer). Only Linear Light needs the explicit guard: its kernel's -1
    constant would otherwise darken by one.
  - **Linear Light** is `clamp(d + round((2s - 255) * fb / 255) - 1)`. The -1
    reveals the fade's true neutral is 127.5, not 128 (at low Fill the s=128
    row darkens d by exactly 1); at fb=255 the formula collapses to the pinned
    `d + 2s - 256`.
  - **Vivid Light** fades each half's 100%-kernel integer term, NOT the source
    byte: dodge divisor = `255 - round(round((s-128)*255/127) * fb/255)`, burn
    doubled = `255 - round((255 - round(s*255/128)) * fb/255)`; the ramps keep
    their 100% rounding (burn half down, dodge half up).
  - **Hard Mix below 100% Fill is a steep ramp, not a threshold**:
    `clamp(round((d - A) * 255 / (255 - fb2)))` with anchor
    `A = round((255-s) * fb2 / 255)` and `fb2 = fb - (fb >= 128)`
    (equivalently `round(fb * 254/255)`; the nine-fill sweep pinned the step
    uniquely, fill 49% -> fb2 125 vs fill 50% -> fb2 127).
  - The alpha split is the shared special-Fill one: the blend result carries
    the full `coverage x opacity` weight while Fill scales only output-alpha
    growth and the transparent-backdrop source term. COM probes (vivid at
    Fill 50 under opacity 50 and under a uniform mask) match
    `lerp(d, kernel, coverage)` exactly. Note Photoshop quantizes opacity to a
    byte before weighting (50% = 128/255); Patchy keeps float opacity, a
    pre-existing global <=1/255 divergence.
  - Pinned by `blend_math_light_modes_special_fill_match_photoshop_captures`
    and the light-mode rows of `compositor_fill_opacity_matches_photoshop_modes`.
    The capture PNGs (`cal_fill_*.png`) and `calibrate_fill_blends.ps1` live
    machine-local in `local-test-fixtures/ps-blend-captures/`.

## Dissolve (July 2026)

Dissolve is the one mode that is NOT a colour function. Coverage becomes the
probability that a pixel is painted at all: the pixel is either fully painted
through Normal or not painted, so a Dissolve layer never produces a blended
intermediate value. It therefore cannot live in `blend_rgb`, which takes
neither alpha nor a coordinate. `blend_rgb` returns the source for Dissolve so
that the callers with no pixel coordinate (the eyedropper's
`compose_layer_pixel`, the filter fade loops) degrade to Normal.

The math is `dissolve_coverage(x, y, alpha, field)` in `blend_math.cpp`, and the
compositor applies it. **Patchy's noise field is its own, not a reconstruction of
Photoshop's** — a compatible rendering, the same standing as Add Noise, Mosaic
and Plastic Wrap. The threshold is `splitmix64(x | y << 32 ^ salt)` compared on
an explicit 24-bit integer scale against `lround(alpha * 2^24)`, which keeps the
uniform mapping exact on every toolchain (the AGENTS.md determinism invariant)
and makes coverage monotone in alpha, so raising Opacity only adds pixels
instead of reshuffling the pattern.

- **The field is a pure function of the DOCUMENT coordinate.** That is what lets
  a dirty-rect repaint, a strip-parallel render and a full flatten all reproduce
  the same pattern; `image_document_io.cpp` states the clip-equals-full
  invariant the patch machinery depends on. Never make this stateful, and never
  key it on a layer-local or buffer-local coordinate.
- **Each dissolved effect uses its own `DissolveField` salt**, so a dissolved
  drop shadow and a dissolved outer glow on one layer do not dither onto
  identical pixels. The layer's own pixels, its vector stroke, and a Dissolve
  group's merged result all share `DissolveField::Layer`.
- Applied at: the base pixel pass and the vector stroke pass, both
  `IsolatedClipGroupTarget` merges (a Dissolve GROUP dithers its merged result,
  so children still composite against each other at full strength),
  `composite_adjustment_layer`, and the two effect choke points
  `composite_effect_color` and `fold_effect_color`. The inline satin fold in the
  base pass handles it separately, deliberately: routing that fold through
  `fold_effect_color` would also give it the burn/dodge opacity pre-fold and
  move pinned satin bytes.
- **Fill and Opacity are not special-cased.** Dissolve is not one of Photoshop's
  eight special-Fill modes, so both simply compound into the probability
  (`coverage x opacity x fill`), which is what the compositor already computes.
- **Clip coverage stays undithered.** The threshold runs after
  `record_clip_coverage`, because a clipping run is masked by the base's
  transparency and not by what the base actually painted (ps-compat.md).
- Adjustment layers otherwise ignore their blend mode entirely. Dissolve is the
  one mode honored there, because it is a coverage decision: the adjustment
  lands whole on a dithered subset of pixels.

Three deliberate divergences from Photoshop:

- **Zoomed out, the canvas shows a wash rather than noise.** `display_mip_cache_`
  smooth-downscales a full-resolution composite, so below 100% zoom the dither
  box-filters into uniform partial-opacity grey. Photoshop dissolves at screen
  resolution. Flatten and every export are correct; this is preview-only, and
  there is no reduced-resolution composite hook to intercept.
- **Inside a Smart Object the field is Smart-Object-local**, since the nested
  document composites in its own coordinate space. Moving the Smart Object on
  the parent canvas does not re-dither its interior.
- **Recipes and Smart Filters exclude Dissolve** (`recipe_blend_mode_supported`).
  Their blend step mixes through integer weights rather than making a coverage
  decision, so Dissolve there would need its own design; the filter combos hide
  it instead of offering a mode that gets rejected on save.

Aseprite has no dissolve, so the .ase writer marks it lossy (Normal) like the
other unrepresentable modes. Pinned by
`blend_dissolve_coverage_is_deterministic_and_uniform`,
`compositor_dissolve_is_anchored_to_document_coordinates`,
`compositor_dissolve_compounds_fill_and_opacity`,
`compositor_dissolve_dithers_layer_effects` and the UI suite's
`ui_dissolve_clipped_render_matches_full_render`.
