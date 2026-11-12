# File formats: registry, per-format quirks, PSB, document alpha

Read before changing format I/O, file filters, notices, or alpha/mask import.

Related documents:

- [file-formats-psd.md](file-formats-psd.md): PSB wire rules, CMYK, gray and the other non-RGB modes on read, 16 and 32-bit PSD/PSB import, saved PSD channels, and flat-image alpha on open.
- [psd-layer-blocks.md](psd-layer-blocks.md): PSD per-layer blocks: adjustments, clipping, blending, locks and styles.
- [psd-reader-hardening.md](psd-reader-hardening.md): damaged PackBits scanlines, the PSD/PSB header, nesting and record limits, and the decode budget every reader checks before sizing a buffer from declared dimensions.
- [file-formats-saving.md](file-formats-saving.md): the save guard for layered documents bound for flat formats, the shared flatten and export-transform helpers, and the Saving progress dialog.

## Registry and dispatch

Save/Save As writes and saved-state bookkeeping live in `ui/document_save_coordinator.cpp`; dialog preflight, status text and export entry points in `main_window_files.cpp`. The coordinator shares its layer-loss classification with those dialogs.

- `builtin_format_registry()` (format_registry.cpp, function-local static) is the single instance. `load_document_from_path` (main_window_files.cpp) consults it BEFORE the QImageReader fallback; a throwing registry read still falls back to Qt where a plugin exists; if Qt also fails the REGISTRY error is reported. Handlers may be read-only (`write == nullptr`) and may carry a `sniff` check for the smart-object classifier (`.ase` Aseprite vs Adobe `ASEF` swatches); the open path matches by extension.
- One filter table: `file_format_entries()` (main_window_files.cpp) generates open/save/export filters, `is_supported_image_extension`, `save_file_filter_for_path`, and `path_with_default_extension`. Display names sit in `QT_TRANSLATE_NOOP("QObject", ...)` and are shown through `translate_data_text` ([localization.md](localization.md)).
- A new format needs one table row, one registry row, one writer branch. A new openable extension also goes in each platform's "Open with" list: `$PatchyOpenWithExtensions` (packaging/windows/InstallPatchy.ps1), the `MimeType` line of packaging/linux/com.rtsoft.patchy.desktop (its shared-mime-info type), and `CFBundleDocumentTypes` in packaging/macos/Info.plist.in (macOS ignores `CFBundleTypeExtensions` in an entry with `LSItemContentTypes`, so extensions without a listed type go in the extension-only entries). `open_with_lists_claim_every_openable_extension` (UI suite) checks all three against `MainWindow::openable_file_extensions()`. Left out on purpose: PDF everywhere, JPEG XR outside Windows, two-part names (`.xcf.gz`) on Windows and macOS, and extensions shared-mime-info has no type for (PSB, Affinity, Paint.NET, `.rttex`). Linux also leaves out Krita's and GIMP's own types (`application/x-krita`, `image/x-xcf`, `image/x-compressed-xcf`), and the test fails if they return: GNOME has no handler rank, and for a type with no default Patchy's Flatpak or AppImage entry sorts ahead of distro Krita and GIMP, so claiming them takes those users' double-clicks (gio and xdg-mime on Ubuntu 24.04, October 2026).

