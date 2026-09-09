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
rounded 8-bit output. Shadow lift is pinned at black; the highlight ramp is not
pinned at white, so -100 dims blown areas. Defaults are neutral.

`DevelopSession` retains unpacked sensor data for repeated developments. Decoding
uses `open_buffer`, never narrow file paths. `raw_white_balance.{hpp,cpp}` maps
temperature/tint through `cam_xyz`: Planckian below 4000 K, CIE daylight above,
tint as Duv offset, bisection for the inverse As Shot display. Without a usable
matrix, treat as sRGB. LibRaw floats are not byte-stable across toolchains; tests
assert statistics, never hashes.

## Defaults, noise reduction, and white balance

New photos use As Shot WB, exposure 0, brightness 1, neutral tone/color controls,
histogram auto-brightening off, AHD, full output dimensions, and Auto noise
reduction. Patchy retains camera-to-sRGB conversion; no Adobe profiles are bundled.
Neutral controls also mean no photographic base tone curve or camera look. Adobe
Color applies its own color and tone rendering underneath zeroed adjustment
sliders, so matching slider values does not imply matching developed pixels.
Displayed temperature/tint estimates alone are not a measure of that difference.
`effective_noise_reduction` owns the fixed processing-version-1 ISO policy:

| ISO | Wavelet threshold | FBDD |
|---|---:|---|
| Up to 400 | 0 | Off |
| 800 | 50 | Light |
| 1600 | 100 | Full |
| 3200 | 150 | Full |
| 6400 | 200 | Full |
| 12800 and above | 250 | Full |

Wavelet strength interpolates logarithmically; FBDD changes at the listed
thresholds. ISO 5000 resolves to 182/Full. Auto applies only to three-color 2x2
Bayer layouts with finite, positive ISO metadata. Other layouts and unknown ISO
get no automatic reduction, explained in the dialog. Auto values are read-only;
switching to Manual seeds both controls from effective values. Off disables both
engines. Manual retains the existing algorithms/range and LibRaw's sensor support.
This policy reduces noise; it does not reproduce Camera Raw's noise processing.
Residual color speckling remains visible in high-ISO Sony shadows. LibRaw's
post-demosaic color-difference median filter is currently disabled. Any added
base rendering or color-noise stage needs an explicit processing-version contract
for existing sidecars and validation of thin colored details across the corpus.

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
JSON with `format: "patchy.rawprefs"`, `version: 1`, `processingVersion: 1`, and a
`parameters` object. No paths, window geometry, preview zoom, or hidden cache.

Store all normalized develop parameters, including explicit enum string tokens.
Require correct types, finite bounded numbers, integer wavelet strength, and
supported schema/processing versions. Inactive manual/custom parameters retain
their values. Preserve unknown root and parameter fields in supported sidecars.
Slider display rounding must not change untouched sidecar precision. Damaged,
unreadable, or unsupported sidecars yield a notice and default rendering; preserve
them unless the user explicitly replaces them. Changes to persisted processing
require a deliberate processing-version decision; never silently interpret a
newer version as version 1.

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
choice. Draft may use LibRaw's fast half-size processing, bypassing Bayer
demosaic/FBDD. Final always runs full processing with selected demosaic/noise
settings. Final half-size output averages each 2x2 block after 16-bit tone/color
operations, including partial blocks at odd edges, before rounded 8-bit conversion.
Results include intended output dimensions, quality, effective processing and WB
metadata. `document_from_developed` lets Open reuse accurate preview pixels.

Cancellation is checked at LibRaw checkpoints and output rows. LibRaw recycles
on cancellation: before another decode, reopen/unpack retained bytes with the
original neutral decoder options. A previous wavelet threshold must not affect
active dimensions during re-identification. One worker owns each session.

The dialog has one decoder worker and one latest pending request. Edits invalidate
old completions and request a draft after 200 ms; 500 ms idle or slider release
requests accurate refinement. Open reuses a matching accurate cache or waits;
draft pixels are never imported. Drafts remain visibly marked as refining. A
refinement error marks the remaining preview incomplete and offers Retry Preview.
Closing immediately disarms callbacks and cancels obsolete processing.

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
