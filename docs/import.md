# Import menu: scanner, sprite sheets, image sequences; seamless tiling

The File > Import features plus the seamless-tiling tooling (preview window, in-canvas tiling mode, seam shifting). File-format readers live in [file-formats.md](file-formats.md); the platform-specific scanner backends are also inventoried in [platform.md](platform.md).

## Scanner import

- Windows uses WIA (`scanner_import_win.cpp`); macOS uses ImageKit/ImageCaptureCore (`scanner_import_mac.mm`). macOS exposes scanners only (no cameras) and acquisition is single-image.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition for offscreen scanner-import tests.
- The macOS AppKit sheet MUST complete asynchronously after returning to the native run loop: a nested `QEventLoop` makes every sheet control ignore mouse input.

## Sprite sheets

Import/export are the testable Qt-free pair `compose_sprite_sheet`/`slice_sprite_sheet`.

## Image sequences

The sibling of the sprite-sheet pair: File > Import > Image Sequence to Layers imports one file per layer, and File > Export Layers as Image Sequence writes one file per top-level layer (bottom to top, matching the sprite-sheet frame semantics; the options dialog's scope radios pick visible-only, the default, or all layers). Testable helpers live in `src/ui/image_sequence_dialog.{hpp,cpp}`; the file-dialog flows are `MainWindow::import_image_sequence`/`export_image_sequence` in `main_window_files.cpp`.

- Ordering is a natural sort (numeric-aware, case-insensitive: crap2.bmp before crap10.bmp) via `sorted_sequence_paths`. Selecting a single file whose base name ends in digits expands to the whole sibling run (`expand_numbered_sequence`: same prefix + digits + extension); the import confirmation dialog lists the ordered files so the expansion is visible before anything imports.
- Mixed sizes: canvas is the max width x max height and smaller frames top-left align (registration stays at the (0,0) origin). Layer names come from file base names; only the first (bottom) layer starts visible, like sprite-sheet slicing.
- Export naming: the save dialog picks the first file (trailing digits in the typed name set prefix/start/padding via `naming_from_save_base_name`), then the options dialog chooses Numbered vs Layer names (sanitized + case-insensitively deduped, `image_sequence_file_names`) with a live name preview. Files besides the one the save dialog confirmed get one overwrite prompt. Each frame renders through the shared `render_layer_isolated` (image_document_io, also used by the sprite-sheet composer) and routes through `write_flat_image_file` with the format's usual save options.

## Seamless tiling: preview window, in-canvas mode, seam shifting

Three cooperating features for tile art (July 2026 expansion):

- **Seamless Tile Preview** (View menu): a Qt::Tool window polling a CONST-only revision probe (mutable Layer accessors bump revisions; see [performance.md](performance.md)) and closing via a `done(int)` override (see [ui-conventions.md](ui-conventions.md); `ui_tile_preview_window_tracks_document_edits` pins it). The tick has three content bands: probe changes re-render immediately at or below 1 Mpx, after the probe holds still for one tick up to 16 Mpx (so brush drags are not recomposited per tick), and via the manual Refresh button beyond that. A document IDENTITY change (tab switch/open/close; separate pointer+dims probe) re-renders immediately at any size — `tick()` must never early-return before probing, that was the bug that froze the window on >1 Mpx documents (`ui_tile_preview_follows_document_switches_and_large_edits` pins it).
- **Seamless Tiling in Window** (View menu, per-document check synced in `activate_document_tab`): CanvasWidget paints wrap copies of the committed composite around the document — `draw_tiling_preview` in canvas_widget_render.cpp draws from `render_cache_`/display mips (session previews and all overlays stay on the center tile), with a textured-fill fallback above ~24 visible ghost tiles from a pixmap cache cleared in `invalidate_display_mip_cache()`. Partial updates in `document_changed_impl` replicate dirty rects across visible wrap offsets (or widen to a full update on the textured path); `ui_canvas_tiling_mode_paints_ghost_tiles_live` pins ghost pixels, live stroke replication (via PaintRegionRecorder), and the per-tab check sync. The action handler is on `triggered`, not `toggled`, so the tab-switch setChecked sync cannot re-apply it.
- **Shift Seams to Center** (Image menu + a button in the preview window): `wrap_offset_document` (core/document_geometry.cpp) wrap-rolls raster layers (normalized to the canvas rect first), layer masks (expanded over default_color), and document channels; object-like layers (text, shape layers, placed records) translate whole without wrapping and shape layers re-raster via the `transform_document_vector_data` translate pass. The applied offset is recorded in document metadata (`kTileSeamOffsetMetadataKey` in tile_preview_window.hpp, "dx,dy") so the second press applies the exact inverse even for odd dimensions and undo/redo (whole-Document snapshots) can never desync the parity. The MainWindow toggle follows the rotate-canvas template including the smart-object guard. Rolling only permutes pixels, so palette-mode docs need no snap.
