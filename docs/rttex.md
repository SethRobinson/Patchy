# Proton textures (.rttex): wire layout, RTPack parity, verification

Full record for the Proton SDK texture reader and writer. Read this before touching
`rttex_document_io.*`, the `saveOptions/rttex*` keys, or the `patchy.rttex.*` session
metadata. Registry and filter-table wiring rules live in [file-formats.md](file-formats.md).

## What the format is

`.rttex` is the texture container of Seth Robinson's Proton SDK (the engine behind the
RTsoft games). It exists because early mobile GPUs wanted power-of-two textures while the
game still needed to know which part of the texture was the real image: the header records
the padded texture size AND the true image size, and the engine scales its texture
coordinates by the ratio. Everything below was verified against the Proton source
(`shared/util/RTFileFormat.h`, `shared/util/ResourceUtils.cpp`, `shared/Renderer/Surface.cpp`,
`shared/Renderer/SoftSurface.cpp`, `RTPack/source/TexturePacker.cpp`) and against every one of
the 6675 `.rttex` files in the Proton tree.

Two things the format is commonly misremembered as doing, and does not:

- There is no PNG inside. Lossless textures are raw pixels; the compression is the RTPACK
  zlib wrapper that RTPack adds in a second pass. The engine treats a non-JPEG embedded
  payload as raw RGB, so a PNG payload would render as garbage.
- JPEG payloads never carry alpha. The census found 249 embedded-file textures, all JPEG,
  all with `bUsesAlpha = 0`, no bytes after the JPEG, one mip level, reserved fields zero.
  RTPack's `ImageCanBeUltraCompressed` refuses images with transparency. The "JPEG plus
  alpha" memory is `JPGSurfaceLoader`'s synthetic alpha channel, added at load time for
  non-power-of-two JPEGs so premultiplied blending hides padding seams; it is not file data.

## Wire layout

Everything is little-endian and naturally packed (Proton's structs carry no `#pragma pack`;
the layouts below are what its compilers produce and what the files contain). Patchy reads
and writes them field by field through `binary_le.hpp`, never through native structs.

RTPACK wrapper, 32 bytes, present on every file RTPack's second pass touched:

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 6 | `"RTPACK"` | not NUL terminated |
| 6 | 1 | version | always 0 |
| 7 | 1 | reserved | 0 |
| 8 | 4 | compressedSize | bytes after this header |
| 12 | 4 | decompressedSize | exact inflated size; the engine inflates into exactly this |
| 16 | 1 | compressionType | 1 = zlib. 0 = none is defined but never written, and the engine cannot read it; Patchy accepts it as stored |
| 17 | 15 | reserved | 0 |
| 32 | | zlib stream | RFC1950 (`78 9C` header, adler32 trailer); miniz `mz_compress` / `mz_uncompress` produce and consume it |

The same wrapper carries `.rtfont` (an "RTFONT" record with a whole `.rttex` appended) and
`.rtpak`. Patchy's sniff accepts the wrapper because the payload cannot be checked without
inflating; the reader then reports a non-texture payload with a clear message.

`rttex_header`, 100 bytes (note that height precedes width in both headers):

| Offset | Size | Field |
|---|---|---|
| 0 | 6 | `"RTTXTR"` |
| 6 | 1 | version (0) |
| 7 | 1 | reserved |
| 8 | 4 | height (padded texture height) |
| 12 | 4 | width (padded texture width) |
| 16 | 4 | format |
| 20 | 4 | originalHeight (true image height) |
| 24 | 4 | originalWidth (true image width) |
| 28 | 1 | bUsesAlpha |
| 29 | 1 | bAlreadyCompressed (1 for JPEG payloads; advisory, nothing in the engine reads it) |
| 30 | 2 | reservedFlags |
| 32 | 4 | mipmapCount |
| 36 | 64 | reserved (16 ints) |

Then `mipmapCount` times a 24-byte `rttex_mip_header` (height, width, dataSize, mipLevel,
two reserved ints) followed by `dataSize` bytes. Mip 0 pixels start at offset 124. Every
file in the tree has one mip level: the non-PVR RTPack build always writes 1 and the engine
generates GL mipmaps itself. Patchy reads level 0 only (with a notice when more exist) and
always writes 1.

