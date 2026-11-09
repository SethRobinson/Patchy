# PDF viewer

Opening a PDF (File > Open, Open Recent, a drop, a second launch) shows it in the PDF viewer instead of the Import PDF dialog. Editing pages goes through the viewer's Edit..., which runs the ordinary import ([pdf-import.md](pdf-import.md)). Print as Book is in [pdf-book-print.md](pdf-book-print.md). Desktop builds with Qt PDF only (`PATCHY_HAVE_QT_PDF`); elsewhere a PDF open behaves as before.

Related documents:

- [pdf-book-print.md](pdf-book-print.md): the sidebar's Print as Book: imposition styles, the dialog, output, assembly steps.

## Files

- `src/core/page_view_layout.{hpp,cpp}` (Qt-free): which pages share a row (`page_spreads`: Single, TwoPages, TwoPagesCoverAlone, mirrored for right to left), where each page sits (`layout_page_view`, all rows for continuous scrolling or one row), fit zoom (`fit_pixels_per_point`), zoom steps. Tests: `page_view_*` (patchy_core_tests).
- `src/ui/pdf_page_view.{hpp,cpp}`: `PdfPageView`, the page area (a `QAbstractScrollArea`): rendering, cache, scrolling, zoom, page turns.
- `src/ui/pdf_viewer_window.{hpp,cpp}`: `PdfViewerWindow` (`pdfViewerWindow`), toolbar, sidebar, settings.
- `src/ui/main_window_pdf_viewer.cpp`: `MainWindow::open_pdf_viewer` and `import_pdf_pages_from_viewer`.

## Opening

- `MainWindow::open_document_path(path, existing, PdfOpen)`: with `PdfOpen::Viewer` (the default) an interactive PDF open goes to `open_pdf_viewer` unless a script run is active, the run is unattended, or Preferences "Open PDF files in the PDF viewer" (`preferencesOpenPdfInViewerCheck`, `imports/pdfOpenInViewer`, default on) is off. The check runs before the already-open lookup, so a PDF whose page 1 is open as a document still opens the viewer. `PdfOpen::Import` keeps the Import PDF dialog: scripts (`app.open`), Edit Contents of a linked Smart Object, `MainWindowTestAccess::open_document_path` (the import tests drive that dialog; `open_document_path_as_user` reaches the viewer).
- One viewer per file: opening a file a viewer shows raises it (paths compared canonically, without case on Windows and macOS). The viewer is a MainWindow-parented `Qt::Window` with `WA_DeleteOnClose` (the float-window pattern), never a nested event loop, so several viewers and the main window stay usable together. The first open covers the main window's geometry; later opens restore `pdfViewer/geometry`.
- Passwords: `load_pdf_for_viewing` (pdf_import.cpp) asks up to three times and shows `pdfOpenFailedMessageBox` on failure. Edit... hands the accepted password on (`PdfImportDialogStart::password`), so it is not asked twice.

## Edit...

