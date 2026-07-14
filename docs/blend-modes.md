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
