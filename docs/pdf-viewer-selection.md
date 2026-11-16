# PDF viewer: selecting and copying text

Split from [pdf-viewer.md](pdf-viewer.md), which has the viewer's layouts, keys, Find, links and rendering.

## Files

- `src/ui/pdf_text_selection.{hpp,cpp}`: `PdfTextSelection` (owned by the page view): where each page's text is, the selection, and the worker that asks PDFium. `pdf_copied_text` is what Copy hands out.
- `src/ui/pdf_page_view.{hpp,cpp}`: the mouse (`MouseDrag::Select`), the pointer shape, the tint (`paint_text_selection`), the page menu.
- `src/ui/pdf_viewer_window.cpp`: the Copy and Select All keys, Esc, the clipboard (`copy_text`).

## What it does

- **Drag.** A left press on text selects once the pointer has moved 3 px (`kSelectDragDistance`), from the character pressed to the character under the pointer, across lines and across every page that is laid out (all of them when Continuous, the two pages of a spread otherwise). Beside a line the pointer takes that line's end, below the last line the end of the text. Outside the page area the view scrolls (`scroll_for_selection_drag`, every 30 ms, faster the further out) and the selection follows; a wheel turn during a drag does the same (`scrollContentsBy`). One row at a time, a drag never turns the page.
- **A press beside the text pans**, as before, and so does the middle button anywhere. A press on a link stays a click until the pointer has gone a drag's distance; then it selects when the link is over text and pans when it is not (an image link).
- **Pointer** (`update_hover`): a hand over a link, an I-beam over text, the arrow elsewhere. "Over text" (`PdfTextSelection::text_at`, which also decides what a press does) is within 2 px of a line piece, or within 35% of the line's height above or below it: the gaps between the lines of a paragraph count, the wider gap between paragraphs does not.
- **Double-click** selects the word (letters, digits, marks and underscores; a punctuation mark stands by itself; a space selects nothing), and is the double-click for full screen only beside the text. **A third click** within the double-click time selects the line: the run of line pieces that share most of their height with the one clicked.
- **Shift+click** selects from where the selection started to the click; a held Shift+click goes on as a drag. After a double-click, a triple-click or Select All the word, line or page stays in: the selection grows from its far end.
- **A click** (no drag) drops the selection: on text it also sets the place Shift+click reaches from.
- **Copy**: the platform's Copy keys (Ctrl+C and Ctrl+Insert on Windows) and the page menu's Copy (`pdfViewerCopyAction`). **Select All** (Ctrl+A) takes all the text of the page being read, both pages of a spread; the menu's Select All on This Page (`pdfViewerSelectPageAction`) the page under the pointer. There is no Select All for the whole document. **Esc** drops the selection once the find bar is closed, and leaves full screen only after that.
- **The page menu** (`pdfViewerPageMenu`, a right-click on a page): the link's entries first when a link is under the pointer, a separator, Copy (off with nothing selected), Select All on This Page (off on a page read and found without text). Beside the pages the menu shows only while text is selected, with Copy alone.
- **A PDF or page without text** (a scan): the pointer stays an arrow, a drag pans, a double-click is full screen, Select All selects nothing, Copy is off.

## How a point becomes a character

Qt PDF (6.8) has no call for "the character under this point". It has `QPdfDocument::getAllText(page)` (text and a rectangle per line piece), `getSelectionAtIndex(page, start, length)` (text and rectangles of a range of PDFium characters) and `getSelection(page, start point, end point)`. Each parses the page and takes PDFium's lock, which the page renders hold, so none of them runs on the GUI thread.

