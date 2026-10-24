# .pdn (Paint.NET) format: container, import mapping, legal method

Deep reference for the Paint.NET (.pdn) importer (GitHub issue 379). Read this before touching `src/formats/pdn_document_io` or `src/formats/nrbf_reader`. The registry entry and the open-dialog rules stay in [file-formats.md](file-formats.md); blend math in [blend-modes.md](blend-modes.md).

`src/formats/pdn_document_io.{hpp,cpp}`, read-only, Qt-free (so the wasm build has it too). Registry id `patchy.formats.pdn`, extension `.pdn`, sniff on `PDN3`; `sniff_openable_image_format` names it "Paint.NET" so a .pdn under another name opens by content. The importer rests on `src/formats/nrbf_reader.{hpp,cpp}`, a general reader for .NET BinaryFormatter streams that knows nothing about Paint.NET.

## Legal method

Paint.NET is closed-source freeware; its licence forbids decompiling it. The format record here comes from the published MS-NRBF specification, the MIT-licensed pypdn reader's notes (its source was read as documentation; no code was copied), the Just Solve the File Format wiki, and byte observation of public sample files and of files made for this work. Never decompile or disassemble Paint.NET, and never load its assemblies to read or write files. Paint.NET itself may open, flatten and save files we author (that is how the calibration fixtures were made); third-party sample files stay untracked in `local-test-fixtures/pdn/`.

## Container

- `PDN3`, then a 24-bit little-endian length and that many bytes of UTF-8 XML: `<pdnImage width= height= layers= savedWithVersion=><custom><thumb png="base64"/></custom></pdnImage>`. The thumbnail is a PNG of at most 256 px on the long side. `savedWithVersion` is not reliable (Paint.NET 5.1.12 kept a 4.21 value when it re-saved our file); the graph's `savedWith` is.
- Then `00 01` and an uncompressed NRBF stream, or `1F 8B` and one gzip member holding `[00 01]` plus the stream. The reader tells the two apart by the NRBF header's version fields (`graph_start`), because the stream itself also starts with `00`.
- Paint.NET 1.x/2.x files have no header: the whole file is the gzipped stream. They open by extension only (the registry sniff matches PDN3 alone, since a bare gzip file could be anything).
- After the stream's MessageEnd come the deferred pixel blocks.

## The object graph

Root `PaintDotNet.Document`: `width`, `height`, `layers` -> `PaintDotNet.LayerList` {`parent`, `ArrayList+_items` (object array padded with nulls past the size), `ArrayList+_size`, `ArrayList+_version`}, `savedWith` -> `System.Version`, user metadata. Member sets differ by version: 3.0x has `tag`, `disposed`; later ones `isDisposed`. Look members up by exact name and treat each as optional.

Each layer `PaintDotNet.BitmapLayer`: `properties` -> `BitmapLayer+BitmapLayerProperties` {`blendOp`: an empty class `PaintDotNet.UserBlendOps+<Name>BlendOp`}, `surface` -> `PaintDotNet.Surface` {`width`, `height`, `stride`, `scan0` -> `PaintDotNet.MemoryBlock` {`length64`, `hasParent`, `deferred`, plus `bitmapWidth`/`bitmapHeight` in some 3.x files}}, `Layer+width`, `Layer+height`, `Layer+properties` -> `Layer+LayerProperties` {`name`, `visible`, `isBackground`, `opacity` (byte), `blendMode` -> `PaintDotNet.LayerBlendMode` {`value__`} from 4.0 on}. `properties` and `Layer+properties` are different objects; never match member names by suffix.

Blend mode: `blendMode.value__` when present, else the blend-op class name (3.x). Paint.NET's numbering: 0 Normal, 1 Multiply, 2 Additive, 3 ColorBurn, 4 ColorDodge, 5 Reflect, 6 Glow, 7 Overlay, 8 Difference, 9 Negation, 10 Lighten, 11 Darken, 12 Screen, 13 Xor. An unknown value opens as Normal with a notice.

