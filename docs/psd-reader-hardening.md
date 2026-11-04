# PSD reader hardening: damaged PackBits, limits and the decode budget

Split from [file-formats.md](file-formats.md). Read before touching `decode_packbits_scanline`, the PSD/PSB header validation or the nesting and record caps, and before any reader sizes a buffer from dimensions the file declares.

## Damaged PackBits scanlines

Legacy PSDs carry corrupt RLE scanlines that Photoshop still opens, so image-channel readers recover instead of failing. `decode_packbits_scanline` (psd_descriptor.cpp) is the lenient decoder every layer, composite, and saved-channel row goes through: an overrunning run is clipped, a short scanline keeps zeroes, and the result is always exactly the channel width. Each row's declared byte count still positions the stream, so a damaged row cannot desync later rows (Photoshop abandons the channel at the first bad row; Patchy resyncs on the row table, verified against PS 2026). Readers count recovered rows; `append_damaged_row_notice` emits ONE notice per document. Everything else (patterns, brushes, filter effects, ILBM) keeps the strict `decode_packbits`; its exact-length contract catches misparses.

Validate headers before buffer allocation: reject zero or over-limit canvases (PSD 30,000, PSB 300,000 per side). Form layer/mask rectangles in 64-bit and reject sizes past the PSB limit. Descriptor nesting (`Objc`/`VlLs`/`ObAr`) errors past 64 levels; check item counts against remaining bytes before reserve. Group nesting caps at 64 (Photoshop allows 10): flatten deeper boundary records into the deepest kept group and drop their folder records.

## Decode budget

`src/support/decode_budget.hpp` is the shared check for buffers sized from declared
dimensions. A reader calls it before allocating, and a failure goes through the reader's
normal error (a truncated or damaged-file message), so the open fails cleanly:

- `checked_pixel_count`/`checked_decode_bytes` form width x height x bytes in 64 bits and cap
  one image or plane at 2^36 pixels (2^28 on a 32-bit `size_t`, wasm32), so later `size_t`
  offsets up to 16 bytes per pixel cannot wrap.
- Decoded bytes at file precision may exceed the encoded bytes behind them by at most
  1032:1 (deflate's ceiling, above PackBits, PCX and Radiance RLE), or fit the 64 MiB
  allowance (the PSD composite allows raw data no expansion). Honest damaged files pass; a
  small file declaring a huge image does not.

Users: PSD layer records check each layer rectangle against its largest color or alpha
channel before the plane is allocated; layers with no data (zero-length channels) share one
64 MiB allowance per layer section; a mask or other channel whose payload cannot fill its
rectangle is skipped as damaged (counted in the damaged-row notice). The flat composite
(`require_composite_fits_data`) checks the header against the remaining image data before
the canvas buffer exists, for the flat path, saved channels and layered TIFF. Radiance HDR
checks the resolution line, OpenEXR the data window, the tile grid and `chunkCount` (which
tinyexr trusts for scanline files) before `LoadEXRImageFromMemory`, PCX its decoded size.
A `std::bad_alloc` during a PSD read becomes "There is not enough memory available to open
this file." rather than a damaged channel.

## Other caps

PSD channels/extra data use bounded sub-readers. PSD/PSB writing caps expanded
native layer records, including group boundaries, at Photoshop's limit of 8000.
Empty documents save a transparent placeholder without changing the live
tree. PCX/ILBM validate encoded length; PCX and strict PackBits cap their initial
reserve and grow as runs decode. Aseprite cels also grow as decoded. JXR float decode uses bounded WIC row batches.

