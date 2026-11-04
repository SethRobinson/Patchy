# PDF import and export performance

The profiling hooks and measured costs of [pdf.md](pdf.md)'s import and export paths.

`PATCHY_UI_PROFILE=1` prints per-page stage timings to stderr (`ui/ui_profile.hpp`): `pdf_import.{probe,open_reader,probe_pages,thumbnail,read_file,vector_page,render,raster_fallback,trim,to_document}`, `open.{add_session,fit_to_view}`, `pdf_pages.{pending_layers,add_session,pump}`, and `pdf_export.{page,pass_through,composite,encode,write_page,new_page,raster_chunk_flatten,raster_chunk_draw,text_merge,finish}`. `patchy_perf_tests.exe pdfopen [file]` and `pdfsave [file]` time the whole flow (file from the argument, else `PATCHY_PERF_PDF`, else `local-test-fixtures/pdf/C2_Kyoto_House_Plans_Compressed.pdf`; `[SKIP]` when absent; `PATCHY_PERF_PDF_PAGES=<n|all>` caps the pages, default 8; `PATCHY_PERF_PDF_KEEP=1` keeps the written files). Both run with isolated settings and are never part of the default perf run.

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

Editable export of a big canvas: each raster chunk composites only its own render area (`flatten_document_rgba8_rect`, the compositor's clip) in horizontal bands of about 4 Mpx on workers (`flatten_area_in_bands`, pdf_export_editable.cpp). Opening an editable PDF decodes and resamples its page images (`render_pending_pdf_image_layers`) on workers, at most four at a time (half the hardware threads when that is fewer) to bound memory, behind `pdfPageImagesProgressDialog` once the wait passes 400 ms.

Not done, in order of what they would buy on a long scanned PDF:
- **Multi-process rendering.** PDFium's lock is per process, so N render-worker `patchy.exe` processes would cut the 85-page open from about 45 s to under 10 s here. Needs an early `main()` branch ahead of single-instance forwarding, a password channel off the command line, crash/cancel handling, and an in-process fallback.
- **Lazy page documents.** Decode a page's pixels the first time it is shown or edited (pass-through never needs untouched pixels); every reader of a document (thumbnails, scripts, commands) would have to cope with a pixel-less session.
- **Pass-through under edits.** With the imported layer unchanged and everything above it Normal, keep the original image and add the layers above as one cropped `/SMask` image.
- **Flate, CCITT, and JBIG2 pass-through** needs `File` to return a stream decrypted but still filtered (`stream_data` decodes everything except the image codecs). An image-stream replacement post-pass would give Qt-engine pages the same codecs.
