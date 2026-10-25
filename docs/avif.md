# AVIF (.avif, .avifs)

Read before changing `avif_document_io`, the vendored libavif, dav1d or libaom, or their
filter and registry rows. Registry and filter wiring rules live in [file-formats.md](file-formats.md);
the licensing rule in [legal-constraints.md](legal-constraints.md) and the research record
in [patent-research-formats.md](patent-research-formats.md). Added October 16, 2026 for
GitHub issue 120: until then AVIF opened only through an OS decoder (the Windows AV1
extension, macOS ImageIO) and never in the AppImage.

## Read and write, everywhere native

- Windows, macOS, Linux Flatpak and AppImage decode in-process through libavif 1.4.2 over
  dav1d 1.5.4 and encode through libaom 3.15.2. The AV1 licensing position was accepted on
  October 16, 2026 (AOMedia's royalty-free patent grant versus the Sisvel pool; GIMP,
  Firefox, Chrome and ImageMagick ship AV1 codecs), and writing was added the same day
  because GIMP writes AVIF. dav1d stays the decoder (faster than libaom's); libaom is
  built encoder-only, so `avifDecoder` never sees it.
- The browser build (`EMSCRIPTEN`) compiles none of the three: `avif::is_available()` is
  false, the filter row is hidden, the registry reader and writer throw their browser
  messages. Qt's wasm build has no AVIF plug-in either, so AVIF stays unopenable there.

## Where it lives

- `cmake/patchy_avif.cmake` builds `patchy_dav1d` (the common sources plus the thirteen
  `*_tmpl.c` bit-depth templates compiled twice as `patchy_dav1d_8bpc` / `_16bpc` object
  libraries, `BITDEPTH` 8 and 16, exactly meson's split), `patchy_aom` (libaom's
  encoder-only library for the generic CPU: `CONFIG_AV1_DECODER 0`, every `HAVE_<simd>` 0)
  and `patchy_avif` (libavif's `src/*.c` with `codec_dav1d.c` + `codec_aom.c`,
  `AVIF_CODEC_DAV1D=1 AVIF_CODEC_AOM=1 AVIF_CODEC_AOM_ENCODE=1`, plus the five-file libyuv
  scaling subset upstream bundles for builds without a system libyuv; it is pure C, and
  libavif itself is C11 for `static_assert`). No assembly anywhere: no nasm or perl on any
  host, the same bytes on every toolchain, fast enough for stills. The generated headers
  are written by the CMake function from fixed facts per OS: dav1d's `config.h` and
  `vcs_version.h` (Windows: the win32 pthread shim and dav1d's MSVC `stdatomic.h` compat
  header, prepended because MSVC's own header needs an experimental switch in C; macOS: no
  `memalign`, no affinity calls; Linux: `_GNU_SOURCE`, `memalign`, affinity) and libaom's
  `config/aom_config.h` plus `aom_config.c` (Windows: `HAVE_PTHREAD_H 0`, so
  `aom_util/aom_pthread.h` wraps Win32 threads). libaom's three `*_rtcd.h` tables and
  `aom_version.h`, which upstream generates with perl, are arch-independent for the generic
  target and are checked in under `src/formats/libaom/patchy-config/config`, next to
  `sources.cmake`, the source list harvested from upstream's configure (regenerate both on
  an upgrade: configure upstream with `-DAOM_TARGET_CPU=generic -DCONFIG_AV1_DECODER=0`,
  copy `config/`, and list the objects of the library targets from `build.ninja`).
  Warnings are off for all three archives like every other vendored codec. Upstream sources
  are unmodified (`.gitattributes` keeps them byte-exact); `src/formats/dav1d` holds
  `COPYING`, `include/` and `src/` without the `arm`, `x86`, `ppc`, `riscv`, `loongarch`
  and `ext` assembly directories; `src/formats/libavif` holds `LICENSE`, `include/avif`, the
  needed `src/*.c` and `third_party/libyuv` with its LICENSE; `src/formats/libaom` holds
  `LICENSE`, `PATENTS`, `AUTHORS`, the library directories (`aom`, `aom_dsp`, `aom_mem`,
  `aom_ports`, `aom_scale`, `aom_util`, `av1/common`, `av1/encoder`) without their SIMD
  subdirectories, and `third_party/fastfeat` (BSD-3) and `third_party/vector` (MIT).
- `src/formats/avif_document_io.{hpp,cpp}`: the Qt-free reader. `sniff` accepts an ftyp
  whose major or compatible brand is `avif` or `avis`; `heif::sniff` still rejects those
  brands and this sniff rejects the HEVC brands, so the two readers never claim each other's
  files (`avif_decodes_synthetic_still_and_rejects_damage`).
- Registry row `patchy.formats.avif` (reader, writer, sniff); filter row "AVIF Image" (open
  and save, `.avif` the save extension, gated on `is_available()`); `kDensitylessFormats`
  entry; the `.avif`/`.avifs` branch in `load_document_from_path` runs before the registry
  row so sequence frames get translated "Frame N" names and `pixels_in_source_space` /
  `orientation_applied` reach File Info; the writer branch in `write_flat_image_file`
  (`image_format_preserves_alpha` and the metadata carriers list include `avif`); the
  extensions sit in the Windows "Open with" list.
- `image_open_diagnosis` has no AVIF-specific reason: a damaged `.avif` keeps the decoder's
  own message, and AVIF bytes under another extension get the rename hint.

## What the reader does

- `avifDecoder` with `ignoreExif`/`ignoreXMP` (File Info reads the items from the file
  through `extract_bmff`), `AVIF_STRICT_DISABLED` (older encoders left out `pixi` or wrote
  odd `clap` boxes; viewers open those), `imageSizeLimit` 268M pixels, threads from the
  shared worker budget (1 to 16).
