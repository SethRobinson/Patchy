# File Info: save stamp and saved metadata

Split from [file-info.md](file-info.md): the save stamp PSD, PSB and JPEG saves record in XMP and EXIF, the fresh thumbnails PSD and JPEG saves write, and the Saved metadata preference (None, Basic, Personal).

## Save stamp

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
EXIF. Recovery snapshots, File > Export > Flat Image, flat formats other than JPEG and layered TIFF
are not stamped; the core writers never stamp, so the canaries hold.

## Thumbnails

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
  Strip All Metadata, Export > Flat Image with Metadata None) gets none. `ImageSaveOptions::jpeg_exif_thumbnail`
  (default true; scripts' `thumbnail`) turns it off: File > Export for Web writes it only when
  "Include thumbnail" is on ([web-export.md](web-export.md)). That checkbox and a script's explicit
  `thumbnail: true` also set `jpeg_exif_thumbnail_without_metadata`: a JPEG whose metadata choice
  writes no EXIF (None) then gets one APP1 Exif holding only IFD0 X/YResolution and
  ResolutionUnit and the IFD1 thumbnail (`thumbnail_only_exif`), no XMP, IPTC or other tags.
  Without it None writes no APP1, as before; Save, Save As and Export never set it.

## Saved metadata preference

Preferences > Metadata and Watermarks, "Saved metadata" (`ui/save_metadata_settings.*`,
`main_window_save_metadata.cpp`). Settings keys, permanent: `saveMetadata/autoAdd` (`none`,
`basic` default, `personal`; also the scripting tokens), `saveMetadata/authorName`,
`saveMetadata/copyrightNotice`, `saveMetadata/contactEmail`, `saveMetadata/contactWebsite`,
`saveMetadata/askBeforeAdding` (true), `saveMetadata/dataMining` (a PLUS Data Mining URI, empty =
Not set, the default; combo `preferencesSavedMetadataDataMiningCombo`, #658).

- None: no save stamp (`write_prepared(..., stamp_save = false)`) and nothing personal. What the
  file already carries is kept; Strip All Metadata removes it.
- Basic: the save stamp above for PSD, PSB and JPEG Save/Save As. JPEG was added October
  25, 2026 (Seth), reversing the October 23 rule for JPEG only: the other flat formats stay
  unstamped, so a document without metadata still saves them byte-identically (the
  injection rule in [file-info.md](file-info.md#writing)), and File > Export > Flat Image follows only its Metadata combo.
- Personal: Basic plus `add_personal_save_metadata` before a save to a format that carries
  metadata (PSD, PSB, JPEG, PNG, TIFF, WebP, GIF, JPEG XL, AVIF, PDF author): the name goes into
  `dc:creator` beside any creator already there (never replacing one), the copyright notice into
  `dc:rights` with status Copyrighted, email and website into `Iptc4xmpCore:CreatorContactInfo`,
  the data mining choice into `plus:DataMining` ([file-info.md](file-info.md#data-mining)),
  each only where empty, through `apply_fields` (so EXIF Artist/Copyright and IIM By-line/2:116
  follow only where those blocks exist). It is a save-time File Info change like the stamp, no
  undo step. With asking on, `savePersonalMetadataPrompt` (Add, Skip, "Always add without
  asking", which turns asking off) shows the name, copyright and any creator already named (and the
  data mining choice when the file has none). No prompt and no change when neither the name (empty,
  or the file already names the user, case-insensitive) nor the data mining choice (none chosen, or
  the file already has a value) is to be added, the format carries no metadata, or Skip was chosen for this document
  (`DocumentSession::personal_save_metadata_skipped`, until it closes). Scripted and unattended
  saves never ask: with asking on they add nothing, with it off they add. Exports never add
  personal info (they follow the Export dialog's Metadata combo, None included), and recovery
  snapshots never pass through this path.
