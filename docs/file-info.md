# File Info: image metadata

File > File Info (Ctrl+Alt+Shift+I, hotkey id `file.file_info`) shows every metadata item
Patchy can read from the document's source file, edits the Photoshop-style Description and
IPTC fields, and stages Strip All Metadata, Remove Location and Remove Edit History. File >
Strip All Metadata (`file.strip_metadata`, no default key) strips without opening the dialog.
Both are undoable (`push_undo_snapshot`) and change the file only on the next save.

## Model

- `DocumentFileInfo` (`src/core/document_file_info.hpp`, `Document::file_info()`) holds the
  canonical blocks: XMP packet, EXIF as a TIFF stream (no `Exif\0\0` prefix), IPTC-IIM
  bytes, the 1061 caption digest, the PSD copyright flag/URL, text entries (PNG
  tEXt/zTXt/iTXt, JPEG/GIF comments, read-only PDF `/Info` and SVG title/desc), opaque PSD
  metadata resources, PNG color chunks, the source ICC profile (display only), observed
  items that are shown but never written (C2PA, MPF images, trailers, thumbnails outside
  EXIF, linked profile paths) and the source file's block layout.
- Bulk bytes are `shared_ptr<const vector>`, so undo snapshots share them.
- The profile Patchy writes is `DocumentColorState::embedded_icc_profile`, never
  `source_icc`.
- Qt-free code lives in `src/formats/metadata/`: `tiff_ifd` (IFD reader/writer, caps and
  cycle detection), `exif_tags` (names, formatting, privacy), `xmp` (full-fidelity RDF model
  on `svg::parse_xml` with `keep_namespace_declarations`), `iptc_iim` (IIM, 8BIM IRB, MD5
  digest via `pdf::md5`), `jumbf` (C2PA box labels, capped CBOR), `metadata_containers`
  (per-format extraction), `metadata_view` (All Metadata rows and curated summaries),
  `metadata_edit` (fields, strip, save preparation) and `metadata_inject` (writing).
- The dialog is `src/ui/file_info_dialog.cpp`; it edits a working copy and returns it on OK.

## Capture on open

- PSD/PSB: `lift_file_info_resources` (end of `psd::DocumentIo::read`) moves resources
  1028, 1033, 1034, 1035, 1036, 1051, 1057, 1058, 1059, 1060, 1061, 1084-1087, 8000 and
  Patchy's 4213 out of `raw_psd_image_resources`. The writer puts them back from
  `file_info()`. A blob that still contains any of them was never lifted (a document built
  by other code); it stays authoritative and is written as before.
- Everything else: `capture_file_info` (`main_window_files.cpp`) runs
  `extract_file_info_from_path` on the file after the document is built. Files over 512 MB
  are scanned from their first 64 MB.
- An RGB source profile becomes the embedded profile only when the decoder left the pixels
  in the file's color space (Qt's PNG/JPEG/TIFF/WebP/GIF readers, the BMP reader, the JPEG XL
  reader for 8 and 16-bit RGB). WIC
  HEIF/JPEG XR, LibRaw and the HEIF Qt fallback convert to sRGB, so their profile is
  display-only, as are gray and CMYK profiles; `source_icc_note` says why.
- When the decoder applied the orientation (Qt `autoTransform` reported a transformation,
  the WIC HEIF reader, LibRaw), `bake_orientation` writes EXIF Orientation and XMP
  `tiff:Orientation` back as 1 and records the original as an observed item.
- Place, Files as Layers, smart objects and the clipboard capture nothing.

## Containers read