- The primary image converts through `avifImageYUVToRGB` into `AVIF_RGB_FORMAT_RGBA`,
  straight alpha, at depth 8, or 16 when the file stores 10 or 12-bit samples and deep
  editing is on (libavif rescales to 16); PQ and HLG files take the HDR path below. Gray (4:0:0) and every chroma layout come out as
  RGBA; the document is always RGBA.
- Transforms are applied in MIAF order on the RGBA pixels: `clap` (through
  `avifCropRectFromCleanApertureBox`; an invalid box is ignored with a notice), `irot`
  (anti-clockwise quarter turns), `imir` (axis 0 top/bottom, 1 left/right). `orientation_applied`
  is always true: the AVIF rules make these properties replace any EXIF orientation, so
  File Info writes the tag back as normal.
- Color: an ICC profile lands in `color_state().embedded_icc_profile` with
  `pixels_in_source_space` true (libavif converts only the YUV matrix, not primaries or
  transfer). Without a profile, SDR nclx values are not converted. Gain maps are ignored
  (the SDR base image opens).
- HDR: a file without an ICC profile whose nclx transfer is PQ (16) or HLG (18) decodes at
  16 bits and goes through `hdr::signal_rgba16_to_linear_srgb`
  ([hdr-formats.md](hdr-formats.md#pq-and-hlg)) with the file's primaries
  (`avifColorPrimariesGetValues`; unspecified means BT.2020): with deep editing on it opens
  as a 32-bit linear sRGB document, 1.0 at 203 nits, out-of-gamut colors negative; with it
  off the same values go through the JPEG XR highlight rolloff to 8 bits with the shared
  "tone mapped" notice, as JPEG XL does. Sequences convert every frame the same way.
  `avif_pq_and_hlg_open_as_linear_32_bit` encodes PQ and HLG sources straight through
  libavif (the core tests link `patchy_avif` for this, `PATCHY_TEST_LIBAVIF`).
- Image sequences (`imageCount > 1`): one full-canvas layer per frame, frame 1 on top, named
  through `ReadOptions::frame_name` (the UI's translated "Frame N <delay>s"), delays from
  `imageTiming.duration`, `repetitionCount` in `avif.loop_count` (0 = forever, otherwise
  plays). A frame that changes size or fails to decode ends the import with a notice; the
  budget is 1G decoded pixels. Progressive layers are not exposed (`allowProgressive` off),
  so a progressive still opens as one image.

## What the writer does

- `write_avif(document, WriteOptions)`: the flattened document (8-bit flatten for 8 and
  32-bit documents, the deep flatten for 16-bit) becomes one `avifImage` at depth 8 or 12
  (AVIF's maximum; libavif rescales the 16-bit RGBA input), 4:4:4, full range, through
  `avifImageRGBToYUV` with `ignoreAlpha` unless some pixel is translucent. An RGB ICC profile
  goes in through `avifImageSetProfileICC` with nclx primaries/transfer unspecified;
  without one the nclx values say sRGB. Lossless selects the identity matrix at quality 100
  (8-bit pixels and alpha come back exactly, `avif_round_trips_losslessly_and_lossy_through_libaom`);
  lossy uses BT.601 at `quality` 0-100 with the alpha plane at least at 90. `speed` is
  libavif's 0 (slowest) to 10, default 6 like avifenc; `autoTiling` is on and the thread
  count comes from the shared worker budget. EXIF (a TIFF stream) and XMP from
  `prepare_save_metadata` are set with `avifImageSetMetadataExif`/`XMP`, so no injection
  step follows the encode. No animation writer: layered documents flatten (a follow-up if
  anyone asks; libavif's `avifEncoderAddImage` supports it).
- Options: `ImageSaveOptions::avif_quality` (80), `avif_lossless`, `avif_speed` (6);
  settings keys `saveOptions/avifQuality`, `avifLossless`, `avifSpeed`; the "AVIF Options"
  dialog (`avifSaveOptionsDialog`: quality slider, Speed spin box, Lossless check, the
  12-bit note, the shared metadata row); script `saveAs`/`exportAs` keys `quality` (0 to
  100), `lossless`, `speed` (0 to 10), `metadata`. The registry writer uses the defaults.
- Speed of the generic C encoder, measured October 16, 2026 on the 24-thread dev box: the
  800x600 sample saves in about 0.15 s at speed 6 (10 KB), 0.09 s at speed 8, 0.13 s lossless;
  large photos take seconds, which the dialog note mentions. A libaom upgrade or SIMD
  enablement repeats the licence read in [legal-constraints.md](legal-constraints.md).
- Tests (tests/core/flat_formats_misc_tests.cpp): the 511-byte lossless quadrant file
  embedded as bytes (`kQuadrantsAvif`, written by Pillow 12.1), the committed
  `test-fixtures/avif/frames.avif` sequence (three frames, 100/200/300 ms), and the
  twelve cgohlke/imagecodecs-samples files in `local-test-fixtures` (gray/RGB, alpha, 8 and
  16-bit, sequences; [SKIP] when absent), and the 8-bit lossless, 8-bit lossy and 16-bit
  round trips through libaom. Decoded colors of foreign files are checked to a tolerance of
  4, never byte-pinned: lossless AVIF from other encoders still goes through a YUV matrix.
  UI: `ui_avif_opens_and_saves_as_a_read_write_format`, `ui_avif_save_options_persist_and_dialog`,
  `ui_script_save_as_options_write_avif`.
