# File Info: image metadata

File > File Info (Ctrl+Alt+Shift+I, hotkey id `file.file_info`) shows every metadata item
Patchy can read from the document's source file, edits the Photoshop-style Description and
IPTC fields, and stages Strip All Metadata, Remove Location and Remove Edit History. File >
Metadata > Strip All Metadata (`file.strip_metadata`, no default key) strips without opening the dialog.
Both are undoable (`push_undo_snapshot`) and change the file only on the next save.
File > Metadata > Scan for Watermarks... (`file.scan_for_watermarks`, no default key; also on the
document tab's menu) opens File Info on the Watermarks page with the scan run.

Related documents:

- [file-info-save-stamp.md](file-info-save-stamp.md): the save stamp PSD, PSB and JPEG saves record in XMP and EXIF, the fresh PSD and JPEG thumbnails, and the Saved metadata preference (None, Basic, Personal).
- [file-info-watermarks.md](file-info-watermarks.md): watermarks declared in C2PA metadata and the on-request pixel scan for the open Stable Diffusion invisible watermarks (payloads, decoder, match rule, limits).

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
- XCF: the XCF reader fills `file_info()` from GIMP's parasites (comment, Exif, XMP, and the
  `gimp-image-metadata` tag list as Exif, XMP and IPTC; [xcf.md](xcf.md)) and names GIMP as
  the Application when the file names no creator; `capture_file_info` keeps it as it keeps PSD's.
- Everything else: `capture_file_info` (`main_window_files.cpp`) runs
  `extract_file_info_from_path` on the file after the document is built. Files over 512 MB
  are scanned from their first 64 MB.
- An RGB source profile becomes the embedded profile only when the decoder left the pixels
  in the file's color space (Qt's PNG/JPEG/TIFF/WebP/GIF readers, the BMP and AVIF readers, the
  JPEG XL reader for 8 and 16-bit RGB). WIC HEIF/JPEG XR, LibRaw and the HEIF Qt fallback convert to sRGB, so their profile is
  display-only, as are gray and CMYK profiles; `source_icc_note` says why. A kept RGB
  profile that is not sRGB then converts to sRGB on open (on by default) and becomes
  display-only too ([rgb-profiles.md](rgb-profiles.md)).
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
copyright gaps and is authoritative for the capture date: DateTimeOriginal joined with
OffsetTimeOriginal (DateTime, the Modified fallback, with OffsetTime); without an offset, an
XMP CreateDate naming the same second keeps its time zone. `apply_fields` writes changed
fields to XMP always and to EXIF (ImageDescription, Artist, Copyright) and IIM only where
those blocks exist; a rewritten IIM gets a fresh digest. Copyright status also sets the PSD
copyright flag.

## Data Mining

