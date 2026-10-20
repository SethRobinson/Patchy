# Patent research: record review of October 20, 2026

Claim-level check of six questions raised by an outside review of Patchy's patent records. Claim
text and status were read on Google Patents on 2026-10-20; statuses and expiry dates are Google's
estimates, unchecked against USPTO fee records. This is an engineering record in the style of the
other `patent-research*.md` records: what each claim requires, what Patchy's design does, and how
they differ. The binding rules stay in
[legal-constraints.md](legal-constraints.md); nothing here changes behavior by itself. Items 4
(G'MIC) and 5 (plug-in fixtures) are recorded in [patent-research-gmic.md](patent-research-gmic.md)
and `test-fixtures/photoshop-plugins/README.md`.

## 1. US 9058699 B2 and its parent US 8380005 B1 (Adobe): preview, then refined render

**Record correction.** The earlier record said US 9058699 "claims compositing source gradients
through a guidance field". It does not. Its independent claims never mention gradients:

- Claim 1: receiving an input to initiate a preview of a composite image that includes an object
  extracted from one image as included in another image; displaying the composite image in a
  first resolution during the preview; subsequent to the preview, rendering a refined version of
  the composite image as a result of a compositing operation, in a second resolution higher than
  the first.
- Claim 11: displaying a preview of such a composite image in a first resolution, and rendering,
  after the preview, a refined version including the extracted object in a higher resolution.
- Claim 18: the system form of claim 11, plus displaying the refined version.
- Dependents add a superimposed copy of the source image (2 to 4, 12, 13), merging so the object
  appears inserted (5, 14), extraction by a drawn perimeter or foreground/background classification
  (6 to 8, 15 to 17, 19, 20), editing during the preview (9, 10).

The parent US 8380005 B1 (application 12/364,378, priority 2009-02-02, granted 2013-02-19, Active,
adjusted expiry 2031-12-23, 40 claims) has the same shape with more elements: claims 1 and 18
require accessing an image on selection, displaying a copy of it superimposed over the other
image, extracting the object from that copy, merging for a preview, then refining on a
higher-resolution representation of the object's portion. The fast gradient solver appears only in
dependents (3, 6, 7, 20, 22). The gradient-domain method is the specification's, not a claim of
either patent. US 9058699 (filed 2013-01-18 as a continuation) is Active to 2029-02-02.

**"Resolution" in the specification.** The preview comes from the lowest level of an image pyramid
("The resolution of this initial image may be lower than that of the image from which the object
of interest was extracted"), and the final rendering refines it on the higher levels. So the claims
compare the compositing resolution of the preview with that of the final render, which is also
how Patchy's previews differ from their commits.

**Where Patchy composites a preview at reduced resolution and then renders at full resolution**
([interactive-previews.md](interactive-previews.md)): the Move tool's live patches at zoom at or
below 50% (`composite_level >= 1`, rendered from the preview-scaled document), the Move proxy
snapshot above `kMoveProxyMaxPixels` (4 M px, sqrt downscale) and the Free Transform proxy under
the same rule; each is followed by the accurate full-resolution patch render on release (or the
deferred commit). The filter dialogs are not in this set: `make_filter_preview_proxy` proxies the
active layer's own pixels, so the dialog preview is not a composite image. Ordinary zoomed-out
viewing renders the full-resolution composite first and only scales it for display, so no
higher-resolution render follows a preview.

**Claim 11 against a Move drag at 50% zoom of a layer pasted from another document, element by
element.** (a) "composite image including an object extracted from one image as included in
another image": the document holds such an object whenever the user pasted a selection from another
document or placed a file; the application does not track provenance. (b) "displaying a preview ...
in a first resolution": the live patches are composited from the half- or quarter-resolution
document, while the displayed pixel grid is the same before and after release, so the difference is
in the compositing resolution, which is the specification's usage. (c) "rendering, after
displaying the preview, a refined version ... in a second resolution that is higher": the release
renders the accurate full-resolution patches. Claim 1 adds an input that initiates the preview (the
press and drag) and "as a result of a compositing operation" (ordinary layer compositing here; the
specification's compositing operation is the gradient-domain merge). Claim 18 adds displaying the
refined version, which the release repaint does. US 8380005 claim 1 differs from Patchy's
behavior: Patchy never displays a copy of the whole source image superimposed
over the destination and then extracts the object from that copy inside the destination (Copy
extracts in the source document; File > Place inserts the whole image and extracts nothing).

**Comparison.** The earlier record misdescribed the claims. Claims 1, 11 and 18 describe a composite
previewed at a lower compositing resolution and then rendered at a higher one, and Patchy's
reduced-resolution drag and transform previews of a document that holds pasted content have that
structure. The claims' terms "composite image", "compositing operation" and "resolution" come from
a specification devoted to gradient-domain compositing of an extracted object, while Patchy's Move
preview is a generic proxy of any layer. Published earlier work describing the same technique: a
screen-resolution preview of a composite followed by a full-resolution final render is the Live
Picture / FITS model (1993; the expired US 6181836 family in [patent-research.md](patent-research.md))
and the proxy previews Photoshop and others shipped before the 2009 priority date.

**Options recorded (no behavior changed).** A: keep the previews and re-check at 2029-02-02
(US 9058699) and 2031-12-23 (US 8380005). B: never composite a
preview at reduced resolution: where the Move or Free Transform path would use the scaled document
or a downscaled proxy, use the existing dashed outline preview instead (small to medium effort,
both outline modes exist; cost: no pixel preview when zoomed out past 50% or on large sets). C:
full-resolution live previews everywhere (large performance cost; not recommended). The
"never composite source gradients" rule in legal-constraints.md stays as an engineering boundary
(the gradient-domain field was not surveyed beyond these two patents and the PatchMatch trio), but
it no longer cites US 9058699 as its source.

A later note (2026-10-20): claim terms are read on their own words, so the specification's
gradient-domain embodiments do not by themselves narrow "composite image"; the comparison above
therefore rests on the design facts. Decision (2026-10-20), after the prior-art element map in
[patent-research-preview.md](patent-research-preview.md) was built: A, keep the previews; that
record documents the published earlier work, and the re-check dates are on the watch list.

## 2. US 7768525 B2 (Microsoft, "Dynamic paint pickup"): the Mixer Brush

Status: Active, adjusted expiry 2028-03-07 (application 11/419,074, filed 2006-05-18, granted
2010-08-03; Microsoft Technology Licensing).

**Claims read.** Claim 1 (method): determine an injection color and store it in an injection data
store; copy pixels from a pickup surface of a different graphics type than the target surface into
a pickup data store; contaminate the paint data store's color attributes with the pickup pixels
and inject the injection color into it, both at a plurality of stamps after the first stamp;
determine a stamp location; paint pixels there from the contaminated and injected paint data
store. Claim 16 (media): claim 1 without the graphics-type element, with the mixing based on an
indicated percentage mixture value. Claim 14 (system): a data store, memory and a processor that
(i) copies pixels from the target surface, (ii) contaminates "a paint data store containing an
injection color attribute" with those pixels based on a mixture value that is an indicated
percentage, (iii) injects the injection color attribute into the paint data store, "wherein the
contaminating and the injecting are achieved together", and (iv) paints a stamp based on the paint
data store. Claim 15 adds repeating the functions during the stroke. Claim 14 alone has no
plurality-of-stamps or persistence element; the specification says the paint data store "may be
static over one or more stamps or may change from one stamp to the next" and may be any buffer.

**The 2026-08-14 record answered the wrong store.** It tested the persistent pickup average ("never
seeded from or blended with the foreground") and concluded the injection-into-store element is
missed. Against claim 14 the better candidate for the "paint data store" is the per-dab deposit
temporary in `mixer_brush_dab_color` (src/core/pixel_tools.cpp): it starts as the foreground color
(a store containing the injection color), is moved toward the pickup average by `ratio` (a product
of the Wet and Mix percentages, so a mixture value that is an indicated percentage), and is painted
as the stamp. Element by element: (i) copying pixels from the target surface: the 9x9 disc sampled
from the stroke-start snapshot of the target layer; present. (ii) contaminating a store containing
the injection color with those pixels by a percentage mixture: present in substance (the
contamination uses a decayed average of the copied pixels rather than the pixels themselves, a small
difference). (iii) injecting the injection color into the store, "wherein the contaminating and the
injecting are achieved together": the claim itself allows one operation to do both, so initializing
the per-dab temporary from the foreground and then moving it toward the pickup color can be read as
injection and contamination achieved together (the foreground share of the result is the injected
color, the pickup share the contamination). The first reading of this record, that no injection act
exists because the foreground is only an initial value that is consumed and discarded, is one
reading and not the only one. Element (iii) therefore admits both readings. (iv) painting a stamp
from the store: present.

**Comparison.** Claim 14 does not require persistence, the earlier record's reasoning does not
reach it, and element (iii) admits both readings; the related question (whether re-creating the
deposit from the foreground every dab is the same way of keeping the foreground present as
re-injecting it into a contaminated store) stays open as well. The term ends 2028-03-07.

**Alternative design recorded (not made).** Deposit the two colors as two
ordinary stamps instead of one mixed color: stamp the foreground at alpha a1 = a(1 - r)/(1 - ar),
then the pickup color at alpha a2 = ar, each through the normal stroke compositor (a = the dab
alpha after dry-out, r = the deposit ratio). Over(Over(canvas, fg, a1), pickup, a2) expands to
canvas(1 - a) + a(1 - r) fg + ar pickup, exactly the current lerp-then-over, so output is unchanged
up to rounding. No variable ever holds a mixture of foreground and pickup: there is no paint data
store containing the injection color to contaminate (ii) and nothing to inject (iii); the
persistent average stays canvas-only as before. Effort: small to medium (the dab callback in
canvas_widget_brush.cpp returns two stamps; mixer dabs cost two composites; the stroke compositor
must composite the second stamp over the first inside the stroke buffer, which already takes
per-dab colors). Test to pin it: a core test that the mixer dab API yields a foreground stamp and
a pickup stamp and no mixed color, plus a pixel comparison of a calibrated stroke against the
current reference within 1/255 (`mixer_brush_pickup_average_follows_canvas_and_dries_only_at_wet_zero`
stays as the model test). Element comparison of the two-stamp design: stamp one (foreground at a1)
copies nothing (i), contaminates no store since its color is the pure foreground (ii), injects
nothing (iii), and paints a stamp (iv); stamp two (pickup at a2) uses a color copied from the target
surface (i) and paints a stamp (iv), and the only place the two colors meet is the canvas pixel after
stamp one, which could be read as "a paint data store containing an injection color attribute" that
stamp two "contaminates" (ii, iii). Whether the paint data store of claim 14 is a buffer distinct
from the target surface being painted (the claim names both) decides whether (ii) and (iii) are
absent, and that reading is not settled. The algebraic identity would also have to be verified in
the real stroke compositor across dab alpha, the Opacity cap, Flow accumulation, Wet-0 dry-out and
every blend mode, where two sequential stamps need not equal one mixed stamp. Decision
(2026-10-20): A, the per-dab deposit stays as it is.

## 3. US 8971623 B2 (Apple, "Overlaid user interface tools for applying effects to image"): Tilt-Shift grips

Status: Active, adjusted expiry 2032-12-26 (filed 2012-09-27, priority 2012-03-06, granted
2015-03-03).

**Claims read.** Claim 1 (medium, GUI): a display area; an image processing tool for (i) receiving
a selection of a location of the image, (ii) overlaying on the image a user interface item
comprising "a first line located to a first side of the selected location that divides the image
into a first area that includes the location and a second area that does not include the location
and a second line located to the other side of the location that divides the image into a third
area that includes the location and a fourth area that does not", (iii) defining the effect area
as the second and fourth areas, (iv) applying the effect there. Claims 9 and 15 are the same with
a touch input at the location. Dependents: horizontal lines (2, 3, 13, 19), a user-adjustable
distance between the lines (4, 11, 17), blur (5, 8), a predefined initial distance (10, 16), an
amount varying with distance from the location (7), tilt-shift (14, 20), darkening (12, 18). The
specification describes "two visible horizontal lines overlaid on the image" that are moved and
spread by dragging; it never says they run edge to edge, and the figures show full-width lines.

**Patchy** (`draw_tilt_shift_overlay`, src/ui/zoomable_image_preview.cpp): the user places the
center (i, present), the blur applies outside the band (iii and iv, present), and each band edge is
marked by a 52 px grip bar centered on the axis through the center, clipped inside the image.
Element (ii) is where the designs differ. A short bar does not divide the image into two areas: both
sides stay connected around its ends, so the overlaid item does not comprise lines that divide the
image on that reading. The other reading: the bar is the visible part of the boundary line the tool
uses, and the defined area is exactly the two half-planes it marks, so "line" means that boundary
and the bar merely draws less of it.

**Comparison.** The claim does not say edge to edge, so the grip design differs from element (ii)
on the first reading above and not on the second. The record's "nothing spans or divides the
image" describes the drawing correctly, no more. Published earlier work describing on-image band
interfaces (Instagram, Snapseed and TiltShift Generator before 2012-03-06) is noted in the main
record.

**Alternative design recorded (not made).** Draw no boundary marks at all:
delete `draw_boundary` and its two calls, keep the filled focus-handle dots and the hollow
transition-handle dots already drawn at the same points, the rotation handle line (it passes
through the location rather than lying to one side of it, so it is not a flanking line) and the
center crosshair. Dots are not lines and divide nothing. Effort: small (about 20 lines in
zoomable_image_preview.cpp; the pinned tests `ui_filter_gallery_tilt_shift_overlay_uses_grip_bars`
and `ui_filter_gallery_tilt_shift_grips_never_span_the_visible_image` become one "no boundary
marks" test; the README Tilt-Shift screenshot is re-shot). Usability cost: the band's edges are
shown only by the handle dots and by the preview blur itself. Alternative: keep the bars, with a
re-check at 2032-12-26. Decision (2026-10-20): B, draw no boundary marks; implemented the same
day: `draw_boundary` removed from
`draw_tilt_shift_overlay`, the two grip tests merged into
`ui_filter_gallery_tilt_shift_overlay_has_no_boundary_marks`.

## 6. Status sweep of application-number and "pending" entries

- **US 2022/0207115 A1** granted as **US 11914681 B2** on 2024-02-27 (Adobe, application
  17/138,237, filed 2020-12-30, Active, adjusted expiry 2041-11-20). Independent claims 1, 9 and 16:
  receive a request for a sequence of two editing operations, determine each operation's set of
  supported features (claim 9: supported image file types with a file-type hierarchy), select a
  commonly supported feature by comparing the sets, and execute the sequence with it. Nothing
  about content-derived parameters. Patchy has no operation pipeline or feature negotiation, so
  the auto-adjustment caveat that pointed at this application is closed; nothing to watch.
- **US 2023/0129341 A1** (Adobe object-mask previews): Google Patents still shows it as pending.
  USPTO full-text search lists patent **12731259** under its exact title; the grant's front page
  could not be fetched (truncated downloads), so the application number and the allowed claims are
  unconfirmed. Watch item kept in [patent-research-selection.md](patent-research-selection.md).
- **US 2019/0130548 A1** (Adobe, propagating changes across similar shapes in artboards) granted as
  **US 10402954 B2** on 2019-09-03 (Active, adjusted expiry 2037-12-22). Conclusion unchanged.
- **US 2018/0059919 A1** (Adobe Responsive Design Controls): Abandoned.
- **US 2024/0362825 A1** (Adobe transformer matting continuation, filed 2024-07-02): still pending.
- **US 2025/0104196** granted as US 12561768 (already in the G'MIC record; the main record now
  says so).
- **Microsoft US 2022/0109891 A1** (application 17/552,295, the rANS continuation): still pending.
- **US 8427559 B2** (Buades, Coll, Morel non-local means; US application 11/579,380 filed
  2005-04-13): Expired - Fee Related. Used in item 4.

## Decisions (October 20, 2026) and watch list

- Item 1: A (decided 2026-10-20 after the prior-art element map in
  [patent-research-preview.md](patent-research-preview.md) was built): the reduced-resolution Move
  and Free Transform previews stay as they are; that record documents the published earlier work;
  re-check at the dates below. The map's "what would complete the record" list stays an optional
  follow-up.
- Item 2: A, keep the Mixer Brush deposit as it is.
- Item 3: B, the Tilt-Shift overlay draws no boundary marks (handle dots, rotation line and
  center crosshair only).
- Item 4: no change (A).
- Item 5: B (revised later on 2026-10-20, after the reviewer's note on Corresponding Source):
  the four binaries, the license texts and the `.afs` sources left git; `scripts/fetch-test-plugins`
  downloads them by hash into `local-test-fixtures` and the tests skip without them. Record in
  `test-fixtures/photoshop-plugins/README.md`; the binaries remain in the history before that date.
- Watch list: re-check Microsoft US 7768525 at 2028-03-07 (Mixer Brush deposit), Adobe
  US 9058699 at 2029-02-02 and US 8380005 at 2031-12-23 (reduced-resolution composite
  previews), Apple US 8971623 at 2032-12-26 (any new on-image band control), and patent
  12731259 once its application number is confirmed.

## Not verified

USPTO fee and assignment records for every status above; the application number and claims of
patent 12731259; continuations of US 8380005 / US 9058699 filed after 2013; prosecution history
that could narrow "composite image" or "resolution"; any Adobe filing on proxy previews under
other titles; the allowed claims of US 2023/0129341 A1.