- **Text areas** (`PdfPageTextAreas`, read once per page for the pages on screen, `request_pages` from the view's render scheduling): the page's text, PDFium's character count, the line pieces in character order, and the first character that has a box of its own (`base_index`, probed with one-character selections, 64 at most).
- **The caret under a point** (`caret_at`): the point is moved onto the nearest line piece (the piece whose row it is in or nearest to, then the nearest across: `nearest_piece`), to the piece's middle line, so the neighbouring lines are as far as they can be. Then `getSelection(page, middle of the base character, that point)` is asked: its first character is the base character, and its end index is the index of the character under the point, plus one when the point is in the character's second half. That end index is the caret. On the base character itself Qt PDF reports nothing, and the half is told from the character's box. Qt PDF finds a character within 8 points of a point (`CharacterHitTolerance` 16), which the piece's middle line always is. `base_hit` is the character Qt PDF takes the base point for, read once with a selection from there to the end of the last piece (the base character, unless another one's box holds that point too). Qt PDF hands every rectangle out in whole points (`mapPageToView` goes through integer device coordinates), so a caret and a mark are true to about half a point; tests aim just outside a word, never "one point inside" a narrow letter.
- **Indices are PDFium characters throughout**, never offsets into the page's text: the text counts UTF-16 units, and the two differ where PDFium's text leaves a character out or a character lies outside the BMP (see Find in pdf-viewer.md). The selection is two carets (`PdfTextCaret`: page, index); its text and rectangles come from `getSelectionAtIndex` per page (`Piece`), a whole page from its text areas. Qt PDF measures a range's rectangles for as many characters as its text has units, so `make_piece` asks again for more characters while the units fall short of the range.
- **A word** (`answer_word`): the 96 characters around the caret are read in one call and the word found in them; the selection made from that range must return the same text, else (the indices and the units disagreed near by) the word is followed one character at a time.
- **A line** (`answer_line`): carets at the left end of the leftmost piece and the right end of the rightmost piece of the row.
- **Across pages** (`add_range`): the anchor's page from the anchor to its end, the pages between whole, the focus page up to the focus. A page without text has no piece. A pointer on a page without text reaches to the end of the last page with text before it (or the start of the first after it, going back).

## The worker

One task at a time (`launch_async`, the answer posted back), like the search and the links. A question (`Request`: Range, Word, Line, Pages) waits in `pending_`, and a newer one replaces it: a drag's move only records where the pointer is, so a queued run of moves (Wayland delivers each motion) costs nothing, and the selection follows as answers come in. An answer carries its gesture's `epoch_` and is dropped when a click, a word or a clear came after it. Pieces the selection already has are handed to the task and kept when their range is unchanged, so a drag asks PDFium only about the page under the pointer. With no question waiting, the task reads text areas in slices of about 12 ms. `set_document(nullptr)` and the destructor set the task's cancel flag and wait for it.

## What Copy hands out

`pdf_copied_text`: plain text; PDFium's `\r\n` between lines becomes `\n` (Qt writes the platform's line ends to the clipboard); PDFium's mark for a hyphen that ends a line inside a word (U+FFFE, U+0002, with no line break behind it in Qt 6.8's PDFium) becomes `-` and a line break, so "exam-" / "ple" is copied as the page shows it; other control characters go. Pages are joined with a line break. `PdfViewerWindow::copy_text` writes the clipboard (`set_link_handlers` replaces it in tests).

## Drawing

`paint_text_selection`, under the find highlights: each piece's rectangles are mapped from page points through the view's quarter turns and the page's slot on every paint (`page_area_to_content`), so the tint holds through zoom, rotation, layout changes and detail renders with nothing cached. Role `pdf_text_selection` at 35%, blue, the same in Dark and Light because the page is paper in both. One winding-filled path per page.

## Keys

Copy and Select All are window keys like the rest (pdf-viewer.md, Keys): always enabled, so with nothing selected the key does nothing instead of running the main window's Copy, which would put a layer on the clipboard. In a text field (the find field, the page field, the zoom box: `types_its_own_enter`) they are the field's own. Esc with the bar closed is taken in the window's event filter, with its `ShortcutOverride`, while there is a selection or the window is full screen, wherever the keyboard is in the viewer.

## Not built

Selecting with the keyboard, a Select All for the whole document, dragging a selection out as text, lines of text set down the page (a triple-click there takes the piece's row), and reading order other than PDFium's character order (in a page of columns a drag from one column to the next takes the characters between in that order).

## Tests

`tests/ui/pdf_viewer_select_tests.cpp`, on the Find fixture (five small pages, one without text): `ui_pdf_viewer_select_text_by_dragging` (pointer shapes, inside a line, the half-character rule, across lines, backwards, beside and below a line, 31 moves with no answer taken in between, the tint on the words, pans beside the text and with the middle button, a click drops it), `ui_pdf_viewer_select_text_words_lines_and_shift_click`, `ui_pdf_viewer_select_text_copy_keys_and_menu` (Ctrl+C, Ctrl+Insert, Ctrl+A and Esc sent to the viewer's window as the window system does, with the main window used first and every main-window command on those keys counted; the keys in the find field and on the page list; the clipboard; the menu; a spread), `ui_pdf_viewer_select_text_follows_the_page` (the mark on its words at three zooms, in a detail render and at every quarter turn; drags in a turned view, across a spread both reading directions, down into the next page, Shift+click two pages on over a page without text), `ui_pdf_viewer_select_text_copies_plain_text` (`pdf_copied_text`, the page with words broken at line ends), `ui_pdf_viewer_select_text_without_a_text_layer`, `ui_pdf_viewer_select_text_sample_document` (writes the `ui_pdf_viewer_select_text` capture). The link tests cover the page menu on a link and a drag that starts on one.