| Format | Read |
|---|---|
| JPEG | APP1 Exif, XMP and Extended XMP (merged after the GUID MD5 check), APP2 ICC chunks, APP13 IRB (IPTC, 1061, 1034/1035, thumbnails), COM, JFIF/JFXX thumbnails, APP2 MPF summary, APP11 JUMBF (C2PA), APP12 Ducky, FLIR, other APPn, data after EOI |
| PNG | tEXt/zTXt/iTXt (zlib capped at 16 MB), `XML:com.adobe.xmp`, ImageMagick `Raw profile type exif/xmp/iptc/8bim/icc`, eXIf, iCCP, sRGB/gAMA/cHRM/cICP/mDCV/cLLI/sBIT, tIME, caBX |
| TIFF and TIFF-based raw | IFD0 descriptive tags, 700, 33723, 34377, 34675, Exif/GPS/Interop IFDs, SubIFD previews, 37724 layer data (also opened as layers: [tiff.md](tiff.md)); image-structure tags stay out of the EXIF slot |
| WebP | ICCP, EXIF (with or without the `Exif\0\0` prefix), `XMP `, C2PA |
| JPEG XL | `Exif` and `xml ` boxes (also `brob`-compressed), the original color profile (libjxl synthesizes one for an encoded color space; none for plain sRGB), the box layout |
| HEIF/AVIF/CR3 | `Exif` and `mime` XMP items (iloc versions 0-2, idat), primary `colr` (ICC or nclx), irot/imir, thumbnail and auxiliary items, XMP and C2PA uuid boxes, CR3 CMT1/2/4 |
| RAF | the embedded preview JPEG's EXIF/XMP/ICC |
| GIF | Comment extensions, `XMP DataXMP` with its 258-byte trailer, `ICCRGBG1012` |
| BMP | V5 embedded profile; a linked profile is shown as a path (MachineInfo) |
| PDF | `/Info` strings (read only) and the catalog `/Metadata` XMP |
| SVG | `<title>`, `<desc>`, `<metadata>` RDF (typed nodes such as `cc:Work`), Inkscape path attributes |

Non-TIFF raws other than CR3 and RAF (CRW, X3F, MRW and the like) show no camera data:
LibRaw's `RawFileInfo` is not mapped into EXIF.

## Reading rules (MWG)

`read_fields` takes XMP first. IPTC-IIM fills gaps, and wins when the 1061 digest is present
and does not match the IIM (another program edited it). EXIF fills description, author and
copyright gaps and is authoritative for the capture date. `apply_fields` writes changed
fields to XMP always and to EXIF (ImageDescription, Artist, Copyright) and IIM only where
those blocks exist; a rewritten IIM gets a fresh digest. Copyright status also sets the PSD
copyright flag.

## Strip and removals

- Strip All Metadata (`strip_file_info`/`strip_metadata`) clears XMP, EXIF, IPTC, digest,
  copyright flag/URL, text entries, PSD metadata resources and observed items. The color
  profile and PNG color chunks stay unless the confirmation's checkbox drops them. The
  resolution lives in print settings and is never touched; the source layout list stays.
- Remove Location drops the EXIF GPS IFD and every XMP `exif:GPS*` property.
- Remove Edit History drops `xmpMM:History`, `DerivedFrom`, `OriginalDocumentID`,
  `Ingredients`, `Pantry`, `Manifest`, `photoshop:DocumentAncestors`, `TextLayers`,
  `History`, all `crs:` properties, and PSD resources 1086, 1087 and 8000.

## Writing

- Save and Save As write everything (`MetadataPolicy::All`). Export's shared section has a
  Metadata combo (`exportMetadataCombo`; None, Copyright and Contact Info, All), persisted
  as `saveOptions/exportMetadata` with tokens `none`/`copyright`/`all` (default `none`) and
  shown only for JPEG, PNG, TIFF, WebP, GIF and JPEG XL. `reset_export_options` puts every prompt
  back to All. Scripts pass `metadata: "all" | "copyright" | "none"` to `saveAs`/`exportAs`.
- `prepare_save_metadata` always writes EXIF Orientation and XMP `tiff:Orientation` as 1,
  sets PixelX/YDimension and XMP pixel sizes where present to the output size, rewrites
  existing X/YResolution from print settings, and drops the EXIF IFD1 thumbnail and
  `xmp:Thumbnails` (stale after edits). `CopyrightAndContact` keeps EXIF Artist/Copyright,
  XMP `dc:creator`, `dc:rights`, `xmpRights:*`, `photoshop:AuthorsPosition` and
  `CreatorContactInfo`, and IIM 2:80/2:85/2:116. `None` keeps only the profile.
