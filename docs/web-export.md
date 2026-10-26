# Export for Web

File > Export for Web... (`file.export_for_web`, Ctrl+Alt+Shift+S; action
`fileExportForWebAction`) writes a copy of the document in a web format from a dialog with live,
real encoded previews and sizes. It sits directly under Save As, not in the Export submenu (Seth,
October 26, 2026). Export > Flat Image is unchanged and still covers every format; it gave up the
Ctrl+Alt+Shift+S default when this command arrived (a user binding keeps working). "Save for Web"
is another product's command name, and the layout (a format list with live sizes, a split
divider) is Patchy's own.

Code: `src/ui/web_export.{hpp,cpp}` (settings, preview engine, panes, dialog),
`MainWindow::export_for_web` in `src/ui/main_window_files.cpp` (file name and write), the writer
hooks in `src/ui/image_document_io.cpp`.

## Formats and options

Stage 1 lists JPEG, WebP and PNG (`WebExportFormat`, append-only; GIF and palette PNG with dither
are stage 2, AVIF and JPEG XL stage 3). Each format keeps its own settings.

- JPEG: Quality 1-100 (default 80), Progressive (default on), Optimize coding (optimized Huffman
  tables, default on), Background color for transparent areas (default white; JPEG has no alpha).
- WebP: Quality 0-100 (75), Lossless, and "Animation from visible layers" when the document has
  two or more layers and a visible one (the animated WebP writer, as Flat Image).
- PNG: full color, lossless, transparency kept.
- Image size: Width, Height and Scale with a proportion link; Resample Smooth (the export resize,
  `export_width`/`export_height`) or Pixel art (whole-pixel `export_scale` 1x-16x, the size fields
  then follow the scale); Trim transparent edges. The size is per document, never persisted.
- Metadata (None by default, Copyright and Contact Info, All) and "Include thumbnail" (JPEG only,
  off, enabled only when Metadata is not None; it adds the EXIF IFD1 thumbnail, which is written
  only when the file keeps EXIF). Save and Save As still always write that thumbnail
  (`ImageSaveOptions::jpeg_exif_thumbnail` defaults to true; [file-info.md](file-info.md)).
- Color: an sRGB document (every RGB document after open, [rgb-profiles.md](rgb-profiles.md))
  writes no profile and the row says so. A document that still carries another RGB profile gets
  "Convert to sRGB (from <profile>)", on by default; unchecked, the file keeps that profile.
- 16 and 32-bit documents export 8-bit here (`document_for_8bit_writer` once per dialog).
- Not offered: CMYK and the non-web formats (Export > Flat Image).

`jpeg_progressive` and `jpeg_optimize` default to false in `ImageSaveOptions`, so Save, Save As,
Export Flat Image and the byte canaries keep their bytes; `configure_image_writer` sets the Qt
writer options only when asked for.

## Preview engine

`WebExportPreviewEngine` runs one worker at a time over a job queue (selected format first, then
the others one at a time). `request()` replaces the queue and bumps the generation; a queued job
whose generation is stale when its worker starts is dropped unrun. Each job:

1. Narrows the source to 8 bits once per dialog.
2. Prepares the resized, trimmed, sRGB-converted stand-in (`export_transformed_document`), cached
   by its geometry signature, and its display image (`flat_export_qimage`, ARGB32 premultiplied).
3. Encodes through `encode_flat_image_bytes` (the real `write_flat_image_file` into a private
   temporary folder) with `without_export_transforms` options plus JPEG's background fill, or from
   the whole document for an animated WebP.
4. Decodes the bytes with Qt's reader (the one Patchy opens these formats with).

Results are cached by signature (format, geometry, metadata and the format's options; at most 12),
so equal settings never encode twice and switching back is instant. The dialog shows a result only
when its signature matches the current settings; until then the last image stays up with
"encoding...". Settings changes restart a 200 ms debounce. Save reuses the current result's bytes
(`write_encoded_image_file`, a QSaveFile); while the current settings are still encoding, Save
waits for them. With no current result, `export_for_web` encodes on the save worker.

Panes (`WebExportPane`, a ZoomableImagePreview) draw only cached images: below 100% each image's
smooth reduction is built once per size on a worker (`ScaledImageCache`). Compare modes: Split
(a divider at a fraction of the pane width, original left, result right, dragged with the
mouse), Side by side (a second pane; `ZoomableImagePreview::view_state`/`set_view_state` keep
zoom and pan in step), Result only.

## Settings (permanent identifiers)

`exportForWeb/format` (`jpeg`, `webp`, `png`), `exportForWeb/jpegQuality`, `jpegProgressive`,
`jpegOptimize`, `jpegBackground` (`#rrggbb`), `webpQuality`, `webpLossless`, `webpAnimate`,
`metadata` (`none`, `copyright`, `all`), `thumbnail`, `resample` (`smooth`, `pixelArt`), `trim`,
`convertToSrgb`, `compare` (`split`, `sideBySide`, `result`), `revealWhenDone`, all under
`exportForWeb/`. Saved when the dialog is accepted. Reset returns every option to its first-use
default and keeps the compare mode and reveal choice.

## Scripting

`doc.exportAs(path, options)` and `doc.saveAs` take `progressive`, `optimizeCoding` and
`thumbnail` for `.jpg`/`.jpeg` (strict, like every save option;
[scripting-api-changes.md](scripting-api-changes.md)). `metadata` was already there.

## Tests

UI: `ui_web_export_defaults_and_options`, `ui_web_export_jpeg_progressive_and_optimize_read_back`,
`ui_web_export_engine_drops_stale_requests`, `ui_web_export_dialog_previews_and_saves` (captures
`web_export_jpeg_split` and `web_export_png_side_by_side`), `ui_web_export_metadata_and_thumbnail_choices`,
`ui_unicode_export_for_web_writes_encoded_bytes`, `ui_script_export_as_jpeg_coding_options`, the
dialog sweep, `ui_file_export_menu_actions_registered` and `ui_photoshop_shortcuts_are_registered`.
