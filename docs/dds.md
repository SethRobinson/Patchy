# DirectDraw Surface (.dds): formats, alpha rules, writer, verification

Full record for the DDS texture reader and writer. Read this before touching
`dds_document_io.*`, the `saveOptions/dds*` keys, the `patchy.dds.*` session metadata, or
the vendored `bcdec`, `stb_dxt` and `bc7enc` sources. Registry and filter-table wiring rules live in
[file-formats.md](file-formats.md). The feature exists because the NVIDIA and Intel DDS
Photoshop plug-ins are `.8bi` format plug-ins, which the legacy host never runs
(GitHub issue 81); Patchy reads and writes DDS itself instead, on every platform.

## What the format is

DDS is the texture container of DirectX and of most game engines: a `"DDS "` magic, a
124-byte header, a 32-byte pixel format inside it, an optional 20-byte DX10 extension,
then raw pixel data. Nothing is compressed at the container level; the pixel data is
either uncompressed (described by bit masks or a DXGI format) or one of the GPU block
formats BC1 to BC7, which pack 4x4 texels into 8 or 16 bytes. Every writer disagrees with
every reader about the small print, so the rules below say what Patchy trusts.

## Wire layout

Everything is little-endian and read field by field through `binary_le.hpp`, never as a
native struct. Offsets are from the start of the file.

| Offset | Size | Field | Patchy |
|---|---|---|---|
| 0 | 4 | `"DDS "` (0x20534444) | required |
| 4 | 4 | dwSize | must be 124 |
| 8 | 4 | dwFlags | read; `DDSD_DEPTH` marks volume depth |
| 12 | 4 | dwHeight, 16 dwWidth | 1..16384 each, at most 2^28 pixels per image and summed |
| 20 | 4 | dwPitchOrLinearSize | ignored: legacy writers lie, sizes come from the format |
| 24 | 4 | dwDepth | volume slices (1 otherwise) |
| 28 | 4 | dwMipMapCount | 0 reads as 1; above 15 rejected |
| 32 | 44 | dwReserved1 | skipped |
| 76 | 32 | ddspf: dwSize (32), dwFlags, dwFourCC, dwRGBBitCount, R, G, B, A masks | see formats |
| 108 | 4 | dwCaps, 112 dwCaps2 (cubemap and face bits, volume), 116..127 caps3, caps4, reserved | read |
| 128 | 20 | DX10 header when dwFourCC is `DX10`: dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2 | read; arraySize 0 (Pillow) reads as 1, above 2048 rejected |

Pixel data starts at 128, or 148 with a DX10 header. For a 2D texture, cubemap or array,
every image (face, element) is stored with its whole mip chain before the next image. For
a volume texture every slice of level 0 comes first, then every slice of level 1, and so
on. Patchy reads mip 0 of every image and requires it to be present; a file cut short
inside its later mip levels still opens (the notice names the dropped level count).

## Formats the reader understands

Resolution order: a DX10 header wins, then a FourCC, then the bit masks.

- **Masked uncompressed** (`DDPF_RGB`, `DDPF_LUMINANCE`, `DDPF_ALPHA`, `DDPF_ALPHAPIXELS`;
  8, 16, 24 or 32 bits per pixel): decoded generically from the masks. A channel of n bits
  expands by bit replication when n < 8 (the GPU's rule, so 4-bit codes become code * 17 and
  0 and the top code land on 0 and 255) and by rounded rescale when n > 8 (10- and 16-bit
  channels). Luminance replicates into RGB; an alpha-only format (A8) opens as white with
  its alpha and a notice; a missing alpha mask means opaque. A mask written above the pixel
  size slides down to fit (Pillow writes its 8-bit luminance mask as 0xFF000000). The
  recorded source name is D3D style (`A8R8G8B8`, `X8R8G8B8`, `R5G6B5`, `A1R5G5B5`,
  `A4R4G4B4`, `A2R10G10B10`, `L8`, `A8L8`, `A8`). Palettized, YUV and bump-map (signed
  DuDv) formats are rejected by name.
