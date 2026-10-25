# File formats: registry, per-format quirks, PSB, document alpha

Read before changing format I/O, file filters, notices, or alpha/mask import.

## Registry and dispatch

Save/Save As writes and saved-state bookkeeping live in `ui/document_save_coordinator.cpp`; dialog preflight, status text and export entry points in `main_window_files.cpp`. The coordinator shares its layer-loss classification with those dialogs.

- `builtin_format_registry()` (format_registry.cpp, function-local static) is the single instance. `load_document_from_path` (main_window_files.cpp) consults it BEFORE the QImageReader fallback; a throwing registry read still falls back to Qt where a plugin exists; if Qt also fails the REGISTRY error is reported. Handlers may be read-only (`write == nullptr`) and may carry a `sniff` check for the smart-object classifier (`.ase` Aseprite vs Adobe `ASEF` swatches); the open path matches by extension.
- One filter table: `file_format_entries()` (main_window_files.cpp) generates open/save/export filters, `is_supported_image_extension`, `save_file_filter_for_path`, and `path_with_default_extension`. Display names sit in `QT_TRANSLATE_NOOP("QObject", ...)` and are shown through `translate_data_text` ([localization.md](localization.md)).
- A new format needs one table row, one registry row, one writer branch. A new openable extension also goes in `$PatchyOpenWithExtensions` (packaging/windows/InstallPatchy.ps1), the Windows "Open with" list; PDF is left out of it on purpose.

- When QImageReader rejects a file, `open_failure_reason` (main_window_files.cpp) replaces Qt's two generic strings with what `diagnose_undecodable_image` (image_open_diagnosis.cpp, Qt-free; `undecodable_image_diagnosis_names_the_real_reason`) reads from the bytes: empty, text, another format's signature Patchy cannot open, a cut-short, 12-bit, lossless or hierarchical JPEG, a TIFF codec the libtiff Qt bundles lacks, or a TIFF sample layout Qt rejects (layout sweep: agents_local.md). JPEG-compressed TIFF (compression 7) decodes instead: `ui/tiff_jpeg_reader.cpp` splices JPEGTables into each strip or tile and decodes through Qt's JPEG reader (`ui_jpeg_compressed_tiff_opens_through_the_jpeg_decoder`); old-style JPEG (6) and 12-bit strips still fail, with their reasons. The Open failed box shows the native path (issues 112 and 117). HDR/EXR/JPEG 2000: [hdr-formats.md](hdr-formats.md); AVIF: [avif.md](avif.md).
- Open by content (issue 362, `ui_open_by_content_when_the_extension_is_wrong`): `load_document_from_path` and the drag check sniff the head (`sniff_openable_image_format`). Unknown names (`src.bin`, none) and PNG/JPEG/GIF/WebP/BMP swaps read as the content, other known names only once the name fails (raws are TIFF inside). `content_extension` makes Save a Save As with that extension.

## Open-dialog filter contract

Open, sprite-sheet, and image-sequence dialogs pass `FilterNameDetails::Hidden` through `get_open_file_names`. File > Open and Open Recent Folder are multi-select: each pick opens as its own document in dialog order, the last one active, through the multi-file drop loop (`ui_open_dialog_opens_every_selected_file`). Qt shows only the text left of the last `(` and uses the final parenthesized list as the machine filter, so `open_file_filter()` deliberately writes each row as `Name (hint) (patterns)`: the hint is the pattern list, cut after `kMaxVisibleFilterPatterns` (6) with "and more" (`visible_pattern_hint`), the spec is complete.

The Open dialog reopens on the last picked row (`lastOpenFilter`, written only when files were picked; `ui_open_dialog_remembers_last_filter`, issue 114). The HEIF row exists only where a decoder does (`heif_decoder_available`, issue 113; [heif.md](heif.md)).

The visible portion must keep a `*.` token. The Windows 11 native dialog appends the complete semicolon-joined pattern list to any filter name without one, so the all-formats row uses the short `(*.psd *.png *.jpg and more)` hint instead of ~50 patterns. `ui_open_dialog_hides_name_filter_details` pins the shape, the row length limit and the raw hint. The hint cap suits GNOME's GTK 4 portal dialog (about ten rows, no scrollbar; issue 270, [wayland.md](wayland.md)).

