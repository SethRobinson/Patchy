#pragma once

#include "ocr/ocr_engine.hpp"
#include "ui/book_scan_project.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace patchy::ui {

// Writes a book project as an image-page PDF (docs/book-scan.md): every page the same
// size (resolved_page_size), in book_page_order, through pdf::ImageWriter, so the
// destination is replaced only when the whole file is written. Color and Grayscale pages
// are JPEG at the project's quality preset (lossless is Flate), Black and White pages
// one bit per pixel, Flate-compressed. Title and Author go into the PDF's /Info.

// Called on the calling thread after each page is written (and, while a page's text is
// being recognized, every few milliseconds with the same numbers, so the caller's window
// stays alive and its Cancel is seen); false cancels.
using BookPdfProgress = std::function<bool(int pages_done, int page_count)>;

// A searchable book PDF: with an engine, every page that is not blank is read once, as it
// was cut and before its output conversion (black and white included), and its lines are
// written invisibly over the page image, one run per line (docs/pdf-searchable-text.md).
// The engine and language are the user's picks, passed in; the pages themselves, their
// sizes and their image data are what they are without it. Without an engine: images only.
//
// Binding rule (the "Book scanning" and "Text recognition" entries of
// docs/legal-constraints-scanning.md): recognition runs only here, at export, when the user
// asked for it; what it finds becomes PDF text and nothing else. It never feeds page
// detection, cutting, straightening or tone choices, and nothing is kept in the project.
struct BookPdfRecognition {
  std::shared_ptr<ocr::Engine> engine;
  std::string language;
};

struct BookPdfResult {
  bool ok{false};
  bool cancelled{false};
  QString error;
  int pages{0};
};

[[nodiscard]] BookPdfResult write_book_pdf(const BookProject& project, const QString& path,
                                           const BookPdfProgress& progress = {},
                                           const BookPdfRecognition& recognition = {});

// One-bit DeviceGray samples (1 = white, rows padded to whole bytes) from a Gray8 page
// that holds only 0 and 255 (apply_book_output's Black and White result).
[[nodiscard]] std::vector<std::uint8_t> pack_one_bit_rows(const PixelBuffer& page);

}  // namespace patchy::ui
