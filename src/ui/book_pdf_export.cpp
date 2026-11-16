#include "ui/book_pdf_export.hpp"

#include "core/worker_budget.hpp"
#include "formats/miniz/miniz.h"
#include "formats/pdf_image_writer.hpp"
#include "ui/background_workers.hpp"
#include "ui/pdf_export.hpp"
#include "ui/pdf_recognized_text.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QSize>
#include <QSizeF>

#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
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

// One recognizer for the whole export. It serves one caller at a time, so the encode
// workers take turns at it; Cancel reaches the page being read.
struct PageReader {
  std::unique_ptr<ocr::Recognizer> recognizer;
  std::mutex mutex;
  ocr::Cancel cancel;
};

// The page's text as invisible lines. `cut` is the page as it was cut from its scan, before
// the output conversion: an engine reads the scan's own tones, not a thresholded copy.
std::vector<pdf::TextLine> read_book_page(PageReader& reader, const PixelBuffer& cut, double width_points,
                                          double height_points) {
  const int channels = cut.format().channels;
  if (cut.format().bit_depth != BitDepth::UInt8 || (channels != 1 && channels != 3 && channels != 4)) {
    return {};
  }
  ocr::ImageView view;
  view.data = cut.data().data();
  view.width = cut.width();
  view.height = cut.height();
  view.stride = static_cast<std::ptrdiff_t>(cut.stride_bytes());
  view.layout = channels == 1 ? ocr::PixelLayout::Gray8 : channels == 3 ? ocr::PixelLayout::Rgb8 : ocr::PixelLayout::Rgba8;
  ocr::Result result;
  try {
    const std::lock_guard lock(reader.mutex);
    result = reader.recognizer->recognize(view, &reader.cancel);
  } catch (const ocr::RecognitionCancelled&) {
    throw;
  } catch (const std::exception& error) {
    throw std::runtime_error(QCoreApplication::translate("BookPdfExport", "The text on a page could not be recognized: %1")
                                 .arg(QString::fromUtf8(error.what()))
                                 .toStdString());
  }
  return pdf_text_lines(result, QSize(cut.width(), cut.height()), QSizeF(width_points, height_points));
}

pdf::ImagePage encode_book_page(const BookProject& project, const BookPageRef& ref, const BookPageSize& size,
                                const PdfExportOptions& options, BookScanCache& cache, PageReader* reader) {
  const double width_points = size.width * 72.0;
  const double height_points = size.height * 72.0;
  // A page that is read is cut first, read, and only then converted for output; the image
  // that is written is the one an export without recognition writes.
  const bool read = reader != nullptr && ref.kind != BookPageRef::Kind::Blank;
  PixelBuffer page = render_book_page(project, ref, size, project.output_dpi, cache, /*apply_output*/ !read);
  if (page.empty()) {
    throw std::runtime_error(QCoreApplication::translate("BookPdfExport", "A scan of this book could not be read.").toStdString());
  }
  std::vector<pdf::TextLine> text;
  if (read) {
    text = read_book_page(*reader, page, width_points, height_points);
    page = apply_book_output(page, page_output_options(project, ref));
  }
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
    encoded.text = std::move(text);
    return encoded;
  }
  auto encoded = pdf_detail::encode_page_image(book_image_from_pixels(page), width_points, height_points, options);
  encoded.text = std::move(text);
  return encoded;
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

BookPdfResult write_book_pdf(const BookProject& project, const QString& path, const BookPdfProgress& progress,
                             const BookPdfRecognition& recognition) {
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
  std::unique_ptr<PageReader> reader;
  if (recognition.engine != nullptr) {
    reader = std::make_unique<PageReader>();
    try {
      // Opened off this thread, as every engine call is (docs/ocr.md).
      reader->recognizer =
          launch_async([&recognition] { return recognition.engine->open(recognition.language); }).get();
    } catch (const std::exception& error) {
      result.error = QCoreApplication::translate("BookPdfExport", "Text recognition could not start: %1")
                         .arg(QString::fromUtf8(error.what()));
      return result;
    }
  }
  try {
    pdf::ImageWriter writer(to_filesystem_path(path));
    std::vector<std::pair<std::string, std::string>> info;
    info.emplace_back("Title", project.title.toStdString());
    info.emplace_back("Author", project.author.toStdString());
    writer.set_document_info(std::move(info));
    const int workers = kBackgroundWorkRunsInline ? 1 : std::max(1, max_blocking_fanout_workers(kMaxEncodeWorkers));
    std::deque<std::future<pdf::ImagePage>> in_flight;
    // Declared after the queue, so it runs first on the way out: pages still being read
    // when an error or a cancel ends the export stop at once instead of reading to the end.
    struct StopReading {
      PageReader* reader;
      ~StopReading() {
        if (reader != nullptr) {
          reader->cancel.request();
        }
      }
    } stop_reading{reader.get()};
    const auto cancel_export = [&] {
      if (reader != nullptr) {
        reader->cancel.request();
      }
      for (auto& pending : in_flight) {
        pending.wait();
      }
      writer.abort();
      result.cancelled = true;
    };
    std::size_t next = 0;
    int written = 0;
    const int count = static_cast<int>(pages.size());
    while (written < count) {
      while (next < pages.size() && static_cast<int>(in_flight.size()) < workers) {
        const BookPageRef ref = pages[next++];
        in_flight.push_back(launch_async([&project, ref, size, options, &cache, page_reader = reader.get()]() {
          return encode_book_page(project, ref, size, options, cache, page_reader);
        }));
      }
      if (reader != nullptr && progress) {
        // Reading a page takes seconds: keep the caller's window alive and its Cancel live.
        bool keep_going = true;
        while (keep_going && in_flight.front().wait_for(std::chrono::milliseconds(15)) == std::future_status::timeout) {
          keep_going = progress(written, count);
        }
        if (!keep_going) {
          cancel_export();
          return result;
        }
      }
      pdf::ImagePage page = in_flight.front().get();
      in_flight.pop_front();
      writer.add_page(page);
      ++written;
      if (progress && !progress(written, count)) {
        cancel_export();
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