Desktop file drops copy the accepted local paths and queue processing on the main window after the handler returns. Never enter import dialogs or decode inside the native drop callback: Windows Explorer waits for it (a RAW or PDF dialog would block it). Main-window, canvas, tab-area, and floating-window drops share this path and keep drop order; cancelling one import still opens the rest; closing the main window first discards the queue.

## Per-format catalogue

Everything reads and writes except camera raw, HEIF/HEIC, .af, .pdn, .kra and portable anymaps (read-only); JPEG XR reads and writes on Windows only. Modules live in src/formats/, Qt-free, explicit-endian via `binary_le.hpp` (LE) or `psd_binary.hpp` (BE).

- PSD/PSB: PSB section below plus [ps-compat.md](ps-compat.md); 16/32-bit files open at their depth while deep editing is on ([high-bit-depth.md](high-bit-depth.md)), else convert to 8-bit (section below). An unnamed layer record takes Photoshop's display name: "Layer N" (N counts ordinary layers from the bottom, 1-based), or "Background" for a bottom record with no transparency channel.
- BMP: includes 32-bit `BI_RGB`/compression 0, whose 4th byte Patchy keeps (feeds document-alpha import below).
- ICO/CUR: every embedded size imports as a hidden layer named "WxH"; the writer reuses a matching "WxH" pixel layer verbatim, so sizes round-trip. 256px entries are PNG-compressed via `ico::set_png_codec` (installed in the MainWindow ctor). CUR hotspots ride layer metadata `patchy.cursor_hotspot`.
- TGA: types 1/2/3/9/10/11, both origin flags; 15/16-bit rejected; palette-mode documents write type 1 indexed.
- Aseprite: frame 1 only; layer tree/blend modes/opacity/closed folders (layer flag 32) round-trip; zlib cels via vendored `src/formats/miniz/`; verified against the real Aseprite CLI.
- PCX: 8-bit indexed EOF-palette plus 24-bit 3-plane RLE.
- ILBM/PBM: ByteRun1 via the shared `psd::decode_packbits`/`encode_packbits_row` (psd_descriptor.{hpp,cpp}); EHB supported, HAM rejected; writer emits planar ILBM with masking type 2.
- Portable anymaps `.pbm/.pgm/.ppm/.pnm`: a read-only filter row, no registry row; Qt's PPM handler reads P1 to P6 and sniffs the magic behind `.pnm` (`ui_portable_anymap_files_open`). No PAM or PFM.
- GIF and animated WebP: [animation.md](animation.md) owns import, export, timing, loop counts, codec isolation and scripted animation.
- PNG/JPEG/TIFF and still WebP: Qt readers/writers. Photoshop layers in a TIFF (tag 37724) open as layers: [tiff.md](tiff.md). WebP options retain quality (`saveOptions/webpQuality`, default 75) and lossless (`saveOptions/webpLossless`; quality 100 also means lossless).
- JPEG XR (.jxr/.wdp/.hdp): Windows only, read and write through the in-box WIC codec; the filter row is gated on `jxr::is_available()`, and the registry row has a writer so Save stays on .jxr. Details: [jxr.md](jxr.md).
- JPEG XL `.jxl`: read and write everywhere (vendored libjxl) at 8, 16 or 32 bits, animations as layers; encoder patches and dots stay off: [jxl.md](jxl.md).
- OpenRaster `.ora`: layered read and write, baking what it cannot hold: [ora.md](ora.md).
- Proton `.rttex`: read and write everywhere; the RTPACK wrapper, pixel layouts, embedded-JPEG variant and PVRTC rejection: [rttex.md](rttex.md).
- DDS `.dds`: read and write everywhere (vendored bcdec, stb_dxt and bc7enc; BC1-BC7 in, A8R8G8B8/DXT/BC4/BC5/BC7 out): [dds.md](dds.md).

## Camera raw (CR2/CR3/NEF/ARW/RAF/DNG, ...)

