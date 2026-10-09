#include "ui/pdf_export.hpp"

#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"
#include "core/worker_budget.hpp"
#include "formats/miniz/miniz.h"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/image_document_io.hpp"
#include "ui/print_internal.hpp"
#include "ui/qt_paths.hpp"
#include "ui/ui_profile.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRect>
#include <QSizeF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

constexpr double kPointsPerInch = 72.0;
// PDF 1.7 (ISO 32000-1, Annex C) caps a page at 14400 units, i.e. 200 inches. A very
// large document at a low PPI would exceed that, so the page shrinks to the cap and the
// image simply prints smaller; the pixels are untouched either way.
constexpr double kMaxPagePoints = 14400.0;

}  // namespace

namespace pdf_detail {

QPageSize document_page_size(const Document& document) {
  const double horizontal_ppi = print_detail::document_horizontal_ppi(document);
  const double vertical_ppi = print_detail::document_vertical_ppi(document);
  double page_width_points = document.width() / horizontal_ppi * kPointsPerInch;
  double page_height_points = document.height() / vertical_ppi * kPointsPerInch;
  if (const double longest = std::max(page_width_points, page_height_points); longest > kMaxPagePoints) {
    const double fit = kMaxPagePoints / longest;
    page_width_points *= fit;
    page_height_points *= fit;
  }
  // QPageSize defaults to FuzzyMatch, which would snap a nearly-Letter page to Letter and
  // change the document's physical size. Exact sizes only.
  return QPageSize(QSizeF(std::max(page_width_points, 1.0), std::max(page_height_points, 1.0)), QPageSize::Point,
                   QString(), QPageSize::ExactMatch);
}

void configure_document_page(QPdfWriter& writer, const Document& document) {
  const double horizontal_ppi = print_detail::document_horizontal_ppi(document);
  writer.setCreator(QStringLiteral("Patchy"));
  writer.setPageSize(document_page_size(document));
  writer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0));
  // The device resolution only sets the painter's logical grid; keeping it at the
  // document's own PPI makes that grid one unit per document pixel.
  writer.setResolution(std::clamp(static_cast<int>(std::lround(horizontal_ppi)), 72, 2400));
}

}  // namespace pdf_detail

namespace {

// Walks the visible tree: `leaves` counts visible non-group layers, `plain` stays true
// while every one of them is a pixel layer an image represents completely.
void count_visible_leaves(const std::vector<Layer>& layers, int& leaves, bool& plain) {
  for (const auto& layer : layers) {
    if (!layer.visible()) {
      continue;
    }
    if (layer.kind() == LayerKind::Group) {
      count_visible_leaves(layer.children(), leaves, plain);
      continue;
    }
    ++leaves;
    if (layer.kind() != LayerKind::Pixel || layer_is_vector_shape(layer) || layer_is_text(layer)) {
      plain = false;
    }
  }
}

// PNG "Up" prediction (every row carries filter type 2) ahead of Flate: scans and flat
// artwork both repeat vertically, and readers already need it for ordinary PDFs.
std::vector<std::uint8_t> flate_with_up_predictor(const std::vector<std::uint8_t>& samples, int width, int height,
                                                  int channels) {
  const auto stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
  std::vector<std::uint8_t> filtered((stride + 1U) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const auto* row = samples.data() + stride * static_cast<std::size_t>(y);
    auto* out = filtered.data() + (stride + 1U) * static_cast<std::size_t>(y);
    *out++ = 2;
    if (y == 0) {
      std::copy(row, row + stride, out);
      continue;
    }
    const auto* above = row - stride;
    for (std::size_t x = 0; x < stride; ++x) {
      out[x] = static_cast<std::uint8_t>(row[x] - above[x]);
    }
  }
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(filtered.size()));
  std::vector<std::uint8_t> compressed(bound);
  if (mz_compress2(compressed.data(), &bound, filtered.data(), static_cast<mz_ulong>(filtered.size()),
                   MZ_DEFAULT_LEVEL) != MZ_OK) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }
  compressed.resize(bound);
  return compressed;
}

