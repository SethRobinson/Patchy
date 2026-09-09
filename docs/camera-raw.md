# Camera raw import

Read before changing RAW decoding, develop defaults, or preview quality.

## Decoder and precision

Vendored LibRaw 0.22.1 (`src/formats/libraw/`, static `patchy_libraw`, PRIVATE into
`patchy_formats`; public header LibRaw-free). Licensing/build rules: the CMake
comment and NOTICE-THIRD-PARTY.md. CDDL-1.0 elected; stock tarball only, demosaic
packs are GPL.

`raw_document_io.{hpp,cpp}` develops to 16-bit sRGB with an explicit sRGB transfer
curve; LibRaw defaults to BT.709. `raw_tone.{hpp,cpp}` applies contrast, highlights,
and shadows through one composed 65536-entry LUT, then saturation/vibrance, before
rounded 8-bit output. The Natural profile runs first, on 16-bit sRGB. Shadow lift
is pinned at black; the highlight ramp is not pinned at white, so -100 dims blown
areas. Adjustment sliders default to zero independently of the selected profile.

`DevelopSession` opens metadata and the embedded thumbnail before unpacking, then
retains unpacked sensor data for repeated developments. Decoding
uses `open_buffer`, never narrow file paths. `raw_white_balance.{hpp,cpp}` maps
temperature/tint through `cam_xyz`: Planckian below 4000 K, CIE daylight above,
tint as Duv offset, bisection for the inverse As Shot display. Without a usable
matrix, treat as sRGB. LibRaw floats are not byte-stable across toolchains; tests
assert statistics, never hashes.

## Defaults, noise reduction, and white balance

New photos use Natural rendering, As Shot WB, exposure 0, brightness 1, neutral tone/color controls,
histogram auto-brightening off, AHD, full output dimensions, and Auto noise
reduction. Patchy retains camera-to-sRGB conversion; no Adobe profiles are bundled.
Natural is Patchy's fixed photographic rendering, not an Adobe or camera-matching
profile. A self-authored monotone cubic curve deepens shadows, opens midtones and
rolls highlights gently toward white. It maps sRGB luma, scales all color
differences together to retain hue, and adds modest color enhancement that fades
on saturated colors. Gamut bounds limit that scale before quantization, avoiding
individual channel clipping. The curve and constants live in `raw_tone.cpp`.
Processing version 3 gives Natural brighter midtones, stronger contrast and more
color enhancement. Version 2 retains its original Natural curve and color strength.
Neutral bypasses this stage for straight camera-to-sRGB output. WB multipliers,
exposure, histogram behavior and the adjustment sliders are independent of it.

`effective_noise_reduction` owns the fixed ISO policy. Version 2 strengthens
wavelet reduction to control visible high-ISO brightness grain, retains FBDD,
and adds color-difference median passes:

| ISO | Wavelet threshold | FBDD | Color passes |
|---|---:|---|---:|
| Up to 400 | 0 | Off | 0 |
| 800 | 100 | Light | 1 |
| 1600 | 200 | Full | 2 |
| 3200 | 300 | Full | 2 |
| 6400 | 400 | Full | 2 |
| 12800 and above | 500 | Full | 2 |

Wavelet strength interpolates logarithmically; FBDD changes at the listed
thresholds; color passes change at 800 and 1600. ISO 4000 resolves to 332/Full/2
and ISO 5000 to 364/Full/2. Version 1 uses half the wavelet strength before
rounding, the same FBDD thresholds, and no color passes (ISO 5000: 182/Full/0).
Auto applies only to three-color 2x2
Bayer layouts with finite, positive ISO metadata. Other layouts and unknown ISO
get no automatic reduction, explained in the dialog. Auto values are read-only;
switching to Manual seeds all controls from effective values. Off disables all
three stages. Manual retains the existing wavelet/FBDD ranges and sensor support;
the new Color noise control selects 0..4 median passes on supported Bayer layouts.
LibRaw's stock post-demosaic 3x3 median filter cleans R-G/B-G differences while
retaining green detail. It is not luminance smoothing, and stronger settings can
soften thin colored detail. No new codec, demosaic pack or learned algorithm.

As Shot decoding uses the camera's recorded multipliers directly. Its displayed
temperature/tint is Patchy's matrix-based estimate, not Adobe's calibration.
Auto uses LibRaw's gray-world estimate. Completed developments return effective
multipliers and estimated temperature/tint. Pending Auto displays Calculating;
switching to Custom starts from the latest effective balance.

## Per-photo settings contract

`imports/showRawDevelopDialog` still controls the interactive dialog. Legacy
`imports/rawDevelop*` keys remain untouched and are never applied or migrated.
The only settings store is the source's complete filename plus `.rawprefs`, for
example `FX300416.ARW.rawprefs`. `raw_develop_settings.{hpp,cpp}` reads/writes UTF-8
JSON with `format: "patchy.rawprefs"`, `version: 1`, `processingVersion: 3`, and a
`parameters` object. No paths, window geometry, preview zoom, or hidden cache.

