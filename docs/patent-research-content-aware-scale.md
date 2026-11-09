# Patent research: Content-Aware Scale (seam carving)

Claim-level record behind the "Content-Aware Scale" bullet in [legal-constraints-filters.md](legal-constraints-filters.md) (GitHub issue 681; research of 2026-11-09, claim text and legal events read on Google Patents). Companion to [patent-research.md](patent-research.md). An engineering record of the request and of the published claims considered; act on the binding bullet, not on this record alone. Nothing is built yet.

## The request

Issue 681 asks for Edit > Content-Aware Scale: resize a layer so that low-detail areas absorb the change while people and objects keep their shape, also on a selection. In Photoshop (CS4 onward) the command puts transform handles on the layer or selection and resizes live while they are dragged, with an Amount (mix with plain scaling), Protect (an alpha channel kept intact) and Protect Skin Tones. The method underneath is seam carving (Avidan and Shamir, SIGGRAPH 2007): find the connected one-pixel path of least importance from one edge to the other, remove it (or duplicate it to grow), repeat.

## Patents read

Status and dates are Google Patents' header and Legal Events table as of 2026-11-09. "Lapsed" means a maintenance fee went unpaid and the USPTO recorded the patent as expired; a lapsed patent can be reinstated on petition, so the lapsed rows are re-read before any build.

### Active

- **US 7477800 B2, MERL, "Method for retargeting images"** (Avidan, Shamir; priority 2007-03-06, continuation of US 7747107, filed 2008-07-10, granted 2009-01-13; all three maintenance fees paid, the 12th on 2020-03-29; no term adjustment or terminal disclaimer shown; anticipated expiration **2027-03-06**). The seam carving patent itself. Independent claims, verbatim elements:
  - Claim 1: build an energy image in which each pixel holds an energy from an energy function; find from it a minimal-energy seam, one pixel wide, from one side to the opposite side; apply the seam to the source image to get a target image that keeps the rectangular shape; the energy function is the gradient magnitude of each source pixel.
  - Claim 14: the same, except the seam is applied to the energy image and the target is reconstructed from the next energy image; gradient magnitude again.
  - Claim 16 (real time): determine the order of seam applications by repeating energy image, seam, apply (any energy function); enlarge the source image with seams; build an index map giving each pixel of the enlarged image its seam order; pick the index for the wanted size; produce the target from the enlarged image and the map.
  - Plain seam carving with the usual gradient energy (Sobel or finite differences) meets every element of claim 1. Forward energy (Rubinstein, Shamir, Avidan 2008: the cost of the new pixel neighbours a removal creates, no per-pixel energy image) differs from the "energy image ... gradient magnitude" elements of claims 1 and 14, but its costs are absolute differences of neighbouring pixels, close to a one-axis gradient magnitude, and claim 16 names no energy function. Every claim ends on 2027-03-06.