- **Legacy FourCC**: `DXT1` (BC1 with 1-bit alpha), `DXT2`/`DXT3` (BC2, DXT2 premultiplied),
  `DXT4`/`DXT5` (BC3, DXT4 premultiplied), `ATI1`/`BC4U`/`BC4S`, `ATI2`/`BC5U`/`BC5S`, and the
  numeric D3DFMT codes 36 (A16B16G16R16), 113 (A16B16G16R16F) and 116 (A32B32G32R32F).
  Other FourCCs (the YUV family, R16F, G16R16F, R32F, G32R32F, CxV8U8) are rejected with the
  code in the message.
- **DXGI formats**: R32G32B32A32_FLOAT (2), R32G32B32_FLOAT (6), R16G16B16A16_FLOAT (10),
  R16G16B16A16_UNORM (11), R10G10B10A2_UNORM (24), R8G8B8A8_UNORM and _SRGB (28, 29),
  R8G8_UNORM (49), R8_UNORM (61, opened as gray), A8_UNORM (65), BC1 (71, 72), BC2 (74, 75),
  BC3 (77, 78), BC4 UNORM/SNORM (80, 81), BC5 UNORM/SNORM (83, 84), B5G6R5 (85),
  B5G5R5A1 (86), B8G8R8A8 and B8G8R8X8 with their _SRGB twins (87, 88, 91, 93),
  BC6H UF16/SF16 (95, 96), BC7 (98, 99), B4G4R4A4 (115). The TYPELESS twins of the block
  formats and of the 8-bit formats (27, 70, 73, 76, 79, 82, 90, 92, 94, 97) read as UNORM,
  which is what every other reader does and what Pillow's writer emits. Everything else is
  rejected by number. `_SRGB` variants decode to the same bytes: Patchy's 8-bit pipeline
  holds display-encoded values.
- **Block decode** goes through the vendored `bcdec` (`src/formats/bcdec/`, MIT or
  Unlicense, built with `BCDEC_BC4BC5_PRECISE` so the signed variants exist). Each 4x4
  block decodes into a scratch and only the in-image overlap is copied, so
  non-multiple-of-4 sizes (legal under DX10, and every small mip) never write outside the
  image. BC4 opens as gray; BC5 opens as red and green with blue 0 and a notice; SNORM
  values map -127..127 onto 0..255 (`((v + 127) * 255 + 127) / 254`).
- **Float sources** (half and float uncompressed, BC6H) are assumed linear with sRGB
  primaries and go through the JPEG XR HDR curve, `jxr::tone_map_scrgb_to_rgba8`
  ([jxr.md](jxr.md)), a strip of rows at a time (the curve is per pixel, so strips give
  the same bytes as a whole-image pass). `dds::half_to_float` is a bit-exact binary16
  converter (subnormals, infinities, NaN). The import notice says the pixels were tone
  mapped.
- **Layouts**: cubemap faces (`+X -X +Y -Y +Z -Z`, legacy files may hold a subset and are
  named in file order; DX10 cubemap arrays append the element number), volume slices
  (`Slice N`) and array elements (`Element N`) become pixel layers with only the first
  visible, the ICO convention. Import only: a save flattens to one 2D texture after the
  usual layered-document warning ([file-formats.md](file-formats.md), the Photoshop-style
  save guard; `ui_dds_cubemap_import_is_layered_and_save_routes_to_save_as` pins the
  routing). A 1D DX10 texture reads as height 1.

No density is recorded; the UI's `kDensitylessFormats` opens the file at 72 PPI.

## Transparency

Alpha is a first-class requirement of this format (Seth, October 2026):

1. Every alpha-bearing source puts its alpha into an `rgba8` layer (masked RGBA, A8, A8L8,
   BC1's punch-through texels, BC2's 4-bit alpha times 17, BC3, BC7, the DX10 RGBA, 16-bit,
   5551, 4444 and 1010102 formats, float alpha as linear coverage). Sources without alpha
   produce `rgb8`. `load_document_from_path` then runs `promote_flat_alpha_to_layer_mask`
   like for every flat format, so a translucent texture opens as an opaque RGB layer plus
   an editable layer mask carrying the document-alpha marker, and an RGBA file whose alpha
   is 255 everywhere opens as plain RGB.
