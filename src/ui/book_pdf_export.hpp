#pragma once

#include "ui/book_scan_project.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy::ui {

// Writes a book project as an image-page PDF (docs/book-scan.md): every page the same
// size (resolved_page_size), in book_page_order, through pdf::ImageWriter, so the
// destination is replaced only when the whole file is written. Color and Grayscale pages
// are JPEG at the project's quality preset (lossless is Flate), Black and White pages
// one bit per pixel, Flate-compressed. Title and Author go into the PDF's /Info.

// Called on the calling thread after each page is written; false cancels.
using BookPdfProgress = std::function<bool(int pages_done, int page_count)>;

struct BookPdfResult {
  bool ok{false};
  bool cancelled{false};
  QString error;
  int pages{0};
};

[[nodiscard]] BookPdfResult write_book_pdf(const BookProject& project, const QString& path,
                                           const BookPdfProgress& progress = {});

// One-bit DeviceGray samples (1 = white, rows padded to whole bytes) from a Gray8 page
// that holds only 0 and 255 (apply_book_output's Black and White result).
[[nodiscard]] std::vector<std::uint8_t> pack_one_bit_rows(const PixelBuffer& page);

}  // namespace patchy::ui
