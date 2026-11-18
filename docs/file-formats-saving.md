# Saving: the flat-format save guard, writer helpers, save progress

Split from [file-formats.md](file-formats.md): what Save and Save As do when a layered document meets a flat format, the shared flatten and export-transform helpers, and the Saving progress dialog.

## Layered documents and flat formats (the Photoshop-style save guard)

A document opened from a flat format that has grown structure a flat save would discard must never silently flatten back over its file:

- `flat_save_discards_layers(document)` covers multiple layers, groups, adjustments, text, smart objects, shapes, authored raster or vector masks, layer styles, and non-identity Blend If. SVG asks its writer which features would bake ([svg.md](svg.md)). A single pixel layer with only the document-alpha raster mask can save its mask in the flat file's alpha plane.
- PSD/PSB, Aseprite and ORA have layered writers. ICO and CUR preserve one plain origin-aligned square pixel layer per size, named "WxH". With additional artwork the icon writer renders the visible composite for all sizes. TIFF keeps editable layers by default ([tiff.md](tiff.md)).
- Ordinary Save on an imported multi-page TIFF or multi-frame Aseprite file opens Save As with a PSD filename. The session keeps the source path for Reopen. Recovery omits that source path so a recovered partial import also asks for a destination. Explicitly choosing the original filename in Save As, including a script's `doc.saveAs(doc.path)`, replaces the original with the imported document.
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
- PDF: reports each page through `report_save_progress` (`ScopedSaveProgress`). PDF, editable pages included, writes on the worker too (Qt paints text off the GUI thread). Every piece of font state the worker reaches is locked: the caches and once-per-run registration sets in `text_font_service.cpp`, and the name-table index in `font_face_name_index.cpp`, which `fontDatabaseChanged` resets by bumping a generation so the GUI-thread slot never waits on a scanning worker (`ui_font_caches_stay_consistent_across_threads_while_the_database_changes`).

Scripted and unattended saves never show the dialog. Test: `ui_save_shows_progress_with_artboard_names`.
