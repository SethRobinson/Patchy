# Gradient methods: Smooth and Stripes

Split from [gradients.md](gradients.md). `GradientInterpolationMethod` (src/core/layer.hpp) is append-only and saved by value: Classic 0, Perceptual 1, Linear 2, Smooth 3, Stripes 4. Classic, Perceptual and Linear are described in [gradients.md](gradients.md#rendering); this page records the two methods added for GitHub issue 742 and how they were measured.

Every gradient shares the list: Gradient Overlay, gradient Stroke, gradient Outer Glow and Inner Glow, Gradient Fill layers, gradient shape paints and Gradient Map (the Method box of `make_gradient_controls`, items "Smooth" and "Stripes" in the `gradient interpolation` translation context). Scripts name them `"smooth"` and `"stripes"` in a gradient's `interpolation`. The Gradient tool's own gradient has no method and stays Classic.

## Smooth and Stripes

Both are fixed formulas of the stops, fitted black-box to Photoshop 2027's rendered output (16-bit 4096 x 4 Gradient Fill layers created over COM, read back as 16-bit PNG rows). No Photoshop code was inspected. Neither uses Smoothness: Photoshop renders the same pixels at 0%, 50% and 100%.

**Stripes** shows each stop's color unchanged from that stop's location up to the next stop, so the bands are hard steps. The pixel at a stop's location already has the new color, and a last stop at 100% never shows. Midpoints are ignored. Opacity stops step the same way. Patchy matches Photoshop exactly (`stripes_gradient_stop`, src/core/blend_math.cpp).

**Smooth** is a spline in OKLab (`smooth_gradient_sample`):

- Knots are every stop and every midpoint. A midpoint's knot sits at its location and holds the average of its two stops, in OKLab.
- Each piece between two knots is a cubic Hermite curve. The slope at an interior knot is the slope of the straight line between its two neighbors (a Catmull-Rom spline when the knots are evenly spaced).
- The first and last knot take 0.6 of their own piece's slope. When that stop sits inside the gradient, with a flat run of its color before or after it, the factor is 0.4.
- Each piece is clamped, per channel, to the range of its two knots. Photoshop never overshoots a stop's color either: the curve flattens at the stop instead.
- Opacity stops run through the same spline on the opacity value.

Accuracy against the captures, in 8-bit levels (largest error over the 4096 samples):

| Case | Largest error |
| --- | --- |
| Stripes, every capture | 0 |
| Smooth, interior pieces between evenly spaced stops with 50% midpoints | 0.2 |
| Smooth, two to five evenly spaced stops, whole gradient (black to white, grays, red to green to blue) | 0.8 to 2.4 |
| Smooth, unevenly spaced stops or a first stop inside the gradient | 3 to 8 |
| Smooth, midpoints moved to 20% to 30% or 70% | 8 to 11 |

Measured and not modeled:

- The first and last piece are not cubic in Photoshop (a polynomial needs degree 7 to follow them). The 0.6 end slope keeps the model within 2.4 levels there.
- With unevenly spaced knots Photoshop's curve stays smooth, but none of the tangent rules tried (neighbor slope, mean of the two slopes, the parabola through three knots, a parametric curve, per-stop index spacing) reproduces it. The neighbor slope is the closest and is what Patchy uses.
- Photoshop's opacity ramp sags below the spline between two far-apart opacities (20% to 80% passes 45.8% at the middle, not 50%).
- With the Perceptual and Linear methods Photoshop also changes how a partly transparent gradient is blended (a 20% black stop over white shows 14.1%). Patchy blends every method the same way.

Gradient Map passes `ease_every_method` for Perceptual and Linear; Smooth and Stripes ignore it, as they ignore Smoothness.

Never replace these formulas with a search over candidate color paths, or any step that discards candidate paths by a uniformity test: that is the subject of Adobe US 10930021 and US 11328458 ([patent-research.md](patent-research.md), "Gradient methods Smooth and Stripes").

Tests: `gradient_smooth_and_stripes_match_photoshop_samples` pins Photoshop's colors, `gradient_smooth_and_stripes_ignore_smoothness_and_keep_other_methods` the rules above, `psd_gradient_smooth_and_stripes_round_trip` the file tokens (tests/core/layer_style_noise_methods_tests.cpp).

## PSD tokens

The descriptor enum `gradientInterpolationMethodType` (key `gradientsInterpolationMethod`, or `gs99` in a Gradient Overlay) holds four-character codes in Photoshop 2027's files: `Gcls`, `Perc`, `Lnr `, `Smoo` and `GIMs` (Stripes; its stringID is `stripes`). The reader accepts the codes and the stringIDs. The writers keep `Gcls`, `perceptual` and `linear` as before and write `Smoo` and `GIMs`. A Gradient Map's `grdm` block stores the same four characters after its flags.

Before this, a Smooth gradient read as Perceptual and a Stripes gradient as Classic.

Over Action Manager the Gradient Overlay key is the charID `gs99` (`charIDToTypeID`); `stringIDToTypeID('gs99')` is a different id, and a descriptor that uses it leaves the overlay on Classic without an error.

## Photoshop exchange check

`psd_layer_style_742_exchange_file_round_trips` leaves `test-artifacts/layer-style-742.psd`: Gradient Overlays in both methods, a Smooth gradient Outer Glow, Noise on all four shadow and glow effects, and a Pattern Overlay and a Bevel Texture with offsets. Photoshop 2027 opened it without a dialog (`scripts\dev\photoshop-flatten-check.ps1`, November 22, 2026) and returned every method, Noise value and offset through Action Manager. Its flatten against Patchy's: the Stripes overlay and the offset Pattern Overlay are identical, the Smooth overlay within 2 levels (mean 0.28), and the grain agrees in strength (20 x 20 block averages within 4.4 levels on the shadows, 1.2 on the gradient glow, 15.5 at one glow corner; whole-effect averages within 0.3). The offset Bevel Texture differs: Photoshop ignores that offset ([psd-layer-styles.md](psd-layer-styles.md)). A Photoshop file edited and saved by Patchy (`layer-style-742-photoshop-edited.psd`) opened clean the same way.

The other direction is `photoshop_noise_fixture_imports_edits_and_saves_if_available` and `photoshop_gradient_method_fills_match_reference_if_available`, on files Photoshop wrote: the settings import, the render stays within the same bounds, and an edited save reads back.

## Captures

`local-test-fixtures/layer-style-742/` (machine-local) holds the capture scripts (`methods.jsx`, `make_method_jobs*.py`), the 16-bit rows under `methods/`, and `model_smooth.py`, which evaluates the model above against every row.