- **US 8160398 B1, Adobe, "Independent resizing of multiple image regions"** (Avidan, Chien, Intwala; filed 2008-07-31; 12th-year fee paid 2023-10-17; expires **2031-02-16**). Claims 1, 23, 35: for each previously defined region of the image, smaller than the whole image, compute per-pixel energy from the pixel's value and resize that region by those energies, independently of and without affecting any other portion; output the resized regions plus the unchanged portions. Content-aware scaling of a selection inside a layer, the rest left as it was, is these steps.
- **US 8290300 B2, Adobe, "Seam-based reduction and expansion of images with color-weighted priority"** (Avidan, Chien, Intwala, Wang; filed 2008-07-31; 12th-year fee paid 2024-04-16; expires **2031-05-12**; Chinese family member CN 101777175 B). Claims 1, 15, 27: apply a weighting to one or more pixel values of the image indicating their importance for resizing; compute per-pixel energy dependent on that weighting; find the lowest-cost seam; resize along it. Dependent claims: a factor above one for a value range, skin-tone colors (claims 9, 10), a value range or a value picked by the user (claims 11, 25). Protect Skin Tones, and any color- or value-based priority, is this claim. The weighting is of pixel values (colors); a protect mask taken from the user's selection or a saved channel marks locations, not values.
- **US 8659622 B2, Adobe, "Systems and methods for creating and editing seam carving masks"** (Dhawan; filed 2009-08-31; 12th-year fee paid 2025-08-25; expires **2032-05-08**). Claim 1: compute per-pixel energy from neighbouring-pixel derivatives; generate a seam carving mask (pixels to protect or target) from that; show it as an overlay; let the user add or remove pixels; store it for a later edit. Claim 10: a suggested mask from differences of weighted energies, overlay, user edits, stored. A mask that starts from the user's own selection or channel, never computed from energy, lacks the generate-from-energy element.
- **US 8218900 B1, Adobe, "Non-linear image scaling with seam energy"** (Avidan, Chien, Intwala; filed 2008-07-31; 12th-year fee paid 2024-01-10; expires **2031-05-09**). Claims 1, 15, 27: per-pixel energy, lowest-cost seam, and a resize along the seam that applies an image scaling technique to the seam itself. Seam removal and seam duplication (the 2007 paper's insertion, averaging the seam with its neighbours) do not scale the seam; stretching or resampling a band around each seam does.
- **US 8581937 B2, Adobe, "Seam-based reduction and expansion of images using partial solution matrix dependent on dynamic programming access pattern"** (Intwala; filed 2008-10-14; 12th-year fee paid 2025-05-12; expires **2031-06-17**). Claims 1, 12, 21: seam carving along the lowest-cost seam, where the image is split into non-overlapping portions of a predetermined shape whose partial dynamic-programming solutions are independent, computed concurrently, then combined into the solution. Splitting the seam search into concurrently solved tiles is this claim; a single-threaded search per seam, with the energy map computed in parallel, is not.
- **US 8358876 B1, Adobe, "System and method for content aware in place translations in images"** (Gilra, Avidan; filed 2009-05-20; 12th-year fee paid 2024-07-22; expires **2031-04-23**). Claims 1, 10, 17: the user selects a portion to move and gives an amount and direction; seam-crop the region ahead of it, resize the region behind it, and output the image with the portion moved. Moving content by carving one side and growing the other is this claim.
- **US 8963960 B2, Adobe, "System and method for content aware hybrid cropping and seam carving of images"** (Gilra; filed 2009-05-20; 8th-year fee paid 2022-08-24, 12th due by 2027-02-24 with surcharge; expires **2032-02-01**). Claims 1 and 19 need no seams: a bounding box of a portion, the user drags edges or corners to give the amount, plus an input giving one edge or corner priority over the others, and a resize based on that priority. Claim 13 adds energies and seam costs from that priority. A per-edge or per-corner priority control on the handles is these claims; plain handles that only set the target size are not.

### Other assignees and later filings

Found by a wider search on 2026-11-09 (Adobe, Corel, Xerox, Google, universities). Where the fee record could not be read (Google Patents refused automated queries partway through), the expiry is filing date plus 20 years plus the term adjustment printed on the front page, marked "fees not read"; those are read before a build.

- **US 12437411 B2, Adobe, "Localized seam carving and expansion with configurable localization threshold"** (Gilra; filed 2022-11-14, granted 2025-10-07; about 2043-12). Claims 1, 8, 15: a user-chosen point inside one of several low-activity regions, a requested adjustment that includes the bounding box of an object to insert, seams chosen in that region, the number to duplicate set by comparing the seams with the box, then duplicate or remove them in the region. Local seam expansion to make room for an inserted object is these claims.
- **US 8483513 B2 and its continuation US 9111364 B2, Corel, "Method of content aware image resizing"** (filed 2010-01-22; US 8483513 "Active - Reinstated" to 2032-01-25, US 9111364 active to 2030-04-13). A path search on a reduced saliency map whose scale factor comes from the device or the content (US 9111364: an integer factor above one), then a change to the source along that path. A seam search on a downscaled energy map is these claims (Adobe's similar US 8280191 lapsed; these did not).
- **US 8340411 B2, Xerox, "Smart image resizing with color-based entropy and gradient operators"** (filed 2010-03-05; about 2031-07, fees not read). Claim 9: a per-pixel importance from the luminance and at least one chrominance value of neighbouring pixels, then seam removal by that importance. An energy built from a luminance and chrominance split is this claim; an energy from one luminance plane, or from each RGB channel's gradient summed, has no chrominance element.
- **US 8134578 B2, Xerox, "Hybrid importance maps for content aware digital image resizing"** (filed 2008-07-17; about 2030-06, fees not read). Several operators' importance maps, each normalized, blended with weights that sum to one, then a resize keeping pixels by the blend; no seams named. A user protect mask added to one energy map is not a normalized weighted blend of operator maps.
- **US 8270771 B2, Xerox, "Iterative selection of pixel paths for content aware image resizing"** (filed 2008-12-09; about 2031-07, fees not read). Candidate seams re-weighted by their distance to seams already chosen, then the best chosen. A distance penalty against earlier seams is this claim.
- **US 8792744 B2, Michigan State University, "Generalized multiscale seam carving framework"** (filed 2012-10-02; about 2033-03, fees not read). Energy maps filtered to smaller scales, upsampled and combined into a composite, then the lowest seam of the composite removed. A multiscale energy composite is this claim.
- **US 8873887 B2, Google, "Systems and methods for resizing an image"** (filed 2013-01-24; about 2033-05, fees not read). Given a seam cost threshold and a step size, remove min(count of seams under the threshold, step) seams per round, repeat, display. Batches set by a cost threshold are this claim; one seam per step is not.
- **US 7873211 B1, Google** (video seams with temporal coherence, about 2029-08) and the warp-based families (Seiko Epson US 8374462 anchor points, National Tsing Hua US 8295647, Hebrew University US 8249394 shift-maps, Disney US 9202258 and its art-directable video family, Samsung US 12260510 and US 12033297 importance-weighted warps): video or mesh and shift-map warps, no seam search on a still image.
- **US 8406482 B1, Adobe, automatic skin tone detection** (filed 2008-08-28; about 2031-05, fees not read): a two-stage image-specific skin model. Only relevant to skin protection, which stays out under US 8290300 anyway.
- Lapsed: Kodak US 8213745 (2020) and US 8184928, Thomson US 9741092 (2025-08-22), New York University US 9760973.

### Lapsed or ended

- US 7747107 B2 (MERL, the parent of US 7477800, any energy function): lapsed 2022-06-29.
- US 8380010 B2 (MERL, graph-cut seams and forward energy for images and video; priority 2007-11-30): lapsed, effective 2025-02-19.
- US 8400473 B2 (Interdisciplinary Center Herzliya, multi-operator retargeting): lapsed 2017-03-19.
- Adobe, all filed 2008: US 8280186 (energy from a look-up table), US 8280187 (energy from color frequency) and US 8280191 (seams found on a lower-resolution pyramid level) lapsed 2024-10-02; US 8265424 (seam replication with per-seam replication factors) lapsed 2024-09-11; US 8270765 and US 8270766 (one operation that seam-carves then scales the rest: Photoshop's Amount) lapsed 2024-09-18; US 8180177 (concurrent removal over strips of a retargeting matrix) lapsed 2024-05-15; US 8625932 (energy recomputed only in a neighbourhood of the removed seam) lapsed 2026-01-07.
- US 9031358 B2 (Qualcomm, video seams from straight-line energies) lapsed 2019; US 2022/0020113 A1 (Instasize, seams guided by object masks) was abandoned in 2023 without a grant.

### Outside the United States

EP 1968008 (MERL) shows as ceased in its family table. Not read on 2026-11-09 (Google Patents returned errors): JP 5111095 B2 (MERL, filed 2008, its 20-year term ends by 2028-03) and CN 101777175 B (Adobe, filed 2009-07-31, term ends by 2029-07-31). Both are read before a release that offers the feature.

## What a Patchy version can and cannot have

Before 2027-03-06: any seam search with a gradient-magnitude energy meets US 7477800 claim 1, and claim 16 covers a precomputed seam-order map of an enlarged image with any energy. A forward-energy-only design steps outside the literal elements of claims 1 and 14 but stays close to them (comparison above). The binding bullet holds the feature until that date.

From 2027-03-06, on a whole layer, these steps carry no element set of an active claim above:

- Energy from the layer's own gradients (or forward energy), a minimal one-pixel seam by dynamic programming, removal to shrink and the paper's duplicate-and-average insertion to grow, repeated per seam; recomputing energy only near the removed seam (US 8625932 lapsed); a seam-order map for a fast live preview (claim 16 ends with the patent).
- An Amount that seam-carves part of the change and scales the rest of the layer (US 8270765/8270766 lapsed).
- A protect mask from the user's selection or a saved channel, forcing seams around marked locations (the 2007 paper's user masks; the weighting of US 8290300 is of pixel values, and US 8659622 needs a mask generated from energy).
- Parallel energy computation; the seam search itself one dynamic-programming pass per seam, not split into concurrently solved tiles.
- An energy from a luminance plane or summed RGB channel gradients at full resolution, one seam removed or added per step, no penalty tied to earlier seams.

Kept out while the patents named run:

- Content-aware resize of a selection, or any region less than the whole layer with the rest unchanged: US 8160398, to 2031-02-16. Taking the layer's whole pixel extent as the image is the boundary; a one-click "copy the selection to a layer and carve it" in place of the selection mode needs its own review.
- Protect Skin Tones, a color or value range given priority, user-picked important colors, or any weighting of pixel values beyond the base energy: US 8290300, to 2031-05-12.
- A suggested or automatic protect mask computed from energy and shown for editing: US 8659622, to 2032-05-08.
- Stretching or resampling along seams (non-linear scaling): US 8218900, to 2031-05-09.
- Concurrent tile-wise seam search: US 8581937, to 2031-06-17.
- A seam search on a downscaled energy or saliency map: Corel US 9111364 to 2030-04-13 and US 8483513 to 2032-01-25.
- An energy that combines luminance with chrominance (Xerox US 8340411), a normalized weighted blend of several importance maps (US 8134578), a distance penalty against earlier seams (US 8270771), a multiscale energy composite (US 8792744), or seams removed in threshold-and-step batches (Google US 8873887): each about 2030 to 2033, fees not read.
- Local seam expansion sized to an object being inserted: Adobe US 12437411, about 2043.
- Moving a selected object by carving ahead of it and growing behind it: US 8358876, to 2031-04-23.
- Per-edge or per-corner priority inputs on the resize box: US 8963960, to 2032-02-01.
- Face or object detection steering the seams, learned saliency and video retargeting were not searched; each needs a new review.

## G'MIC Seamcarve

The engine's Seamcarve filters stay out of the allowlist until 2027-03-06 (US 7477800). After that date they are re-checked against the list above before any change to `scripts/dev/gmic/build_allowlist.py`: a filter that carves a selection or region alone, weights colors, or runs a tiled concurrent search stays out.

## Sources (read 2026-11-09)

- https://patents.google.com/patent/US7477800B2/en
- https://patents.google.com/patent/US7747107B2/en
- https://patents.google.com/patent/US8160398B1/en
- https://patents.google.com/patent/US8290300B2/en
- https://patents.google.com/patent/US8659622B2/en
- https://patents.google.com/patent/US8218900B1/en
- https://patents.google.com/patent/US8581937B2/en
- https://patents.google.com/patent/US8358876B1/en
- https://patents.google.com/patent/US8963960B2/en
- https://patents.google.com/patent/US8380010B2/en
- https://patents.google.com/patent/US8400473B2/en
- https://patents.google.com/patent/US8280186B1/en, US8280187B1, US8280191B1, US8265424B1, US8270765B1, US8270766B1, US8180177B1, US8625932B2 (same path)
- https://patents.google.com/patent/US9031358B2/en
- https://patents.google.com/patent/US20220020113A1/en
- https://patents.google.com/patent/US8483513B2/en, US9111364B2, US8213745B2, US9741092B2 (same path)
- USPTO front pages and claims: https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12437411 (same path for 8406482, 8340411, 8134578, 8270771, 8792744, 8873887, 7873211)
- https://helpx.adobe.com/photoshop/desktop/crop-resize-transform/resize-adjust-resolution/preserve-visual-content-when-scaling-images.html
- Avidan and Shamir, "Seam Carving for Content-Aware Image Resizing", SIGGRAPH 2007: https://www.eng.tau.ac.il/~avidan/papers/imretFinal.pdf
