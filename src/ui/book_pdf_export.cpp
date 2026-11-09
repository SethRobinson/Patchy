#include "ui/book_pdf_export.hpp"

#include "core/worker_budget.hpp"
#include "formats/miniz/miniz.h"
#include "formats/pdf_image_writer.hpp"
#include "ui/background_workers.hpp"
#include "ui/pdf_export.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>

#include <deque>
#include <future>
#include <stdexcept>
#include <string>

namespace patchy::ui {

namespace {

constexpr int kMaxEncodeWorkers = 4;

std::vector<std::uint8_t> flate(const std::vector<std::uint8_t>& samples) {
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(samples.size()));
  std::vector<std::uint8_t> compressed(bound);
  if (mz_compress2(compressed.data(), &bound, samples.data(), static_cast<mz_ulong>(samples.size()), MZ_DEFAULT_LEVEL) !=
      MZ_OK) {
    throw std::runtime_error("compression failed");
  }
  compressed.resize(bound);
  return compressed;
}

pdf::ImagePage encode_book_page(const BookProject& project, const BookPageRef& ref, const BookPageSize& size,
                                const PdfExportOptions& options, BookScanCache& cache) {
  const PixelBuffer page = render_book_page(project, ref, size, project.output_dpi, cache);
  if (page.empty()) {
    throw std::runtime_error(QCoreApplication::translate("BookPdfExport", "A scan of this book could not be read.").toStdString());
  }
  const double width_points = size.width * 72.0;
  const double height_points = size.height * 72.0;
  if (page_output_options(project, ref).mode == BookOutputMode::BlackAndWhite) {
    pdf::ImagePage encoded;
    encoded.width_points = width_points;
    encoded.height_points = height_points;
    encoded.image.width = page.width();
    encoded.image.height = page.height();
    encoded.image.bits_per_component = 1;
    encoded.image.color_space = "/DeviceGray";
    encoded.image.filter = "FlateDecode";
    encoded.image.bytes = flate(pack_one_bit_rows(page));
    return encoded;
  }
  return pdf_detail::encode_page_image(book_image_from_pixels(page), width_points, height_points, options);
}

}  // namespace

std::vector<std::uint8_t> pack_one_bit_rows(const PixelBuffer& page) {
  const auto row_bytes = static_cast<std::size_t>((page.width() + 7) / 8);
  std::vector<std::uint8_t> packed(row_bytes * static_cast<std::size_t>(page.height()), 0);
  for (std::int32_t y = 0; y < page.height(); ++y) {
    const auto* row = page.row(y).data();
    auto* out = packed.data() + row_bytes * static_cast<std::size_t>(y);
    for (std::int32_t x = 0; x < page.width(); ++x) {
      if (row[x] >= 128) {
        out[x / 8] = static_cast<std::uint8_t>(out[x / 8] | (0x80U >> (x % 8)));
      }
    }
  }
  return packed;
}

BookPdfResult write_book_pdf(const BookProject& project, const QString& path, const BookPdfProgress& progress) {
  BookPdfResult result;
  const std::vector<BookPageRef> pages = book_page_order(project);
  if (pages.empty()) {
    result.error = QCoreApplication::translate("BookPdfExport", "The book has no pages to export.");
    return result;
  }
  const BookPageSize size = resolved_page_size(project);
  PdfExportOptions options;
  options.auto_grayscale = true;
  if (!apply_pdf_image_quality(project.quality_id, options)) {
    apply_pdf_image_quality(QString::fromLatin1(kDefaultPdfImageQualityId), options);
  }
  BookScanCache cache(project);
  try {
    pdf::ImageWriter writer(to_filesystem_path(path));
    std::vector<std::pair<std::string, std::string>> info;
    info.emplace_back("Title", project.title.toStdString());
    info.emplace_back("Author", project.author.toStdString());
    writer.set_document_info(std::move(info));
    const int workers = kBackgroundWorkRunsInline ? 1 : std::max(1, max_blocking_fanout_workers(kMaxEncodeWorkers));
    std::deque<std::future<pdf::ImagePage>> in_flight;
    std::size_t next = 0;
    int written = 0;
    const int count = static_cast<int>(pages.size());
    while (written < count) {
      while (next < pages.size() && static_cast<int>(in_flight.size()) < workers) {
        const BookPageRef ref = pages[next++];
        in_flight.push_back(launch_async(
            [&project, ref, size, options, &cache]() { return encode_book_page(project, ref, size, options, cache); }));
      }
      pdf::ImagePage page = in_flight.front().get();
      in_flight.pop_front();
      writer.add_page(page);
      ++written;
      if (progress && !progress(written, count)) {
        for (auto& pending : in_flight) {
          pending.wait();
        }
        writer.abort();
        result.cancelled = true;
        return result;
      }
    }
    writer.finish();
    result.ok = true;
    result.pages = count;
  } catch (const std::exception& error) {
    result.error = QString::fromStdString(error.what());
  }
  return result;
}

}  // namespace patchy::ui
