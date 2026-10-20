# Patent research record: Smart Sharpen (`smartSharpen`) and Sharpen Edges

Scan of October 20, 2026. Claim text was taken from Google Patents (US 7724980, US 8687913,
US 9142009, US 8750639, US 7792386, US 7853096, US 7783127) and on USPTO full-image PDFs when
Google Patents rate-limited the scan (US 8391627, US 7412110, US 8260080, US 8897588, US 9076205,
US 9349164, US 9619870, US 8594418, plus cover pages of US 9392173, US 9779489, US 9632679).
Statuses are Google's estimates or cover-page arithmetic (filing date plus the printed term
adjustment), unchecked against USPTO fee or assignment records. The binding rules are in
[legal-constraints.md](legal-constraints.md); earlier sharpening reads are in
[patent-research.md](patent-research.md) (local Sharpen brush, Unsharp Mask).

Question: may Patchy render an imported Smart Filter whose descriptor is Photoshop's Smart
Sharpen (`smartSharpen`), and may Patchy offer a Smart Sharpen command that authors one? A
renderer that follows a stored descriptor performs the same method as one driven by a dialog, so
importing does not avoid a method claim: what matters is what Patchy computes. Equally, what
Photoshop computes internally does not bind Patchy; only Patchy's own computation is compared with the claims.

## What a `smartSharpen` descriptor can hold