IPTC Data Mining (#658; IPTC Photo Metadata 2023.1, PLUS LDF XMP specification "Data Mining" and
"Other Constraints"): `FileInfoFields::data_mining` is `plus:DataMining`, a simple property holding one
URI of the PLUS vocabulary (`kDataMiningValues`, nine `http://ns.useplus.org/ldf/vocab/DMI-*` values in
the specification's order; empty = not set); `other_constraints` is `plus:OtherConstraints`, a Lang Alt,
the human-readable constraint `DMI-PROHIBITED-SEECONSTRAINT` points to. Both go through the XMP field
table, so `apply_fields` writes them and Strip All Metadata drops them with the packet.
`data_mining_value_index` matches the vocabulary ignoring ASCII case (PLUS 2.0.1 changed the
identifiers' case). A value outside the vocabulary, or a known one in another case, is never
rewritten: the File Info combo (`fileInfoDataMiningCombo`, Description page under Copyright Info URL,
with `fileInfoOtherConstraintsEdit`) keeps the stored text as the selected item's data and shows an
unknown value as "Other: <value>". It is a declaration; nothing enforces it. The Preferences default
(`saveMetadata/dataMining`) is in [file-info-save-stamp.md](file-info-save-stamp.md#saved-metadata-preference).

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
  Metadata combo (`exportMetadataCombo`; None, Basic, Copyright and Contact Info, All), persisted
  as `saveOptions/exportMetadata` with tokens `none`/`basic`/`copyright`/`all` (default `none`) and
  shown only for JPEG, PNG, TIFF, WebP, GIF, JPEG XL and AVIF. `reset_export_options` puts every prompt
  back to All. Scripts pass `metadata: "all" | "copyright" | "basic" | "none"` to `saveAs`/`exportAs`.
  File > Export for Web has its own Metadata combo (`exportForWeb/metadata`, default `none`).
  `MetadataPolicy` is append-only (Basic was added last).
- Basic (October 27, 2026) means what Preferences "Saved metadata: Basic" means: the save stamp
  ([file-info-save-stamp.md](file-info-save-stamp.md#save-stamp)) and nothing else.
  `prepare_save_metadata` stamps an empty File Info for the file being
  written (`save_stamp_now`, a `SaveStampDocument` with its MIME type as dc:format, mode 3, or 4
  for a CMYK JPEG, the output size and the document's resolution): EXIF IFD0 Orientation,
  resolution, Software and DateTime, Exif IFD version, color space and pixel size; XMP
  CreatorTool, Create/Modify/MetadataDate (the export time), dc:format, photoshop:ColorMode,
  fresh xmpMM ids and one "created" history entry. Nothing from the document's File Info
  (no author, copyright, description, keywords, camera data or GPS). Without a stamp (the core
  writers) Basic writes what None writes.
- `prepare_save_metadata` always writes EXIF Orientation and XMP `tiff:Orientation` as 1,
  sets PixelX/YDimension and XMP pixel sizes where present to the output size, rewrites
  existing X/YResolution from print settings, and drops the EXIF IFD1 thumbnail and
  `xmp:Thumbnails` (stale after edits; JPEG writes a fresh IFD1, "Thumbnails" in
  [file-info-save-stamp.md](file-info-save-stamp.md#thumbnails)). `CopyrightAndContact` keeps EXIF Artist/Copyright,
  XMP `dc:creator`, `dc:rights`, `xmpRights:*`, `photoshop:AuthorsPosition`,
  `CreatorContactInfo`, `plus:DataMining` and `plus:OtherConstraints`, and IIM 2:80/2:85/2:116. `None` keeps only the profile.
- `inject_metadata` replaces what the encoder wrote in the same containers. An empty
  `SaveMetadata` leaves the encoder's bytes untouched, so documents without metadata or a
  profile save exactly the bytes the encoder wrote.
  - JPEG: after the leading APP0 segments, APP1 Exif (a MakerNote is dropped first if the
    block exceeds one segment), APP1 XMP or standard plus Extended XMP split at 60000
    bytes, APP2 ICC chunks, APP13 IPTC with the digest, COM per comment.
  - PNG: after IHDR, iCCP (sRGB/gAMA/cHRM then give way), cICP/mDCV/cLLI, eXIf, iTXt XMP and
    text chunks (tEXt when Latin-1, zTXt for compressed sources, iTXt otherwise). sBIT is
    never carried. Text chunks with other keys (PatchyPaletteNames) stay.
  - TIFF: a new IFD0 appended at the end of Qt's file with XMP, IPTC, IRB, ICC, the EXIF
    descriptive tags and new Exif/GPS IFDs, re-encoded to the file's byte order. An IRB
    already there (a layered TIFF's, [tiff.md](tiff.md)) keeps its other resources.
  - WebP: libwebpmux chunks, also for animated WebP.
  - Animated PNG: the PNG rules, after the writer's IHDR (before acTL).
  - GIF: Comment and XMP extensions after the global color table (the header becomes
    GIF89a), also for animated GIF.
  - JPEG XL and AVIF: no injection; the writer gets EXIF and XMP from `save_metadata_for` and emits
    them itself (JPEG XL as uncompressed `Exif` and `xml ` boxes, also animated; docs/jxl.md, docs/avif.md).
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
  the fields above reach a PDF), JFIF thumbnails. Only PSD/PSB, JPEG, PNG, TIFF, WebP, GIF,
  JPEG XL, AVIF and PDF saves write File Info metadata.

## Limitations

- MakerNotes are copied verbatim inside the rebuilt EXIF block; makers that use absolute
  offsets (Canon among them) can end up pointing at the wrong bytes, as with most writers
  other than ExifTool.
- A PSD written by the core writer without `encode_thumbnail` (recovery snapshots,
  scripts calling it directly) keeps the source thumbnails, which can show the pre-edit
  image.

## Display

- The privacy notice above the pages lists the privacy classes present (timestamps
  excepted). Its Don't Show Again button (`fileInfoHidePrivacyNoticeButton`, #417) turns it
  off for every document; Preferences > Metadata and Watermarks
  (`preferencesFileInfoPrivacyNoticeCheck`) turns it back on. Settings key, permanent:
  `fileInfo/showPrivacyNotice` (true by default). A pixel watermark found by Scan for
  Watermarks still shows there.
- All Metadata lists every row from `describe_file_info` grouped by container, with a
  privacy column. Group, name and value text there are specification identifiers and
  container descriptions shown untranslated, as ExifTool does (docs/localization.md); the
  curated pages translate their labels.
- Dates are shown readably, never rewritten (#500): EXIF DateTime, DateTimeOriginal,
  DateTimeDigitized and GPSDateStamp as `2026-10-27 13:18:05` / `2026-10-27`
  (`exif_date_display`, also the Camera page), the IPTC-IIM date datasets (1:70, 2:30, 2:37,
  2:55, 2:62) as `2026-10-27` and the time datasets (1:80, 2:35, 2:38, 2:60, 2:63) as
  `13:18:05+02:00` (`iim_display_text`). A value that does not match the specification's
  pattern is shown as stored. These rows are read-only; the editable Date Created field is
  XMP photoshop:DateCreated, converted to IIM by `apply_fields` as before.
- Time zones: an XMP date whose zone is hour-only (`+03`, as GIMP writes xmpMM:History
  `stEvt:when`) or basic (`+0300`) shows as `+03:00` in All Metadata, the History page and the
  Created/Modified labels (`xmp_date_display`), and EXIF OffsetTime* `+03` as `+03:00`
  (`exif_offset_display`). `Z` and `+03:00` stay as they are; stored values never change.
- GPS placeholders: when the EXIF GPS IFD has no GPSLatitude and no GPSLongitude, All Metadata
  leaves out GPSVersionID and a zero GPSAltitude with its GPSAltitudeRef (GIMP writes
  GPSAltitude 0/100 into every file); other GPS tags, a nonzero altitude included, still show.
  With nothing left the EXIF GPS group and the location privacy class disappear. The block is
  still saved as it is; Remove Location still drops it.
- Raw XMP and Copy XMP show `xmp_display_text`: the packet without its padding (whitespace and
  NULs at the end and before the closing `<?xpacket end=...?>`, which stays on its own line).
  The packet in the document and in saved files keeps its padding.
- Privacy classes (`MetadataPrivacy`, string ids from `privacy_class_id`, permanent): none,
  location, deviceIdentity, personal, hiddenImage, editHistory, machineInfo, timestamp,
  aiPrompt, provenance, watermark. The banner lists every class present except timestamps.
- AI Generation reads Automatic1111/Forge `parameters` (also from EXIF UserComment),
  ComfyUI `prompt`/`workflow`, InvokeAI, NovelAI, Fooocus, IPTC DigitalSourceType and
  C2PA actions. The page shows only for a generator record or a DigitalSourceType naming a
  trained model (`is_ai_digital_source_type`: trainedAlgorithmicMedia,
  compositeWithTrainedAlgorithmicMedia). Any other source type (digitalCapture, digitalArt, ...)
  shows as "Digital source type" on the History and Provenance page with no privacy class.

## Tests

Core: `metadata_*` in `tests/core/file_info_metadata_tests.cpp` (every container from
self-built bytes, deterministic mutation pass, Unicode paths, PSD round trip, edit, strip,
save rules, injection round trips, the save stamp and `metadata_psd_save_writes_a_fresh_thumbnail`) and `watermark_*` in
`tests/core/invisible_watermark_tests.cpp`. UI: `ui_file_info_*`, `ui_psd_save_*` and `ui_jpeg_save_writes_an_exif_thumbnail` in
`tests/ui/file_info_tests.cpp`;
scripting: `ui_script_file_info_*`, `ui_script_scan_for_watermarks`. Saved metadata preference:
`ui_save_metadata_*` in `tests/ui/save_metadata_tests.cpp`. Data Mining: `metadata_data_mining_round_trips`,
`ui_file_info_data_mining_field`, `ui_save_metadata_data_mining_default`, `ui_script_file_info_data_mining`.