User metadata: 3.5 and later write `userMetadataItems`, a `KeyValuePair<string,string>[]` (a BinaryArray of system-class records); 3.x writes `userMetaData`, a `NameValueCollection` {`Keys`: string[], `Values`: ArrayList[] of strings}. Both can be present (3.5x); the items array wins. EXIF lives there as one entry per tag: key `$exif.tag<N>` (5.x: `$exif.tag<N>[0]`), value `<exif id="305" len="17" type="2" value="base64"/>` with the raw little-endian TIFF value bytes. Paint.NET writes Software (305), ResolutionUnit (296), XResolution (282) and YResolution (283) on every save.

## Deferred pixel blocks

For every `MemoryBlock` with `deferred` true, in the order the blocks' records appear in the stream (which is also layer order in every file seen): a format byte (0 gzip chunks, 1 raw), a big-endian u32 chunk size (256 KiB in 3.x and 4.x files, 1 MiB in some 3.x), then ceil(`length64` / chunk size) chunks of {u32 BE chunk index, u32 BE stored size, bytes}. Chunks come in any order: diskBad-250.pdn (Paint.NET 4.5) starts with chunk 1. Each gzip chunk is a complete gzip member; Patchy checks its CRC and reports a mismatch as possible damage. Pixels are BGRA, straight alpha, rows `stride` bytes apart; `length64` is at least stride x (height - 1) + width x 4.

A block several layers point at is decoded once and shared (copy-on-write). A block with `hasParent` true (a view into another block) has not been seen in any file and opens as an empty layer with the damage notice; so does a non-deferred block without a byte array member covering it.

## Import mapping

