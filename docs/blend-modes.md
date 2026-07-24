# Blend modes

Everything a new blend mode touches, and the calibrated rounding rules. `BlendMode` (core/layer.hpp) is append-only: the enum rides combo item data, casts, and file maps keyed on the existing order - only append (see the universal invariants in `AGENTS.md`).

## Adding a mode: the full checklist

Adding a blend mode means updating ALL of:

- `blend_math.cpp` — the pixel math.
- `blend_mode_ui.cpp` — display order is decoupled from enum order via combo item data; insert the new mode at its Photoshop menu position.
- The three PSD maps: the 4-char blend key map (whose read direction also carries the CS-era descriptor charID aliases like 'Drkn') AND the lfx2 stringID map, in BOTH read and write directions (lfx2 blend modes are written as full stringIDs, never 4-char codes — see [ps-compat.md](ps-compat.md)).
- The Aseprite map in both directions.

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
- Known gap: Photoshop treats Vivid/Linear Light and Hard Mix as special-Fill
  modes (its "eight special modes"); Patchy's `blend_mode_has_special_fill`
  deliberately does not include them yet, so fill-opacity behavior below 100%
  is uncalibrated for the three light modes.
