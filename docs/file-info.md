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
    descriptive tags and new Exif/GPS IFDs, re-encoded to the file's byte order. An IRB
    already there (a layered TIFF's, [tiff.md](tiff.md)) keeps its other resources.
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

### Save stamp

A PSD/PSB Save or Save As (`DocumentSaveCoordinator::write_prepared`, also scripts and
`--export`) records itself in the session's XMP and EXIF before the writer runs, through
`stamp_file_info_for_save` with `save_stamp_now()` (local time and offset, `"Patchy " +
PATCHY_VERSION`, a fresh `QUuid`): xmp:ModifyDate and xmp:MetadataDate become the save
time; xmp:CreateDate and xmp:CreatorTool are set only when absent; xmpMM:History gains a
stEvt entry ("created" with no prior CreateDate or History, else "saved" with changed "/")
whose instanceID `xmp.iid:<guid>` is also the new xmpMM:InstanceID; the "created" save
also sets xmpMM:DocumentID and OriginalDocumentID to `xmp.did:<guid>` when absent. An EXIF
block the document already has gets IFD0 Software (the stamp's software) and DateTime
(local `YYYY:MM:DD HH:MM:SS`); a document without EXIF gets none. That is Photoshop's own
behavior: a file another application created keeps its creator (a Photoshop 23.4 save of
pinball_from_photoshop.psd left CreatorTool at "Adobe Photoshop 21.1" and added a history
entry), while a Photoshop 27.11 save rewrites EXIF Software and DateTime even in a file
another application created and gives every History entry the instanceID that
xmpMM:InstanceID repeats. Dates are ISO 8601 with the local offset (`xmp_date`). An
unparseable packet is left alone (the EXIF is still stamped). Recovery snapshots, flat
formats and layered TIFF are not stamped; the core PSD writer is untouched, so the
canaries hold. After Strip All Metadata the next PSD save writes the XMP stamp again
(dates, creator and ids only, no EXIF).
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
  aiPrompt, provenance, watermark. The banner lists every class present except timestamps.
- AI Generation reads Automatic1111/Forge `parameters` (also from EXIF UserComment),
  ComfyUI `prompt`/`workflow`, InvokeAI, NovelAI, Fooocus, IPTC DigitalSourceType and
  C2PA actions.

## Watermarks

Detection only: Patchy never embeds, removes or masks a watermark (docs/legal-constraints.md,
"Watermark detection").

- Declared in metadata: a C2PA `c2pa.soft-binding` assertion (its `alg`, such as
  `com.adobe.trustmark.Q`) or an action named `c2pa.watermarked*` (`summarize_jumbf`) adds the
  observed item "Watermark declared in Content Credentials", privacy `watermark`. IPTC
  DigitalSourceType says how an image was made, not that it carries a mark, and stays aiPrompt.
- Pixel scan: the Watermarks page's Scan for Watermarks (`fileInfoScanWatermarksButton`,
  result `fileInfoWatermarkScanResult`) and `doc.scanForWatermarks()`. It runs only on that
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
  decoder (Google SynthID); the page says so. Adobe TrustMark needs a downloaded ONNX model
  and is not included.

## Tests

Core: `metadata_*` in `tests/core/file_info_metadata_tests.cpp` (every container from
self-built bytes, deterministic mutation pass, Unicode paths, PSD round trip, edit, strip,
save rules, injection round trips) and `watermark_*` in
`tests/core/invisible_watermark_tests.cpp`. UI: `ui_file_info_*` in `tests/ui/file_info_tests.cpp`;
scripting: `ui_script_file_info_*`, `ui_script_scan_for_watermarks`.
