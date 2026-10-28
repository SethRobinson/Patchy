# File Info: image metadata

File > File Info (Ctrl+Alt+Shift+I, hotkey id `file.file_info`) shows every metadata item
Patchy can read from the document's source file, edits the Photoshop-style Description and
IPTC fields, and stages Strip All Metadata, Remove Location and Remove Edit History. File >
Strip All Metadata (`file.strip_metadata`, no default key) strips without opening the dialog.
Both are undoable (`push_undo_snapshot`) and change the file only on the next save.
File > Scan for Watermarks... (`file.scan_for_watermarks`, no default key; also on the
document tab's menu) opens File Info on the Watermarks page with the scan run.

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
  in the file's color space (Qt's PNG/JPEG/TIFF/WebP/GIF readers, the BMP reader, the JPEG XL
  reader for 8 and 16-bit RGB). WIC
  HEIF/JPEG XR, LibRaw and the HEIF Qt fallback convert to sRGB, so their profile is
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
  shown only for JPEG, PNG, TIFF, WebP, GIF and JPEG XL. `reset_export_options` puts every prompt
  back to All. Scripts pass `metadata: "all" | "copyright" | "basic" | "none"` to `saveAs`/`exportAs`.
  File > Export for Web has its own Metadata combo (`exportForWeb/metadata`, default `none`).
  `MetadataPolicy` is append-only (Basic was added last).
