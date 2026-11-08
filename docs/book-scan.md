# Scan a Book to PDF

File > Import > Scan a Book to PDF (`fileImportScanBookAction`, `file.import_scan_book_pdf`) turns a bound book into an image-page PDF whose pages all share one size, scanning a spread per key press. File > Import > Resume Book Scan (`fileImportResumeBookScanAction`, `file.import_resume_book_scan`) reopens a project folder. Both are desktop-only menu entries, `QAction::NoRole`. Binding legal boundary: "Book scanning" in [legal-constraints.md](legal-constraints.md#book-scanning) (record: [patent-research-book-scanning.md](patent-research-book-scanning.md)); scanner backends: [import-scanner.md](import-scanner.md).

## Files

- `src/core/book_scan.{hpp,cpp}` (Qt-free): `OrientedBox` geometry, `detect_book_outline` (the Divide Scanned Photos detector, largest region), `detect_gutter_fraction`, `detect_spread`, uniform sizing (`median_page_size`, `largest_page_size`, `cover_page_size`), `uniform_spread_page_box`/`centered_page_box`, `extract_book_page`, `apply_book_output`, the grayscale hint, `book_spread_fits_bed`, `unturned_rect`. Tests: `book_scan_*` (patchy_core_tests).
- `src/ui/scanner_session.{hpp,cpp}` and `scanner_session_win.cpp`: repeated scans with fixed settings ([import-scanner.md](import-scanner.md#scanner-sessions)).
- `src/ui/book_scan_project.{hpp,cpp}`: the project model, `book.json`, page order, page sizing and `render_book_page`; `src/ui/book_pdf_export.{hpp,cpp}`: `write_book_pdf`.
- `src/ui/book_scan_dialog.{hpp,cpp}`: the wizard (`bookScanDialog`, non-modal through `run_non_modal_dialog`) and `BookScanPreview`, its box editor. MainWindow entry points and Open Pages as Documents live in `main_window_files.cpp`.

## Flow

1. Setup: title, author (PDF `/Info`), project folder (default `<Documents>/Patchy Book Scans/<title> <date>`, remembered root `bookScan/folderRoot`), scanner (`bookScan/deviceId`), "Use the system scan dialog for each scan" (switches to the dialog session). Scan Cover scans the open book's outside at 300 dpi Color, whole bed (nearest offered resolution at or above 300); Skip Cover measures from the pages and leaves the covers out.
2. Cover: the upright cover scan with the outline and two spine lines (drag edges, lines, or the whole box; Tilt spin for the angle). Top of the book points Up/Right/Down/Left sets `up_turns` (lossless quarter turns applied to every scan) and re-detects. Reading direction, capture resolution and mode, scan area (Book area: the outline's bounds in scanner coordinates plus 15 mm, clamped to the bed; or Whole glass), Two pages / One page per scan, Include covers. One page per scan is preselected, with a notice, when the outline plus 0.1 inch on each side does not fit the reported bed in either orientation.
3. Scanning: Scan (default button, also Space) scans with the fixed settings; each scan is detected once (spread outline and gutter, or one page outline) and shown with its editable boxes; Use (Both / Left only / Right only), Tilt, Turn This Scan 180 Degrees, Rescan Last (written beside the old file, then moved onto its name, so a failed rescan keeps the old scan), Cancel Scan (direct sessions), Finished. A page box reaching the scan edge shows the "may reach past the scanned area" warning. After the first spread of a Color capture with no colored pages, a hint offers Switch to Grayscale (the user decides). Optional beep (`bookScan/beep`).
4. Review: thumbnails in page order (drag to reorder, Delete, Turn 180 Degrees, Insert Blank Page, Scan More Pages), page size (Measured median, Largest, From the cover, Custom, A4, A5, US Letter, 6 x 9 in, 5.5 x 8.5 in; units follow the locale's measurement system), output Color / Grayscale / Black and white with a global threshold and a live preview, resolution (as scanned, or 600 to 150 dpi), image quality (`pdf_image_quality_presets`). Export PDF..., or Open Pages as Documents (untitled, modified sessions at the output resolution). After an interactive export the user may delete the project: only the files the manifest lists, then the two folders if empty (never a recursive delete).

Settings keys (permanent): `bookScan/folderRoot`, `bookScan/deviceId`, `bookScan/dpi`, `bookScan/colorMode` (`color`/`gray`), `bookScan/scanArea` (`book`/`glass`), `bookScan/readingDirection` (`ltr`/`rtl`), `bookScan/includeCovers`, `bookScan/beep`.

## Geometry rules

- Every scan is turned upright first (`up_turns` plus a scan's own `extra_turns`), so left and right are always the spread's halves.
- Spread detection: the Divide Scanned Photos background model and min-area rectangle (largest region). A detection smaller than half the previous spread (or a tenth of the image without one) is distrusted and the previous spread kept, for white pages on a white lid; the user corrects the box.
- Gutter: inside the straightened spread, 512 columns by 192 rows, each column's 80th-percentile luminance (text lines are dark marks on white paper and keep a white percentile; the fold's shadow darkens every row), smoothed over 9 columns, darkest in the central 30%, the middle of the run within a quarter of the contrast. Under 6 luminance levels of contrast the spread splits at its middle.
- One uniform page size for the whole PDF; each page box is that size, the spread's angle, vertically centered, anchored at the page's fore-edge (the gutter edge bends under the binding). Covers and one-page scans are centered. Extraction is rotation resampling only (bilinear, area-averaged supersampling when shrinking, white beyond the scan).
- Output: fixed Rec. 601 gray weights, global black/white points, one global threshold. Reading direction only orders pages (right page first for right to left; the front cover is the right half of a left-to-right cover scan).

## book.json

Format id `patchy-book-scan`, version 1, written through `write_file_bytes_atomically` after every scan and edit. Keys and tokens are permanent: `title`, `author`, `device`, `capture` (`dpi`, `mode` `color`/`gray`, `bookArea`, `areaInches`), `upTurns`, `direction` (`ltr`/`rtl`), `mode` (`two-pages`/`one-page`), `includeCovers`, `finished`, `cover` (`file`, `dpi`, `outline` {`cx`,`cy`,`w`,`h`,`angle`}, `spineLeft`, `spineRight`, `edited`), `scans` [{`file`, `dpi`, `turns`, `spread`, `gutter`, `edited`, `use` `both`/`left`/`right`}], optional `pages` [{`kind` `scan`/`blank`/`front-cover`/`back-cover`, `scan`, `side` `left`/`right`, `turns`}], `output` (`sizePolicy` `median`/`largest`/`cover`/`custom`, `customSize`, `mode` `color`/`gray`/`bw`, `blackPoint`, `whitePoint`, `threshold`, `dpi`, `quality`). Scan files are relative paths inside the folder; a manifest naming an absolute path, a drive or `..` is refused. Scans are stored as lossless PNG in `scans/` (`cover.png`, `spread-0001.png` or `page-0001.png`).

## PDF

`write_book_pdf` streams pages through `pdf::ImageWriter` ([pdf.md](pdf.md)): every page the uniform size in points, image over the whole page, rendered and encoded on up to 4 workers and written in order, `BookScanCache` holding at most 4 upright scans. Color and Grayscale pages go through `pdf_detail::encode_page_image` (gray pages are one-component); Black and White pages are one bit per pixel, Flate-compressed (`/BitsPerComponent 1`). Cancel removes the partial file and keeps an existing destination. Scripts: `app.buildBookPdf(folder, path, options)` ([scripting-guide.md](../scripts/bundled/scripting-guide.md)).

## Tests

Core: `book_scan_*`, `pdf_image_writer_one_bit_pages`. UI (offscreen, test scanner): `ui_book_scan_menu_actions_registered`, `ui_book_scan_full_flow_exports_uniform_pages`, `ui_book_scan_black_and_white_and_script_export` (covers `app.buildBookPdf`), `ui_book_scan_resume_and_rescan`, `ui_book_scan_one_page_mode_offered_for_wide_book`, `ui_book_scan_rtl_orders_pages`, `ui_unicode_book_scan_project_folder`. The WIA backend needs a real scanner and is checked by hand.

## Not built yet

Direct SANE and ImageCaptureCore sessions (macOS and Linux use the system dialog per scan), CCITT Group 4 for smaller Black and White pages, OCR text, PDF page labels.