2. `DXT2`, `DXT4` and a DX10 header whose `miscFlags2 & 7` is 2 (DDS_ALPHA_MODE_PREMULTIPLIED)
   are un-premultiplied after decoding with the integer rule `a == 0: rgb = 0`, otherwise
   `c' = min(255, (c * 255 + a / 2) / a)`, with one notice per document. Mode 3 (OPAQUE)
   forces alpha to 255 and the layer to `rgb8` with a notice; mode 4 (CUSTOM) keeps the
   channel as straight alpha with a notice; 0 and 1 are straight.
3. The writer keeps alpha: Uncompressed writes the flattened alpha verbatim, BC3 keeps 8-bit
   alpha through stb_dxt's alpha block, and BC1 has only 1 bit, so alpha below 128 cuts the
   texel out (a 3-colour block with index 3) and 128 and above stays opaque. When a BC1
   write cut partially transparent pixels, the notice reports how many
   ("BC1 keeps only 1-bit transparency: N partially transparent pixels were cut out at 50
   percent"). **Automatic resolves to BC1 when every flattened alpha is 255 and to BC3
   otherwise**, the NVIDIA tools' convention, so a translucent image never loses its alpha
   unless the user picks BC1 deliberately.
4. The writer flattens through `flatten_document_rgba8`, so a single pixel layer whose only
   mask carries the document-alpha marker exports non-destructively: the RGB stays the
   original colours and the mask becomes the alpha plane. Colours under alpha 0 do not
   survive a flatten, which is why the round-trip tests skip them.

## Writer

One 2D texture. `Compression` is append-only: `Automatic`, `Uncompressed`, `Bc1`, `Bc3`,
`Bc4`, `Bc5`, `Bc7`. Everything but BC7 takes a legacy header (Pillow, texconv, the
Photoshop plug-ins and every engine read it); BC7 exists only under a DX10 header.

- Header flags `CAPS | HEIGHT | WIDTH | PIXELFORMAT`, plus `PITCH` (uncompressed, `width *
  4`) or `LINEARSIZE` (level 0's block bytes), plus `MIPMAPCOUNT` with the level count when
  mipmaps are written (otherwise the count field is 0). Caps `TEXTURE`, plus `COMPLEX |
  MIPMAP` with mipmaps. Pixel format: `A8R8G8B8` (flags 0x41, 32 bits, masks R 0x00FF0000
  G 0x0000FF00 B 0x000000FF A 0xFF000000) for Uncompressed, including fully opaque images
  (one header shape); FourCC `DXT1`, `DXT5`, `ATI1` (BC4) or `ATI2` (BC5), the legacy
  spellings every reader knows (texconv writes `BC4U`, which Patchy also reads); `DX10` for
  BC7, followed by the 20-byte DX10 header (DXGI 98 BC7_UNORM, 2D, miscFlag 0, arraySize 1,
  miscFlags2 0 as texconv writes it), so BC7 pixels start at 148.
- Uncompressed rows are top-down B, G, R, A bytes. Block levels are 4x4 blocks row-major;
  texels outside a level (partial edge blocks, levels below 4 px) repeat the nearest edge
  texel before encoding so they never pull the endpoints toward black.
- BC1 four-colour blocks and BC3 blocks come from the vendored `stb_dxt` 1.12
  (`src/formats/stb/stb_dxt.h`, public domain or MIT, `STB_DXT_HIGHQUAL`, no dither).
  stb_dxt never emits the three-colour (transparent) BC1 mode, so a block with any texel
  below alpha 128 goes to Patchy's own encoder: a range fit over the opaque texels
  (bounding-box endpoints packed to 565, `c0 <= c1` selects the mode), palette c0, c1,
  their midpoint, and index 3 for every cut-out texel. Integer math only.
- BC4 and BC5 blocks come from stb_dxt too (`stb_compress_bc4_block`, `stb_compress_bc5_block`,
  integer math). BC4 stores the flattened image's luminance (`dds::luminance8`, Rec. 601
  integer weights `(r * 299 + g * 587 + b * 114 + 500) / 1000`); BC5 stores the red and green
  channels. Both drop transparency; a notice says so when the image had colour (BC4), blue
  (BC5) or any translucency, and nothing when the input already fit the format.
- BC7 blocks come from the vendored `bc7enc` (`src/formats/bc7enc/bc7enc.c`, Richard
  Geldreich, MIT or public domain): modes 1 and 6 (mode 6 for every block with alpha), uber
  level 1, linear weights so alpha counts as much as colour, `bc7enc_compress_block_init`
  called once from the writer thread, modes 5 and 7 tried for alpha blocks as bc7enc's
  defaults do. On a smooth gradient BC7 lands above 35 dB. Alpha: mode 6 runs one index line
  through RGBA and mode 5 gives alpha two index bits, so where colour and alpha vary in
  different directions inside a block, alpha lands within about 12 to 14 (BC3 gives 16 to 18
  on the same content); where alpha is flat or follows the colour it is near exact.
  Weighting alpha higher or raising the uber level did not move those numbers (October
  2026), so the defaults stay. bc7enc has no mode 4 (separate 3-bit alpha indices), which is
  what would fix it.
- stb_dxt's endpoint search is a float power iteration and bc7enc's least-squares fits are
  float too. CMake compiles both TUs with `-ffp-contract=off` on GCC and Clang (MSVC's
  `/fp:precise` already forbids fused multiply-add) so every toolchain produces the same
  bytes; `dds_writer_bytes_are_stable` pins seven writes by FNV-1a hash and the mac and
  linux remote runs check the pin. If a toolchain ever disagrees, the fallback is to route
  every block through the integer range-fit encoder.
- Mipmaps: `generate_mip_chain` box-filters down to 1x1. Each side halves (never below 1);
  the last destination column and row absorb an odd leftover source column or row (5
  wide becomes 2 texels averaging columns {0, 1} and {2, 3, 4}); colours average
  alpha-weighted (`(sum c*a + sum a / 2) / sum a`, or the plain rounded mean when every
  alpha is 0) and alpha averages plainly with rounding. `mip_count_for` is
  `1 + floor(log2(max(w, h)))`.
- Block rows encode in parallel (`append_block_level`: `std::async` over rows of blocks, at
  most 8 workers, within the wasm fan-out budget of `core/worker_budget.hpp`). Every block
  lands in its own slot, so the bytes never depend on scheduling and the canary still pins
  them. The first block row runs on the calling thread before the fan-out because stb_dxt
  and bc7enc build their lookup tables on first use; keep that order.
- Writes go through `formats::write_file_bytes` (atomic).
- `preview_levels(document, compression, mipmaps)` is the save without the file: it
  flattens like `write_dds`, resolves Automatic by the alpha rule, encodes every level with
  the same block encoders and decodes each with the reader's `decode_image` (BC4 as gray,
  BC5 as red and green, Uncompressed as the identity), returning the texels plus each
  level's payload size. The Preview Mipmaps window is built on it, so what it shows is
  exactly what a reader will sample.

