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
