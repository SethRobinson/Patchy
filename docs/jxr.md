# JPEG XR (.jxr/.wdp/.hdp): WIC codec, HDR tone map

Full record for the JPEG XR reader and writer. Read this before touching `jxr_document_io.*`
or the tone-map constants. Registry and filter-table wiring rules live in
[file-formats.md](file-formats.md); the no-vendored-codec boundary lives in
[legal-constraints.md](legal-constraints.md).

## What the format is and why it is here

JPEG XR (ISO/IEC 29199-2, ITU-T T.832) is Microsoft's HD Photo / Windows Media Photo. A
TIFF-like container whose magic is `49 49 BC` plus a nonzero version byte; the version byte
is what separates it from TIFF, which shares the `49 49` little-endian byte order mark and
carries `2A 00` there. Lossy or lossless, alpha, tiling, and, unusually for a photo format,
8/16/32-bit integer **and 16/32-bit float** channels.

That float capability is the reason Patchy supports it: NVIDIA's in-game capture (GeForce
Experience / NVIDIA App) writes HDR game screenshots as `.jxr` in 32-bit float scRGB, and
the Windows Game Bar writes 16-bit half. Windows opens them because the Windows Imaging
Component has carried a JPEG XR codec since Vista.

## Windows only, by design

Both the decoder (`CLSID_WICWmpDecoder`) and the encoder are in-box on every supported
Windows, so unlike HEIF there is no Store package to probe, no missing-codec markers, no
Store deep link, and no codec of Patchy's own. `patchy_formats` already linked
`windowscodecs` and `ole32` for the HEIF reader, so this added no dependency.

No other platform has a decoder and Qt ships no JPEG XR plugin, so `read_jxr` and
`write_jxr` always throw there. The whole filter-table row is gated on
`jxr::is_available()`, following the PDF row's conditional-open pattern, so no platform
offers an open or a save that could only fail.

Files:

- `jxr_document_io.{hpp,cpp}`: extensions, sniff, the tone map, the Document-level writers,
  and the non-Windows throwing stubs. Qt-free and platform-neutral, so the tone map is
  pinned by tests on every platform.
- `jxr_document_io_win.cpp`: WIC decode and encode.
- `wic_com.hpp`: `ComPtr`, `CoInitGuard`, `hresult_text`, `create_srgb_transform`, shared
  with `heif_document_io_win.cpp` (they were extracted from it). Windows-only, included
  only from sources guarded by `WIN32` in CMakeLists.txt.

`jxr::jxr_extensions()` is the single source of truth for the registry, the dialog table
and the writer branch. `.wdp` and `.hdp` are the pre-standardization HD Photo extensions
the same codec handles.

## Read and write, unlike the other platform-codec format

The registry row carries a real writer. That is deliberate and load-bearing: a null writer
is what makes `is_read_only_source_extension` route Save to Save As with a `.psd` default,
which is right for HEIF and camera raw and wrong here. `ui_jxr_opens_and_saves_as_a_read_write_format`
pins the difference.

Save As and Export raise a quality plus Lossless dialog, persisting `saveOptions/jxrQuality`
(default 90) and `saveOptions/jxrLossless` (default false). Those keys are a compatibility
contract; never rename them.

Encoder details:

- WIC's `Lossless` property overrides `ImageQuality`, so the two are never written together
  and the dialog greys the slider out when Lossless is checked.
- `UseCodecOptions` stays false (its default), so the codec maps `ImageQuality` onto its own
  Quality/Overlap/Subsampling table rather than Patchy guessing at those three.
- An opaque flatten writes a 24bpp BGR frame, an alpha-carrying one 32bpp BGRA, so an
  ordinary screenshot does not grow a pointless alpha plane.
- `SetPixelFormat` negotiates: WIC may not honor the request, so pixels are packed to
  whatever it returns, not to what was asked for.
- Output is always 8-bit. Patchy's pipeline holds nothing deeper, so a file opened from an
  HDR capture is written back as the tone mapped image, which the dialog says out loud.

## The HDR tone map

