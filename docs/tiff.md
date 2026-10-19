# Layered TIFF

Read before changing TIFF open or save behavior. Flat TIFF decoding is Qt's (libtiff) plus the
JPEG-in-TIFF reader ([file-formats.md](file-formats.md)); File Info capture is
[file-info.md](file-info.md). This document covers Photoshop's layers inside a TIFF.

## Wire format

Photoshop's "Save layers" TIFF keeps the flattened composite in the first directory (IFD0) and
the layers in tag 37724 (ImageSourceData, type UNDEFINED):

- `Adobe Photoshop Document Data Block\0` (36 bytes), or `Adobe Photoshop Document Data V0002\0`
  for PSB-style widths (u64 channel lengths, u64 lengths for the wide-key set of
  `tagged_block_length_is_u64`, u32 RLE row counts; never seen in the wild).
- Then tagged blocks: signature, key, u32 length, payload, padded to 4 bytes. `Layr` (8-bit),
  `Lr16` or `Lr32` holds the layer info exactly as a PSD's layer info section (count, records,
  channel data); `LMsk` is the PSD global layer mask info; `Patt`, `FMsk`, `cinf`, `lnk2` and
  the rest are the PSD's document-level tagged blocks.
- Tag 34377 is the PSD image resource block, big-endian in either TIFF byte order. Tag 34675
  carries the ICC profile; the resource block may lack 1039.

### Byte order

The layer data follows the TIFF's byte order. In an IBM PC (`II`) file every structural field
is little-endian: lengths, counts, rectangles, channel ids, compression codes, RLE row counts,
four-character codes (so `8BIM` reads `MIB8` and `Layr` reads `ryaL`), UTF-16 code units
(`luni`, descriptor strings), doubles and every descriptor value. Channel pixel samples stay
big-endian (16-bit and float samples, inside raw, RLE and ZIP data alike), and embedded files
in `lnk2` are untouched. Facts pinned on Photoshop CC 2014 and 23.5 files from GIMP merge
request 910 (local fixture `layered-tiff/gimp-mr910`): their IBM PC and Macintosh `ret` saves
decode to identical 16-bit layers. Each block's own signature (`8BIM` or `MIB8`) decides the
order it is read in; a file that mixes them is refused.

Pinned byte for byte: `scripts/dev/tiff/ps-save-layered-tiffs.ps1` saves Photoshop PSDs as
IBM PC and Macintosh TIFFs into `local-test-fixtures/layered-tiff/photoshop` (11 documents with
text, effects, adjustments, embedded smart objects with smart filters, vector masks, patterns,
8/16/32 bits), and `tiff_layers_photoshop_byte_order_pairs_match_if_available` requires each
little-endian file to convert to exactly its big-endian twin's layer data, nothing dropped.
Exceptions Photoshop makes, all pinned there:

- `Txt2` and the EngineData inside `TySh` are byte streams, identical in both orders.
- An unedited legacy text layer keeps its `TySh` text version and descriptor big-endian inside
  an IBM PC file (the CS4-era Flat-filter-list.psd); the descriptor version (16) tells which.
- `hue2` ends in doubles after its 136 bytes of i16 fields.
- `liFD` version 8 ends with a versioned descriptor (`contentID`).

Still unpinned: non-identity blending ranges (read as byte quadruples; identity ranges look the
same either way).

## Reading

`psd::read_layered_tiff` (src/psd/psd_tiff.cpp, Qt-free) builds a PSD stream and parses it with
`psd::DocumentIo::read`, so a layered TIFF gets every layer feature a PSD does:

- Header from IFD0: width, height, BitsPerSample (8, 16, or 32 with SampleFormat 3) and
  Photometric (0/1 gray, 2 RGB, 5 CMYK, 8/9 Lab; anything else is refused). Channels are the
  color channels plus one when the layer count is negative (merged transparency).
- Resources from 34377 plus a synthesized 1039 (from 34675) and 1005 (from X/YResolution) when
  absent. Empty color mode data.
- 8-bit: the layer block becomes the layer info section. 16/32-bit: an empty section plus the
  `Lr16`/`Lr32` global block, the PSD layout. A layer block under another depth's key is used
  only when it is the only one.
- The image data section is two bytes: the layered read never decodes a composite
  (`retain_flat_composite` is forced off). Saved alpha channels in IFD0 are not imported.
- The document is marked `metadata().values["tiff.layers"] = "photoshop"`; `psd.version` stays
  "PSD", which keeps the flat-alpha promotion off as for PSDs.

Little-endian data converts first (src/psd/psd_little_endian.cpp): a walker reads each field
little-endian and rewrites it big-endian in place, mirroring the PSD parsers field for field
(layer records, mask data, channel data, descriptors and every value type the descriptor reader
knows, `TySh`, `PlLd`, `SoLd`, `shmd`, `lrFX`, path records, `levl`, `curv`, `hue2`, `blnc`,
`brit`, `expA`, `selc`, `mixr`, `post`, `thrs`, `lsct`, `fxrp`, `Patt`, `lnk2`, `FEid`/`FXid`,
`LMsk`, `FMsk`, and any key holding a version-16 descriptor; empty payloads and `Txt2` pass
as they are). A block that does not walk to its end (zero padding allowed) is dropped rather
than guessed, and the open says so once: `tySh` (the modern `TySh` carries the content),
`grdm`, `phfl`, `Mt16`/`Mt32`/`Mtrn`/`Alph`, `Anno`, and unknown layouts.

