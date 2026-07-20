# Blend modes

Everything a new blend mode touches, and the calibrated rounding rules. `BlendMode` (core/layer.hpp) is append-only: the enum rides combo item data, casts, and file maps keyed on the existing order — only append (see AGENTS.md gotchas).

## Adding a mode: the full checklist

Adding a blend mode means updating ALL of:

- `blend_math.cpp` — the pixel math.
- `blend_mode_ui.cpp` — display order is decoupled from enum order via combo item data; insert the new mode at its Photoshop menu position.
- The three PSD maps: the 4-char blend key map AND the lfx2 stringID map, in BOTH read and write directions (lfx2 blend modes are written as full stringIDs, never 4-char codes — see [ps-compat.md](ps-compat.md)).
- The Aseprite map in both directions.

## Calibrated math rules

- Non-separable modes (Hue/Saturation/Color/Luminosity) use the PDF-spec set_lum/set_sat algorithm.
- Exclusion rounds the s*d/255 product BEFORE doubling; Divide rounds to nearest. Both verified against Photoshop and Aseprite.
- `aseprite_blend_modes_match_aseprite_render` pins the Aseprite-parity set in-suite.

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
- Recorded deviation found during this calibration: Photoshop's Color Burn and
  Color Dodge round to NEAREST, while Patchy's long-standing kernels floor
  (~16k of 65536 entries differ by one). Changing them would move byte-pinned
  compositor goldens, so they stay as-is until a deliberate re-pin.
