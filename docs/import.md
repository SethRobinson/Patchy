# Import menu: scanner, sprite sheets, tile preview

The File > Import features. File-format readers live in [file-formats.md](file-formats.md); the platform-specific scanner backends are also inventoried in [platform.md](platform.md).

## Scanner import

- Windows uses WIA (`scanner_import_win.cpp`); macOS uses ImageKit/ImageCaptureCore (`scanner_import_mac.mm`). macOS exposes scanners only (no cameras) and acquisition is single-image.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition for offscreen scanner-import tests.
- The macOS AppKit sheet MUST complete asynchronously after returning to the native run loop: a nested `QEventLoop` makes every sheet control ignore mouse input.

## Sprite sheets

Import/export are the testable Qt-free pair `compose_sprite_sheet`/`slice_sprite_sheet`.

## Seamless Tile Preview

A Qt::Tool window polling a CONST-only revision probe (mutable Layer accessors bump revisions — see the AGENTS.md gotcha "Reads must not bump layer revisions") and closing via a `done(int)` override (see the chrome-dialog gotcha; `ui_tile_preview_window_tracks_document_edits` pins it).