- Basic (October 27, 2026) means what Preferences "Saved metadata: Basic" means: the save stamp
  below and nothing else. `prepare_save_metadata` stamps an empty File Info for the file being
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
  `xmp:Thumbnails` (stale after edits; JPEG writes a fresh IFD1, "Thumbnails" below). `CopyrightAndContact` keeps EXIF Artist/Copyright,
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
    descriptive tags and new Exif/GPS IFDs, re-encoded to the file's byte order. An IRB
    already there (a layered TIFF's, [tiff.md](tiff.md)) keeps its other resources.
  - WebP: libwebpmux chunks, also for animated WebP.
  - Animated PNG: the PNG rules, after the writer's IHDR (before acTL).
  - GIF: Comment and XMP extensions after the global color table (the header becomes
    GIF89a), also for animated GIF.
  - JPEG XL: no injection; the writer gets EXIF and XMP from `save_metadata_for` and emits
    uncompressed `Exif` and `xml ` boxes itself, also for animated JPEG XL (docs/jxl.md).
- PSD: `image_resources_for_document` writes the lifted resources (XMP still goes through
  `repair_xmp_date_timezones`) plus resource 4213 (`'PtcM'`, v1, big-endian: u32 count,
  then per entry u8 FileInfoTextKind and key/value/language as u32 length + UTF-8) for PNG
  text and comments. Patchy-born documents carry none, so the byte-stability canaries are
  unchanged.

### Save stamp

A PSD/PSB or JPEG Save or Save As (`DocumentSaveCoordinator::write_prepared`, also scripts'
`saveAs` and `--export`; JPEG since October 25, 2026, not a script's `exportAs` copy) records itself in the session's XMP and EXIF before the writer runs, through
`stamp_file_info_for_save` with `save_stamp_now()` (local time and offset, `"Patchy " +
PATCHY_VERSION`, a fresh `QUuid`): xmp:ModifyDate and xmp:MetadataDate become the save
time; xmp:CreateDate and xmp:CreatorTool are set only when absent; xmpMM:History gains a
stEvt entry ("created" with no prior CreateDate or History, else "saved" with changed "/")
whose instanceID `xmp.iid:<guid>` is also the new xmpMM:InstanceID; the "created" save
also sets xmpMM:DocumentID and OriginalDocumentID to `xmp.did:<guid>` when absent. An EXIF
block gets IFD0 Software (the stamp's software) and DateTime (local `YYYY:MM:DD HH:MM:SS`).
That is Photoshop's own behavior: a file another application created keeps its creator (a
Photoshop 23.4 save of pinball_from_photoshop.psd left CreatorTool at "Adobe Photoshop
21.1" and added a history entry), while a Photoshop 27.11 save rewrites EXIF Software and
DateTime even in a file another application created and gives every History entry the
instanceID that xmpMM:InstanceID repeats. Dates are ISO 8601 with the local offset
(`xmp_date`). An unparseable packet is left alone (the EXIF is still stamped).

The coordinator also passes `SaveStampDocument` (#418, `stamp_document_for_save`), which
describes the file as Photoshop's own new documents do: XMP dc:format
(`application/vnd.adobe.photoshop`, or `image/jpeg`) and photoshop:ColorMode (the PSD
header mode: `layered_save_color_mode` for PSD, 3 or 4 for a CMYK JPEG) on every save;
EXIF PixelX/YDimension and X/YResolution (from print settings, unit inches) where the block
has them, and no IFD1 thumbnail (it shows an older image). A document without EXIF gets a
new big-endian block: IFD0 Orientation 1, X/YResolution, ResolutionUnit, Software and
DateTime; Exif IFD ExifVersion 0231, ColorSpace (1 for an embedded sRGB profile, else
65535 "uncalibrated", as Photoshop writes untagged files) and PixelX/YDimension. Photoshop
also writes an IFD1 with a zero-length thumbnail pointer; Patchy leaves IFD1 out. After
Strip All Metadata (`DocumentFileInfo::stripped`, set by `strip_file_info`, never saved)
the next save writes the XMP stamp again (dates, creator, format, mode and ids) but no
EXIF. Recovery snapshots, File > Export As, flat formats other than JPEG and layered TIFF
are not stamped; the core writers never stamp, so the canaries hold.

### Thumbnails

- PSD/PSB saves through `write_prepared` set `psd::WriteOptions::encode_thumbnail` (#419):
  the writer reduces the composite it already made (`psd::thumbnail_pixels`: integer box
  average to fit 160 pixels, Photoshop's limit, over white where the merged transparency is
  below 255) and the UI encodes it with Qt's JPEG at quality 75 (`encode_thumbnail_jpeg`).
  Resource 1036 is `thumbnail_resource`: format 1 (kJpegRGB), width, height, padded row
  bytes, total size, JFIF size, 24 bits, 1 plane, then RGB JFIF. Resource 1033 (Photoshop
  4, BGR) and any earlier 1036 are never written then. The thumbnail is the image as saved,
  so it is written in every Saved metadata mode and after Strip All Metadata.
- Without the encoder (recovery snapshots, scripts and tests calling the core writer, the
  byte canaries) the lifted 1033/1036 pass through unchanged.
- JPEG: when a save writes EXIF, `add_jpeg_exif_thumbnail` adds IFD1 (Compression 6,
  72 ppi) with a thumbnail of the saved image, as Photoshop does; it is left out when the
  block would no longer fit one APP1 segment. With the JPEG save stamp a new document's
  Save As has EXIF, so it gets one too; a save without EXIF (Saved metadata None, after
  Strip All Metadata, Export As with Metadata None) gets none. `ImageSaveOptions::jpeg_exif_thumbnail`
  (default true; scripts' `thumbnail`) turns it off: File > Export for Web writes it only when
  "Include thumbnail" is on ([web-export.md](web-export.md)). That checkbox and a script's explicit
  `thumbnail: true` also set `jpeg_exif_thumbnail_without_metadata`: a JPEG whose metadata choice
  writes no EXIF (None) then gets one APP1 Exif holding only IFD0 X/YResolution and
  ResolutionUnit and the IFD1 thumbnail (`thumbnail_only_exif`), no XMP, IPTC or other tags.
  Without it None writes no APP1, as before; Save, Save As and Export never set it.
### Saved metadata preference

Preferences > Metadata and Watermarks, "Saved metadata" (`ui/save_metadata_settings.*`,
`main_window_save_metadata.cpp`). Settings keys, permanent: `saveMetadata/autoAdd` (`none`,
`basic` default, `personal`; also the scripting tokens), `saveMetadata/authorName`,
`saveMetadata/copyrightNotice`, `saveMetadata/contactEmail`, `saveMetadata/contactWebsite`,
`saveMetadata/askBeforeAdding` (true).

- None: no save stamp (`write_prepared(..., stamp_save = false)`) and nothing personal. What the
  file already carries is kept; Strip All Metadata removes it.
- Basic: the save stamp above for PSD, PSB and JPEG Save/Save As. JPEG was added October
  25, 2026 (Seth), reversing the October 23 rule for JPEG only: the other flat formats stay
  unstamped, so a document without metadata still saves them byte-identically (the
  injection rule above), and File > Export As follows only its Metadata combo.
- Personal: Basic plus `add_personal_save_metadata` before a save to a format that carries
  metadata (PSD, PSB, JPEG, PNG, TIFF, WebP, GIF, JPEG XL, AVIF, PDF author): the name goes into
  `dc:creator` beside any creator already there (never replacing one), the copyright notice into
  `dc:rights` with status Copyrighted, email and website into `Iptc4xmpCore:CreatorContactInfo`,
  each only where empty, through `apply_fields` (so EXIF Artist/Copyright and IIM By-line/2:116
  follow only where those blocks exist). It is a save-time File Info change like the stamp, no
  undo step. With asking on, `savePersonalMetadataPrompt` (Add, Skip, "Always add without
  asking", which turns asking off) shows the name, copyright and any creator already named. No
  prompt and no change when the file already names the user (case-insensitive), the name is
  empty, the format carries no metadata, or Skip was chosen for this document
  (`DocumentSession::personal_save_metadata_skipped`, until it closes). Scripted and unattended
  saves never ask: with asking on they add nothing, with it off they add. Exports never add
  personal info (they follow the Export dialog's Metadata combo, None included), and recovery
  snapshots never pass through this path.

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
- Raw XMP and Copy XMP show `xmp_display_text`: the packet without its padding (whitespace and
  NULs at the end and before the closing `<?xpacket end=...?>`, which stays on its own line).
  The packet in the document and in saved files keeps its padding.
- Privacy classes (`MetadataPrivacy`, string ids from `privacy_class_id`, permanent): none,
  location, deviceIdentity, personal, hiddenImage, editHistory, machineInfo, timestamp,
  aiPrompt, provenance, watermark. The banner lists every class present except timestamps.
- AI Generation reads Automatic1111/Forge `parameters` (also from EXIF UserComment),
  ComfyUI `prompt`/`workflow`, InvokeAI, NovelAI, Fooocus, IPTC DigitalSourceType and
  C2PA actions.

## Watermarks

Patchy never removes or masks a watermark (docs/legal-constraints.md, "Watermark detection").
Adobe TrustMark, read by the same Scan for Watermarks once its models are downloaded and added
by Image > Add Invisible Watermark, is in [watermarks.md](watermarks.md).

- Declared in metadata: a C2PA `c2pa.soft-binding` assertion (its `alg`, such as
  `com.adobe.trustmark.Q`) or an action named `c2pa.watermarked*` (`summarize_jumbf`) adds the
  observed item "Watermark declared in Content Credentials", privacy `watermark`. IPTC
  DigitalSourceType says how an image was made, not that it carries a mark, and stays aiPrompt.
- Pixel scan: the Watermarks page's Scan for Watermarks (`fileInfoScanWatermarksButton`,
  per-kind results in [watermarks.md](watermarks.md#scan-results)) and `doc.scanForWatermarks()`. It runs only on that
  request, never at open or per repaint: `Compositor::flatten_rgb8` of the document as it is
  now, then `scan_invisible_watermarks` (`src/core/invisible_watermark.cpp`). A hit adds a
  "Pixel scan" / "Invisible watermark" row (privacy watermark) to All Metadata and a banner
  sentence saying stripping metadata does not remove it. Nothing is stored or saved.
- Known payloads (ids permanent), bits MSB first as `numpy.unpackbits`: `stableDiffusion1`, the
  17 bytes "StableDiffusionV1" (CompVis/stable-diffusion `scripts/txt2img.py`, `img2img.py`);
  `stableDiffusion2`, "SDV2" (Stability-AI/stablediffusion scripts); `stableDiffusionXL`, the
  48-bit 0xB3EC907BB19E (Stability-AI/generative-models `WATERMARK_MESSAGE`, also diffusers'
  `StableDiffusionXLWatermarker`).
- Decoder: a Patchy-owned reimplementation of the decode side of ShieldMnt's
  invisible-watermark (MIT; `imwatermark/maxDct.py` `EmbedMaxDct.decode`, `watermark.py`, read
  October 2026); no code copied. OpenCV's 8-bit BGR-to-YUV in fixed point (Y = (1868 B + 9617 G
  + 4899 R + 2^13) >> 14, U = ((B - Y) 8061 + 128 * 2^14 + 2^13) >> 14), the level-1 Haar
  approximation of U over the top-left (h/4*4) x (w/4*4) pixels, then per 4 x 4 block of
  coefficients (8 x 8 pixels, row-major) the largest of the 15 non-DC coefficients (first on
  ties): bit 1 when its value mod 36 exceeds 18. Block n votes for bit n mod L; a bit is 1
  when ones * 255 > votes * 127, as the library's `mean * 255 > 127`. A coefficient is half a
  2 x 2 sum of U (never negative), so the test is `sum % 72 > 36` in integers: exact and the
  same on every toolchain. Only U is read: the library's scales are [0, 36, 36] but it loops
  `range(2)`. Images under 256 x 256 pixels (the library's minimum) are not scanned.
- Eight readings per payload (`DwtDctDecodeOptions`): both channel orders (callers that hand
  the library RGB as BGR); U measured from 0 (the SD scripts embed in 8 bits, U centered on
  128) or from OpenCV's float chroma zero, |sum - 510| (SDXL embeds in float BGR, where U is
  centered on 0.5 and coefficients can be negative, so an 8-bit reading flips those blocks);
  and two bit rules. The library's `mean * 255 > 127` misreads many 0 bits: a marked 1 reads 1
  in nearly every block, but setting a 0 can lower the block's largest coefficient below
  another one, which then reads 1 about half the time. Stability's own SDXL `detect.py` allows
  for this and calls 36 or more of 48 matching bits "very likely watermarked". The second rule
  (`TwoCluster`) puts the threshold where the per-bit averages split into two groups (largest
  between-group variance, exact integers). On the test images the library rule leaves 4 to 36
  bits wrong and the two-group rule none.
- Match rule: a payload is found when any reading has at most L/12 bits (rounded down)
  different: 11 of 136, 4 of 48, 2 of 32. On an unmarked image the decoded word is close to
  random, so it lands that close to a given payload with odds of about 1.2e-7 (32 bits),
  7.6e-10 (48) and far less (136) per reading; over the eight readings and three payloads the
  worst case stays near 1e-6 (the 32-bit SD 2.x payload). The core tests check photo-like,
  flat, gradient, checkerboard, noise and other-payload images. The embedding side exists only
  as a test reference (`tests/invisible_watermark_reference.hpp`) of both pixel paths.
- Limits: the 8-pixel block grid must line up with the original, so a crop off that grid, a
  resize, rotation or heavy repainting removes the mark. JPEG recompression does too: the mark
  lives in fine chroma detail, and in `ui_file_info_scans_for_watermarks` a marked SDXL image
  still decodes exactly after Qt's JPEG at quality 100 but keeps 6 of 48 bits wrong at 95 and
  19 at 90 (not found). Many watermarks have no public
  decoder (Google SynthID, Digimarc); the page says so under the results.

## Tests

Core: `metadata_*` in `tests/core/file_info_metadata_tests.cpp` (every container from
self-built bytes, deterministic mutation pass, Unicode paths, PSD round trip, edit, strip,
save rules, injection round trips, the save stamp and `metadata_psd_save_writes_a_fresh_thumbnail`) and `watermark_*` in
`tests/core/invisible_watermark_tests.cpp`. UI: `ui_file_info_*`, `ui_psd_save_*` and `ui_jpeg_save_writes_an_exif_thumbnail` in
`tests/ui/file_info_tests.cpp`;
scripting: `ui_script_file_info_*`, `ui_script_scan_for_watermarks`. Saved metadata preference:
`ui_save_metadata_*` in `tests/ui/save_metadata_tests.cpp`.