Amount (%), Radius (px), noiseReduction (%, edge-adaptive in Photoshop's description), Remove
(gaussianBlur, lensBlur, motionBlur, the last with an Angle), useLegacy (legacy mode adds More
Accurate), presetKind (a dialog preset label, no pixel effect), and Shadows and Highlights groups
of Fade Amount, Tonal Width and Radius. Photoshop CS2 (2005) introduced the filter with Remove,
Angle, More Accurate and the Shadows/Highlights tabs; that version is now the "legacy" mode.
Photoshop CC (shipped June 2013) replaced the default mode with what Adobe's launch material
called adaptive sharpening that minimizes noise and halos, and added Reduce Noise.

## Patents read

| Patent | Holder, subject | Status (Google or cover) | Every independent claim requires |
|---|---|---|---|
| US 7724980 B1 | Adobe, selective sharpening | Active, adjusted expiry 2029-03-24 (12th-year fee paid 2021) | Claims 1, 12, 23: user input of a "sharpening limit"; a portion of pixels "less than all" chosen by that limit and by each pixel's "sharpness ... relative to others"; sharpening only that portion, none outside it. |
| US 8687913 B2 | Adobe, deblurring by local patch self-similarity | Active to 2032-07-17 (12th-year fee paid 2025-10) | Claims 1, 8, 15: "down-sampling an input blurred image"; convolving the downsized image with a blur kernel; for each input patch "finding a corresponding patch" in the smoothed image; high-frequency components from those patch pairs applied to the input patches. |
| US 9142009 B2 | Adobe, patch-based locally content-adaptive sharpening | Active to 2033-07-30 (8th-year fee paid 2023) | Claims 1, 21: differences between the image and its blur where the input patch and blurred patch "are not located at corresponding locations". Claims 10, 19, 20: patch updates using "a second location of a similar patch" different from the patch's own (19 adds halo or ringing reduction from that intensity, 20 a nearest-similar search in a proximity), accumulated into a high-frequency image. |
| US 8750639 B2 | Microsoft (now listed Zhigu Holdings), automatic sharpening | Expired, fee related: lapse effective 2026-06-10 | Amount "automatically" calculated from histogram deviations at two resolutions, then a thresholded unsharp mask. Lapsed; listed for completeness. |
| US 9349164 B2 | Adobe, deblurring energy with non-local-means | Division of a 2012-11-12 filing, PTA 0: about 2032-11-12; fee status unchecked | Claims 1, 7: a known two-dimensional blur kernel and an interim latent image fed to an energy function that "includes a non-local-means denoising operation"; an approximation image added to that operation; minimization starting from a black approximation image. |
| US 8897588 B2 | Adobe, data-driven edge-based deblurring | About 2033-02-02 (PTA 74 days) | A "sharp patch library", predicted sharp image, and "an estimated blur kernel" generated from it, iterated. |
| US 9076205 B2 | Adobe, edge direction and curve deblurring | About 2033-02-11 (PTA 84 days) | Pixels selected by edge-direction groups or near-right-angle curves, then "generating ... a blur kernel" from them. |
| US 9619870 B2 | Adobe, scale-adaptive blind deblurring | Terminal disclaimer to a 2015-04-22 filing: about 2035-04 | Observations at two scale levels, "calculating a first estimated blur kernel", accumulating estimates, deconvolving with the estimate. |
| US 9392173, US 9779489, US 9632679 | Adobe, Camera Shake Reduction family | About 2034-02, 2033-10 and 2035-08 | Titles, abstracts and covers only: kernel estimation from light streaks, automatically suggested estimation regions, a UI for managing estimated kernels. Claims not read in full. |
| US 7853096 B1, US 8260080 B1 (parent US 7412105) | Adobe, tone selective adjustment (Shadow/Highlight) | Expired: 2024-04-14 (Google) and about 2023-10-28 (continuation of a 2003-10-03 filing, PTA 25 days) | Neighborhood size, tone function shape and strength parameters; shadow and highlight neighborhood intensities; local weights from them; adjusting each location by those weights. |
| US 7783127 B1 | Adobe, determining adjustment parameters | Expired 2023-10-03 | Automatic parameter search over test values. |
| US 7792386 B1 | Adobe, difference kernels | Expired 2024-09-18 | Incremental convolution through a difference kernel along a scanline. |
| US 7412110 B1, US 8391627 B1 | Adobe, forward and backward kernels (Lens Blur depth of field) | 7412110: filed 2004-01-28 plus 678 days, about 2025-12-06; 8391627 is terminally disclaimed to it | A backward kernel per neighbouring pixel "specified based on a local attribute" (depth), combined with the forward kernel. Expired, and a fixed-kernel sharpen has no per-pixel backward kernel. |

Also surfaced and set aside: US 8594418 B2 (Adobe, ex Pixmantec; PCT filed 2006-02-16 with 1866
days PTA, about 2031-03) claims concurrently presented controls for several modification
categories combined into one modification set applied in a single composite operation. It is not
about sharpening; it belongs to a separate review of any multi-category adjustment panel, not to
one filter. No Adobe patent naming Smart Sharpen, sharpening fade by tone, or a user-entered
motion angle for deconvolution was found (an absence, not a finding of freedom).

US 8750639 is Microsoft's, not Adobe's, and it lapsed in 2026 (corrected in patent-research.md).

## Findings per mode

Plain unsharp masking (output = input + amount * (input - Gaussian(input)), colocated, every pixel
sharpened) meets no independent claim above: no sharpening limit or pixel subset (US 7724980), no
down-sampling or patch correspondence (US 8687913), no non-colocated patch difference or similar
patch (US 9142009), no estimated kernel (the deblurring family), no NLM energy (US 9349164). It is
also decades-old prior art, and Patchy's Unsharp Mask is already reviewed.

1. **Remove = Gaussian Blur, Noise Reduction 0, no fade: ALLOWED.** Computed as unsharp masking with
   the user's Radius and Amount and no threshold, it lacks the elements listed above. In legacy
   mode Photoshop's own output is widely described as Unsharp Mask's (Adobe's help says Gaussian
   Blur is "the method used by the Unsharp Mask filter"), so Patchy can aim for pixel parity
   there, subject to calibration. In the CC (non-legacy) mode Photoshop runs its 2013 adaptive
   algorithm. US 9142009 was filed six weeks after that release, by an Adobe inventor whose
   specification contrasts the invention with unsharp masking and Smart Sharpen; that suggests,
   without proving, that Photoshop's non-legacy mode practices it. Patchy rendering that mode with
   unsharp masking is ALLOWED but will not match Photoshop's pixels, and reproducing those pixels by
   patch search would be NOT ALLOWED under US 9142009 (to 2033-07-30) and possibly US 8687913 (to
   2032-07-17).
2. **Gaussian Blur with Noise Reduction above 0: ALLOWED under a fixed design, otherwise NOT ALLOWED.**
   Allowed design: the unsharp-mask detail is multiplied by a per-pixel gain computed from a fixed
   local gradient magnitude of the low-pass image and one scale derived only from the user's
   Noise Reduction value, the gain kept strictly above zero (a floor, never an exclusion), with no
   ranking, percentile, histogram or image-statistics normalization. That is soft coring and
   adaptive unsharp masking, both decades-old published techniques. It misses US 7724980's
   "portion ... less than all" and "relative to others" elements (every pixel is sharpened, by an
   absolute measure) and every patch element of US 8687913 and US 9142009. NOT ALLOWED forms: excluding
   a user-set share of the least sharp pixels (US 7724980, to 2029-03-24); patch search,
   non-local or self-similar denoising feeding the high-frequency image (US 9142009, US 8687913);
   an NLM term inside a deconvolution energy (US 9349164, about 2032-11-12). Not reviewed, so not
   allowed without a new check: guided filtering, BM3D, histogram bilateral (US 8594445 already
   binds Surface Blur).
3. **Remove = Lens Blur: ALLOWED only as textbook non-blind deconvolution; UNRESOLVED beyond it.** A
   disk PSF built from the user's Radius, inverted by a fixed number of Richardson-Lucy or Van
   Cittert iterations or one Wiener/Tikhonov solve with a fixed regularization (raised only by the
   user's Noise Reduction), forms every residual at colocated pixels and never estimates the
   kernel. It meets no claim read: no patch correspondence or down-sampling, no estimated kernel
   (US 8897588, US 9076205, US 9619870 and siblings), no NLM energy, no per-pixel backward kernel.
   Those methods date from 1931 to 1974 and are the prior art the deblurring patents cite. Two
   cautions: Photoshop describes its Lens mode as detecting edges and detail, which Patchy would
   only approximate, so imported stacks would not match Photoshop's pixels; and the sweep covered
   Adobe only. Non-Adobe deblurring patents (ringing suppression, sparse or hyper-Laplacian
   priors, multi-scale or edge-adaptive regularization) were not swept: any of those additions
   needs its own check.
4. **Remove = Motion Blur with Angle: same verdict as Lens Blur.** The PSF is one straight line
   from the user's Angle and Radius (the reviewed Motion Blur kernel), inverted by the same
   textbook non-blind method. Every Adobe deblurring claim read requires estimating the kernel
   from the image; a user-entered kernel misses that element. Never estimate angle or length from
   content, never use a curved or path kernel (the motion-path family, US 9723204, to 2035), never
   vary the kernel per pixel.
5. **Legacy mode and More Accurate: ALLOWED under the same conditions.** Legacy behavior has been
   public since Photoshop CS2 (April 2005); every Adobe filing found from that window has expired
   (US 7792386, US 7412110 and its continuation, the Shadow/Highlight family). More Accurate as a
   fixed repeat count of the same colocated correction (iterated unsharp masking or more
   deconvolution iterations) adds no claimed element; US 9142009 iterates too, but its claims
   turn on patches, not iteration. Do not make the iteration count, radius or amount automatic.
6. **Shadows/Highlights fade: ALLOWED.** A per-pixel attenuation from a Radius-sized neighborhood
   luminance and a Tonal Width curve, scaled by Fade Amount, is the tone-selective weighting of
   the Shadow/Highlight family, whose claims would apply to it but which expired between October
   2023 and April 2024 (US 7412105, US 7853096, US 8260080; US 7783127 expired 2023-10-03). Keep
   the weight a fixed function of local luminance and the three user values; no automatic
   parameter choice.

## Approved boundary (decided October 20, 2026): legacy only

Patchy renders and round-trips an imported `smartSharpen` entry only when `useLegacy` is true,
`blur` is Gaussian (`GsnB`), `noiseReduction` is 0, and no other key (`moreAccurate`, a Shadows
or Highlights fade) is present. It runs Patchy's calibrated Unsharp Mask with the entry's Amount
and Radius and threshold 0. COM probes (October 20, 2026, noise, ramp, impulses, mixed alpha)
showed Photoshop's legacy Gaussian output byte-identical to its Unsharp Mask at 100%/2 px and
250%/0.7 px; More Accurate, a Shadows fade and Lens Remove each change the pixels. In legacy
mode Photoshop ignores Noise Reduction (20% rendered identically), but the approved case still
requires 0. Parameter edits stay inside that case; no Filter-menu command authors the filter.
Every other descriptor stays preview-locked and byte-preserved.

## Reviewed, not approved

The following wider boundary was reviewed above and is NOT approved; implementing any of it
needs a recorded decision first:

Patchy may render and author `smartSharpen` when its computation is:

- one Gaussian low-pass at the user's Radius (Gaussian mode) or one fixed user-derived PSF (disk
  for Lens, straight line for Motion), never estimated from the image;
- colocated residuals only: unsharp masking, or Richardson-Lucy, Van Cittert, Wiener or Tikhonov
  non-blind deconvolution with a fixed iteration count or regularization;
- Noise Reduction as a strictly positive per-pixel gain from a fixed gradient measure and the
  user's value alone;
- Shadows/Highlights fade as a fixed tone weight from local luminance and the user's Fade Amount,
  Tonal Width and Radius;
- More Accurate as a fixed repeat count.

Never, while the cited patents are active: patch search or similar-patch differences (US 9142009,
to 2033-07-30), down-sampled patch correspondence (US 8687913, to 2032-07-17), sharpening only a
ranked share of pixels under a user limit (US 7724980, to 2029-03-24), NLM inside a deconvolution
energy (US 9349164, about 2032-11-12), any blur-kernel estimation or automatic radius, angle or
amount (the Camera Shake Reduction family, 2033 to 2035).

Suggested staging: render and author the Gaussian modes (legacy and CC, any Noise Reduction under
the design above, any fade) first; Photoshop parity is a goal only for legacy Gaussian with Noise
Reduction 0, and the CC modes carry a documented tolerance. Keep imported Lens Blur and Motion
Blur descriptors byte-preserved and preview-locked until a recorded decision accepts the textbook-deconvolution
boundary and the parity gap, since Patchy's output there cannot match Photoshop's.

## Sharpen Edges (`ShrE`), approved (October 20, 2026)

Photoshop's Sharpen Edges (Photoshop 1.0, 1990) stores no settings. Fitted from COM probes:
per channel, out = S + w * (Sharpen(S) - S), where Sharpen is the 3x3 Sharpen kernel (centre 8,
cross -1, / 4, rounded) and w = clamp(floor(|Sobel(S)|) - 64, 0, 191) / 191, quantized to 1/255;
alpha follows the same rule. It matches Photoshop within one level on 99.65% of samples.

Element by element:
- US 7724980 claim 1 needs a user-set "sharpening limit" and sharpening only a subset of pixels
  chosen by their sharpness relative to other pixels (a ranking). Sharpen Edges has neither: a
  fixed per-pixel weight from the local gradient with hard-coded constants, no ranking against
  other pixels, no user control.
- US 8687913 and US 9142009 need patch matching; US 9349164 a non-local-means term. None present.
- US 8750639 (Microsoft) lapsed on 2026-06-10 and no longer binds.
- The feature predates every one of these filings.

Rule: the threshold (64) and the weight span (191) stay fixed constants. They are never user-set,
never ranked against other pixels, and never derived from the image.

## Not verified

Fee and assignment status of US 9349164, US 8897588, US 9076205, US 9619870 and the Camera Shake
Reduction siblings; US 8260080's status beyond cover-page arithmetic; full claims of US 9392173,
US 9779489, US 9632679 and US 8705881; Adobe continuations of US 9142009 or US 8687913; any
non-Adobe deblurring or adaptive-sharpening patent; the US 7724980 assignment to GM recorded in 2006, which looks like a records
error.