Read-only LibRaw import through `raw_document_io.{hpp,cpp}`, always to an 8-bit document. Decoder, precision, licensing, per-photo `.rawprefs` sidecars (automated opens read them, never write them), defaults, and tests: [camera-raw.md](camera-raw.md).

## HEIF/HEIC (.heic/.heif/.hif)

Read-only through platform decoders (never a shipped HEVC codec), with 16-bit and HDR/32-bit import where supported. Depth, codecs, messages and tests: [heif.md](heif.md). AVIF uses a separate reader ([avif.md](avif.md)).

## .af (Affinity)

Read-only importer; registry id `patchy.formats.af`, sniff on magic `00 FF 4B 41`; the filter row claims `.af` plus 2.x `.afphoto/.afdesign/.afpub`. Format record: [af-format.md](af-format.md).

## .pdn (Paint.NET)

Read-only importer; registry id `patchy.formats.pdn`, sniff on `PDN3`; header-less 1.x/2.x files open by extension. Format record: [pdn-format.md](pdn-format.md).

## .xcf (GIMP)

Read-only importer; registry id `patchy.formats.xcf`, sniff on `gimp xcf ` (gzip, bzip2 and xz wrappers unwrap first); `*.xcf.gz/.bz2/.xz` names route through `xcf::is_compressed_xcf_file_name`. Format record, mappings and the render comparison: [xcf.md](xcf.md).

## .kra (Krita)

Read-only importer; registry id `patchy.formats.kra`, sniff on the stored `mimetype` entry holding `application/x-krita` at byte 30; the filter row "Krita Document" claims `.kra` and `.krz`. The open path calls the reader with the file's folder (file layers load from it) instead of the registry row. Format record: [kra.md](kra.md).

## PDF

Import and export: [pdf.md](pdf.md). PDF has no registry row and is a read-only source; the filter-string and save-guard rules still apply to it.

## Layered documents and flat formats (the Photoshop-style save guard)

A document opened from a flat format that has grown structure a flat save would discard must never silently flatten back over its file:

