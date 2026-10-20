# Patent research record: rendering imported Blur Gallery Smart Filters

Scan of October 20, 2026. Claim elements were read on Google Patents; statuses are Google's
estimates, unchecked against USPTO fee or assignment records. The binding rules are in
[legal-constraints.md](legal-constraints.md); the descriptor and render notes are in
[smart-filters-native.md](smart-filters-native.md). Earlier reads of the same families:
[patent-research.md](patent-research.md) (Tilt-Shift, Lens and Iris Blur) and
[patent-research-gmic.md](patent-research-gmic.md) (field blur, bokeh, motion path).

Question: may Patchy render (and later author) Photoshop's Blur Gallery Smart Filter, the
`blurbTransform` descriptor, instead of keeping imported stacks preview-locked? A renderer that
follows a stored descriptor performs the same method as one driven by a dialog: importing does
not avoid a method claim. What matters is what the renderer computes.

## What a `blurbTransform` descriptor can hold

One `blurbWidgetList` of widgets, each with a type, location, enable flag and per-type geometry
(Tilt-Shift inner and outer points, blur amount, distortion; Iris ellipse, roundness and feather;
Field pins with radii; Path curves with speed and taper; Spin ellipses with angle), plus global
Effects (Light Bokeh, Bokeh Color, Light Range), Noise, and High Quality / Save Mask flags.
Photoshop renders every widget into one per-pixel blur-radius mask and applies a kernel sized
per pixel, the method its own patents describe.

## Patents read (independent claims, element by element)

| Patent | Status (Google) | Every independent claim requires |
|---|---|---|
| US 8831371, blur patterns | Active to 2032-12-13 | Iterated input of parameters for one of several blur patterns, each modifying one blur mask; at least two instances with different blur amounts; the instances combined in the mask. Same-type multiply and different-type add (dep. 9, 10); per-pixel kernel by radius (dep. 7, 8); iris and tilt-shift parameters (dep. 14 to 16). |
| US 9019310, field blur | Active to 2033-03-20 | A location and a blur radius for each of one or more field blur instances; a mask storing a blur radius per pixel as a uniform, linear or complex gradient field; a kernel applied to each pixel sized by its mask radius. One instance = uniform (dep. 3), two = linear gradient (dep. 4). |
| US 8824793, bokeh | Active to 2032-12-16 | A boost amount, an upper and a lower threshold defining a light range, and boosting specular highlights in the blur region selected by that range. Colorfulness (Bokeh Color) is dep. 4. |
| US 9723204, US 9955065, US 9779484, motion path | Active to 2035 | A path describing the blur trajectory given through a user interface, with a curved kernel (9723204), a mesh recalculated as points move (9955065), or a mesh motion field with per-pixel kernels (9779484 claims 1 and 10); 9779484 claim 18 adds flash points on the trajectory. |

No Adobe patent specific to Spin Blur was found (an absence, not a finding; see also
patent-research-gmic.md). No claim read covers adding grain noise after a blur.

## Findings per widget kind

- **Field Blur, two or more pins:** has every element of US 9019310 claim 1 (pins, per-pixel radius
  gradient field, per-pixel kernel) and, with different radii, US 8831371 claim 1. Not
  renderable. Rendering it through blended fixed blur levels instead of a per-pixel kernel is
  the unresolved G'MIC Depth-of-Field question; Patchy did not offer that filter either.
- **Field Blur, one pin:** a single uniform blur. Rendered as one whole-image kernel with no
  mask, nothing of claim 1's mask element is performed; a uniform blur is decades-old prior art.
- **Any two or more enabled widgets in one descriptor** (two irises, tilt-shift plus iris,
  several spins): has every element of US 8831371 claim 1 when the amounts differ, and Photoshop combines
  them in one mask. Not renderable.
- **Tilt-Shift, one widget:** inside the design already recorded in patent-research.md when
  rendered with Patchy's existing design (one linear pattern, a 0..1 mix mask, blending fixed
  whole-image blur levels). Not reviewed: Distortion other than 0 (a kernel whose shape changes
  with position).
- **Iris, one widget:** inside the existing Iris Blur design when rendered as one fixed
  blur blended through one scalar ellipse mask. Photoshop's rounded-rectangle outer boundary
  and inner feather points are geometry of that one mask, not a second pattern. Moving Iris
  to Tilt-Shift's fixed-level blending is the same structure the record describes for
  Tilt-Shift; still one pattern and no per-pixel kernel.
- **Spin, one widget:** one rotational blur (classic Radial Blur Spin, prior art) blended
  through one ellipse mask: the Iris structure. No path, so the motion-path claims are not met.
- **Path Blur:** has every element of US 9723204 claims 1 and 12 and, through its per-pixel kernels,
  US 9779484. Not renderable.
- **Effects:** Light Bokeh above 0 has every element of US 8824793 claim 1 (boost plus light range); Bokeh
  Color only matters with it. A descriptor whose Light Bokeh is 0 performs no boost.
- **Noise:** no claim found; still a Patchy-original approximation of Photoshop's grain.
- **Save Mask to Channels:** writing Photoshop's per-pixel blur mask out as a channel is
  US 9019310's mask element; Patchy never writes one and keeps the flag byte-preserved.

## Outcome

Decision (October 20, 2026): Blur Gallery stays byte-preserved and preview-locked; the
binding rule is unchanged. The only `blurbTransform` in the fixture corpus (Tilt-Shift with
Distortion -27.25) would stay locked even under the boundary below, so changing the rule would
unlock no known file. The boundary is recorded for a later decision.

Proposed, NOT approved: Patchy could render a `blurbTransform` stack only when the descriptor has exactly one enabled
widget of kind Tilt-Shift (Distortion 0), Iris, Spin, or single-pin Field, Light Bokeh 0, and
uses only Patchy's recorded single-pattern designs (fixed blur levels or one blur, one scalar mix
mask, no per-pixel radius mask or kernel). Every other descriptor stays byte-preserved and
preview-locked. Matching Photoshop byte for byte is not a goal here: Photoshop's renderer is the
patented per-pixel method, so Patchy's output is calibrated for geometry and blur strength with a
documented tolerance. Authoring follows the same boundary: the dialog edits one widget and
cannot add a second widget or turn on bokeh.

## Not verified

Continuations in the Adobe 2012 Blur Gallery family filed after these grants; Apple US 8971623
against any new on-canvas widget (Patchy's grip-bar rule still binds); construction of "blur
mask" against a level-blend mix mask; legal status and fee payment; any Adobe filing on Blur
Gallery noise or Spin Blur under other titles.