- `inject_metadata` replaces what the encoder wrote in the same containers. An empty
  `SaveMetadata` leaves the encoder's bytes untouched, so documents without metadata or a
  profile save byte-identically to before this feature.
  - JPEG: after the leading APP0 segments, APP1 Exif (a MakerNote is dropped first if the
    block exceeds one segment), APP1 XMP or standard plus Extended XMP split at 60000
    bytes, APP2 ICC chunks, APP13 IPTC with the digest, COM per comment.
  - PNG: after IHDR, iCCP (sRGB/gAMA/cHRM then give way), cICP/mDCV/cLLI, eXIf, iTXt XMP and
    text chunks (tEXt when Latin-1, zTXt for compressed sources, iTXt otherwise). sBIT is
    never carried. Text chunks with other keys (PatchyPaletteNames) stay.
  - TIFF: a new IFD0 appended at the end of Qt's file with XMP, IPTC, IRB, ICC, the EXIF
    descriptive tags and new Exif/GPS IFDs, re-encoded to the file's byte order.
  - WebP: libwebpmux chunks, also for animated WebP.
  - GIF: Comment and XMP extensions after the global color table (the header becomes
    GIF89a), also for animated GIF.
  - JPEG XL: no injection; the writer gets EXIF and XMP from `save_metadata_for` and emits
    uncompressed `Exif` and `xml ` boxes itself, also for animated JPEG XL (docs/jxl.md).
- PSD: `image_resources_for_document` writes the lifted resources (XMP still goes through
  `repair_xmp_date_timezones`) plus resource 4213 (`'PtcM'`, v1, big-endian: u32 count,
  then per entry u8 FileInfoTextKind and key/value/language as u32 length + UTF-8) for PNG
  text and comments. Patchy-born documents carry none, so the byte-stability canaries are
  unchanged.
- PDF: the image-page writer (`pdf::ImageWriter::set_document_info`) writes `/Info` Title,
  Author, Subject (description) and Keywords from the first page's fields as UTF-16 hex
  strings; the Qt PDF paths (editable layers) set the title only.
- Not carried: C2PA (edits invalidate the signature), MPF secondary images and other
  trailers (Qt writes one image), a source PDF's `/Info` and SVG title/desc entries (only
  the fields above reach a PDF), JFIF thumbnails. BMP, ICO, TGA, PCX, ILBM, DDS, Proton,
  JPEG XR and SVG saves write no File Info metadata.

## Limitations

- MakerNotes are copied verbatim inside the rebuilt EXIF block; makers that use absolute
  offsets (Canon among them) can end up pointing at the wrong bytes, as with most writers
  other than ExifTool.
- PSD thumbnails (1033/1036) are kept verbatim and can show the pre-edit image until
  stripped.

## Display

- All Metadata lists every row from `describe_file_info` grouped by container, with a
  privacy column. Group, name and value text there are specification identifiers and
  container descriptions shown untranslated, as ExifTool does (docs/localization.md); the
  curated pages translate their labels.
- Privacy classes (`MetadataPrivacy`, string ids from `privacy_class_id`, permanent): none,
  location, deviceIdentity, personal, hiddenImage, editHistory, machineInfo, timestamp,
  aiPrompt, provenance. The banner lists every class present except timestamps.
- AI Generation reads Automatic1111/Forge `parameters` (also from EXIF UserComment),
  ComfyUI `prompt`/`workflow`, InvokeAI, NovelAI, Fooocus, IPTC DigitalSourceType and
  C2PA actions.

## Tests

Core: `metadata_*` in `tests/core/file_info_metadata_tests.cpp` (every container from
self-built bytes, deterministic mutation pass, Unicode paths, PSD round trip, edit, strip,
save rules, injection round trips). UI: `ui_file_info_*` in `tests/ui/file_info_tests.cpp`;
scripting: `ui_script_file_info_*`.
