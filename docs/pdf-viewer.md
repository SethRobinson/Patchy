# PDF viewer

Desktop builds with Qt PDF (`PATCHY_HAVE_QT_PDF`) open PDFs in the viewer by default. This applies to File > Open, Open Recent, dropped files and files passed by a second launch. Choose **Edit...** to open the [Import PDF dialog](pdf-import.md). Builds without Qt PDF can export PDFs but cannot open them.

Related documents:

- [pdf-book-print.md](pdf-book-print.md): the sidebar's Print as Book: imposition styles, the dialog, output, assembly steps.
- [pdf-viewer-selection.md](pdf-viewer-selection.md): selecting text with the mouse and copying it: the drag, words and lines, Copy and Select All, how a pointer position becomes a character.
- [pdf-viewer-ocr.md](pdf-viewer-ocr.md): text recognition on scanned pages, on request, so Find and selection work on them.

## Files

- `src/core/page_view_layout.{hpp,cpp}` (Qt-free): which pages share a row (`page_spreads`: Single, TwoPages, TwoPagesCoverAlone, mirrored for right to left), where each page sits (`layout_page_view`, all rows for continuous scrolling or one row), fit zoom (`fit_pixels_per_point`), zoom steps. Tests: `page_view_*` (patchy_core_tests).
- `src/ui/pdf_page_view.{hpp,cpp}`: `PdfPageView`, the page area (a `QAbstractScrollArea`): rendering, cache, scrolling, zoom, page turns.
- `src/ui/pdf_viewer_window.{hpp,cpp}`: `PdfViewerWindow` (`pdfViewerWindow`), toolbar, find bar, sidebar, settings.
- `src/ui/pdf_text_search.{hpp,cpp}`: `PdfTextSearch`, Find's engine (page text, matching rules, where matches sit).
- `src/ui/pdf_links.{hpp,cpp}`: `PdfLinkIndex` (a page's links, read on a worker), `pdf_link_url_opens`, `read_pdf_outline`.
- `src/ui/pdf_text_selection.{hpp,cpp}`: `PdfTextSelection`, the text selection ([pdf-viewer-selection.md](pdf-viewer-selection.md)).
- `src/ui/main_window_pdf_viewer.cpp`: `MainWindow::open_pdf_viewer` and `import_pdf_pages_from_viewer`.

## Opening

- `MainWindow::open_document_path(path, existing, PdfOpen)`: with `PdfOpen::Viewer` (the default) an interactive PDF open goes to `open_pdf_viewer` unless a script run is active, the run is unattended, or Preferences "Open PDF files in the PDF viewer" (`preferencesOpenPdfInViewerCheck`, `imports/pdfOpenInViewer`, default on) is off. The check runs before the already-open lookup, so a PDF whose page 1 is open as a document still opens the viewer. `PdfOpen::Import` keeps the Import PDF dialog: scripts (`app.open`), Edit Contents of a linked Smart Object, `MainWindowTestAccess::open_document_path` (the import tests drive that dialog; `open_document_path_as_user` reaches the viewer).
- One viewer per file: opening a file a viewer shows raises it (paths compared canonically, without case on Windows and macOS). The viewer is a MainWindow-parented `Qt::Window` with `WA_DeleteOnClose` (the float-window pattern), never a nested event loop, so several viewers and the main window stay usable together. The first open covers the main window's geometry; later opens restore `pdfViewer/geometry`.
- Drops: files dropped on a viewer (`set_file_drop_handlers`, wired in `open_pdf_viewer`) go through `MainWindow::accept_open_file_drag`/`open_dropped_files`, like a drop on the main window or a float window: a PDF opens its own viewer, an image or PSD a document, after which the main window is raised (only when the active document changed). Test: `ui_pdf_viewer_opens_dropped_files`.
- Passwords: `load_pdf_for_viewing` (pdf_import.cpp) asks up to three times and shows `pdfOpenFailedMessageBox` on failure. Edit... hands the accepted password on (`PdfImportDialogStart::password`), so it is not asked twice.

## Edit...

**Edit...** opens Import PDF over the viewer with all pages selected. The current row is the first page of the spread being viewed, scrolled into view. The dialog reuses the password that opened the file. On a successful import the main window activates the new document and closes the viewer. Cancel keeps the viewer open.

`pdfViewerEditButton` calls `import_pdf_pages_from_viewer`, which sets `pdf_import_start_` and `pdf_import_dialog_parent_` before calling `open_document_path(path, LoadCopy, PdfOpen::Import)`. A change of active session signals success.

## View

- **Layouts** (`pdfViewerLayoutCombo`, `pdfViewer/pages`: `single`/`two`/`book`): One Page, Two Pages (1-2, 3-4), Two Pages, Cover Alone (1, 2-3, 4-5: a book or comic opened flat). The two pages of a spread touch, no gap, so art across the fold reads whole.
- **Continuous** (`pdfViewerContinuousAction`, `pdfViewer/continuous`, default on): every row stacked and scrolled. Off shows one row at a time, centred; the wheel scrolls inside a zoomed row and turns the page at its top or bottom edge after 120 units of push, then ignores the wheel for 350 ms (`kFlipCooldownMs`) so a trackpad fling turns one page.
- **Right to Left** (`pdfViewerRightToLeftAction`, `pdfViewer/direction` `ltr`/`rtl`): the earlier page of a pair on the right; Left/Right arrow keys follow the screen, so Left goes forward.
- **Zoom** (`pdfViewerZoomCombo`, `pdfViewer/zoom`: `page`, `width` or a percent): Fit Page, Fit Width, Actual Size (100% = 1 pt is 1/72 inch at the screen's logical DPI), presets, typed values; Ctrl+wheel zooms about the pointer by 1.2 per notch, clamped 5% to 1600%. Fits are recomputed for the current row on every resize. Scroll-bar policies are fixed on or off where a bar appearing would change the width a fit measures (`update_scroll_policies`), so fits never oscillate.
- **Rotate Clockwise** turns every page a quarter (view only, never saved).
- **Full Screen** (F11, a double-click; Esc or the same keys return) hides toolbar and sidebar.
- **Page navigation.** PgUp, PgDn, Space and Shift+Space move through the pages. Home and End jump to the ends. Arrow keys turn pages when there is no overflow to scroll in that direction. The main canvas excludes the viewer from `MainWindow::spacebar_canvas_pan_target_in_window` so Space reaches the pages.
- **Pointer.** Left-drag on text to select it. Left-drag beside text or middle-drag to pan. A left click follows a link (see Links).
- **Sidebar** (`pdfViewerSidebarAction`, `pdfViewer/sidebar`): Edit..., Print as Book..., and page thumbnails (`pdfViewerThumbnails`; blank placeholders of the right shape, rendered only once on screen and only while the Pages view shows, after every visible page), with the Outline beside them when the PDF has one (see Outline).
- **Reading position**: the page shown at close is remembered per file (`pdfViewer/positions`, "page<TAB>path" lines, newest first, at most 50).

### Keyboard shortcuts

| Action | Viewer shortcut |
|---|---|
| Zoom in / out | Ctrl+= or Ctrl++ / Ctrl+- |
| Fit Page / Actual Size / Fit Width | Ctrl+0 / Ctrl+1 / Ctrl+2 |
| Print as Book / Find | Ctrl+P / Ctrl+F |
| Next match | F3 or Ctrl+G (see Find for reverse search) |
| Back / Forward | Alt+Left / Alt+Right |
| Copy / Select All | Ctrl+C / Ctrl+A in the page text. See [text selection](pdf-viewer-selection.md). |
| Toggle full screen | F11 |
| Close viewer | Ctrl+W, the platform's Close keys (including Command+W on macOS and Ctrl+F4 on Windows), or the user's File > Close binding |

The viewer owns window shortcuts. To resolve collisions with the main window's
`Qt::ApplicationShortcut` actions, its application event filter claims `ShortcutOverride`
for its own `WindowShortcut` actions (`window_key_action`) and triggers the action on
keypress. Other shortcuts pass through. `standard_keys` uses every entry from
`QKeySequence::keyBindings` for a standard action. The viewer looks up the user's
File > Close binding through `set_close_key_source` at keypress. It claims that binding
only after the focused control passes it on, so a letter bound to Close can still be
typed in the page and zoom fields and an existing viewer shortcut keeps its meaning.

## Find

The find bar (`pdfViewerFindBar`, a row under the toolbar, hidden until asked for) searches the text of every page. `PdfTextSearch` (`src/ui/pdf_text_search.{hpp,cpp}`) is the engine; the window owns the bar and the page view draws the matches.

- **Opening and keys.** Ctrl+F (every platform Find key) or the toolbar's Find button (`pdfViewerFindAction`) shows the bar with the keyboard in the field (`pdfViewerFindEdit`) and its text selected, and searches that text again from the page being read. While the bar is open, Enter is next and Shift+Enter previous wherever the keyboard is in the viewer: the field, a page, an option's check box, the sidebar (a click on any of those takes the keyboard out of the field). Only the page field, the zoom box and other text fields keep their own Enter and Esc (`types_its_own_enter`), and only the control that holds the keyboard counts, so an Enter the page field passes on does not also step. F3 and Ctrl+G step forward and Shift+F3 and Ctrl+Shift+G back on every platform, with the platform's own Find Next and Find Previous keys, from anywhere in the viewer (with the bar closed they open it). The step buttons' tips name Enter and the platform's Find Next or Find Previous key in native text ("Next match (Enter, F3)"; Command+G on macOS). Esc anywhere in the viewer, or the bar's close button, closes the bar: the highlights go, the keyboard returns to the pages, and the text is kept for the next Ctrl+F (never saved). Esc leaves full screen only once the bar is closed. The find keys are window keys like the rest (see Keys), so they win over the main window's Ctrl+F and Ctrl+G; `window_key_action` tries a key as typed before its unshifted form, so Shift+F3 is never F3. Enter and Esc are taken in the window's event filter, with their `ShortcutOverride`, so no main-window hotkey on those keys runs instead. A step typed before the search has a current match (Enter right behind the text) is kept and taken from the first match once it is known (`pending_steps_`, dropped by the next change of text), so the key lands on the same match whether or not the search had answered.
- **Status** (`pdfViewerFindStatusLabel`): "3 of 17" (the current match's place in document order among those found so far), "Searching..." until a current match is known, "No matches", or "This PDF has no searchable text" when every page was read and none holds text (a scan). The step buttons (`pdfViewerFindPreviousButton`, `pdfViewerFindNextButton`) are disabled without matches. A step past either end goes around, and `pdfViewerFindNoteLabel` says so for three seconds.
- **Options** (remembered: `pdfViewer/findMatchCase`, `pdfViewer/findWholeWords`, both off by default): Match case; Whole words, which wants no letter, digit, mark or underscore touching an end of the match that is itself a letter or digit.
- **Matching** (`prepare_pdf_search_text`, `find_pdf_text_matches`). Page text and query go through the same preparation, so each rule only widens what matches: a run of spaces, tabs and line breaks is one space (a phrase matches across lines); a hyphen that ends a line between letters or digits is dropped with the break ("exam-", "ple" reads "example"), as are PDFium's own marks for such a hyphen (U+0002, U+FFFE; Qt 6.8's PDFium returns "exam", U+FFFE, "ple" with no line break), and a query that types the hyphen ("well-known") is also tried without it across each such join; compatibility forms are decomposed (NFKD per character: ligatures, full-width letters, accents as letter plus mark), curly quotes become straight, the dashes a hyphen; soft hyphens and zero-width characters go. Case is folded by Qt's Unicode simple folding (every script) unless Match case. Accents count: "cafe" does not find "café", and a match never ends before a letter's combining mark. Matches never overlap. Each prepared character keeps its source offset and length, so a match is reported as a range of the page's own text.
- **Background work.** One worker task at a time (`launch_async`, results posted back), each a slice of about 12 ms so a page render never waits long for PDFium's lock. A search task reads page text it does not have yet (`QPdfDocument::getAllText`, kept for the life of the viewer) and matches it, pages in order from the page being read, around the end, so near results come first and the count grows as pages come in. The current match is the first at or after the current match (typing on keeps the place) or the top of the page being read; it is chosen only once every page before it in that order is searched. `set_document(nullptr)` and the destructor wait for the worker; `~PdfViewerWindow` detaches the search like the view. Qt's `QPdfSearchModel` is not used: it has no case or whole-word options, joins nothing across lines, and searches on the GUI thread.
- **Where a match is** is worked out lazily: for the current match, then for the matches on the pages on screen (`set_visible_pages`, from the view's render scheduling), search and placement tasks taking turns. `QPdfDocument::getSelectionAtIndex` gives a rectangle per line piece in points from the page's top left; each call parses the page, which is why nothing is placed that is not looked at. Its indices count PDFium characters: the text's UTF-16 units, except where PDFium counts a character its text leaves out (the match is probed up to 12 characters further on) or returns more units than characters (code points are counted). The selection's own text must equal the match, else the match has no rectangle: it is still counted and stepped to, page only.
- **Drawing** (`PdfPageView::paint_text_matches`). Rectangles are mapped from page points through the view's quarter turns and the page's slot on every paint, so they hold through zoom, rotation, layout changes and detail renders with nothing cached. Roles `pdf_find_match` (every match, 42% over the page) and `pdf_find_current_match` (55% plus a 2 px outline), the same in Dark and Light because the page is paper in both. One winding-filled path per color, so overlapping line pieces tint once.
- **Showing the current match** (`show_current_text_match`, on a new current match and again when it is placed): with one row at a time the view turns to the match's row at once; continuous, it scrolls to the page only if none of it shows. Once placed, a match within 12 px of the window's edge or outside it is centred; one already in view moves nothing.
- No results list in the sidebar.

## Links

`PdfLinkIndex` (`src/ui/pdf_links.{hpp,cpp}`, owned by the page view) reads a page's links; the page view shows and follows them.

- **Reading.** Qt PDF's `QPdfLinkModel` gives a page's link annotations, then the addresses PDFium finds written in the page's text (`https://...`, `www.`, mail addresses), each with rectangles in points from the page's top left. It reads through PDFium behind Qt PDF's lock, so a model is made, used and destroyed inside a worker task (`launch_async`, slices of about 12 ms, results posted back), for the pages on screen only (`request_pages`, from the view's render scheduling); a page's links are kept for the life of the viewer. A link into the document is a `PdfJumpTarget` (page, point); any other is a `QUrl`. Qt PDF drops a link whose action is a script (`/S /JavaScript`), gives a Launch or remote GoTo action as a `file:` address, lists a text address once per rectangle (duplicates are dropped), and measures a link's point on the page the link is on, so the view clamps the point to the target page.
- **Pointer.** The page area tracks the mouse: over a link the cursor is a pointing hand (`update_hover`, also after a scroll, a relayout and when a page's links arrive) and the tooltip (`viewportEvent`, `QEvent::ToolTip`) says "Page 12", or the address with a second line: "Ctrl-click to open the link" (`%CTRL%`, Command on macOS), or that Patchy does not open this kind of link. The tooltip is bound to the link's rectangle, so it goes when the pointer leaves. An address is the PDF's own text: one that could read as rich text is escaped (`Qt::convertFromPlainText`).
- **Click** (left button, press and release without moving a drag's distance; `MouseDrag`, `press_link_`). A link into the document jumps (`jump_requested`, see Back and Forward). An address opens only with Ctrl held (the modifiers of the release event; Qt reads Command as Control on macOS) and only when `pdf_link_url_opens` takes it; a plain click opens nothing and shows the tooltip at once. A press on a link that moves on selects the link's text, or pans when the link is not over text. A second click on a link, or within the double-click time of a click that followed one, is a click, never the double-click that toggles full screen.
- **What opens** (`pdf_link_url_opens`): `http` and `https` with a host, and `mailto` with an address. Nothing else ever reaches `QDesktopServices::openUrl`: `file:`, `javascript:`, a program to launch, a relative address and every other scheme can only be copied. The check runs where the click is read and again in `PdfViewerWindow::open_link`. Nothing stats or reads a link's target.
- **Right-click on a link** (the page menu, `pdfViewerPageMenu`): Open Link (`pdfViewerLinkOpenAction`, disabled for an address that does not open) and Copy Link Address (`pdfViewerLinkCopyAction`, the encoded address, `pdf_link_address`); for a link into the document, Go to Page N (`pdfViewerLinkGoAction`). Under them, and alone off a link, the page's own entries: Copy and Select All on This Page ([pdf-viewer-selection.md](pdf-viewer-selection.md)).
- **Where a link is**: `hit_link` maps each link's rectangle with `page_area_to_content`, the mapping the find highlights use, on every question, so links hold through zoom, rotation, layout changes and detail renders with nothing cached.
- **Jumping** (`PdfPageView::go_to_target`): the target's row is shown (turned to, one row at a time) and the point named is put at the top of the window, the layout margin under the edge, and brought into view across when the page is wider than the window. A target without a point (`/Fit`, or a point Qt PDF does not report: it wants both x and y of an `/XYZ` destination) is the top of its page. The link's zoom is ignored: the reader's zoom stays.
- `PdfViewerWindow::set_link_handlers` replaces what opens and what copies (tests: no browser, no clipboard).

## Back and Forward

`PdfViewerWindow` keeps two stacks of `PdfViewPosition` (the page being read, and the point of a page at the window's top left corner as a share of that page, so a place means the same at another zoom, layout or rotation), 100 places at most.

- **What is a jump** (`jump`, which records the position it left unless the view did not move): a link, an outline entry, the page field, a click in the page list. Page turns by keys, the wheel, the toolbar's page buttons, scrolling and Find steps are not.
- **Going back**: Alt+Left and Alt+Right on every platform, with the platform's own Back and Forward keys that hold a modifier (Command+[ and Command+] on macOS; the bare Backspace Windows lists is left alone), the mouse's Back and Forward buttons anywhere in the window (the application event filter), and the toolbar's `pdfViewerBackAction` and `pdfViewerForwardAction` (icons `arrow-left`, `arrow-right`), disabled with nowhere to go. The key actions are separate and always enabled, so with an empty history the key does nothing instead of falling through to the pages (where the arrow turns one) or the main window. A key that moves the caret by a word in a text field (Option+Left on macOS) stays with the field (`moves_caret_by_word`).
- A new jump drops the Forward stack. `restore_view_position` turns to the row when one row shows at a time, then puts the anchor point back.

## Outline

- `read_pdf_outline` copies Qt PDF's `QPdfBookmarkModel` into `PdfOutlineEntry` trees (title, target, children; 64 levels and 50,000 entries at most) once, in the constructor before the first render, because the model reads behind PDFium's lock on the GUI thread.
- A PDF with an outline gets two tabs over the sidebar's list (`pdfViewerSidebarTabs`: Pages, Outline; `pdfViewerSidebarStack` holds the thumbnails and the tree `pdfViewerOutline`). A PDF without one shows no tabs. The view chosen last is remembered (`pdfViewer/sidebarView`: `pages`/`outline`) and changed only by the user's own choice, so a PDF without an outline does not reset it. Showing the outline widens a sidebar narrower than 250 px.
- A click on an entry jumps (history) and hands the keyboard to the pages; Enter on an entry jumps and stays in the tree (with the find bar open Enter is Find's). An entry that goes nowhere in the document (a web address) does nothing. A single entry at the top starts open, the rest closed: Qt PDF does not report the PDF's own open and closed states.
- **The entry being read** is the tree's current item (`update_outline_current`, on every page change, layout change and expand or collapse): the entry followed last while its page shows, else the first entry with the last start page at or before the last page of the row shown. It is marked on the outermost closed entry above it; nothing is opened for the reader.

## Rendering

- PDFium renders behind Qt PDF's global lock, so one worker (`launch_async`) is all a document can use: one request in flight, results posted back with `invokeMethod`. `set_document(nullptr)` and the destructor wait for it; `~PdfViewerWindow` detaches the document first because the view (a child widget) outlives the window's members.
- After each render the wanted list is rebuilt from scratch (stale requests never pile up), in priority order: visible pages nearest the middle, detail renders, then the rows either side (the next row first), then thumbnails, newest request first.
- A whole-page image holds at most 8 MP (`kMaxBasePixels`). Zoomed further, the visible part of the page plus a quarter window around it renders sharp (`QPdfDocumentRenderOptions::setScaledSize` + `setScaledClipRect`, output the clip's size) over the scaled whole-page image. The worker renders unturned and turns the result itself (`unturned_clip` maps the clip), composited on white, opaque `RGB32`.
- While a sharper render is on its way the view draws the best image it has scaled, and a rotation shows the old render turned. Cache: 512 MB, least recently drawn pages first, never one on screen or prefetched. A failed render is kept as a blank page and not retried.
- Colors: the backdrop is `canvas_backdrop`, page outlines `canvas_document_border` (drawn first, pages over them, so a spread has no line at the fold); paper white is a content color.

## Tests

- **Opening and navigation.** `ui_pdf_viewer_opens_pdf_files` checks viewer reuse, the opening preference, remembered page, and Edit's selection and focus. `ui_pdf_viewer_open_updates_home_recent_list` covers [Recent Files](ui-conventions-menus.md#recent-files). `ui_pdf_viewer_spreads_and_page_turns` covers layout, page turns, zoom and full screen. `ui_pdf_viewer_close_keys_close_only_the_viewer` checks standard and rebound Close keys with both windows open.
- **Rendering and output.** `ui_pdf_viewer_detail_renders_match_the_page` pins Qt PDF clipping at each quarter turn. `ui_pdf_book_print_writes_sheets_pdf` covers Unicode output paths.
- **Find.** `ui_pdf_viewer_find_text_matching_rules`, `ui_pdf_viewer_find_text_counts_and_options` and `ui_pdf_viewer_find_text_steps_through_matches` cover matching, options, order, wrapping and spreads. `ui_pdf_viewer_find_text_reports_no_text_layer` covers image-only pages. `ui_pdf_viewer_find_text_highlights_follow_the_page` covers three zooms, detail rendering and every quarter turn. `ui_pdf_viewer_find_text_sample_document` writes the six-page prose capture.
- **Shortcut focus.** `ui_pdf_viewer_find_text_keys_work_as_typed` sends `QTest::keyClick` to the `QWindow` with focus in different controls. Sending it directly to a widget bypasses the window system's focus routing. The history test uses the window path too.
- **Links.** The six-page fixture in `tests/ui/pdf_viewer_links_tests.cpp` contains link targets and a nested outline. `ui_pdf_viewer_links_are_read_from_the_pdf`, `ui_pdf_viewer_links_hover_and_click` and `ui_pdf_viewer_links_context_menu` cover allowed addresses, mouse gestures and context actions. `ui_pdf_viewer_links_jump_in_every_layout` and `ui_pdf_viewer_links_follow_the_page` check target placement and hit areas through zoom, rotation, detail rendering and spreads.
- **History and outline.** `ui_pdf_viewer_history_back_and_forward` checks jumps, side buttons and restoring the reading position at another zoom. `ui_pdf_viewer_outline_sidebar` covers the selected entry, remembered sidebar view and PDFs without an outline. `ui_pdf_viewer_links_sample_document` captures the link tooltip. [Text selection tests](pdf-viewer-selection.md) cover the page text.

The viewer has no find-results list, separate page-number labels, or support for the PDF's saved outline expansion state or links to another PDF.
