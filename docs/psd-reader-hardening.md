# PSD reader hardening: damaged PackBits and limits

Split from [file-formats.md](file-formats.md). Read before touching `decode_packbits_scanline`, the PSD/PSB header validation or the nesting and record caps.

## Damaged PackBits scanlines

Legacy PSDs carry corrupt RLE scanlines that Photoshop still opens, so image-channel readers recover instead of failing. `decode_packbits_scanline` (psd_descriptor.cpp) is the lenient decoder every layer, composite, and saved-channel row goes through: an overrunning run is clipped, a short scanline keeps zeroes, and the result is always exactly the channel width. Each row's declared byte count still positions the stream, so a damaged row cannot desync later rows (Photoshop abandons the channel at the first bad row; Patchy resyncs on the row table, verified against PS 2026). Readers count recovered rows; `append_damaged_row_notice` emits ONE notice per document. Everything else (patterns, brushes, filter effects, ILBM) keeps the strict `decode_packbits`; its exact-length contract catches misparses.

Validate headers before buffer allocation: reject zero or over-limit canvases (PSD 30,000, PSB 300,000 per side). Form layer/mask rectangles in 64-bit and reject sizes past the PSB limit. Descriptor nesting (`Objc`/`VlLs`/`ObAr`) errors past 64 levels; check item counts against remaining bytes before reserve. Group nesting caps at 64 (Photoshop allows 10): flatten deeper boundary records into the deepest kept group and drop their folder records.

PSD channels/extra data use bounded sub-readers. PSD/PSB writing caps expanded
native layer records, including group boundaries, at Photoshop's limit of 8000.
Empty documents save a transparent placeholder without changing the live
tree. PCX/ILBM validate encoded length; PCX and strict PackBits cap their initial
reserve and grow as runs decode. Aseprite cels also grow as decoded. JXR float decode uses bounded WIC row batches.