`pdfViewerEditButton` calls `import_pdf_pages_from_viewer`: it sets `pdf_import_start_` (pages = the current row's pages, the password) and `pdf_import_dialog_parent_` (the viewer, so the dialog opens over it), then `open_document_path(path, LoadCopy, PdfOpen::Import)`. `run_pdf_import_dialog` preselects those pages. Success is "the active session changed"; the viewer then closes and the main window is raised. Cancel keeps the viewer.

## View

- **Layouts** (`pdfViewerLayoutCombo`, `pdfViewer/pages`: `single`/`two`/`book`): One Page, Two Pages (1-2, 3-4), Two Pages, Cover Alone (1, 2-3, 4-5: a book or comic opened flat). The two pages of a spread touch, no gap, so art across the fold reads whole.
- **Continuous** (`pdfViewerContinuousAction`, `pdfViewer/continuous`, default on): every row stacked and scrolled. Off shows one row at a time, centred; the wheel scrolls inside a zoomed row and turns the page at its top or bottom edge after 120 units of push, then ignores the wheel for 350 ms (`kFlipCooldownMs`) so a trackpad fling turns one page.
- **Right to Left** (`pdfViewerRightToLeftAction`, `pdfViewer/direction` `ltr`/`rtl`): the earlier page of a pair on the right; Left/Right arrow keys follow the screen, so Left goes forward.
- **Zoom** (`pdfViewerZoomCombo`, `pdfViewer/zoom`: `page`, `width` or a percent): Fit Page, Fit Width, Actual Size (100% = 1 pt is 1/72 inch at the screen's logical DPI), presets, typed values; Ctrl+wheel zooms about the pointer by 1.2 per notch, clamped 5% to 1600%. Fits are recomputed for the current row on every resize. Scroll-bar policies are fixed on or off where a bar appearing would change the width a fit measures (`update_scroll_policies`), so fits never oscillate.
- **Rotate Clockwise** turns every page a quarter (view only, never saved).
- **Full Screen** (F11, a double-click; Esc or the same keys return) hides toolbar and sidebar.
- **Keys** (window shortcuts, not application hotkeys: the viewer is not the main window, so nothing joins `HotkeyRegistry`): Ctrl+= / Ctrl++ zoom in, Ctrl+- zoom out, Ctrl+0 Fit Page, Ctrl+1 Actual Size, Ctrl+2 Fit Width, Ctrl+P Print as Book, Ctrl+W (the platform Close key) close, F11 full screen; in the page area PgUp/PgDn/Space/Shift+Space, Home/End, arrows (turn pages when nothing overflows that way). A left or middle drag pans. The main window's hotkeys are `Qt::ApplicationShortcut`, so a key both bind would be ambiguous to Qt and run neither: the viewer's application event filter claims the `ShortcutOverride` of any key one of its own `WindowShortcut` actions binds (`window_key_action`) and triggers that action on the press.
- **Sidebar** (`pdfViewerSidebarAction`, `pdfViewer/sidebar`): Edit..., Print as Book..., and page thumbnails (`pdfViewerThumbnails`; blank placeholders of the right shape, rendered only once on screen, after every visible page).
- **Reading position**: the page shown at close is remembered per file (`pdfViewer/positions`, "page<TAB>path" lines, newest first, at most 50).

## Rendering

- PDFium renders behind Qt PDF's global lock, so one worker (`launch_async`) is all a document can use: one request in flight, results posted back with `invokeMethod`. `set_document(nullptr)` and the destructor wait for it; `~PdfViewerWindow` detaches the document first because the view (a child widget) outlives the window's members.
- After each render the wanted list is rebuilt from scratch (stale requests never pile up), in priority order: visible pages nearest the middle, detail renders, then the rows either side (the next row first), then thumbnails, newest request first.
- A whole-page image holds at most 8 MP (`kMaxBasePixels`). Zoomed further, the visible part of the page plus a quarter window around it renders sharp (`QPdfDocumentRenderOptions::setScaledSize` + `setScaledClipRect`, output the clip's size) over the scaled whole-page image. The worker renders unturned and turns the result itself (`unturned_clip` maps the clip), composited on white, opaque `RGB32`.
- While a sharper render is on its way the view draws the best image it has scaled, and a rotation shows the old render turned. Cache: 512 MB, least recently drawn pages first, never one on screen or prefetched. A failed render is kept as a blank page and not retried.
- Colors: the backdrop is `canvas_backdrop`, page outlines `canvas_document_border` (drawn first, pages over them, so a spread has no line at the fold); paper white is a content color.

## Tests

`ui_pdf_viewer_opens_pdf_files` (viewer instead of import, one viewer per file, Edit... preselects the page and closes the viewer, remembered page, the Preferences switch), `ui_pdf_viewer_spreads_and_page_turns` (cover-alone rows, right-to-left keys, wheel turn and cooldown, Home/End, page field, continuous scrolling, zoom steps, full screen, persisted layout), `ui_pdf_viewer_detail_renders_match_the_page` (pins Qt PDF's clip semantics and every quarter turn of a detail render), Unicode paths in `ui_pdf_book_print_writes_sheets_pdf`. Not built yet: text search and selection, links, a page-number labels track.
