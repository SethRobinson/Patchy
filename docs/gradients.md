# Gradients and Photoshop GRD presets

Patchy uses `GradientDefinition` for reusable gradient content and `LayerStyleGradient` for placement. A definition contains the name, Solid or Noise form, Photoshop smoothness (`Intr`, 0-4096), independent color and opacity stops, destination-stop midpoints, and dynamic foreground/background roles. Noise definitions keep the Photoshop seed, roughness, transparency and color-restriction switches, RGB/HSB/Lab model, and four minimum/maximum channel ranges.

Layer-style placement adds Linear, Radial, Angle, Reflected, and Diamond geometry, angle, scale, reverse, dither, interpolation method, Align with Layer, and X/Y percentage offsets. The Stroke effect alone adds Shape Burst (descriptor stringID `shapeburst`), which follows the stroke band's distance field and ignores angle, scale, offsets, and alignment; see docs/ps-compat.md for the calibrated mapping. Preset selection replaces only the definition. It must not overwrite any placement field. Dynamic foreground/background stops stay live in the Gradient tool; layer-style preset selection resolves them from the current Patchy colors before the style is stored in the document.

## Rendering

Imported 8/16-bit CMYK, grayscale and Lab gradients retain their original
descriptors and document mode on eligible layered saves:
[native color preservation](psd-native-color.md). Their RGB editing preview
still interpolates the converted stops.

- Classic applies the stored smoothness as cubic interpolation after destination-stop midpoint remapping.
- Perceptual interpolates in OKLab.
- Linear interpolates in linear-light RGB.
- Noise and dither use fixed integer hashing. Do not replace this with a standard-library random distribution because output must remain identical across toolchains.
- Deep layers: the Gradient tool paints float colors through `EditOptions::deep_primary` (no banding), and gradient fill layers render a deep raster (`deep_gradient_fill_raster`, linear-light interpolation at 32 bits); see [high-bit-depth.md](high-bit-depth.md).

`gradient_position` is the shared point-mapped-style geometry function; Shape Burst does not go through it (the stroke renderer derives its position from the band's Euclidean distance field, `stroke_alpha_mask`'s optional plane). Linear and Reflected spans use the layer rectangle projected onto the selected angle, so 90-degree gradients span the layer height rather than its width. For `Align with Layer`, Gradient Overlay and gradient Stroke use the source's nonzero-alpha bounds; PSD channel padding must not compress the visible range. The local alpha bounds are cached by the layer's globally unique pixel revision because finding them is an O(width * height) scan. Transient render pixel overrides bypass that cache. `gradient_color`, `gradient_stop_opacity`, and `gradient_color_dithered` are shared by layer effects and preset thumbnails.

Dynamic Vector Preview passes full canvas/fill/stroke paint bounds separately
from each vector raster clip. Layer-effect paint bounds use its scoped render
context. Tile boundaries never become gradient anchors; ordinary document bakes
keep the original defaults. See [vector-preview.md](vector-preview.md).

## GRD files

`src/psd/grd_io.*` reads and writes Photoshop `8BGR` version 5 files containing a version-16 `GrdL` descriptor. It supports solid `CstS`, noise `ClNs`, dynamic `FrgC`/`BckC` stops, ZString display names, and the trailing `8BIMphry` hierarchy. Imports are limited to 32 MiB, 4096 gradients, and 256 stops per list. A damaged tail may return the valid decoded prefix with warnings; structural damage before the first usable gradient is an error. Gray `Grsc` stops read `Gry ` as Photoshop's black percentage (100 = black), the same convention as lfx2 gray effect colors.

The application library lives under the settings directory's `gradients/` folder. Each entry is one single-gradient `.grd` plus a JSON sidecar with its fixed storage id, canonical name, and folder path. Default ids and English names in `src/core/gradient_presets.cpp` are persisted and append-only. New defaults need a new `introduced_version` and a `kDefaultGradientsVersion` bump; never rename or reuse an existing id.

The quick picker (`src/ui/gradient_preset_popup.{hpp,cpp}`) and Gradient Manager read the same `GradientLibrary`. The quick picker anchors to the layer-style Preset buttons, the gradient toolbar's Presets button, and the Edit Gradient Stops dialog's Preset button; its Manage Gradients button falls through to the Gradient Manager. The Gradient tool has no definition-backed state: applying a preset there resolves foreground/background stops from the current colors and flattens the definition into sampled stops (33, or 65 for Noise), so the applied result is static. Manager writes are immediate. Restore repairs changed or deleted built-ins without deleting user gradients. Import deduplicates identical name/payload pairs but permits equal names with different definitions. Folder and subtree export includes matching `phry` markers.

