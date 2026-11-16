# PDF viewer: text recognition on scanned pages

Split from [pdf-viewer.md](pdf-viewer.md): Find and text selection on pages that have no text of their own (a scan), through text recognition ([ocr.md](ocr.md)). Desktop builds with Qt PDF only, like the viewer.

## Rules

The binding entry is "Text recognition" in [legal-constraints-scanning.md](legal-constraints-scanning.md#text-recognition).

- **Nothing is read until the user asks.** Opening a PDF, typing in Find and selecting never recognize anything. The three ways in (the sidebar's Recognize Text..., the Find bar's button beside "This PDF has no searchable text", the page menu's Recognize Text on This Page...) all go through `PdfViewerWindow::recognize_text`, which asks for the engine and the language every time (`request_ocr_choice` in `ui/ocr_controls`, as `pdfViewerRecognizeDialog`: a note, the shared `OcrEnginePicker` with prefix `pdfViewerOcr`, Recognize and Cancel; it is in the offscreen dialog sweep). A PDF's language is not the last one's, and nothing looks at the pages to pick it.
- **Off at every open.** `PdfPageOcr::set_document` forgets everything; a second window on the same file starts with nothing recognized.
- **Only pages without text of their own are read** (`getAllText` empty), so a page's real text is never replaced by a reading of it.
- **Text and places only, in memory.** What is kept is `PdfOcrPageText` per page, for as long as the window lives. Nothing is written to the PDF, a file or settings (the engine and language picks aside), and no page image is kept.
- **The places are for the screen.** Highlights and carets use the engine's word boxes to place the characters of a line. That is display only: nothing here writes text into a PDF, and the one-run-per-line rule of [pdf-searchable-text.md](pdf-searchable-text.md) is about files.

## Files

- `src/ui/pdf_ocr_text.{hpp,cpp}`: `PdfOcrPageText`, a page's recognized text and where it is; no PDFium.
- `src/ui/pdf_page_ocr.{hpp,cpp}`: `PdfPageOcr`, the runs: which pages, the worker, Stop.
- `PdfTextSearch::set_recognized_text`, `PdfTextSelection::set_recognized_text`: how Find and selection take a page's recognized text.

## The text of a page (`PdfOcrPageText`)

`text` is the recognized lines in the engine's order joined with `\n`. Each `PdfOcrLine` has its run of `text` (`start`, `count`), its reading axis (`from`, `to`: the middles of the edges it starts and ends at; a column runs from its top edge down), its `thickness`, its `box`, and `edges`: `count + 1` places along the axis, where each UTF-16 unit starts and the last ends. With two or more word boxes from the engine, a word's units sit evenly inside the word and what lies between two words takes the gap; a line whose words do not spell its text, or with one word, is spaced evenly. Everything is in points from the top left of the page as stored, like the rest of the viewer.

- `caret_at(point)`: the nearest line (by its box), then the nearest edge along it; never between the halves of a surrogate pair.
- `range_rects(start, count)`: one rectangle per line the range touches (the strip between two edges; the rectangle around it for a turned line).
- `line_range_at(point)`: the nearest line's run.
- `pdf_ocr_image_to_page(image pixels, page points, quarter turns)`: the page is read the way the user sees it (the view's rotation applied, so a sideways scan the user turned upright is read upright), and its places go back to the page as stored.

## Runs (`PdfPageOcr`)

`start(engine, language, pages, quarter_turns)` queues the pages still unread and reads one at a time on a worker: the page's own text first (a page that has some is `OwnText` and done); else the page rendered on white at 300 pixels per inch (less for a page past 8 megapixels), the engine opened by the first page that needs it and reused, then `pdf_ocr_page_text`. States: `Unread`, `OwnText`, `Recognized`, `NoText`. `page_done(page)` hands the page over; the window gives a recognized page's text to Find and to selection and repaints.

- **Stop** (`stop()`, the same sidebar button, labelled Stop Recognizing while a run is going): clears the queue and cancels the read in flight; that page stays `Unread`, what was read stays.
- **An engine failure ends the run** with the engine's words (`last_error`, shown as "Text recognition stopped: ..."); the page it failed on stays `Unread`, so another run, or another engine, can take it.
- **Closing the window, or a new run, mid-read**: the worker is cancelled and waited for; a generation counter drops its answer.
- Renders take PDFium's lock like the view's own; one page at a time, so the pages on screen keep rendering between them.

## Find

`PdfTextSearch::set_recognized_text(page, text)` replaces the page's text with the recognized one (`PdfPageText::recognized`), bumps the query generation so an answer about the old empty text is dropped, takes the page's matches out of the count and searches it again. `locate_match` answers from `range_rects` for such a page: no PDFium call. A phrase matches across recognized lines as across a page's own (a line break is a space to Find). `text_layer()` turns `Present` with the first recognized page, so the status goes from "This PDF has no searchable text" to matches; while a run is going with no text yet it reads "Recognizing page N of M...". The Find bar's button (`pdfViewerFindRecognizeButton`) shows only while the status is "no searchable text" and no run is going.

## Selection

`PdfTextSelection::set_recognized_text(page, text)` makes the recognized lines the page's text areas (`PdfPageTextAreas::recognized`). The four places the worker asks PDFium branch on it: `caret_at`, `make_piece` (the text is a substring, the rectangles `range_rects`), `answer_word` (the word is read from the string) and `answer_line` (`line_range_at`). Everything above them (drags, Shift+click, Select All, ranges across pages, Copy, the I-beam over text) is the same code as for a page's own text ([pdf-viewer-selection.md](pdf-viewer-selection.md)).

## The controls

Sidebar: `pdfViewerRecognizeButton` under Print as Book..., and `pdfViewerRecognizeStatusLabel` (hidden while it has nothing to say): "Recognizing page N of M...", then what the run did. Page menu on a page read and found without text: `pdfViewerRecognizePageAction`.

## Limits

- Right-to-left scripts are searched and selected in the order the engine gives them.
- A page with a little text of its own (a stamp, a page number) over a scan counts as having text and is not read.
- Recognition is not handed on to Edit...: Import PDF's own option reads the pages again ([ocr.md](ocr.md#import-pdf)).
- The view's rotation at the time a run starts is the one its pages are read at.

## Tests

`tests/ui/pdf_viewer_ocr_tests.cpp` (the scripted fake engine): `ui_pdf_viewer_ocr_text_model` (words, the gap, a column, carets, ranges over two lines, the turned mapping against Qt's own), `ui_pdf_viewer_ocr_find_and_select_on_a_scan` (nothing read by opening, searching or cancelling the question; then matches where the engine put the word, a phrase across lines, select all, a word, a line, a range, Copy, two pages; a second window starts empty), `ui_pdf_viewer_ocr_reads_only_pages_without_text` (one page from the page menu's path, a page with its own text left alone, no page read twice, Find over both kinds), `ui_pdf_viewer_ocr_stop_failure_and_close` (Stop, closing mid-run, an engine that fails, no engine).
