# PDF import and export performance

The profiling hooks and measured costs of [pdf.md](pdf.md)'s import and export paths.

`PATCHY_UI_PROFILE=1` prints per-page stage timings to stderr (`ui/ui_profile.hpp`; one write per line, workers log too): `pdf_import.{probe,open_reader,probe_pages,thumbnail,read_file,vector_page,render,raster_fallback,trim,to_document}`, `pdf_pages.pending_text`, `pdf_images.{pass,decode,source_stamp}` (the one image pass, each decode and each per-document fingerprint on its worker), `pdf_merge.{total,place_page,page_source,finish}` (the merge, one page placed, one page's fingerprint on its worker, the depth conversion), `open.{add_session,fit_to_view}`, `pdf_pages.{finish,add_session,pump}`, and `pdf_export.{page,pass_through,composite,encode,write_page,new_page,raster_chunk_flatten,raster_chunk_draw,text_merge,finish}`. `patchy_perf_tests.exe pdfopen [file]` and `pdfsave [file]` time the whole flow (file from the argument, else `PATCHY_PERF_PDF`, else `local-test-fixtures/pdf/C2_Kyoto_House_Plans_Compressed.pdf`; `[SKIP]` when absent; `PATCHY_PERF_PDF_PAGES=<n|all>` caps the pages, default 8; `PATCHY_PERF_PDF_KEEP=1` keeps the written files). `pdfopen` ends with the real open through the Import PDF dialog: `PATCHY_PERF_PDF_TARGET=documents|layers|artboards` and `PATCHY_PERF_PDF_PPI=<n>` set "Pages become" and the resolution (else the stored defaults), `PATCHY_PERF_PDF_GUI_ONLY=1` skips the parts before it, and it reports the size label, every progress dialog that came up (`progress_dialog` lines) and every stretch of 200 ms or more in which the UI thread did not turn (`ui_stall` lines, `longest_stall_ms`). Both run with isolated settings and are never part of the default perf run.

Reference file: 86 MB, 85 pages, each one 3496x5019 8-bit gray JPEG 2000 image (about 1 MB per page), imported at 300 ppi. Per page on a 12900KS (September 2026 baseline):

| Step | Time | Note |
|---|---|---|
| PDFium render (`QPdfDocument::render`) | 490 ms on a P-core, to 1100 ms at below-normal priority | about 85% of the open. Decode-bound: smoothing flags change nothing, half size still costs 290 ms. PDFium is single-threaded behind Qt PDF's global lock, and the PDF path never hands JPX streams to the vendored OpenJPEG ([hdr-formats.md](hdr-formats.md)), so this is the floor of the current in-process path |
| Open the `PageReader` + probe all 85 pages | 2 ms + 3 ms | routes scans straight to the flatten path |
| Thumbnail | 14 ms each | PDFium decodes JPX at reduced resolution for small targets; the dialog shows in about 50 ms |
| `document_from_qimage` + source capture (composite + hash) | 32 ms + 60 ms | on a second worker, hidden behind the next page's render |
| Session add + first paint pump | 6 ms | background sessions; the tab still appears as each is made |
| Qt-engine export: flat lossless / flat JPEG 94 / editable lossless | 2000 ms and 8.9 MB (8.8x the source) / 280 ms and 2.9 MB / 3100 ms and 8.9 MB | what a page routed to Qt costs. Composite is 29 ms; the rest is Qt converting and deflating RGB, plus a 1000 ms single-threaded `flatten_document_rgba8` per raster page in editable mode |
| Image-writer export: `lossless` / `high` (the default, editable on or off) / `medium` / `low` | 420 ms and 4.9 MB / 78 ms and 2.1 MB / 74 ms and 1.3 MB / 71 ms and 0.92 MB | gray one-channel data, 4 encode workers; wall time per page over an 8-page run |
| Export, untouched import (pass-through, the default) | 52 ms per page: all 85 pages in 4.5 s, 86,067,370 bytes against the source's 86,074,801 | every JPX stream byte-identical and in order, `/Rotate 270` and the placement matrix repeated; the time is the composite and its hash |
| Text-merge post-pass | 69 ms per 71 MB | skipped unless a layer went out as text |

Timing caveat: `run-throttled.bat` runs at below-normal priority, and on a hybrid CPU Windows moves sustained background-priority work to the E-cores after about 3 s, so the same render reads 490 ms early in a run and 1100 ms later. Judge import changes by the non-render stages, or by one real open with `PATCHY_UI_PROFILE=1`.

Editable export of a big canvas: each raster chunk composites only its own render area (`flatten_document_rgba8_rect`, the compositor's clip) in horizontal bands of about 4 Mpx on workers (`flatten_area_in_bands`, pdf_export_editable.cpp). Opening an editable PDF decodes and resamples its page images (`render_pending_pdf_image_layers`, one pass over every page of the import) on workers, at most `pdf_page_task_window` at a time (four, half the hardware threads when that is fewer, fewer for huge pages) to bound memory, behind the open's one progress dialog ([pdf-import.md](pdf-import.md), "Progress").

Merged-import reference: 4.9 MB, 8 pages, each one JPEG on a 2092 x 3168 pt page (29 x 44 inches), Editable at 300 ppi as Artboards: eight 8717 x 13200 px pages (115 Mpx each) on one 26351 x 39800 px canvas (1,048 Mpx). `pdfopen` with `PATCHY_PERF_PDF_TARGET=artboards` on an i9-14900HX laptop with 32 GB, below-normal priority, offscreen (November 2026). A desktop at normal priority runs every stage several times faster; the proportions hold.

| Stage | Before | Now |
|---|---|---|
| Page images (decode, bicubic to 115 Mpx) | 8 passes, 67 s, a "1 of 1" dialog each | one pass, 53 s, four at a time (9 s a decode alone, 9 to 31 s side by side: memory-bound) |
| Fingerprint of each page document | 0.8 s x 8 on the UI thread | not taken for a merge (the merge takes its own) |
| Merge: place a page | 1 ms | 0.2 ms |
| Merge: fingerprint a page as its exporter derives it | 17.5 s a page on the UI thread: 143 s frozen | 17 to 22 s a page on four workers: 41 s, progress shown. One page: crop 0.4 s, paper and clip rasters 1.9 s (13 s before the rectangle shortcut), composite 14 to 19 s, hash 0.3 s |
| Session add | 39 s (the canvas composited inside its first paint) | 0.2 s |
| First frame of the 1,048 Mpx canvas | inside those 39 s, then 20 s frozen building display mips | 41 s on a worker after the tab is up; spinner, window responsive |
| Import pressed to tab open / to first frame | 269 s / 269 s | 94 s / 135 s |
| Longest stretch the UI thread did not turn | 142.8 s | under 0.2 s |

Not done, in order of what they would buy on a long scanned PDF:
- **Multi-process rendering.** PDFium's lock is per process, so N render-worker `patchy.exe` processes would cut the 85-page open from about 45 s to under 10 s here. Needs an early `main()` branch ahead of single-instance forwarding, a password channel off the command line, crash/cancel handling, and an in-process fallback.
- **Lazy page documents.** Decode a page's pixels the first time it is shown or edited (pass-through never needs untouched pixels); every reader of a document (thumbnails, scripts, commands) would have to cope with a pixel-less session.
- **Pass-through under edits.** With the imported layer unchanged and everything above it Normal, keep the original image and add the layers above as one cropped `/SMask` image.
- **A cheaper fingerprint for a merged artboard page.** A scan with soft edges keeps its artboard's paper, so the page its exporter derives is a group with a clip mask over a paper layer and the scan, and compositing that is 14 of the 17 s above (a scan opaque to its edges unwraps to one layer: 0.8 s). When the artboard's rect is the whole page the clip is the canvas itself: unwrapping to paper plus children would composite severalfold faster, but changes the layer structure `document_for_artboard` hands every exporter (Artboards to Files, print, editable PDF).
- **A lighter first frame for a huge canvas.** The view keeps a full-resolution RGBA frame (4 GB at 1,048 Mpx) and its mips; composing them is the 41 s above. A view-resolution frame first, or a tiled cache, would show the canvas sooner and hold far less.
- **Flate, CCITT, and JBIG2 pass-through** needs `File` to return a stream decrypted but still filtered (`stream_data` decodes everything except the image codecs). An image-stream replacement post-pass would give Qt-engine pages the same codecs.
