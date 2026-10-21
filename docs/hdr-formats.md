# Radiance HDR, OpenEXR and JPEG 2000

Read before changing `hdr_document_io`, `exr_document_io`, `jp2_document_io`, the vendored
tinyexr or OpenJPEG, or their filter-table rows. Registry and filter wiring rules live in
[file-formats.md](file-formats.md); the licensing record in
[legal-constraints.md](legal-constraints.md) and [patent-research-formats.md](patent-research-formats.md).
Added October 16, 2026 for GitHub issue 117 after a corpus sweep showed these as the
unsupported formats worth having.

## Radiance HDR (.hdr, .rgbe)

- `src/formats/hdr_document_io.{hpp,cpp}`, Patchy's own code, no library. Reads the text
  header (`#?RADIANCE` or `#?RGBE`, `FORMAT=32-bit_rle_rgbe`; XYZE and other orientations
  than `-Y h +X w` / `+Y h +X w` throw a translated error), then RGBE scanlines: new-style
  per-channel run-length coding (`2 2 hi lo`) for widths 8..32767, flat quadruples with the
  old `1 1 1 count` repeat marker otherwise. `rgbe_to_float` uses Ward's `(m + 0.5) / 256 *
  2^(e - 128)`; `float_to_rgbe` the matching `frexp` encode. EXPOSURE is ignored (values open
  as stored, as Photoshop does).
- Opens as a 32-bit floating-point RGBA document (alpha 1.0), linear light, no profile
  ([high-bit-depth.md](high-bit-depth.md)). Writes the flattened document as RGBE with
  run-length scanlines; 8 and 16-bit documents are linearized (sRGB transfer) first because
  the format carries radiance, 32-bit ones are written as stored. Lossy at about 1 percent
  (8-bit mantissas); the writer's own RLE keeps literal runs under 128 and encodes runs of 3+.

## OpenEXR (.exr)

- `src/formats/exr_document_io.{hpp,cpp}` over the vendored tinyexr 1.0.9
  (`src/formats/tinyexr/tinyexr.h`, BSD-3-Clause, `TINYEXR_IMPLEMENTATION` in that one
  translation unit, threads and OpenMP off, ZIP through the vendored miniz, whose directory is
  a private include of `patchy_formats` because tinyexr names the header bare). Reads a
  single-part scanline or tiled image (half, float or uint channels, every one requested as
  FLOAT; NONE, RLE, ZIPS, ZIP, PIZ) through the low-level `ParseEXRHeaderFromMemory` +
  `LoadEXRImageFromMemory` pair, mapping R, G, B (+A) or a lone Y channel (+A) into a 32-bit
  float document and pasting level-0 tiles by `offset * tile_size`. `ParseEXRVersion` rejects
  multi-part and deep files with their own messages; a file without R/G/B or Y gets its own
  too, and tinyexr's reason (an unknown codec such as B44, DWA, PXR24 or ZSTD) is appended
  to the generic decode failure. Non-finite samples become 0, alpha is clamped to 0..1.
- Writes four planar FLOAT channels (A, B, G, R, the sorted order the format wants) with ZIP
  compression; 8 and 16-bit documents are linearized first. Round trip is exact for 32-bit
  documents (`exr_round_trips_float_rgba`).

## JPEG 2000 (.jp2, .j2k, .j2c, .jpc, .jpx, .jpf)

- `src/formats/jp2_document_io.{hpp,cpp}` over the vendored OpenJPEG 2.5.3 core library
  (`src/formats/openjpeg`, BSD-2-Clause; `cmake/patchy_openjpeg.cmake` builds it as the static
  `patchy_openjpeg` with `OPJ_STATIC`, warnings off like the other vendored code, and writes
  the two configuration headers itself: MSVC gets `_aligned_malloc`, the others
  `posix_memalign` and `fseeko`; no `MUTEX_*` define, so `thread.c` is the single-threaded
  fallback). Memory streams through `opj_stream_set_*_function`; the JP2 signature box picks
  `OPJ_CODEC_JP2`, a bare `FF 4F FF 51` codestream `OPJ_CODEC_J2K`.
- Reads the first image: 1 component gray, 2 gray+alpha, 3 RGB or sYCC (converted, chroma
  planes repeated to full resolution when subsampled), 4 RGBA or CMYK (naive ink mix plus a
  notice). Signed samples are offset to unsigned, precision 1..16 is rescaled to 8 bits (or
  16 when the first component exceeds 8). An embedded ICC profile lands in
  `color_state().embedded_icc_profile`. Writes a lossless (5/3 reversible, one layer, up to
  six resolutions) JP2 at the document's integer depth; alpha is written only when some pixel
  is not opaque, and float documents flatten to 8-bit. `jp2_round_trips_losslessly_at_8_and_16_bits`
  pins the exact round trip.

## PQ and HLG

`src/formats/hdr_transfer.{hpp,cpp}` (Qt-free, decode only) turns PQ (SMPTE ST 2084) and HLG
(BT.2100) signals into the 32-bit model: linear light, sRGB primaries, 1.0 at SDR reference
white, which ITU-R BT.2408 puts at 203 nits (`kSdrReferenceWhiteNits`, the same constant
libjxl's output is scaled by in the JPEG XL reader, [jxl.md](jxl.md)). PQ codes become
nits / 203; HLG codes go through the inverse OETF, then the BT.2100 reference OOTF at a
1000-nit nominal peak (gamma 1.2, luminance weights of the source primaries), which puts the
75% signal on 203 nits. A 3x3 matrix built from the source chromaticities (Bradford-adapted
when the white differs) takes the result to sRGB primaries; colors outside sRGB stay
negative. `signal_rgba16_to_linear_srgb` runs the transfer through a 65536-entry table, so a
frame costs one table lookup per sample plus the matrix. Users: the AVIF and Linux HEIF readers
([avif.md](avif.md), [heif.md](heif.md)); macOS HEIF lets CoreGraphics do the same
conversion and was measured to agree, Windows HEIF receives scRGB from WIC. `hdr_transfer_decodes_pq_and_hlg_reference_points` pins the
reference points and matrices. Licensing record: [patent-research-formats.md](patent-research-formats.md)
("PQ and HLG decoding").

## Shared wiring

- Registry rows `patchy.formats.hdr`, `patchy.formats.exr`, `patchy.formats.jp2`
  (format_registry.cpp) with sniffers; filter-table rows in `file_format_entries` with the
  primary extension as the save extension; writer branches in `write_flat_image_file`; the
  three identifiers in `kDensitylessFormats` (72 PPI on open); the extensions in the Windows
  "Open with" list (`InstallPatchy.ps1`). The open-failure diagnosis names these formats when
  a file's bytes do not parse ([file-formats.md](file-formats.md)).
