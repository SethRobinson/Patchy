# Gradients and Photoshop GRD presets

Patchy uses `GradientDefinition` for reusable gradient content and `LayerStyleGradient` for placement. A definition contains the name, Solid or Noise form, Photoshop smoothness (`Intr`, 0-4096), independent color and opacity stops, destination-stop midpoints, and dynamic foreground/background roles. Noise definitions keep the Photoshop seed, roughness, transparency and color-restriction switches, RGB/HSB/Lab model, and four minimum/maximum channel ranges.

Layer-style placement adds Linear, Radial, Angle, Reflected, and Diamond geometry, angle, scale, reverse, dither, interpolation method, Align with Layer, and X/Y percentage offsets. The Stroke effect alone adds Shape Burst (descriptor stringID `shapeburst`), which follows the stroke band's distance field and ignores angle, scale, offsets, and alignment; see docs/ps-compat.md for the calibrated mapping. Preset selection replaces only the definition. It must not overwrite any placement field. Dynamic foreground/background stops stay live in the Gradient tool; layer-style preset selection resolves them from the current Patchy colors before the style is stored in the document.

## Rendering

- Classic applies the stored smoothness as cubic interpolation after destination-stop midpoint remapping.
- Perceptual interpolates in OKLab.
- Linear interpolates in linear-light RGB.
- Noise and dither use fixed integer hashing. Do not replace this with a standard-library random distribution because output must remain identical across toolchains.

`gradient_position` is the shared point-mapped-style geometry function; Shape Burst does not go through it (the stroke renderer derives its position from the band's Euclidean distance field, `stroke_alpha_mask`'s optional plane). Linear and Reflected spans use the layer rectangle projected onto the selected angle, so 90-degree gradients span the layer height rather than its width. For `Align with Layer`, Gradient Overlay and gradient Stroke use the source's nonzero-alpha bounds; PSD channel padding must not compress the visible range. The local alpha bounds are cached by the layer's globally unique pixel revision because finding them is an O(width * height) scan. Transient render pixel overrides bypass that cache. `gradient_color`, `gradient_stop_opacity`, and `gradient_color_dithered` are shared by layer effects and preset thumbnails.

## GRD files

`src/psd/grd_io.*` reads and writes Photoshop `8BGR` version 5 files containing a version-16 `GrdL` descriptor. It supports solid `CstS`, noise `ClNs`, dynamic `FrgC`/`BckC` stops, ZString display names, and the trailing `8BIMphry` hierarchy. Imports are limited to 32 MiB, 4096 gradients, and 256 stops per list. A damaged tail may return the valid decoded prefix with warnings; structural damage before the first usable gradient is an error.

The application library lives under the settings directory's `gradients/` folder. Each entry is one single-gradient `.grd` plus a JSON sidecar with its fixed storage id, canonical name, and folder path. Default ids and English names in `src/core/gradient_presets.cpp` are persisted and append-only. New defaults need a new `introduced_version` and a `kDefaultGradientsVersion` bump; never rename or reuse an existing id.

The quick picker (`src/ui/gradient_preset_popup.{hpp,cpp}`) and Gradient Manager read the same `GradientLibrary`. The quick picker anchors to the layer-style Preset buttons, the gradient toolbar's Presets button, and the Edit Gradient Stops dialog's Preset button; its Manage Gradients button falls through to the Gradient Manager. The Gradient tool has no definition-backed state: applying a preset there resolves foreground/background stops from the current colors and flattens the definition into sampled stops (33, or 65 for Noise), so the applied result is static. Manager writes are immediate. Restore repairs changed or deleted built-ins without deleting user gradients. Import deduplicates identical name/payload pairs but permits equal names with different definitions. Folder and subtree export includes matching `phry` markers.

## Stop editor widget

`GradientStopsEditorWidget` is callback-driven: it never mutates its own stop vectors; hosts copy-and-sort, and must never sort the working vectors in place - an in-flight tag or midpoint drag holds an index into them. Photoshop `Mdpn` belongs to the destination/right stop; the first stop's midpoint is unused.

## PSD layer effects

Gradient Overlay `GrFl` and gradient Stroke `FrFX` share the definition codec but have different required descriptor shapes. Untouched imported `lfx2`/`lmfx` remains byte-preserved. Once edited, writers must retain Photoshop's key order and types documented in `docs/ps-compat.md`, including `Grad`, `Angl`, `Type`, `Rvrs`, `Dthr`, interpolation, `Algn`, `Scl`, and `Ofst`.