## Stop editor widget

`GradientStopsEditorWidget` is callback-driven: it never mutates its own stop vectors; hosts copy-and-sort, and must never sort the working vectors in place: an in-flight tag or midpoint drag holds an index into them. Photoshop `Mdpn` belongs to the destination/right stop; the first stop's midpoint is unused.

## PSD layer effects

Gradient Overlay `GrFl` and gradient Stroke `FrFX` share the definition codec but have different required descriptor shapes. Untouched imported `lfx2`/`lmfx` remains byte-preserved. Once edited, writers must retain Photoshop's key order and types documented in `docs/ps-compat.md`, including `Grad`, `Angl`, `Type`, `Rvrs`, `Dthr`, interpolation, `Algn`, `Scl`, and `Ofst`.

Factory reset writes a copied library entry first. In-memory gradients change only
after the save succeeds, so a write failure leaves the current library intact.

## Gradient fill layer geometry

Gradient Fill layers and gradient-filled shapes (`GradientSpanBasis::CenterChord` in `gradient_position`) span the center chord of the aligned bounds (docs/vector-tools.md). Photoshop 2026 COM probes (October 2026; 95 fills, 4x4 to 64x64 and non-square canvases, five types, scale and offsets) pinned two further rules for Linear, Reflected and Radial:

- Each pixel samples at its top-left corner (x, y), not its center. On a 4x4 canvas the half pixel is an eighth of the ramp.
- At 100% scale with no offset the ramp runs between the chord ends truncated to whole pixels: Linear between both truncated ends, Reflected and Radial from the truncated center to the truncated far end. The effective angle follows those integer points, so a nominal 30-degree reflected fill runs at 45 degrees on 4x4, 36.87 on 8x8, 32 on 16x16 and 30.7 on 64x64. Every such probe matches within 1/255. Scaled or offset fills keep the continuous ends, which fit those probes better. `gradient_fill_layer_geometry_matches_photoshop_probes` (tests/core/vector_raster_tests.cpp) pins both rules.

A fill layer without a vector mask aligns to the layer's bounds, which in Photoshop are its user mask's visible samples when the mask hides the rest of the canvas (default color 0): psd-tools' 32-bit `gradient-fill.psd` ramps over the mask's rows 6..100, not the 150-pixel canvas (`fill_layer_mask_bounds` in src/core/vector_raster.cpp, pinned by `gradient_fill_layer_aligns_to_its_mask_bounds`; at 32 bits the result matches Photoshop within 2 levels).

Not modeled: Angle and Diamond (they keep center sampling), and offsets on very small canvases (no candidate rule fit a 16x8 probe). This geometry is what took `photoshop-shape-gradient.psd` from mean error 1.22 to 0.29 against Photoshop and fixed psd-tools' `colormodes/4x4_*` files.

## Noise gradients in PSD fill layers

Noise `ClrS` enum values are `RGBC`, `HSBl`, and `LbCl`. The color-object
classes `HSBC` and `LABC` are not the HSB/Lab enum values: Photoshop silently
interprets them as RGB. PSD fills, layer effects, and GRD exports all write
the native enum values; readers still accept older Patchy spellings.

A Gradient Fill layer or shape stroke may carry a noise (`ClNs`) gradient. Photoshop's PSDs store the channel ranges `Mnm `/`Mxm ` as doubles (79.9988 for 80), where GRD files use longs; the reader accepts both. `gradient_object` (src/psd/psd_vector.cpp) writes the noise form with Photoshop's keys and order (`Nm`, `GrdF`, `ShTr`, `VctC`, `ClrS`, `RndS`, `Smth`, `Mnm`, `Mxm`) and no stop lists. A noise gradient is never "healed" for missing transparency stops: writing it as a stop gradient left an empty `Clrs` list and Photoshop dropped the fill layer on open (psd-tools' `gradients/noise-gradient-*.psd`, found by Testy in October 2026; Photoshop 2026 opens the regenerated file clean with all three layers still Gradient Fill).