std::string up_predictor_parms(int width, int channels) {
  return "<< /Predictor 12 /Colors " + std::to_string(channels) + " /BitsPerComponent 8 /Columns " +
         std::to_string(width) + " >>";
}

std::vector<std::uint8_t> jpeg_bytes(const QImage& image, int quality) {
  QByteArray encoded;
  QBuffer buffer(&encoded);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "JPEG");
  writer.setQuality(std::clamp(quality, 1, 100));
  writer.setOptimizedWrite(true);
  if (!writer.write(image)) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }
  const auto* data = reinterpret_cast<const std::uint8_t*>(encoded.constData());
  return std::vector<std::uint8_t>(data, data + encoded.size());
}

QSizeF document_page_points(const Document& document) {
  return pdf_detail::document_page_size(document).size(QPageSize::Point);
}

// Flat pages, or editable pages that could only ever be one image: composite on the
// calling thread (the compositor fans out over strips already), encode on workers a few
// pages deep, write in page order.
bool write_image_pages(std::span<const Document* const> pages, const QString& path, const PdfExportOptions& options,
                       const PdfPageProgress& progress) {
  pdf::ImageWriter writer(to_filesystem_path(path));
  const int workers = kBackgroundWorkRunsInline ? 1 : max_blocking_fanout_workers(hardware_worker_threads());
  const std::size_t window = workers >= 2 ? static_cast<std::size_t>(std::min(workers, 4)) : 1U;
  std::deque<std::future<pdf::ImagePage>> pending;
  const auto write_oldest = [&] {
    auto page = pending.front().get();
    pending.pop_front();
    const UiProfileScope profile_scope("pdf_export.write_page");
    writer.add_page(page);
  };
  for (std::size_t index = 0; index < pages.size(); ++index) {
    const Document& document = *pages[index];
    const std::string profile_detail = "page=" + std::to_string(index + 1);
    if (progress && !progress(static_cast<int>(index) + 1, static_cast<int>(pages.size()))) {
      pending.clear();  // each future joins its worker
      writer.abort();
      return false;
    }
    QImage image;
    {
      const UiProfileScope profile_scope("pdf_export.composite", profile_detail);
      image = flat_export_qimage(document, true);
    }
    if (image.isNull()) {
      throw std::runtime_error("The document could not be rendered for PDF export.");
    }
    if (options.keep_original_image_data && document_matches_pdf_source(document, image)) {
      // Nothing visible changed since this page came out of a PDF: its own bytes go
      // back, in order behind whatever is still encoding.
      const UiProfileScope profile_scope("pdf_export.pass_through", profile_detail);
      std::promise<pdf::ImagePage> ready;
      ready.set_value(pdf_detail::source_image_page(*document.metadata().pdf_source_page));
      pending.push_back(ready.get_future());
      if (pending.size() >= window) {
        write_oldest();
      }
      continue;
    }
    const QSizeF points = document_page_points(document);
    if (window < 2U) {
      const UiProfileScope profile_scope("pdf_export.encode", profile_detail);
      writer.add_page(pdf_detail::encode_page_image(image, points.width(), points.height(), options));
      continue;
    }
    pending.push_back(launch_async([image = std::move(image), points, options] {
      const UiProfileScope profile_scope("pdf_export.encode");
      return pdf_detail::encode_page_image(image, points.width(), points.height(), options);
    }));
    if (pending.size() >= window) {
      write_oldest();
    }
  }
  while (!pending.empty()) {
    write_oldest();
  }
  const UiProfileScope profile_scope("pdf_export.finish");
  writer.finish();
  return true;
}

}  // namespace

std::span<const PdfImageQualityPreset> pdf_image_quality_presets() {
  static constexpr std::array<PdfImageQualityPreset, 4> kPresets{{
      {"lossless", true, 90},
      {"high", false, 90},
      {"medium", false, 75},
      {"low", false, 50},
  }};
  return kPresets;
}

