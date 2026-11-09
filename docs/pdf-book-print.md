# Print as Book

The PDF viewer's Print as Book... (`pdfViewerPrintBookButton`, Ctrl+P in the viewer; [pdf-viewer.md](pdf-viewer.md)) imposes the PDF's pages on landscape sheets so the printed stack folds, cuts or stacks into a book in reading order, previews every sheet, lists assembly steps, then prints or saves a PDF of the sheets. Desktop builds with Qt PDF only.

## Files

- `src/core/book_imposition.{hpp,cpp}` (Qt-free): `impose_book` (sheets, sides, cells, upside-down panels, signatures, added blanks), `book_binding_edge`, `place_book_page`. Tests: `book_imposition_*` (patchy_core_tests).
- `src/ui/pdf_book_print_dialog.{hpp,cpp}`: options and settings, `render_book_sheet_side`, `book_assembly_steps`, `write_pdf_book`, the dialog (`pdfBookPrintDialog`, through `exec_dialog`) and printing.

## Imposition rules

- Positions are book positions (0 = front cover), mapped to PDF pages through `book_pages` (a page range picks the PDF pages; -1 is an added blank). A side is described as seen after turning the sheet over its vertical axis: what a duplex printer set to flip on the short edge produces.
- Blanks fill the last sheet at the end, or, with "Keep the last page as the back cover" (`pdfBookPrintBackCoverCheck`, on by default when the viewer showed Cover Alone), before the last page.
- **Booklet** (saddle stitch, `booklet`): all sheets nested, pages padded to a multiple of 4; sheet s has `[P-1-2s | 2s]` on the front and `[2s+1 | P-2-2s]` on the back (8 pages: [8|1][2|7], [6|3][4|5], one-based). Right to left mirrors each side.
- **Sewn book** (`signatures`): the same nesting per signature of 1 to 16 sheets (`pdfBookPrintSignatureSheetsSpin`, default 4 = 16 pages); the last signature takes what is left (padding only to a multiple of 4).
- **Cut and stack** (`cut-stack`): leaf n carries pages 2n and 2n+1; the left halves hold the first half of the leaves, the right halves the second, so cutting the stack and putting the right pile under the left reads in order.
- **Mini zine** (`zine`): one-sided, 4 x 2 cells per sheet, 8 pages per sheet (each sheet its own zine). Top row upside down `5 4 3 2`, bottom row `6 7 back front` (one-based), mirrored for right to left. Derived from the fold (long fold printing outside, centre cut through the middle two panels, ends pushed together) and pinned by `book_imposition_mini_zine_layout`.
- **Placement** (`place_book_page`): fit (default) or fill (crops, centred) into the cell less margins: outer margin on top, bottom and the outer edge (default 5 mm), spine margin on the binding edge (default 0, so spreads meet at the fold). Fitted pages sit against the binding edge, leftover space at the outer edge. The binding edge of an even position (a right-hand page in left-to-right) is its left. Creep (booklet and sewn styles, default on): sheet k of its signature (0 = outermost) moves toward the fold by k times the paper thickness (default 0.1 mm). The clip never leaves the cell.
- **Marks** (`pdfBookPrintMarksCheck`, default on), first sides only, ticks inside the outer margin (never over a page; none under 2 pt of margin): dashed fold ticks at the sheet centre (folded styles) and at the zine's panel lines; solid cut ticks (cut and stack); the zine's solid cut line; for sewn books a 3 x 6 mm collation block straddling the fold of each signature's outer sheet from the second signature on, one step lower per signature, so the folded stack shows a staircase down the spine.

## Output

- Sheets render at 150, 300 or 600 dpi (`pdfBookPrintResolutionCombo`) through `QPdfDocument::render` per page into an opaque sheet image, on a worker, the UI pumping events (`wait_with_events`). "Backs print upside down" (`pdfBookPrintTurnBacksCheck`) turns every back 180 degrees for printers that flip on the long edge; the preview never shows it turned.
- **Save PDF...** (`write_pdf_book`): front, back, front, back ... (fronts only for the zine), each page the landscape sheet, through `pdf::ImageWriter` and `pdf_detail::encode_page_image` at the chosen image-quality preset (`pdfBookPrintQualityCombo`); cancelling or failing keeps an existing file. Default name `<stem> (book).pdf` beside the source.
- **Print...**: `QPrinter` (high resolution, full page, landscape, the chosen sheet, zero margins, `DuplexShortSide` unless one-sided or by hand) and the system print dialog without a page range. "Print the second side by hand" (`pdfBookPrintManualDuplexCheck`) prints all first sides as one job, asks the user to reload the stack (`pdfBookPrintReloadMessageBox`), then prints the second sides, optionally last sheet first (`pdfBookPrintReverseBacksCheck`).
- "Add a page with the assembly steps" (`pdfBookPrintStepsPageCheck`) appends the steps as one more page.
- Papers: Letter, Legal, Tabloid, A4, A3, A5, B5 (names from Qt, stored as `QPageSize::key`); the first default follows the locale's measurement system.

## Assembly steps

`book_assembly_steps` writes them with the real numbers: sheet and signature counts, the pages sheet 1 must show (the check that catches a long-edge flip), the binding edge, added blanks. Content per style: print (automatic or by hand, with a one-sheet orientation test), check sheet 1, then booklet (stack cover side down with sheet 1 at the bottom, fold the stack at once, staple through the fold or sew three holes, trim, about 12 sheets at most), sewn book (split into signatures, fold each, stack, check the collation staircase, sew or glue with PVA and a cover, trim), cut and stack (cut, right pile under left, glue, comb, coil or staples, 10 mm spine margin for comb or staples), mini zine (crease eight panels, cut the middle, push into a cross, fold).

## Settings (permanent keys and tokens)

`pdfBookPrint/style` (`booklet`/`signatures`/`cut-stack`/`zine`), `sheetsPerSignature`, `paper`, `backCover`, `scaling` (`fit`/`fill`), `outerMarginMm`, `spineMarginMm`, `creep`, `paperThicknessMm`, `marks`, `turnBacks`, `manualDuplex`, `reverseBacks`, `dpi`, `stepsPage`, `quality`. Reading direction comes from the viewer, never stored here.

## Tests

Core: `book_imposition_*`. UI: `ui_pdf_book_print_imposes_sheets` (booklet sides, back cover, turned backs, right to left, page range, zine cells and which way up, step text), `ui_pdf_book_print_writes_sheets_pdf` (page count and size, steps page, progress, cancel keeps the file, Unicode paths), `ui_pdf_book_print_dialog_follows_choices`. Real printing (the system dialog, duplex drivers, the by-hand flow) is checked by hand.
