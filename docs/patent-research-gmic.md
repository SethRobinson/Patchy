# Patent research record: G'MIC blur filters and on-canvas point markers

Scan of October 13, 2026. Claim text was read on Google Patents; statuses are Google's
estimates, unchecked against USPTO fee or assignment records. The binding rules are in
[legal-constraints.md](legal-constraints.md); the design is in [gmic.md](gmic.md).

Question: may the G'MIC Filter Library put its draggable position markers
(`CanvasWidget::set_preview_point_handles`) on filters named "Blur ...", which
`GmicFilterLibraryDialog::report_points()` held back by name until this scan?

## Filters that declare points

| Id | Name | Points | What the script computes |
|---|---|---|---|
| `fx_blur_radial` | Blur [Radial] | 1, zoom centre | Polar transform about the centre, 1-D blur along the radius, inverse. No mask. |
| `fx_blur_angular` | Blur [Angular] | 1, rotation centre | Polar transform, blur along the angle, inverse. |
| `fx_blur_dof` | Blur [Depth-of-Field] | 1, centre of the sharp zone | A per-pixel field from an ellipse (centre, two radii, angle) scaled by one amplitude, quantised to levels; progressively blurred whole-image copies are composited through the level masks. Another mode takes a user depth layer and ignores the point. |
| `fx_blur_motion` | Blur [Motion] | 5, declared hidden | 2 to 5 points define a spline rasterised into one global kernel with "Weight Decay"; the image is convolved with it. |

No other blur-type filter declares a point. Smooth [Guided] and Smooth [Skin] declare a hidden
preview-split point, which is not a blur control.

## Patents read

| Patent | Holder, subject | Priority, estimated expiry | What every independent claim read requires |
|---|---|---|---|
| US 8831371 | Adobe, blur patterns | 2012-03-02, 2032-12-13 | Two or more pattern instances with different blur amounts combined in one blur mask. Pins and boundary widgets only in dependent claims 11 to 16. |
| US 9019310 | Adobe, field blur gradients | 2012-03-02, 2033-03-20 | A location and a blur radius for each of one or more "field blur" instances; a mask giving a blur radius at each pixel as a uniform, linear or complex gradient field; a kernel applied per pixel by that radius. On-image indication only in dependent claims 6 to 8. |
| US 8824793 | Adobe, bokeh | 2012-03-02, 2032-12-16 | Boost amount with upper and lower light thresholds. |
| US 8693776 | Adobe, bleed | 2012-03-02, 2032-08-20 | Selection region, variable bleed, per-pixel radius mask. |
| US 8971623 | Apple, overlaid tools | 2012-03-06, 2032-12-26 | Two lines flanking a chosen location, each dividing the image; effect applied outside. |
| US 12561768 (was 2025/0104196) | Adobe, synthetic lens blur | 2023-09-26, 2044-05-23 | Focal matte from a focus region and depth map, layered depth, splatting. |
| US 9723204 | Adobe, motion path blur kernel | 2014-08-27, 2035-04-08 | Claim 1: user inputs describing a path for the blur trajectory and a curved kernel shape, applied in real time as inputs arrive. Claim 12: path plus a taper of the light level along it. |
| US 9955065 | Adobe, motion path blur UI | 2014-08-27, 2035-05-01 | Path of selectable points; moving one recalculates the blur in a mesh domain. |
| US 9779484 | Adobe, motion path techniques | 2014-08-04, 2035-08-05 | A mesh or motion field with per-pixel kernels. |
| US 9799103 | ASUSTeK | 2015-09-14, 2036-09-06 | A blur centre plus a drag track whose angle sets level and type. |
| US 7970233, US 7031547 | Nik (U Point) | 2001-10-24, expired 2022 | On-image marks controlling localised effects. |

Not found in web searches (an absence, not a finding): an Adobe patent specific to Spin Blur,
or any Corel, Skylum or Serif patent on a blur-centre control.

## Findings

- **The marker itself.** No independent claim read is met by a draggable position marker
  alone; each needs blur mathematics or overlay geometry beyond a location input. None of the
  Adobe independent claims requires the location to be given on the canvas: a typed X/Y field
  is equally an input indicating a location. The marker moves only dependent claims, which
  matter only when the filter's own mathematics already meets the independent claim.
- **Blur [Radial], Blur [Angular].** No claim read is approached: one pattern, one amount, no
  per-pixel radius mask, no lines. A draggable blur-centre picker (Photoshop's Radial Blur
  "Blur Center" box, in the dialog) is widely described as 1990s prior art; date unverified.
- **Blur [Depth-of-Field].** The closest case, and it comes from the filter, not the marker.
  Against US 9019310 claim 1: a location is arguably present; "blur radius at each instance"
  is doubtful (the centre is always sharp, the amplitude is global); a per-pixel blur-level
  mask is functionally present; "uniform, linear, or complex gradient field" is doubtful (an
  elliptical falloff, which the same family treats as the separate iris pattern); "a blur
  kernel to each pixel" is arguable (G'MIC blurs whole-image copies and selects by mask).
  Unresolved without the specification and prosecution history. The comparison is the same with
  number fields; a marker adds only dependent claim 6.
- **Blur [Motion].** US 9723204 claims 1 and 12 describe a point-defined curved kernel with
  weight decay, which this filter has; "in real time as the inputs are received" is arguable against a
  background re-render. US 9955065 and 9779484 are not met (no mesh, one global kernel).
- **G'MIC's preview commands** (unused by Patchy) draw crosshair lines, isoline ellipses with
  a centre circle, and the motion spline. Running them would break the "nothing drawn but the
  marker" rule.

## Outcome