- `flat_save_discards_layers(document)` (main_window_files.cpp): more than one layer, any group/adjustment/text/smart-object/shape layer, a hand-authored mask, layer styles, or non-identity Blend If. SVG keeps vectors, so `save_discards_layers` asks its writer for a dry run and warns only when something bakes ([svg.md](svg.md)). A single pixel layer whose only mask carries the document-alpha marker stays exempt (that mask IS the flat file's alpha plane).
- `save_extension_preserves_layers()` lists the guard-skipped formats: psd/psb, aseprite/ase, ora, and ico/cur (multi-size icons live as hidden "WxH" layers; the guard would fire on every icon save). TIFF keeps layers too unless its options turn them off ([tiff.md](tiff.md)).
- Save on a layered flat-backed document routes to Save As, defaulting name and filter to `.psd`.
- Explicitly choosing a flat format raises `flattenLayersMessageBox` (default Cancel) and on confirm performs a save-a-copy: the flat file is written but the session keeps its path, title, and modified state. Save As pre-confirms and passes `flatten_confirmed` so the box appears once.
- Exception: a linked smart-object child session keeps real-save semantics after the same warning (the linked file IS that document; the parent refresh needs the write).
- Pins: `ui_save_layered_flat_format_routes_to_save_as_with_psd_default`, `ui_flat_save_of_layered_document_warns_and_saves_copy`.

## Shared writer helpers

`formats/document_flatten.{hpp,cpp}`: `flatten_document_rgba8` (mask-aware: a document-alpha layer exports non-destructively), `indexed_flatten_for_palette_mode` (document palette in file order, exact-then-LUT, appended transparent slot: PNG-8 semantics), `indexed_flatten_quantized` (median-cut fallback for RGB docs; GIF + ILBM share it). Both indexed functions delegate to PixelBuffer-level `indexed_rgba8_with_palette` / `indexed_rgba8_quantized` so animated GIF indexes each frame with identical semantics.

Paths reach the Qt-free readers and writers through `to_filesystem_path` (the AGENTS.md path rule); `rename_first_layer_to_stem` (formats/format_file_io.cpp) names the imported layer through `path_to_utf8`.

All but PSD/Aseprite/ORA flat-export through `write_flat_image_file`. `ImageSaveOptions` carries the EXPORT-only transforms, never Save/Save As: the shared dialog section (every export form except ICO/CUR) persists them under its own `saveOptions/export*` keys so defaults never inherit one (JPEG forces the fill without persisting it; PDF editable layers disable the section). `export_stand_in_document` applies them in order: trim (alpha != 0 bounds; an all-transparent image keeps its size plus a notice), resize (`resample_pixels`, always Bilinear; `export_width/height` describe the FULL canvas, one zero side follows the aspect), nearest-neighbor `export_scale`, then the background fill (straight-alpha matte, opaque result). The stand-in keeps the doc-alpha mask structure and palette metadata unless matted; all-default options take the old byte-identical path; PPI copies verbatim. The animated GIF, WebP and JPEG XL writers apply the same per frame, trimming to the union of all frames' bounds. Metadata rides `metadata_policy`: [file-info.md](file-info.md).

## Save progress

Save, Save As and Export Flat Image (every format but SVG and Aseprite) run their writer through `MainWindow::run_save_write` (`SaveHost::run_write` from `DocumentSaveCoordinator::write_prepared`). A save that passes 400 ms (`PATCHY_SAVE_PROGRESS_DELAY_MS` overrides it for tests) shows `saveProgressDialog`. While it runs, user input waits, but paint and timers keep going. PSD/PSB and the raster formats write a copy-on-write snapshot on a worker thread (`launch_async`, inline on wasm), which is how recovery writes. The writer reports through `SaveProgress`:
- PSD: `psd::WriteOptions::progress` reports compositing, then each top-level layer by name (an artboard's name on an artboard document), then writing. It never changes the bytes (`psd_write_progress_reports_layers_and_keeps_bytes`).
- PDF: reports each page through `report_save_progress` (`ScopedSaveProgress`). PDF, editable pages included, writes on the worker too (Qt paints text off the GUI thread; the font caches shared with the UI in `text_font_service.cpp` are locked).

Scripted and unattended saves never show the dialog. Test: `ui_save_shows_progress_with_artboard_names`.

## PSD adjustment layers, clipping, blending, and layer styles

The per-layer blocks (`.acv`, native adjustment blocks, `blnc`, `brit`/`CgEd`, `plAD`, `hue2`, clipping, `iOpa`, `lspf`, `brst`, Blend If, layer styles, origins) are in [psd-layer-blocks.md](psd-layer-blocks.md).

## Damaged PackBits scanlines and reader limits

PackBits leniency, the PSD/PSB reader limits and the writer caps: [psd-reader-hardening.md](psd-reader-hardening.md).

## Import notices

Readers report dropped or approximated features via `FormatReadResult::notices`. `open_document_path` shows them in the STATUS BAR (first note plus "+N more"); the consolidated `importNoticesMessageBox` popup (`show_import_notices_popup`) appears only with `imports/showPsdWarningsAndInfo` enabled (the preference that also gates the PSD compatibility report). Exception: a conversion a save makes permanent forces it as a warning box (`OpenDocumentResult::force_import_notices_popup`; issue 52), also for Open Folder's background files (their other notes drop); unattended automation never shows it. The PSD reader's metadata triggers it: `psd.depth` above 8 on a document that ended at 8 bits (deep editing off), `psd.color_mode` other than RGB, CMYK or Grayscale, or `psd.ink_adjustments` (adjustments that acted on CMYK inks). Tests assert `statusBar()->currentMessage()`; tests that expect the popup need the REPEATING QTimer dismisser (a one-shot fires during the open-progress phase and the suite hangs).

## Fixtures and verification

Committed fixtures: `test-fixtures/<format>/` (provenance: NOTICE-THIRD-PARTY.md); tests synthesize adversarial files. The PSD set includes PS-resaved acceptance files for layer styles and Blend If (`photoshop-*-roundtrip.psd` plus `-render.bmp`). Verify writers with independent decoders.

## PSB (large document format) read + write

PSB threads `Header::large_document` / `WriteOptions::large_document` through psd_document_io: u64 section/layer-info/channel lengths, u32 RLE row byte counts, header version 2. Save As offers `.psb`; writing a >30k px document as `.psd` errors ("use .psb"; the PSB cap is 300k). Facts pinned against Photoshop 2026 (COM byte-diffs) that the spec gets wrong or omits:

- Tagged-block length width on read = '8B64' signature OR (PSB and the key is in the documented 8-byte list). BOTH rules, not either alone: PS writes 'cinf' as 8B64+u64 in PSBs (not in the spec's list), but PS 2023 writes 'lnk2' as plain '8BIM' + u64 (spec-list key, no 8B64 signature); the signature alone misreads that length and derails the rest of the global block walk (`psb_linked_smart_objects_parse_lnke_if_available` pins it). `UnknownPsdBlock::long_length` records each preserved block's width for re-emit; the writer's upgrade list (`tagged_block_length_is_u64`) = spec set + 'cinf'.
- PS pads the PSB layer-info section to 2 bytes (same as PSD), not 4.
- Old Photoshop writes EMPTY layers (0x0 rect) with zero-length channel data (no payload, no compression marker); the reader treats it as empty.
- CMYK/Grayscale import shares one sRGB conversion per mode for pixels, effects and text. `CmykToRgbTransform` applies ICC resource 1039 (relative intent, BPC) to inverted pixel channels, 'CMYC' colors and /Type 2 ink fractions. Untagged CMYK uses installed U.S. Web Coated (SWOP) v2 when available; absent or invalid profiles otherwise use the naive ink mix. Supported 8-bit stacks also blend native inks ([blend-modes.md](blend-modes.md)). `GrayToRgbTransform` applies an embedded gray profile to pixels, 'Grsc' colors ('Gry ' = black percent, 100 = black) and /Type 0 `[alpha, lightness]`, else copies neutrally; pinned by `issue-39/gray-ramp-dotgain20.psd`. Neither profile enters `color_state()` or RGB re-exports; no default profile is bundled. Legacy 'lrFX' ignores the color-space id. Fixture: `photoshop-cmyk-style-colors.psd`.
- Bitmap, Indexed, Duotone, Lab and Multichannel documents also convert to RGB at read time (`format_from_header`, `psd_channel_data.cpp`): Bitmap is the one 1-bit mode, its single plane packed to byte rows with a set bit black (`read_bitmap_composite_plane`); Indexed maps its plane through the 768-byte table in the color mode data; Duotone reads as its gray plane (the ink curves are not applied); Lab converts per layer with `convert_lab_pixels_to_rgb` (D50 Lab, Bradford to D65, shared sRGB transfer); Multichannel reads its first three planes as cyan, magenta and yellow inks. Gaps: Lab colors inside layer styles and fill layers, the Indexed transparency index. Pinned by `psd_other_color_modes_convert_to_rgb_on_read` and `psd_tools_other_color_modes_open_if_available`.
- The default-false PSD writer paths are pinned byte-identical by `psd_layered_writer_bytes_are_stable` (FNV hash canary; re-pin only for deliberate format changes).

## 16-bit and 32-bit PSD/PSB import

With deep editing on (the default) RGB and unprofiled gray files keep their samples, and the writer emits the document's depth: [high-bit-depth.md](high-bit-depth.md) Phase 2. 16-bit CMYK, profiled gray and Lab color conversion also stays at 16 bits; multichannel imports keep the original samples of their first three planes. Both layer and composite decoding use this path, with transparency kept separately. Eligible layered 8/16-bit CMYK/gray/Lab saves retain the source mode and samples; edits fall back to RGB: [preservation rules](psd-native-color.md). With deep editing off (`PATCHY_DEEP_EDITING=0`, the test harnesses), deep files convert at decode (`convert_channel_to_8bit`), save as 8-bit, and an import notice says so. The reader records `metadata().values["psd.depth"]` ("8", "16", "32"); when the document ended up at 8 bits the UI forces the Import Notes popup and the compatibility report lists the conversion. The wire rules below hold at every setting. Pinned against Photoshop 2026 COM fixtures and a CS4-era file:

- 16-bit samples are full-range big-endian u16 (0..65535, NOT the 0..32768 pattern-resource scale); 8-bit conversion is value/257 with rounding. 32-bit samples are big-endian linear-light floats: converting to 8 bits, color channels clamp to 0..1 and sRGB-encode (matches PS's default Exposure and Gamma conversion on the probe colors); transparency, masks, and saved channels scale linearly instead. A 32-bit document's descriptor colors (fills, gradient stops, effect colors) are linear too and sRGB-encode through `CmykColorConverter::rgb_from_descriptor_rgb` (`psd_tools_32_bit_fill_color_is_linear_if_available`: psd-tools' 300dpi.psb).
- Deep files keep an EMPTY standard layer-info section; the real layers live in the `Lr16`/`Lr32` document-global tagged block (`read_layer_info_records` parses both). The block is consumed, never preserved raw: a deep save regenerates it from the model, and re-emitting a stale deep block (or `Mt16`/`Mt32` data) from a converted 8-bit save would mislead Photoshop. An 8-bit file with an empty standard section and a `Layr` block parses the same way. The legacy top-to-bottom order heuristic is skipped for these blocks. The merged-transparency flag is the sign of the block's layer count (`read_merged_transparency_flag_and_skip_layer_mask`).
- Layer channels decode zip (2) and zip-with-prediction (3) besides raw/RLE; prediction-undo details beside the decoder. PS writes zip-with-prediction for non-empty deep layer channels.
- Composite-section RLE rows are plain PackBits at every depth, NO prediction; zip composites stay rejected. A deep file saved without Maximize Compatibility carries an all-white composite (PS behavior, not a bug); the layers are authoritative.
- Local untracked fixtures: `ps2026-{16,32}bit[-flat].psd` (COM-generated; expected values sampled via COM) and `Flat-filter-list.psd` (CS4-era 16-bit). Tests: `psd_16_bit_*`, `psd_32_bit_*`, `psd_photoshop_*_bit_*`.

## Saved PSD channels and flat-image alpha

PSD/PSB saved alpha and spot channels are ordered, full-canvas `DocumentChannel` planes: not layer masks, never changing the normal composite. Model and UI rules: [channels.md](channels.md). Wire rules:

- A negative layer count structurally marks the first extra composite plane as merged transparency; the reader never exposes that plane in the Channels dock. Every remaining plane after the source color components becomes a saved channel, regardless of name.
- Layered writes emit RGB, optional merged transparency (present only when the layered composite has transparent pixels), then every document channel. The header's channel total stays at or below 56; excess is an error, never silent discard. Opaque documents with no saved channels keep the historical byte-stable writer path.
- Image resources 1006 (legacy Pascal names), 1045 (Unicode names, authoritative), 1053 (identifiers for saved ALPHA channels only: advance its index only for alpha channels), and 1007/1077 (display info) travel in the same order as their planes. Opaque display records travel WITH their channel so reordering cannot detach spot metadata; imported spot records stay opaque-preserved while spot editing is unavailable.
- Raster layer masks stay layer channel `-2`, including masks that began as flat-image alpha; the old `"Alpha 1"` name heuristic and marked-mask promotion are NOT used for layered saves. Group masks ride the folder divider record the same way (mask-data block plus `-2` channel, matching PS); a mask-less group keeps its historical zero-channel record.
- A non-PSD flat image's meaningful per-pixel alpha becomes an editable grayscale layer mask via `promote_flat_alpha_to_layer_mask` (one ordinary pixel layer only; skips uniform alpha, text, smart objects). `kLayerMetadataDocumentAlpha` is the non-destructive flat-export marker: `document_alpha_rgba8` keeps covered RGB values intact when writing one-alpha-plane formats. PSD imports bypass the promotion.
- Layered saves with canvas transparency write PS's merged-alpha shape: straight RGB plus coverage, resource name `"Transparency"`, negative layer count. `psd_layered_write_keeps_merged_transparency_in_composite` and the real Content.psb regression keep it from becoming a phantom saved channel or layer mask.
