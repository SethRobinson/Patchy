# Filter spatial scaling and bounds

Split from [filters.md](filters.md): which parameters scale for proxies, where ranges and translation support are declared, per-filter growth and calibration notes, center and width remaps on padded buffers, soft selections, and the canvas-filling filters.

## Spatial scaling and bounds

Proxies scale only pixel-distance parameters. Version-1 spatial keys are the catalog parameters declared `FilterSpatialScale::Pixels` (pinned by the catalog test). Angles, percentages, samples, intensity, detail, seed and colors never scale. Scaling returns a normalized copy.

Ranges, practical slider limits, growth margins and translation support ("supp") per filter are declared in the typed catalog in `src/filters/filter_engine.cpp` ("Typed catalog ranges") and pinned by the catalog tests; unlisted filters neither grow nor advertise fixed support. Motion Blur's kernels are in [smart-filters-native.md](smart-filters-native.md); Radial Blur's growth and the Clouds embedding are below.

Calibration notes:

- Radial Blur: historical centered growth kept for default compatibility; an edited center grows from the sampled corner sweep, uncapped, failing through the registry's checked padding path rather than clipping. Amount 0: no growth. Native: [smart-filters-native.md](smart-filters-native.md).
- Add Noise: deterministic position-hashed noise on RGB only, uniform or a sum-of-four-uniforms gaussian approximation with no transcendental calls. The seed feeds the hash; amount does not scale for thumbnails (like Analog Grain).
- Box Blur: radii to 12 keep the direct double path (pins, Smart Filter parity); larger ones use an exact int64 running sum.
- Blur, Blur More, Sharpen More, Sharpen Edges: the native kernels (`render_photoshop_classic_filter`). Average: visible pixels' alpha-weighted mean (Photoshop also counts hidden colors). All five skip the dialog.
- Gaussian Blur, High Pass, Unsharp Mask, Median, Dust & Scratches, and Surface Blur share their kernels with the native Smart Filters (`render_photoshop_*` in `smart_filter_renderer.cpp`); the exact math is in [smart-filters-native.md](smart-filters-native.md) "Render semantics". Gaussian's `accepts_legacy_integer` widens pre-decimal integer radii; Median floors and Surface Blur rounds fractional radii for rendering without rewriting the stored value.
- Median, Dust & Scratches, and Surface Blur give transparent pixels the straight RGB of the nearest visible source anywhere in the input, so these, the ellipse/band blurs, Plastic Wrap and Add Noise advertise no finite support and render selected applications with full-layer context. Surface Blur alpha-trims after padding.
- Lens Blur: one deterministic supersampled aperture kernel on premultiplied RGBA (curvature blends the polygon toward a circle); radius 0 is an exact identity; large radii run fixed-point downsample/convolve/upsample stages on a grid anchored to the whole input, a factor-aligned margin alpha-trimmed after.
- Iris Blur: one fixed round aperture blur, premultiplied-blended with the original through one smooth elliptical mask; Focus interior sharp, outside the ellipse fully blurred; blur 0 exact identity. No depth inference, highlight detection, per-pixel kernels, or combined blur patterns.
- Tilt-Shift Blur: angle 0 means horizontal focus lines; focus band sharp, cubic transition, full blur beyond the transition boundaries; blur 0 exact identity; alpha-trimmed after padding.
- Plastic Wrap: one fixed integer height-field formula (smoothed alpha-weighted luminance, gradients, constant-direction relief plus ridge highlights on the original RGB); alpha weighting gives isolated artwork contour relief. Dimensionless, no thumbnail scaling.

When rendering expands an RGBA layer the registry pads every side first, so centers are remapped to the padded buffer, per axis:

```text
padded_percent = 100 * (margin + (original_extent - 1) * percent / 100)
                 / (original_extent + 2 * margin - 1)
```

Selected by the center roles, default 50.0 centers included; never run a centered effect on a padded buffer with the unadjusted percentage. Tilt-Shift focus and transition widths are percentages of the shorter input extent; padding multiplies both by `original_shorter / padded_shorter`. Iris Width/Height are percentages of input width/height; padding scales each by its own original/padded ratio.

A soft selection (feather, Object Selection edge) blends each result pixel with the original by its coverage (`blend_by_selection_coverage`, `filter_workflows`), as in Photoshop; adjustments, Liquify, plug-ins and script `applyFilter`/`fill` do too, as do preview proxies (coverage box-averaged). The UI wrapper decides expansion: a selected operation stays inside the layer bounds; with no selection an RGBA layer may grow, then trims transparent borders. Preview, Cancel, Apply, Undo, and Redo restore pixels and document-space bounds.

Canvas-filling filters (Clouds, Difference Clouds, Fibers, Lens Flare, Flame) are the one exception: the UI wrapper embeds the layer in a transparent buffer covering the union of layer bounds and canvas (never a canvas crop) and renders across it; with a selection, outside pixels restore as usual, so clouds can fill selected regions without layer content. The result is alpha-trimmed against the original bounds: a selected render grows the layer only to the union of old content and selection, and a fully transparent result returns to the original rectangle. No-alpha layers, effective transparent-pixel locks, and layers already covering the canvas skip the embed. Cancel restores the pristine pixels and bounds. Clouds has no native Smart Filter mapping: on a Smart Object its direct action and recipes reach the rasterize prompt, then this path.