Blur [Radial] and Blur [Angular] get their marker; the name-prefix hold-back is gone. Blur
[Depth-of-Field] and Blur [Motion] are not offered (`NAMED` in
`scripts/dev/gmic/build_allowlist.py`).

## Not verified

Construction of the US 9019310 terms against Blur [Depth-of-Field]; US 9723204 against Blur
[Motion]; continuations in the Adobe 2012 and 2014 families and the Apple family; the third
independent claim of US 8831371 and the remaining independent claims of US 8824793; design
patents; legal status and fee payment of every patent listed; dates of the prior-art items.

## Family triage of the offered filters (2026-10-20)

The named exclusions above are a name rule, not a per-filter review, and an outside audit asked
what that leaves uncovered. Method: the 394 offered filters were grouped by the areas
[legal-constraints.md](legal-constraints.md) treats as patent-sensitive, the G'MIC bodies of every
filter in those groups were read in `gmic_stdlib.gmic` down to the CImg entry point, and the CImg
implementation was read where Patchy's own rules forbid a technique (bilateral and median). One
patent was read claim by claim (US 8427559); the rest is triage by algorithm class against the
claim elements already recorded, with no new assignee sweep. Limits: names, bodies and two
implementations only; a filter's defaults may call helpers not listed here.

- **Sharpening by deconvolution** (Patchy's own Unsharp Mask rule names deconvolution as needing
  a review): Sharpen [Deblur] (`deblur`, regularized Jansson-Van Cittert), [Richardson-Lucy]
  (`deblur_richardsonlucy`, 1972) and [Gold-Meinel] (`deblur_goldmeinel`) are non-blind: the
  kernel is a Gaussian of the user's sigma with the user's iteration count. No blur or shake
  estimation from the image, no ranking, no automatic threshold, no patch search, which are the
  elements of the deblurring, ranked-sharpening (US 7724980) and patch-sharpening (US 8687913,
  US 9142009) families the record lists. Offered.
- **Other sharpening**: [Multiscale] (Laplacian-pyramid detail layers smoothed per scale by
  `bilateral` and `guided`), [Texture] (rolling guidance difference), [Shock Filters], [Inverse
  Diffusion] (`sharpen`), [Octave Sharpening] (summed unsharp radii), [Unsharp Mask], [Alpha]
  (`sharpen_alpha`): adaptivity only through range-weighted smoothing, no self-similarity, no
  sharpness ranking. Offered.
- **Denoising with patch search**: Smooth [NL-Means] (`nlmeans`, `nlmeans_core`) is Buades 2005
  non-local means, an exhaustive patch-distance weighted average over a spatial window: no offset
  propagation or random search (outside US 8285055), no candidate lists (outside US 8340463). Its
  own patent, US 8427559 B2 (Buades, Coll, Morel; US application 11/579,380 filed 2005-04-13,
  granted 2013-04-23; claim 1: a learning zone, window-to-window distances, weights from the
  distances, a weighted mean), is "Expired - Fee Related" on Google Patents, so the method is
  prior art. Offered. The name rule missed it (no "Patch" in the name); this entry is the record.
  Smooth [Patch-Based], [Patch-PCA] and the built-in `denoise` stay excluded.
- **Bilateral, median and local histograms** (the Surface Blur rule): CImg `blur_bilateral` is
  the bilateral grid (Paris and Durand 2006: one joint space-range grid, Gaussian-blurred and
  sliced), not a per-window value histogram with a box spatial kernel (US 8594445 claim 1) and not
  merged column histograms slid along the scanline (US 7920741). This covers Smooth [Bilateral],
  the bilateral option of Smooth [Skin] and Sharpen [Multiscale]. CImg `blur_median` sorts each
  window (`median()`), no histogram. `_fx_equalize_local_histograms` builds one weighted histogram
  per pixel from its own crop, never merged or slid; Local Normalization uses blurred mean and
  variance. Absent from the implementation, by reading it.
- **Inpainting and content-aware fill**: every Inpaint filter is excluded. Smooth Abstract
  (`inpaint_pde` over a gradient mask), Solidify (`solidify`, `inpaint_pde`) and Upscale
  [Diffusion] use diffusion inpainting or anisotropic diffusion (Bertalmio 2000 class), no patch
  search: the non-patch helper carve-out above covers them.
- **Gradient domain**: Make Seamless [Diffusion] uses `periodize_poisson` (Moisan 2011 periodic
  plus smooth decomposition, a Fourier Laplacian solve on one image), not compositing of another
  image's gradients. Blend [Seamless] stays excluded.
- **Blur Gallery style**: [Depth-of-Field] and [Motion] stay excluded. Blur [Bloom], [Glow],
  [Linear], [Multidirectional], [Gaussian], [Radial] and [Angular] are one global kernel or one
  polar transform. Artistic / Bokeh draws random discs as a screen layer and reads no highlights,
  so US 8824793's threshold-selected boost is absent.
- **Content classification**: Smooth [Skin] (`detect_skin`, a color-space threshold), Extract
  Objects (connected components of a picked color) and Red-Eye Attenuation (a chroma threshold)
  use fixed color rules, no learned model. The automatic-selection record's detector rule is about
  ML selection; these stay offered and are listed here as the classification-adjacent set.

Outcome: no additional exclusion is recommended from this read; the one real gap (NL-Means) is
closed by expiry. Open: no assignee sweep was run for sharpening, skin smoothing or guided
filtering (Smooth [Guided] implements He 2010; US 9286663 and US 9478017 cite guided filtering and
were not read); `build_allowlist.py`'s named list stays a name rule, and this section is the
per-family record it lacked. Adding a filter to any family above needs the same check.