bool apply_pdf_image_quality(const QString& id, PdfExportOptions& options) {
  for (const auto& preset : pdf_image_quality_presets()) {
    if (id == QLatin1String(preset.id)) {
      options.lossless = preset.lossless;
      options.jpeg_quality = preset.jpeg_quality;
      return true;
    }
  }
  return false;
}

QString pdf_image_quality_id(bool lossless, int jpeg_quality) {
  if (lossless) {
    return QStringLiteral("lossless");
  }
  const PdfImageQualityPreset* nearest = nullptr;
  for (const auto& preset : pdf_image_quality_presets()) {
    if (preset.lossless) {
      continue;
    }
    if (nearest == nullptr ||
        std::abs(preset.jpeg_quality - jpeg_quality) < std::abs(nearest->jpeg_quality - jpeg_quality)) {
      nearest = &preset;
    }
  }
  return QLatin1String(nearest->id);
}

QString stored_pdf_image_quality_id() {
  const auto settings = app_settings();
  const auto stored = settings.value(QStringLiteral("saveOptions/pdfImageQuality")).toString();
  PdfExportOptions probe;
  if (apply_pdf_image_quality(stored, probe)) {
    return stored;
  }
  return QLatin1String(kDefaultPdfImageQualityId);
}

void store_pdf_image_quality_id(const QString& id) {
  PdfExportOptions probe;
  if (!apply_pdf_image_quality(id, probe)) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QStringLiteral("saveOptions/pdfImageQuality"), id);
  // Builds before the presets read only this bool.
  settings.setValue(QStringLiteral("saveOptions/pdfLossless"), probe.lossless);
}

std::uint64_t pdf_composite_hash(const QImage& composite) {
  const QImage rgba =
      composite.format() == QImage::Format_RGBA8888 ? composite : composite.convertToFormat(QImage::Format_RGBA8888);
  const std::uint64_t seed =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(rgba.width())) << 32U) |
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(rgba.height()));
  const auto row_bytes = static_cast<std::size_t>(rgba.width()) * 4U;
  if (static_cast<std::size_t>(rgba.bytesPerLine()) == row_bytes) {
    return hash_pixel_bytes({rgba.constBits(), row_bytes * static_cast<std::size_t>(rgba.height())}, seed);
  }
  std::uint64_t hash = seed;  // padded rows: chain the rows so the padding never counts
  for (int y = 0; y < rgba.height(); ++y) {
    hash = hash_pixel_bytes({rgba.constScanLine(y), row_bytes}, hash);
  }
  return hash;
}

void attach_pdf_source_page(Document& document, const std::shared_ptr<const PdfSourcePage>& source) {
  if (source == nullptr || source->image_bytes == nullptr || source->image_bytes->empty()) {
    return;
  }
  const QImage composite = flat_export_qimage(document, true);
  if (composite.isNull()) {
    return;
  }
  auto stamped = std::make_shared<PdfSourcePage>(*source);  // the byte payloads stay shared
  stamped->document_width = document.width();
  stamped->document_height = document.height();
  stamped->horizontal_ppi = document.print_settings().horizontal_ppi;
  stamped->vertical_ppi = document.print_settings().vertical_ppi;
  stamped->composite_hash = pdf_composite_hash(composite);
  stamped->composite_hash_valid = true;
  document.metadata().pdf_source_page = std::move(stamped);
}

bool document_matches_pdf_source(const Document& document, const QImage& composite) {
  const auto& source = document.metadata().pdf_source_page;
  if (source == nullptr || !source->composite_hash_valid || source->image_bytes == nullptr ||
      source->image_bytes->empty()) {
    return false;
  }
  // A changed resolution changes the page the pixels stand for even when they do not change.
  if (document.width() != source->document_width || document.height() != source->document_height ||
      document.print_settings().horizontal_ppi != source->horizontal_ppi ||
      document.print_settings().vertical_ppi != source->vertical_ppi) {
    return false;
  }
  return pdf_composite_hash(composite) == source->composite_hash;
}