`format` values:

| Value | Meaning | Pixel |
|---|---|---|
| 5121 | `GL_UNSIGNED_BYTE` | RGB 3 bytes when bUsesAlpha is 0, RGBA 4 bytes when 1; straight alpha |
| 33635 | `GL_UNSIGNED_SHORT_5_6_5` | u16, R bits 15..11, G 10..5, B 4..0 |
| 32819 | `GL_UNSIGNED_SHORT_4_4_4_4` | u16, R 15..12, G 11..8, B 7..4, A 3..0 |
| 35840..35843 | PVRTC 4 and 2 bpp | rejected; no files in the tree use it |
| 20000000 | `RT_FORMAT_EMBEDDED_FILE` | a complete JPEG at the padded size; a payload without the FF D8 marker is raw RGB888 |

Census of the tree: 6055 raw 8-bit (4198 with alpha), 267 RGB565, 104 RGBA4444, 249 JPEG.

## Orientation and padding

Raw formats are stored BOTTOM-UP: file row 0 is the bottom screen row. The image is
anchored at the top-left of the padded canvas with transparent black to its right and
below, so in file order the padding rows come first and the image occupies the last
`originalHeight` rows in columns `0..originalWidth-1`. This follows from
`Surface.cpp:950-975`, which maps the image's top edge to texture coordinate `t = 1`, and
was confirmed on real files. Embedded JPEGs are top-down (the JPEG loader flips after
decoding, the writer does not).

Patchy opens a texture at its TRUE size, cropping the padding, and regenerates the padding
on save. With RTPack `-stretch` the recorded original size equals the texture size, so
there is nothing to crop. A recorded original size of 0 or larger than the texture is
treated as unpadded, with a notice.

No density is recorded; the UI's `kDensitylessFormats` opens the file at 72 PPI.

## RTPack parity

The writer mirrors `WriteTextureWithoutPVR` and the RTPack switches:

| RTPack | Patchy option | Behavior |
|---|---|---|
| default | Encoding Rgba8 | RGBA8888, demoted to RGB888 when every pixel is opaque |
| `-force_alpha` | force_alpha | keep RGBA even when opaque |
| `-4444` | Encoding Rgba4444 | RGBA4444, or RGB565 when opaque; rounded quantization `(v * max + 127) / 255`, which the reader's bit replication inverts exactly for 0 and 255 |
| `-ultra_compress q` | Encoding Jpeg + jpeg_quality | embedded JPEG only when the image has no transparency; a transparent image is written as RGBA8888 instead and the status message says so (RTPack silently does the same) |
| default padding | PowerOfTwo Pad | each axis to the next power of two, unchanged when already one |
| `-stretch` | PowerOfTwo Stretch | resample to the power-of-two size through `scale_pixels_resampled` (the Image Size resampler); the original size then equals the texture size |
| `-nopowerof2` | PowerOfTwo None | keep the exact size |
| `-force_square` | force_square | both axes become the larger one, in every mode |
| second pass with no flags | compress (default on) | RTPACK zlib wrapper; RTPack refuses to wrap a file already starting with "RTPACK", Patchy simply writes the wrapper or not |
| `-flipv` | none | a legacy toggle that writes top-down raw data the engine then draws upside down; not exposed |
| `-mipmaps` | none | only the PVR build honors it |

The JPEG encoder is Qt's, injected into the Qt-free formats library once at startup by
`install_rttex_jpeg_codec` (a capture-free lambda over `QImageWriter`), the same mechanism
as `ico::set_png_codec`. With no encoder installed, `Encoding::Jpeg` throws. Decoding an
embedded JPEG uses the vendored stb_image, so reading needs no injection.

## Settings, metadata, and the save flow

Persisted defaults (`saveOptions/*`, compatibility contracts, never renamed):
`rttexEncoding` (`rgba8`|`rgba4444`|`jpeg`), `rttexJpegQuality` (1..100, default 90),
`rttexPowerOfTwo` (`pad`|`stretch`|`none`), `rttexForceSquare`, `rttexForceAlpha`,
`rttexCompress` (default true). The token helpers live with the codec
(`rttex::encoding_token` and friends) so the settings, the dialog, and the metadata below
cannot disagree.