Store all normalized develop parameters, including explicit enum string tokens.
Require correct types, finite bounded numbers, integer wavelet strength, and
supported schema/processing versions. Inactive manual/custom parameters retain
their values. Preserve unknown root and parameter fields in supported sidecars.
Slider display rounding must not change untouched sidecar precision. Damaged,
unreadable, or unsupported sidecars yield a notice and default rendering; preserve
them unless the user explicitly replaces them. Changes to persisted processing
require a deliberate processing-version decision; never silently interpret a
newer version as version 1. Supported processing versions are 1, 2 and 3. Version 1
loads as Neutral with color-noise cleanup disabled and retains its original
wavelet/FBDD policy. Ordinary edits and saves retain version 1; untouched files
remain byte-identical. Version 2 also retains its original rendering on ordinary
edits. Reset, changing Profile, or changing Color noise selects the current
processing version. The dialog explains this when opening older settings. Versions 2 and 3
requires `profile` (`neutral`/`natural`) and integer `colorDenoisePasses` (0..4),
including inactive Manual values. No hidden migration or per-camera settings cache.

Open waits for matching accurate pixels, saves customized settings, and imports.
Done saves and closes without importing. Cancel discards the session. Reset
restores current defaults; Open/Done commits it by removing a recognized sidecar.
An untouched default import creates nothing. Unchanged unsupported files are
preserved on Open/Done; committing Reset over one requires explicit replacement.
Save uses `QSaveFile` atomic replacement without direct-write fallback and detects
external changes since loading. Failed Done stays in the dialog; failed Open
offers Retry, Cancel, or Open Without Saving. Neither operation writes RAW bytes.

The shared filename-opening path reads sidecars for interactive Open, Reopen,
dialog-disabled imports, scripts, and the connector. Automated opens never write
sidecars, irrespective of the dialog preference. Byte-buffer decoding has no
filename: it uses explicit parameters or defaults. Script signatures are unchanged;
the bundled scripting guide and `patchy.d.ts` document the filename behavior.

## Accurate processing and preview scheduling

`DevelopOptions` selects Draft or Final independently of the `half_size` output
choice. Draft uses LibRaw's fast half-size path with wavelet/FBDD/color cleanup
disabled. At the SCALE_COLORS entry checkpoint, supported Bayer drafts average
the black-subtracted four-channel sensor cells in place to a maximum long edge
of 1280, before WB, exposure and RGB conversion. The original rawdata geometry
remains intact for final processing. Other layouts use the regular half-size path
and reduce its output to the same bound. Draft-to-output geometry stays logical.
The latest draft's 16-bit sRGB decode is cached independently of profile, tone,
color, noise and output-size controls; WB, exposure, brightness and highlight
recovery invalidate it. Cache reuse never substitutes for an accurate decode.
Both qualities use the selected rendering profile. Final always runs full processing with selected demosaic/noise
settings. Final half-size output averages each 2x2 block after 16-bit tone/color
operations, including partial blocks at odd edges, before rounded 8-bit conversion.
Results include intended output dimensions, quality, effective processing and WB
metadata. `document_from_developed` lets Open reuse accurate preview pixels.

Cancellation is checked at LibRaw checkpoints and output rows. LibRaw recycles
on cancellation: before another decode, reopen/unpack retained bytes with the
original neutral decoder options. A previous wavelet threshold must not affect
active dimensions during re-identification. One worker owns each session.

The dialog has two bounded lanes, one for drafts and one for accurate processing,
each with one worker, one session and one latest pending request. The separate
draft lane avoids waiting for long LibRaw stages to reach a cancellation checkpoint.
Both lanes use the same retained file-byte snapshot. Edits invalidate old completions
and throttle draft requests to 80 ms even during continuous dragging. Slider release
requests a draft immediately. After 500 ms idle, refinement starts only when the
latest draft has appeared; the timer never replaces a queued draft with full work.
Superseded short drafts may finish into their decode cache; superseded accurate
work cancels at supported checkpoints. Closing cancels both lanes immediately.
Open reuses a matching accurate cache or waits; an early Open waits for the initial
draft to publish the file snapshot before starting the accurate lane. Draft pixels
are never imported and cannot overwrite a matching accurate completion. A
refinement error marks the remaining preview incomplete and offers Retry Preview.
Closing immediately disarms callbacks and cancels obsolete processing.

Worker progress reports monotone 0..100 completed-stage estimates, including
sensor unpacking and output-row processing. Long stages without LibRaw checkpoints
hold their last percentage; there is no elapsed-time animation. The dialog shows
percentages while updating/refining, ignores stale reports, and caps them at 99
until image/document conversion completes. Idle drafts are labeled as waiting for
refinement. Percentages describe processing work, not estimated time remaining.

`ZoomableImagePreview` has logical output dimensions independent of its current
bitmap. Draft replacement preserves fit mode, zoom, pan, and image coordinates.
Below 100%, worker-prepared `QImage::scaled(..., SmoothTransformation)` images
average source samples at physical display resolution. Cache identity includes
source generation and size derived from zoom, viewport, and device pixel ratio.
One pending latest request bounds work. Painting/panning reuse the cache; 100%
and above retain precise pixel viewing. Shared users, including Filter Gallery,
receive the same scaling fix with alpha and overlay coordinates intact.

## Formats and tests

RAW sources are read-only: no writer, empty `save_extensions`; `save_document()`
routes to Save As with `<basename>.psd` via `is_read_only_source_extension`.
`raw::camera_raw_extensions()` owns the extension list. Ambiguous `.raw` is
excluded; TIFF-based raws stay on Qt.

Synthetic fixtures use `tests/synthetic_dng.hpp`. Real samples belong in untracked
`local-test-fixtures/raw/` and tests skip absent samples. Clean-error gaps:
lossy/deflate DNG, JPEG-XL DNG 1.7, Nikon High Efficiency NEF.