- When QImageReader rejects a file, `open_failure_reason` (main_window_files.cpp) replaces Qt's two generic strings with what `diagnose_undecodable_image` (image_open_diagnosis.cpp, Qt-free; `undecodable_image_diagnosis_names_the_real_reason`) reads from the bytes: empty, text, another format's signature Patchy cannot open, a cut-short or hierarchical JPEG, a TIFF codec the libtiff Qt bundles lacks, or a TIFF sample layout Qt rejects (layout sweep: agents_local.md). TIFF layouts Qt refuses (float with extra samples, 32-bit integers, planar float) decode through `psd::read_flat_tiff` first: [tiff.md](tiff.md#flat-fallback-decoder). JPEG-compressed TIFF (compression 7) decodes instead: `ui/tiff_jpeg_reader.cpp` splices JPEGTables into each strip or tile and decodes through Qt's JPEG reader (`ui_jpeg_compressed_tiff_opens_through_the_jpeg_decoder`); old-style JPEG (6) and 12-bit strips still fail, with their reasons. 12-bit and lossless JPEG files decode before that diagnosis: [12-bit and lossless JPEG](#12-bit-and-lossless-jpeg). The Open failed box shows the native path (issues 112 and 117). HDR/EXR/JPEG 2000: [hdr-formats.md](hdr-formats.md); AVIF: [avif.md](avif.md).
- Open by content (issue 362, `ui_open_by_content_when_the_extension_is_wrong`): `load_document_from_path` and the drag check sniff the head (`sniff_openable_image_format`). Unknown names (`src.bin`, none) and PNG/JPEG/GIF/WebP/BMP swaps read as the content, other known names only once the name fails (raws are TIFF inside). `content_extension` makes Save a Save As with that extension.

## Open-dialog filter contract

Open, sprite-sheet, and image-sequence dialogs pass `FilterNameDetails::Hidden` through `get_open_file_names`. File > Open and Open Recent Folder are multi-select: each pick opens as its own document in dialog order, the last one active, through the multi-file drop loop (`ui_open_dialog_opens_every_selected_file`). Qt shows only the text left of the last `(` and uses the final parenthesized list as the machine filter, so `open_file_filter()` deliberately writes each row as `Name (hint) (patterns)`: the hint is the pattern list, cut after `kMaxVisibleFilterPatterns` (6) with "and more" (`visible_pattern_hint`), the spec is complete.

Row order: the all-formats row, then the format rows A to Z by translated name (case-insensitive `QCollator`), then All Files (`ui_open_dialog_lists_formats_alphabetically`, issue 270). Only the Open list is sorted: Save As and Export keep `file_format_entries()` order, Photoshop Document first. The Open dialog reopens on the last picked row (`lastOpenFilter`, written only when files were picked; `ui_open_dialog_remembers_last_filter`, issue 114). The HEIF row exists only where a decoder does (`heif_decoder_available`, issue 113; [heif.md](heif.md)).

The visible portion must keep a `*.` token. The Windows 11 native dialog appends the complete semicolon-joined pattern list to any filter name without one, so the all-formats row uses the short `(*.psd *.png *.jpg and more)` hint instead of ~50 patterns. `ui_open_dialog_hides_name_filter_details` pins the shape, the row length limit and the raw hint. The hint cap suits GNOME's GTK 4 portal dialog (about ten rows, no scrollbar; issue 270, [wayland.md](wayland.md)).

## Save dialogs: the name follows the picked type

Every `get_save_file_name` dialog (Save As, Export, Save a Copy, and the preset, theme and script save dialogs) applies `save_name_for_picked_filter` (dialog_utils.cpp; issue 651): a name ending in an extension some row lists gets the picked row's first extension when the picked row does not list it (`Test.png` under JPEG becomes `Test.jpg`, `Test.PNG` becomes `Test.JPG`, `Test.jpeg` stays). Unlisted extensions (`photo.final`, `notes.tar.gz`), names without one, and catch-all rows (`*`, `*.*`, or a row covering two other rows) leave the name alone; names without an extension still get one from `path_with_default_extension`.

- While the dialog is open: Qt's own dialog (offscreen tests) and GTK swap the name on a type change through `filterSelected`. Qt's widget dialog swaps any extension by itself first, so its typed name is tracked and the rule reapplied to it. The Windows shell dialog swaps natively (Qt's save helper always sets a default extension); macOS's panel follows the type itself; the xdg portal (Flatpak, Snap, `QT_QPA_PLATFORMTHEME` naming a portal) cannot be changed while open.
- After the dialog: the same rule runs on the accepted name, so the picked type wins on every platform (Wine, the portal, a typed `Test.png` with JPEG picked). The dialog only asked about replacing the typed name, so a renamed file that exists gets its own question (`saveReplaceRenamedFileMessageBox`); No reopens the dialog with the new name. Tests: `save_name_for_picked_filter_follows_the_picked_type`, `ui_save_dialog_name_follows_the_picked_type`, `ui_save_dialog_asks_before_replacing_the_renamed_file`, `ui_save_as_name_follows_the_picked_type`. A test that types a name into a save dialog must pick the matching row too, as a user would.

Desktop file drops copy the accepted local paths and queue processing on the main window after the handler returns. Never enter import dialogs or decode inside the native drop callback: Windows Explorer waits for it (a RAW or PDF dialog would block it). Main-window, canvas, tab-area, and floating-window drops share this path and keep drop order; cancelling one import still opens the rest; closing the main window first discards the queue.

## Per-format catalogue

Everything reads and writes except camera raw, HEIF/HEIC, .af, .pdn, .xcf, .kra and portable anymaps (read-only); JPEG XR reads and writes on Windows only. Modules live in src/formats/, Qt-free, explicit-endian via `binary_le.hpp` (LE) or `psd_binary.hpp` (BE).

- PSD/PSB: [PSB rules](file-formats-psd.md#psb-large-document-format-read--write) plus [ps-compat.md](ps-compat.md); 16/32-bit files open at their depth while deep editing is on ([high-bit-depth.md](high-bit-depth.md)), else convert to 8-bit ([deep import](file-formats-psd.md#16-bit-and-32-bit-psdpsb-import)). An unnamed layer record takes Photoshop's display name: "Layer N" (N counts ordinary layers from the bottom, 1-based), or "Background" for a bottom record with no transparency channel.
- BMP: includes 32-bit `BI_RGB`/compression 0, whose 4th byte Patchy keeps (feeds [document-alpha import](file-formats-psd.md#saved-psd-channels-and-flat-image-alpha)).
- ICO/CUR: every embedded size imports as a hidden layer named "WxH"; the writer reuses a matching "WxH" pixel layer verbatim, so sizes round-trip. 256px entries are PNG-compressed via `ico::set_png_codec` (installed in the MainWindow ctor). CUR hotspots ride layer metadata `patchy.cursor_hotspot`.
- TGA: types 1/2/3/9/10/11, both origin flags; 15/16-bit rejected; palette-mode documents write type 1 indexed.
- Aseprite: frame 1 only; layer tree/blend modes/opacity/closed folders (layer flag 32) round-trip; zlib cels via vendored `src/formats/miniz/`; verified against the real Aseprite CLI.
- PCX: 8-bit indexed EOF-palette plus 24-bit 3-plane RLE.
- ILBM/PBM: ByteRun1 via the shared `psd::decode_packbits`/`encode_packbits_row` (psd_descriptor.{hpp,cpp}); EHB supported, HAM rejected; writer emits planar ILBM with masking type 2.
- Portable anymaps `.pbm/.pgm/.ppm/.pnm`: a read-only filter row, no registry row; Qt's PPM handler reads P1 to P6 and sniffs the magic behind `.pnm` (`ui_portable_anymap_files_open`). No PAM or PFM.
- GIF and animated WebP: [animation.md](animation.md) owns import, export, timing, loop counts, codec isolation and scripted animation.
- PNG/JPEG/TIFF and still WebP: Qt readers/writers (12-bit and lossless JPEG: section below). Photoshop layers in a TIFF (tag 37724) open as layers: [tiff.md](tiff.md). WebP options retain quality (`saveOptions/webpQuality`, default 75) and lossless (`saveOptions/webpLossless`; quality 100 also means lossless).
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

Import and export: [pdf.md](pdf.md). PDF has no registry row and is a read-only source; the filter-string and [save-guard](file-formats-saving.md#layered-documents-and-flat-formats-the-photoshop-style-save-guard) rules still apply to it.

## PSD adjustment layers, clipping, blending, and layer styles

The per-layer blocks (`.acv`, native adjustment blocks, `blnc`, `brit`/`CgEd`, `plAD`, `hue2`, clipping, `iOpa`, `lspf`, `brst`, Blend If, layer styles, origins) are in [psd-layer-blocks.md](psd-layer-blocks.md).

## 12-bit and lossless JPEG

Qt's JPEG plug-in reads 8-bit samples only (issue 438). When it fails and the first frame header (`jpeg_deep::needs_deep_decoder`) says a precision other than 8 or lossless coding (SOF3/SOF11), `load_via_qt` decodes through `formats/jpeg_deep_reader.cpp`: the vendored libjpeg-turbo 3.1.4.1, decompression only (`src/formats/libjpeg-turbo`, `cmake/patchy_libjpeg.cmake`; IJG, BSD-3 and zlib licences in NOTICE-THIRD-PARTY.md). 12-bit lossy files (sequential or progressive) and lossless files of 9 to 16 bits open as 16-bit documents (`round(v * 65535 / (2^bits - 1))`), lossless files of 2 to 8 bits at 8 bits; grayscale opens as gray RGB like an 8-bit gray JPEG. CMYK/YCCK deeper than 8 bits and hierarchical frames stay unsupported with their own messages. Ordinary 8-bit JPEGs never reach it, so their bytes and pixels are unchanged; saves are always 8-bit through Qt.

- Every libjpeg-turbo global carries a `patchy_ljt_` prefix (`patchy_libjpeg_symbols.h`, regenerated by `scripts/dev/generate-libjpeg-symbols.py` after an upgrade), so the copy never meets the libjpeg Qt's plug-in uses, shared or static (browser build). The sources are unmodified; the three configure headers come from the CMake file.
- libjpeg's error exit longjmps, so only `jpeg_deep_glue.c` (C) calls the library; no C++ frame sits between `setjmp` and `longjmp`.
- Tests: `jpeg_deep_reads_12bit_and_lossless_fixtures` (core; libjpeg-turbo's `testorig12.jpg` against an independent imagecodecs decode, plus generated 12-bit gray and 16-bit RGB lossless files checked pixel for pixel) and `ui_twelve_bit_jpeg_opens_as_16_bit_document` (the open path, Unicode name).

## Damaged PackBits scanlines and reader limits

PackBits leniency, the PSD/PSB reader limits and the writer caps: [psd-reader-hardening.md](psd-reader-hardening.md).

## Import notices

Readers report dropped or approximated features via `FormatReadResult::notices`. `open_document_path` shows them in the STATUS BAR (first note plus "+N more", no "Opened" prefix, in the yellow notice flash of `show_import_notes_status`; a clean open clears the status bar, since the tab names the file: issues 518 and 487); the consolidated `importNoticesMessageBox` popup (`show_import_notices_popup`) appears only with `imports/showPsdWarningsAndInfo` enabled (the preference that also gates the PSD compatibility report). That report (`show_compatibility_report`) counts a preserved layer block as unknown only when `psd::is_understood_layer_block_key` rejects it (the reader's modeled keys plus Photoshop's routine bookkeeping such as `lyid`, `lnsr`, `shmd`, `fxrp`, `lyvr`; document-level blocks likewise through `is_understood_global_block_key`; #559), lists no text layer Patchy lays out (only a placeholder raster; #646), lists neither preserved unknown image resources nor embedded smart object sources (both round-trip unchanged and ask nothing of the user; #669), starts every per-layer line with `Layer 'Name'` and adds none for pixel layers at the document's depth, 16 and 32 bits included (layers that break it share one count line; #712), opens only when something is left to list, and its "Don't show this again" check turns the preference off. Exception: a conversion a save makes permanent forces it as a warning box (`OpenDocumentResult::force_import_notices_popup`; issue 52), also for Open Folder's background files (their other notes drop); unattended automation never shows it. The PSD reader's metadata triggers it: `psd.depth` above 8 on a document that ended at 8 bits (deep editing off), `psd.color_mode` other than RGB, CMYK or Grayscale, or `psd.ink_adjustments` (adjustments that acted on CMYK inks). Open-time dialogs run one event-loop turn after the new tab is fit (#593), in their old order, skipped if the tab closed. Files opened together (several File > Open picks, a drop, command-line or second-instance files, pasted file paths, Open Folder with its background files' forced notes) form an open batch (`begin_open_batch`/`end_open_batch`, nesting): every file's dialogs wait until the whole batch is open, so none shares the screen with the next file's `openProgressDialog` (#624), then run in open order with that file's tab active, and the tab the batch ended on is active again. saved choices apply at once and running scripts get them inline. A GIMP answer's notes stay out of the popup (#551). Tests pump events, then assert `statusBar()->currentMessage()`; tests that expect the popup need the REPEATING QTimer dismisser (a one-shot fires during the open-progress phase and the suite hangs).

## Fixtures and verification

Committed fixtures: `test-fixtures/<format>/` (provenance: NOTICE-THIRD-PARTY.md); tests synthesize adversarial files. The PSD set includes PS-resaved acceptance files for layer styles and Blend If (`photoshop-*-roundtrip.psd` plus `-render.bmp`). Verify writers with independent decoders.