Hostile input: the IFD walk bounds every offset and count (4096 entries, 65536 pages, cycle
check), the walker bounds every read and nesting (64 levels), and the PSD reader's own limits
apply after conversion. `tiff_layers_damaged_data_throws_or_opens` truncates and byte-flips
synthetic files of both orders.

### App open path

`load_document_from_path` (main_window_files.cpp) tries `open_layered_tiff` for `.tif`/`.tiff`
before Qt. No 37724 header: Qt opens it as always. Header present but unreadable: Qt opens the
composite and the import notes say why ("could not be read, so it opened as a flat image").
A layered open shares the PSD branch's notes (`finish_photoshop_layers_import`) and takes File
Info from the TIFF (`capture_file_info`), not from the PSD resources.

### Multi-page TIFF

A multi-page TIFF opens page 1, as in Photoshop, with a note naming the page count
(`psd::tiff_page_count` skips reduced-resolution previews, NewSubfileType bit 0). Pages are not
imported as layers: pages are usually separate images (scans, faxes, microscopy stacks), GIMP
itself asks Layers-or-Images on import, and a wrong guess would silently merge documents.
Revisit only with a clear signal such as GIMP's PageName tags.

## Writing

`psd::write_layered_tiff` (psd_tiff.cpp) writes the PSD writer's output
(`write_layered_rgb8`, `preserve_source_color_mode` off, so always RGB) as a big-endian
(Macintosh) TIFF, which needs no byte swapping of Patchy's own blocks:

- IFD0: the PSD composite (the compositor's flatten, straight color), interleaved, Adobe Deflate
  (compression 8) in strips of about 256 KB, horizontal differencing (predictor 2) at 8 and
  16 bits, none for 32-bit float (SampleFormat 3). Extra samples: unassociated alpha (2) when
  the layer count is negative, then saved channels as unspecified (0). X/YResolution from the
  print settings in inches.
- 34377: the PSD image resources minus 1039, 1060, 1058, 1059, 1028 and 1061, which a TIFF
  keeps in its own tags (Photoshop's own files carry none of them there). 34675: the embedded
  profile.
- 37724: the header, then the depth's layer block (`Layr` from the layer info section, or the
  PSD's `Lr16`/`Lr32` global block), `LMsk` when the global layer mask info is not empty, then
  the other global blocks in the PSD's order, each padded to 4 bytes.
- Over 4 GB (classic TIFF offsets) throws, naming PSB.

The UI (`write_flat_image_file`, image_document_io.cpp) then injects File Info like any TIFF
(`inject_tiff`): XMP (700), IPTC (33723), the profile and EXIF. `inject_tiff` keeps a 34377
already in the file and replaces only its IPTC record and digest (1028, 1061), so the layer
resources survive; Qt's flat TIFFs carry no 34377, so their bytes are unchanged.

### When a save is layered

`ImageSaveOptions::tiff_layers` (per save, never persisted) decides; unset, the rule in
`MainWindow::save_document_to_path` applies: layered when `flat_save_discards_layers` and
the save is not an export (`export_copy`) and no explicit CMYK color form was asked for. So:

- A document without layers a flat file would lose takes the unchanged Qt path, byte for byte.
- Save (Ctrl+S) on a `.tif` path writes the layers in place: `save_discards_layers` is false
  for TIFF, so it no longer routes to Save As.
- Save As to TIFF with such a document shows TIFF Options (`prompt_tiff_save_options`,
  `tiffSaveOptionsDialog`, `tiffLayersCheck`, on by default). Unchecked: a flattened copy, as
  for any flat format, without the flatten warning (the dialog's hint says it).
- Export Flat Image, a script's `exportAs` and `--export` stay flat; a script's `saveAs` takes
  `layers` (docs/scripting.md, scripting-api-changes.md 2026-10-19).
- A TIFF with layers is RGB: `cmyk_form_available` is false for it, so CMYK Proof asks to save
  in RGB, and `layers: true` with `colorMode: "cmyk"` throws.

Photoshop acceptance: `tiff_layers_writer_acceptance_files_if_available` writes local
Photoshop PSDs and the synthetic document at 8, 16 and 32 bits as layered TIFFs into
`test-artifacts/layered-tiff-acceptance`; `scripts\dev\photoshop-open-check.ps1` opens them in
Photoshop and reports CLEAN, UNKNOWN-DATA, or DIALOG with the dialog's text.

## Tests

Core `tiff_layers_*` (tests/core/tiff_layers_tests.cpp) on synthetic files from
`tests/layered_tiff_fixture.hpp`: one encoder writes either byte order with big-endian samples,
so a little-endian file must yield the same PSD stream byte for byte as its big-endian twin, at
8, 16 and 32 bits. Plus groups, masks, a Color Overlay, Unicode names, merged transparency,
dropped-block notices, refusals, page counts, damage, Unicode paths, and the local GIMP and
Photoshop fixtures; `tiff_layers_writer_*` round-trip the writer (composite decoded and
compared with the compositor, tags, resources, depths, Unicode paths). UI
(tests/ui/layered_tiff_tests.cpp): `ui_layered_tiff_*` and `ui_multi_page_tiff_*` drive the
open and save paths, `ui_tiff_options_dialog_offers_keep_layers` the dialog, and
`ui_script_save_as_tiff_layers_option` the script key.