## Settings, metadata, and the save flow

Persisted defaults (`saveOptions/*`, compatibility contracts, never renamed):
`ddsCompression` (`auto` | `uncompressed` | `bc1` | `bc3` | `bc4` | `bc5` | `bc7`, default
`auto`) and `ddsMipmapMode` (`auto` | `on` | `off`, default `auto`). `ddsMipmaps`, the
1.07 checkbox bool, is read only when `ddsMipmapMode` is absent (true migrates to `on`,
false to `auto`) and is no longer written. The token helpers live with the codec
(`dds::compression_token`, `compression_from_token`, `mipmap_choice_token`,
`mipmap_choice_from_token`) so the settings, the dialog and the metadata cannot disagree.

The reader stamps session-only document metadata: `patchy.dds.compression` (the nearest
export choice for the source: `uncompressed` for masked, 16-bit and float sources, `bc1`
for BC1, `bc3` for BC2 and BC3, `bc4`, `bc5` and `bc7` for their own formats, `bc7` for
BC6H since HDR has no export; never `auto`), `patchy.dds.mipmaps` (`1` when the file
carried more than one level, else `0`) and `patchy.dds.sourceFormat` (the name,
informational). Nothing serializes `DocumentMetadata::values` into any file.

**The user's choices stay Automatic; the opened file is what Automatic resolves against**
(Seth, October 2026). `dds::source_shape_from_metadata` turns the first two keys into a
`SourceShape` (optional compression, optional mipmaps; both empty for a document that did
not come from a .dds), and `MainWindow::image_save_defaults_for_document` carries it in
the non-persisted `ImageSaveOptions::dds_source` instead of overwriting the persisted
choices, so a saved BC3 texture no longer turns the global default into BC3. At write time
(`write_flat_image_file`) `resolve_compression` keeps the opened file's format for an
Automatic choice (a BC3 texture saves back as BC3 with its alpha; a BC1 source stays BC1
even if the image gained translucency, with the cut-out notice) and otherwise leaves
Automatic for the writer's alpha rule; `resolve_mipmaps` makes Automatic follow the opened
file (none when that .dds had none, a chain when it had one) and generate a chain for every
document with no .dds source. Explicit choices pass through. The opened file is the file
the document was loaded from: a later Save As does not restamp the metadata, and a plain
Save to the same path reuses the session's remembered options as for every flat format.

