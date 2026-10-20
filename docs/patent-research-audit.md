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

## Not verified

USPTO fee and assignment records for every status above; the application number and claims of
patent 12731259; continuations of US 8380005 / US 9058699 filed after 2013; prosecution history
that could narrow "composite image" or "resolution"; any Adobe filing on proxy previews under
other titles; the allowed claims of US 2023/0129341 A1.