The reader stamps session-only document metadata (`patchy.rttex.encoding`,
`patchy.rttex.powerOfTwo` = `pad` when both texture sides are powers of two else `none`,
`patchy.rttex.compressed`, and `patchy.rttex.forceAlpha` when the file carried an alpha
channel that is entirely opaque). `MainWindow::image_save_defaults_for_document` reads them,
so a plain Save keeps a 4444 or JPEG texture as it was and Save As prefills the dialog.
Nothing serializes `DocumentMetadata::values` into any file.

Save As and Export raise `rttexSaveOptionsDialog` (encoding, JPEG quality greyed out unless
JPEG is selected, texture size, force square, force alpha, compress). The row is in
`file_format_entries()` unconditionally: the codec is Patchy's own, so every platform reads
and writes. The format stays out of `save_extension_preserves_layers`, so a layered
document keeps the flatten warning and save-a-copy semantics.

## Tests and fixtures

`tests/core/rttex_tests.cpp` pins the byte layout of an uncompressed write against a
hand-built vector, the padding and crop math, alpha detection, the 16-bit quantization,
stretch and force-square, the RTPACK wrapper (inflated with the test's own miniz call),
the JPEG hook (a stub encoder returns the JPEG lifted from a committed fixture, so the
write path and the stb decode both run without Qt), the committed fixtures against values
from an independent Python decoder, the rejection paths, and a Unicode path round trip.
`tests/ui/flat_image_format_tests.cpp` covers the open-and-save-in-place flow, the JPEG
write through Qt's encoder, the settings persistence, the dialog, and the prefill.

Committed fixtures under `test-fixtures/rttex/` are real RTPack output from the Proton
sample apps (provenance in NOTICE-THIRD-PARTY.md): raw RGB and RGBA with and without
padding, an embedded JPEG, RGBA4444, and RGB565. Larger real textures go to untracked
`local-test-fixtures/rttex/`; the sweep test skips when the directory is absent.

Independent verification recipe: a Python decoder (zlib + struct) that parses both headers
and reads a pixel by screen coordinates, and Proton's own loader through the prebuilt
`shared/win/utils/RTPack.exe`. `RTPack.exe -o bmp <copy>.rttex` re-reads a texture through
the engine's `SoftSurface` loader and writes `test_<name>.bmp` beside it; RTPack rewrites
its input in place, so only ever run it on a copy in a scratch directory.
`rttex_writes_inspection_artifacts` leaves one Patchy-written texture per encoding and
size mode under `test-artifacts/rttex/` beside the core test binary, which is the input for
that cross-check. Invoke it as `RTPack.exe -8888 -o bmp <copy>.rttex`: without a
pixel-format switch RTPack only compresses. What the September 2026 run established:

- Every power-of-two raw 8-bit variant (RGBA padded, RGB padded, uncompressed, stretched,
  force-square) and the UI round trip decoded pixel-identically through RTPack, as did a
  texture RTPack itself had built from a fresh PNG.
- The BMP RTPack writes has no alpha channel: padding shows as opaque black and a
  translucent pixel comes back premultiplied. That is the BMP, not the texture.
- An exact-size (`-nopowerof2`) texture comes back anchored at the bottom-left of a
  re-padded canvas with every pixel intact: RTPack pads a bottom-up rttex input from the
  bottom, unlike a PNG input. The engine's GL loader uploads such a texture at its stated
  size and never re-pads.
- 16-bit textures cannot be checked this way: `SoftSurface::LoadRTTexture` accepts only
  8-bit raw and embedded payloads (the GL loader handles 4444 and 565), so those variants
  are covered by the Python decoder only.
- Embedded JPEG (Patchy's and RTPack's own akiko.rttex alike) matches away from edges and
  differs by a few levels within two pixels of a hard edge: two libjpeg builds with
  different chroma upsampling, not an orientation or size problem.

Known gaps: PVRTC payloads are rejected (no decoder, no files use them), mip levels beyond
0 are ignored, and `-flipv` output opens upside down exactly as the engine would draw it.