namespace pdf_detail {

pdf::ImagePage source_image_page(const PdfSourcePage& source) {
  pdf::ImagePage page;
  page.width_points = source.page_width_points;
  page.height_points = source.page_height_points;
  page.rotate = source.rotate;
  page.image_matrix = source.image_matrix;
  page.image.bytes = *source.image_bytes;
  page.image.filter = source.filter;
  page.image.color_space = source.icc_profile != nullptr ? std::string() : source.color_space;
  page.image.decode = source.decode;
  page.image.bits_per_component = source.bits_per_component;
  page.image.width = source.image_width;
  page.image.height = source.image_height;
  page.image.icc_profile = source.icc_profile;
  page.image.icc_components = source.icc_components;
  return page;
}

bool document_is_single_raster_layer(const Document& document) {
  int leaves = 0;
  bool plain = true;
  count_visible_leaves(document.layers(), leaves, plain);
  return leaves == 1 && plain;
}

pdf::ImagePage encode_page_image(const QImage& composite, double width_points, double height_points,
                                 const PdfExportOptions& options) {
  const QImage rgba =
      composite.format() == QImage::Format_RGBA8888 ? composite : composite.convertToFormat(QImage::Format_RGBA8888);
  const int width = rgba.width();
  const int height = rgba.height();
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }
  // One pass decides both questions. A fully transparent pixel has no colour to speak
  // of, so it never makes a gray page RGB.
  bool gray = options.auto_grayscale;
  bool opaque = true;
  for (int y = 0; y < height && (gray || opaque); ++y) {
    const auto* row = rgba.constScanLine(y);
    for (int x = 0; x < width; ++x) {
      const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
      if (pixel[3] != 255U) {
        opaque = false;
        if (pixel[3] == 0U) {
          continue;
        }
      }
      if (pixel[0] != pixel[1] || pixel[1] != pixel[2]) {
        gray = false;
      }
    }
  }
  const int channels = gray ? 1 : 3;
  pdf::ImagePage page;
  page.width_points = width_points;
  page.height_points = height_points;
  page.image.width = width;
  page.image.height = height;
  page.image.bits_per_component = 8;
  page.image.color_space = gray ? "/DeviceGray" : "/DeviceRGB";

  if (options.lossless) {
    std::vector<std::uint8_t> samples(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                      static_cast<std::size_t>(channels));
    auto* out = samples.data();
    for (int y = 0; y < height; ++y) {
      const auto* row = rgba.constScanLine(y);
      for (int x = 0; x < width; ++x) {
        const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
        *out++ = pixel[0];
        if (!gray) {
          *out++ = pixel[1];
          *out++ = pixel[2];
        }
      }
    }
    page.image.bytes = flate_with_up_predictor(samples, width, height, channels);
    page.image.filter = "FlateDecode";
    page.image.decode_parms = up_predictor_parms(width, channels);
  } else {
    QImage source(width, height, gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
      const auto* row = rgba.constScanLine(y);
      auto* out = source.scanLine(y);
      for (int x = 0; x < width; ++x) {
        const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
        *out++ = pixel[0];
        if (!gray) {
          *out++ = pixel[1];
          *out++ = pixel[2];
        }
      }
    }
    page.image.bytes = jpeg_bytes(source, options.jpeg_quality);
    page.image.filter = "DCTDecode";
  }

  if (!opaque) {
    std::vector<std::uint8_t> alpha(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    auto* out = alpha.data();
    for (int y = 0; y < height; ++y) {
      const auto* row = rgba.constScanLine(y);
      for (int x = 0; x < width; ++x) {
        *out++ = row[static_cast<std::size_t>(x) * 4U + 3U];
      }
    }
    pdf::ImageStream mask;
    mask.width = width;
    mask.height = height;
    mask.bytes = flate_with_up_predictor(alpha, width, height, 1);
    mask.filter = "FlateDecode";
    mask.decode_parms = up_predictor_parms(width, 1);
    page.soft_mask = std::move(mask);
  }
  return page;
}

}  // namespace pdf_detail