Save As and Export raise `ddsSaveOptionsDialog`: `ddsCompressionCombo` with the seven
choices (Automatic through BC3 then BC7, BC4, BC5), `ddsMipmapsCombo` (Automatic, Generate
mipmaps, No mipmaps), `ddsMipmapPreviewButton` and `ddsSaveNote`, plus the shared export
section on Export. Both Automatic items name what they resolve to for this document
("Automatic (BC3 / DXT5, as the opened file)", "Automatic (none, as the opened file)",
"Automatic (generate mipmaps)" without a source). `prompt_image_save_options` takes an
optional `const Document*` (Save As and Export pass the active document); the preview
button is hidden without one. **Preview Mipmaps...** runs `preview_levels` under a wait
cursor at the form's current resolved choices, always for the whole chain, and opens
`ddsMipmapPreviewDialog`: `ddsMipmapPreviewSummary` (format, level count, size range,
total payload, and whether every level or only level 0 will be written),
`ddsMipmapPreviewZoomCombo` (100 to 800 percent, nearest neighbour so block artifacts and
BC1 cut-outs stay visible), and per level `ddsMipmapPreviewCaption<N>` (size and bytes)
over `ddsMipmapPreviewLevel<N>` on the transparency checkerboard inside
`ddsMipmapPreviewScroll`. Zoomed pixmaps cap at 8192 px a side; a level that cannot take
the chosen zoom shows at the largest that fits and its caption says so.

The row is in `file_format_entries()` unconditionally and the registry handler carries a
writer, so Save on a document opened from .dds writes in place. The format stays out of
`save_extension_preserves_layers`, so a layered document keeps the flatten warning and
save-a-copy semantics.

The plug-in probe's reason for `.8bi` files, the plug-ins folder README and
[plugins.md](plugins.md) point users at native DDS support, since the plug-ins the
issue reporter installed were format plug-ins.

## Tests, fixtures, and verification