The reader splits on the frame's numeric representation, read through
`IWICPixelFormatInfo2::GetNumericRepresentation`. That is deliberately not a match against a
list of float GUIDs: the query keeps working if the codec grows a format.

- **Integer** frames take the same path as HEIF: format converter to 32bppBGRA, then the
  file's ICC profile to sRGB through `IWICColorTransform`, then BGRA to RGBA. A notice
  reports the reduction when the source was deeper than 8 bits per channel.
- **Float and fixed** frames convert to 128bppRGBAFloat, which also unpremultiplies a
  `128bppPRGBAFloat` source, and go through `tone_map_scrgb_to_rgba8`. **No color transform
  runs on this path**: scRGB is implied by the pixel format rather than by an embedded
  profile, so a transform would double-correct. A notice reports the tone map.

Density follows the HEIF reader's rule exactly: WIC reports 96 DPI when a file records no
density, so that reading means untagged and takes Photoshop's 72 PPI convention. A file
genuinely tagged 96 is indistinguishable and gets the same treatment. `jxr` therefore must
NOT join `kDensitylessFormats` in `main_window_files.cpp`.

### Why the curve has the shape it has

scRGB is linear-light with sRGB primaries where 1.0 is the 80 nit reference white. An HDR
capture legitimately carries values far above 1.0, about 12.5 for a 1000 nit highlight, and
may carry small negative components for out-of-gamut colors. Three options were measured
against a synthesized float ramp before choosing:

| Approach | scRGB 0.25 | 0.5 | 1.0 (SDR white) | 12.5 | Verdict |
|---|---|---|---|---|---|
| Clamp at 1.0 (the 32-bit PSD rule) | 137 | 188 | 255 | 255 | Every highlight flattens to white |
| Hable filmic, white point 11.2 | 85 | 115 | 150 | 255 | SDR content opens dark and washed out |
| **Knee 0.5, ceiling 12.5 (shipped)** | **137** | **188** | **225** | **255** | SDR exact below the knee, 30 codes of highlight |

`highlight_rolloff` is therefore the identity at and below a knee of **0.5**, so shadows and
midtones come out byte-identical to a plain SDR conversion, and above it a rational rolloff
that places scRGB **12.5** exactly on display white with slope 1 at the knee, so the two
halves meet without a visible crease. The scale that satisfies both constraints is
`(1 - knee) * (max - knee) / (max - 1)`.

Alpha is linear coverage and takes neither the curve nor the sRGB transfer. Negative and NaN
color components floor at 0; a NaN alpha sorts to opaque instead.

The constants sit in one named block precisely so the natural follow-up, a Camera Raw-style
develop dialog with exposure and ceiling sliders, can turn them into parameters without
moving the math. Nothing else should hard-code them.

## Tests and known gaps

`tests/core/jxr_tests.cpp` plus two UI tests in `tests/ui/flat_image_format_tests.cpp`.

`test-fixtures/jxr/hdr-ramp.jxr` is self-authored (provenance in NOTICE-THIRD-PARTY.md): an
8x2 lossless 128bppRGBAFloat scRGB ramp whose row 0 walks 0..1 and row 1 walks 1..12.5. It
is the regression guard for the entire float path at once: pixel-format classification, WIC
preserving values above 1.0 through the float conversion, and the curve. A decoder that
clamped at 1.0 would flatten row 1 to a single value and move the expectations by 30 or
more, far outside the tolerance.

Conventions this format follows:

- Statistics and tolerances, never byte pins, for anything the platform codec touched. Same
  rule as HEIF and camera raw. The lossless round trip is the one exact comparison, and it
  compares decoded pixels, never encoder bytes.
- Codec-dependent tests `[SKIP]` where `jxr::is_available()` is false. The sniff, registry
  routing and tone-map tests are pure logic and run on every platform.
- Real captures dropped into untracked `local-test-fixtures/jxr/` are decoded and reported
  by `jxr_reads_real_captures_if_available`, which asserts the frame has range and did not
  blow out to white.

Known gap: EXIF orientation is not applied. Camera-produced JPEG XR is rare and NVIDIA
captures carry none. Adding it means moving `heif::apply_exif_orientation` to a
format-neutral home and reusing it rather than writing a second copy.
