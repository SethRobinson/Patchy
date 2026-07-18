# Import menu: scanner, sprite sheets, image sequences, tile preview

The File > Import features. File-format readers live in [file-formats.md](file-formats.md); the platform-specific scanner backends are also inventoried in [platform.md](platform.md).

## Scanner import

- Windows uses WIA (`scanner_import_win.cpp`); macOS uses ImageKit/ImageCaptureCore (`scanner_import_mac.mm`). macOS exposes scanners only (no cameras) and acquisition is single-image.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition for offscreen scanner-import tests.
- The macOS AppKit sheet MUST complete asynchronously after returning to the native run loop: a nested `QEventLoop` makes every sheet control ignore mouse input.

## Sprite sheets

Import/export are the testable Qt-free pair `compose_sprite_sheet`/`slice_sprite_sheet`.

## Image sequences

The sibling of the sprite-sheet pair: File > Import > Image Sequence to Layers imports one file per layer, and File > Export Layers as Image Sequence writes one file per visible top-level layer (bottom to top, matching the sprite-sheet frame semantics). Testable helpers live in `src/ui/image_sequence_dialog.{hpp,cpp}`; the file-dialog flows are `MainWindow::import_image_sequence`/`export_image_sequence` in `main_window_files.cpp`.

- Ordering is a natural sort (numeric-aware, case-insensitive: crap2.bmp before crap10.bmp) via `sorted_sequence_paths`. Selecting a single file whose base name ends in digits expands to the whole sibling run (`expand_numbered_sequence`: same prefix + digits + extension); the import confirmation dialog lists the ordered files so the expansion is visible before anything imports.
- Mixed sizes: canvas is the max width x max height and smaller frames top-left align (registration stays at the (0,0) origin). Layer names come from file base names; only the first (bottom) layer starts visible, like sprite-sheet slicing.
- Export naming: the save dialog picks the first file (trailing digits in the typed name set prefix/start/padding via `naming_from_save_base_name`), then the options dialog chooses Numbered vs Layer names (sanitized + case-insensitively deduped, `image_sequence_file_names`) with a live name preview. Files besides the one the save dialog confirmed get one overwrite prompt. Each frame renders through the shared `render_layer_isolated` (image_document_io, also used by the sprite-sheet composer) and routes through `write_flat_image_file` with the format's usual save options.

## Seamless Tile Preview

A Qt::Tool window polling a CONST-only revision probe (mutable Layer accessors bump revisions — see the AGENTS.md gotcha "Reads must not bump layer revisions") and closing via a `done(int)` override (see the chrome-dialog gotcha; `ui_tile_preview_window_tracks_document_edits` pins it).