`tests/core/dds_tests.cpp` (filter `dds`) pins the header parse and layouts, the
rejections, hand-built BC1 (both modes, partial blocks), BC2, BC3, BC4 (unsigned and
signed), BC5 blocks, the un-premultiply rule and the four alpha modes, every committed
fixture against Pillow's values, the half-float ramp against `jxr::tone_map_scrgb_to_rgba8`,
BC6H against Pillow's decode through the tone map, cubemap, volume, array and partial
cubemap layering, the writer's header layout, an exact uncompressed round trip, the BC1
cut-out rule and Automatic, BC3 alpha within 8 on a smooth ramp and colour PSNR above 30 dB, BC7 above 33 dB
with alpha within 16, the BC4 luminance and BC5 red/green round trips with their notices, the mip filter
rules, the Automatic resolution against the opened file's shape plus the mipmap tokens
(`dds_automatic_choices_resolve_against_the_opened_file`), the preview against a real
round trip (`dds_preview_levels_match_the_written_file`), the byte canary, a Unicode path
round trip, the inspection artifacts and the local fixture sweep.
`tests/ui/flat_image_format_tests.cpp` (filter `ui_dds`) covers the open-and-save-in-place
flow with the document-alpha mask, the BC3 re-save keeping alpha and writing no chain,
settings persistence with the checkbox migration and the dialog's combos, the source shape
carried beside untouched Automatic choices, a fresh document writing BC1 plus a chain, a
mipmapped source keeping its chain, an explicit No mipmaps winning, the Preview Mipmaps
window's levels, captions, summary and zoom, and the cubemap save routing; `dds` is also
in the UI Unicode write list and the export-options clipping check.

Fixtures under `test-fixtures/dds/` are generated by `scripts/dev/dds/make_dds_fixtures.py`
(self-authored procedural art, provenance in NOTICE-THIRD-PARTY.md): Pillow 12 writes the
uncompressed RGB/RGBA/L/LA, DXT1, DXT3, DXT5 and DX10 BC5 files; the script assembles the
masked 16-bit formats, the DX10 alpha-mode flags, the 16-bit and half-float DX10 files, the
DXT2/DXT4 files (premultiplied pixels encoded by Pillow with the FourCC patched), a legacy
cubemap, a volume, a DX10 array and a mipmapped DXT1 (each level encoded by Pillow and
concatenated); Microsoft's `texconv` (DirectXTex, MIT; a release binary downloaded into an
isolated scratch directory, never committed) produced the BC4, BC7 and BC6H files. Expected
values are Pillow's independent decodes, or for the assembled masked files the quantized
codes the script wrote. Pillow and bcdec differ by at most 1 on interpolated BC texels,
which is the tolerance used for them (2 for the premultiplied files). Pillow cannot
encode BC4, BC6H or BC7 and cannot open DXT2/DXT4, hence texconv and the FourCC patch.

Quality bounds use a smooth gradient (at most 16 levels per texel step). The shared
exactness gradient changes by 37 and 59 levels per step in two independent directions
inside every 4x4 block, which no four-colour BC line can follow: Pillow decodes it at 16
dB too, so it is never the input of a PSNR check. Re-encoding a BC3 file that another tool
wrote can move alpha by up to 18 in blocks that mix alpha 0 with high alpha: Pillow uses the
6-level alpha mode with explicit 0 and 255 there, stb_dxt always the 8-level mode.

Writer cross-check: `dds_writes_inspection_artifacts` leaves one texture per compression
(with and without mipmaps) plus the flattened smooth-gradient sources as BMP under `test-artifacts/dds/`
beside the core test binary; `python -I scripts/dev/dds/verify_dds.py
build/release/test-artifacts/dds` decodes them with Pillow and demands identical pixels for
the uncompressed files, exact cut-out alpha and PSNR above 30 dB for BC1, alpha within
20 plus PSNR above 30 dB for BC3, PSNR above 35 dB with alpha within 16 for BC7, luminance
within 3 for BC4 and red/green within 3 with blue 0 for BC5 (stb_dxt uses only the 8-level alpha mode, so a block that
mixes alpha 0 with high alpha can be off by up to 18; a smooth ramp stays within 4).

Known gaps: no BC6H export (Patchy has no float pixels to feed it); BC7 uses modes 1 and
6 only; cubemap, volume and array export (a save flattens to one 2D texture); palettized, YUV, bump-map and typeless non-block formats;
the header's pitch field is ignored on read; no sRGB-to-linear conversion for `_SRGB`
variants (by design); mip levels beyond 0 are regenerated, never preserved.