- Layers bottom first, each a document-sized RGBA8 pixel layer with its name (UTF-8 from the stream), visibility, opacity (byte / 255) and blend mode. The top layer is active.
- The Background: Paint.NET flags the bottom layer `isBackground` but lets it hold transparency, and the flag can sit on a layer that was moved up (font.pdn). Only a bottom, fully opaque, Normal, full-opacity flagged layer becomes Patchy's Background ([background-layer.md](background-layer.md)): RGB, position and transparency locked. Any other flagged layer stays an ordinary layer.
- Resolution: XResolution/YResolution with ResolutionUnit 2 (inch) or 3 (cm, converted to PPI and shown as px/cm); no tags means Paint.NET's 96 PPI.
- File Info ([file-info.md](file-info.md)): `extract_file_info` recognizes "PDN" and `extract_pdn` rebuilds the EXIF entries into one TIFF stream through `metadata::write_tiff` (GPS tags 0..31 into the GPS IFD, the 0x829A..0xA4FF camera range into the Exif IFD except IFD0's IPTC, ICC and Windows XP tags). Other user-metadata entries show as observed items under "Paint.NET" and are never written.
- Paint.NET has no groups, masks, text, adjustment or vector layers, so nothing else exists to map.

## Fallback tiers and notices

The importer never fails just because the graph is damaged. A header that parses with a graph that does not (or a graph that is not a Paint.NET document, or no layer whose pixels decode) opens the thumbnail as one layer, "Paint.NET preview", at the thumbnail's size, with the notice that the layers could not be read. Without a usable thumbnail the open fails with "This Paint.NET file is damaged and could not be opened." Partial damage keeps every layer: missing or broken chunks open transparent with one notice. All notices are fixed translatable strings; `PATCHY_PDN_TRACE=1` prints the technical reason to stderr.

## Limits

NRBF (`nrbf::Limits`): 64 nested inline records, 262,144 objects, 1,048,576 value slots (members plus array elements, null runs included), 4,096 members per class; every length and count is checked against the bytes left, so a length field cannot allocate more than the file holds. PDN: side 300,000 and 2^28 pixels per layer (the PSB cap every importer shares), 4,096 layers, 8 GiB of declared pixel blocks; a gzip block may claim at most 1,100 times its stored bytes (deflate's limit is about 1,032), the document may be no larger than its largest located block, and layers without readable pixels share one transparent buffer. Whole-stream gzip inflates to at most 1,100 times its size. `pdn_mutation_sweep_never_crashes` (truncations, bit flips, 0xFF runs) pins it.

Inflation of a block's chunks and the BGRA conversion run in parallel (`run_in_parallel`, capped at 16 workers and by `max_blocking_fanout_workers`); the destination buffers are fresh, so their storage is unshared before the workers write. The public samples open in 0 to 40 ms each (the 1920x1080 three-layer ones about 30 ms); a 4000 x 4000 ten-layer file (250 MB, `pdn_large_file_timing_if_available`) opens in 0.5 to 1.5 s on a 32-thread offload host.

## Blend calibration

Paint.NET's own flattened exports are the reference: pypdn's FlattenBlendTest renders (Paint.NET 4.21, local) and Paint.NET 5.1.12 exports of files made for this work (committed). Reflect, Glow, Negation and Xor are new BlendMode values with Paint.NET's exact integer kernels; Normal at full opacity, Additive (Linear Dodge), Difference, Lighten and Darken match exactly. Five modes keep Patchy's Photoshop-calibrated kernels and differ by at most one level except at two corners: Multiply, Screen and Overlay (Paint.NET rounds its products, Photoshop's kernels floor) and Color Burn and Color Dodge (Paint.NET floors its quotients, Photoshop's round). The corners: Paint.NET's Color Burn returns 0 wherever the layer is 0 and its Color Dodge 255 wherever the layer is 255, where Photoshop's kernels keep a white or black backdrop (a full-scale difference on those few pixels). Partial alpha and layer opacity add one level of compositing rounding (two where a rounding mode's own residual meets it), and a stack of several partial-opacity layers over transparency drifts up to 4 levels, compared premultiplied. Changing those kernels would break the Photoshop captures they are pinned to. `pdn_paint_net_5_files_match_their_exports` holds these bounds per mode.

## Fixtures and tests

- `test-fixtures/pdn/`: files we made and Paint.NET 5.1.12 re-saved, each beside Paint.NET's flattened PNG export. `blend-modes.pdn` stacks the 14 modes in 80-row bands over a gradient Background (a 66 x 66 value table per mode, then partial-alpha rows); `opacity-stack.pdn` has partial opacity, a hidden layer and no Background; `odd-size-unicode.pdn` is 33 x 17 with a Unicode layer name at 300 PPI stored as px/cm; `demo.pdn` is a 640 x 400 landscape with Screen, Glow and Xor layers, a hidden layer and a non-ASCII name at 150 PPI. The generator scripts live with the calibration notes outside the repository; the files are what Paint.NET wrote.
- `local-test-fixtures/pdn/` (untracked): the sembiance samples (Paint.NET 3.07 to 4.12) and pypdn's test files with their PNGs, each with a `<name>.oracle.txt` from pypdn (per-layer FNV-1a of the RGBA bytes and the properties). `pdn_public_samples_match_pypdn_if_available` and `pdn_composites_match_paint_net_exports_if_available` skip without them.
- Committed tests build their bytes in code (`tests/pdn_test_builder.hpp`): every NRBF record type and malformed input, chunk order and format, strides, shared blocks, 3.x blend classes, gzip and header-less files, EXIF, the thumbnail fallback, damage, the mutation sweep, Unicode paths and the registry. `ui_pdn_files_open_with_layers_and_by_content` covers the window side.

## Open items

- Paint.NET 6.0 plans a new format with a published specification; it would get its own reader beside this one.
- Saves cannot keep Reflect, Glow, Negation or Xor in PSD, layered TIFF or Aseprite files (Normal plus a notice), and document recovery's PSB snapshots write them as Normal too.