void write_pdf_document_file(const Document& document, const QString& path, const PdfExportOptions& options,
                             std::vector<std::string>* notices) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("The document could not be rendered for PDF export.");
  }
  if (options.editable_layers && !pdf_detail::document_is_single_raster_layer(document)) {
    pdf_detail::write_editable_pdf_document_file(document, path, options, notices);
    return;
  }
  const Document* page = &document;
  write_image_pages(std::span<const Document* const>(&page, 1), path, options, {});
}

bool write_multipage_pdf_file(std::span<const Document* const> pages, const QString& path,
                              const PdfExportOptions& options, std::vector<std::string>* notices,
                              const PdfPageProgress& progress) {
  if (pages.empty()) {
    throw std::runtime_error("There are no pages to export.");
  }
  for (const auto* page : pages) {
    if (page == nullptr || page->width() <= 0 || page->height() <= 0) {
      throw std::runtime_error("The document could not be rendered for PDF export.");
    }
  }
  // Qt's engine is needed only when some page holds a real vector or text object (or
  // several layers that should stay separate images). One writer makes one file, so a
  // single such page sends every page that way.
  const bool needs_qt_engine =
      options.editable_layers && std::any_of(pages.begin(), pages.end(), [](const Document* page) {
        return !pdf_detail::document_is_single_raster_layer(*page);
      });
  if (!needs_qt_engine) {
    return write_image_pages(pages, path, options, progress);
  }

  pdf_detail::QtPdfOutput output(path);
  QPdfWriter writer(output.device());
  // The device resolution is fixed for the whole file (it only sets the painter's
  // logical grid), so it comes from page 1; every page maps its own pixel grid onto
  // its own sheet through the window/viewport pair below.
  pdf_detail::configure_document_page(writer, *pages.front());

  QPainter painter;
  if (!painter.begin(&writer)) {
    throw std::runtime_error("The PDF file could not be opened for writing.");
  }
  bool text_drawn = false;
  for (std::size_t index = 0; index < pages.size(); ++index) {
    const Document& document = *pages[index];
    const std::string profile_detail = "page=" + std::to_string(index + 1);
    const UiProfileScope page_scope("pdf_export.page", profile_detail);
    if (progress && !progress(static_cast<int>(index) + 1, static_cast<int>(pages.size()))) {
      painter.end();
      return false;  // `output` removes the partial temporary; the destination is untouched
    }
    if (index > 0) {
      // A size set right before newPage() applies to the page it starts.
      const UiProfileScope profile_scope("pdf_export.new_page", profile_detail);
      writer.setPageSize(pdf_detail::document_page_size(document));
      if (!writer.newPage()) {
        painter.end();
        throw std::runtime_error("The PDF file could not start a new page.");
      }
    }
    painter.save();
    painter.setViewport(writer.pageLayout().paintRectPixels(writer.resolution()));
    painter.setWindow(QRect(0, 0, document.width(), document.height()));
    pdf_detail::paint_editable_document(painter, document, options, notices, &text_drawn);
    painter.restore();
  }
  {
    const UiProfileScope profile_scope("pdf_export.finish");
    painter.end();
  }
  output.commit(text_drawn);
  return true;
}

std::vector<Document> documents_for_top_level_groups(const Document& document, bool include_ungrouped_layers) {
  std::vector<Document> pages;
  const auto& layers = document.layers();
  for (std::size_t index = layers.size(); index-- > 0;) {
    const Layer& group = layers[index];
    if (group.kind() != LayerKind::Group || !group.visible()) {
      continue;
    }
    Document page = document;
    auto& page_layers = page.layers();
    for (std::size_t other = 0; other < page_layers.size(); ++other) {
      if (other == index) {
        continue;
      }
      Layer& layer = page_layers[other];
      const bool keep = include_ungrouped_layers && layer.kind() != LayerKind::Group;
      if (!keep) {
        layer.set_visible(false);
      }
    }
    pages.push_back(std::move(page));
  }
  return pages;
}

}  // namespace patchy::ui
