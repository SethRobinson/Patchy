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

## Settings and displayed white balance

`imports/showRawDevelopDialog` defaults to true. The dialog loads global last-used
parameters from `imports/rawDevelop*`, saved on successful Open. These are not
per-file settings: opening a new photo without touching a control can still apply
an earlier photo's white balance, exposure, tone, color, denoise, and half-size
choices. Reset restores `DevelopParams{}`. Preference-off and headless paths use
neutral defaults, As Shot WB, AHD, full resolution, and disabled denoise.

Persisted keys never change. `rawDevelopHighlights` stores highlight RECOVERY;
the tonal slider uses `rawDevelopToneHighlights`.

As Shot decoding uses the camera's recorded multipliers directly. Its displayed
temperature/tint is Patchy's matrix-based estimate, not Adobe's calibration. Auto
uses LibRaw's gray-world estimate. Auto does not refresh the temperature/tint
controls with its computed white balance; they retain custom or As Shot values.

## Preview quality limitations

The worker is latest-wins and always develops half size, independently of the
Open at half size checkbox, which controls the final import. On Bayer sensors,
LibRaw's half-size path clears `filters` in `pre_interpolate()`. This skips both
the selected demosaic algorithm and FBDD in `dcraw_process()`. Wavelet denoise
still runs. AHD/FBDD controls therefore do not describe the effective Bayer
preview processing, and full-size final output can differ from the preview.

`ZoomableImagePreview::paintEvent` currently draws the full preview image into a
smaller rectangle with `QPainter::SmoothPixmapTransform`. At large reductions,
this samples too few source pixels and aliases high-ISO color noise. It is not
equivalent to `QImage::scaled(..., Qt::SmoothTransformation)`, which averages
the reduced image. Compare the same decoded RGB through both paths before
attributing fit-preview grain to RAW decoding or denoise. This affects display;
it does not alter the developed document's pixel data.

## Formats and tests

RAW sources are read-only: no writer, empty `save_extensions`; `save_document()`
routes to Save As with `<basename>.psd` via `is_read_only_source_extension`.
`raw::camera_raw_extensions()` owns the extension list. Ambiguous `.raw` is
excluded; TIFF-based raws stay on Qt.

Synthetic fixtures use `tests/synthetic_dng.hpp`. Real samples belong in untracked
`local-test-fixtures/raw/` and tests skip absent samples. Clean-error gaps:
lossy/deflate DNG, JPEG-XL DNG 1.7, Nikon High Efficiency NEF.
