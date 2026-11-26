#include "ui/recent_thumbnail_sources.hpp"

#include "formats/format_registry.hpp"
#include "formats/kra_document_io.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/miniz/miniz.h"
#include "formats/ora_document_io.hpp"
#include "formats/pdn_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/xcf_document_io.hpp"
#include "formats/zip_entry_reader.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_tiff.hpp"
#include "ui/background_workers.hpp"
#include "ui/image_document_io.hpp"
#include "ui/recent_thumbnails.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QTransform>

#ifdef PATCHY_HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

std::atomic<int> g_document_reads{0};
std::atomic<int> g_file_opens{0};

// A PSD keeps its sections in order: header, color mode data, image resources. The embedded
// thumbnail is an image resource, so these two caps bound what is read to find it.
constexpr qint64 kPsdHeaderBytes = 26;
constexpr quint32 kMaxPsdColorModeBytes = 16U * 1024U * 1024U;
constexpr quint32 kMaxPsdResourceBytes = 16U * 1024U * 1024U;
// Paint.NET's header (an XML block with the thumbnail as base64) sits at the front of the file.
constexpr qint64 kPdnHeaderReadBytes = 4LL * 1024 * 1024;
// One preview entry of an OpenRaster or Krita zip, compressed and unpacked.
constexpr std::uint64_t kMaxPreviewEntryBytes = 128ULL * 1024 * 1024;
// A compressed SVG (.svgz), unpacked.
constexpr qint64 kMaxUnpackedSvgBytes = 4 * kRecentThumbnailMaxSvgBytes;

enum class Kind {
  qt_image,        // Patchy opens it through Qt's readers: JPEG, PNG, WebP, GIF and so on
  qt_then_reader,  // Qt's reader first, Patchy's own when Qt has none here: BMP, ICO, TGA, PCX, HEIF
  tiff,            // Qt's reader first, Patchy's flat TIFF decoder for the layouts Qt refuses
  psd,
  ora,
  kra,
  pdn,
  raw,
  xcf,
  reader,  // Patchy's own reader only: AVIF, JPEG XL, JPEG XR, EXR, HDR, JPEG 2000, DDS, Affinity, ...
  pdf,
  svg,
};

struct Source {
  Kind kind{Kind::qt_image};
  const FormatHandler* handler{nullptr};
  // The reader decodes through a codec the platform supplies and the user can install later
  // (HEIF: a Store package on Windows, the distribution's libheif on Linux). A failure is
  // then no verdict on the file: it gets no marker and is looked at again at the next start.
  bool platform_codec{false};
};

Source source_for(const QString& path) {
  const QFileInfo info(path);
  const auto extension = info.suffix().toLower();
  const auto& registry = builtin_format_registry();
  if (extension == QStringLiteral("pdf")) {
    return {Kind::pdf, nullptr};
  }
  if (extension == QStringLiteral("svg") || extension == QStringLiteral("svgz")) {
    return {Kind::svg, nullptr};
  }
  if (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) {
    return {Kind::tiff, nullptr};
  }
  // GIMP's "name.xcf.gz" (docs/xcf.md): the last extension alone says nothing.
  if (xcf::is_compressed_xcf_file_name(info.fileName().toStdString())) {
    return {Kind::xcf, registry.find_by_extension("xcf")};
  }
  const auto* handler = registry.find_by_extension(extension.toStdString());
  if (handler == nullptr) {
    return {Kind::qt_image, nullptr};
  }
  const auto& id = handler->identifier;
  if (id == "patchy.formats.psd") {
    return {Kind::psd, handler};
  }
  if (id == "patchy.formats.ora") {
    return {Kind::ora, handler};
  }
  if (id == "patchy.formats.kra") {
    return {Kind::kra, handler};
  }
  if (id == "patchy.formats.pdn") {
    return {Kind::pdn, handler};
  }
  if (id == "patchy.formats.camera_raw") {
    return {Kind::raw, handler};
  }
  if (id == "patchy.formats.xcf") {
    return {Kind::xcf, handler};
  }
  if (id == "patchy.formats.svg") {
    return {Kind::svg, nullptr};
  }
  if (id == "patchy.formats.heif") {
    return {Kind::qt_then_reader, handler, true};
  }
  if (id == "patchy.formats.bmp" || id == "patchy.formats.ico" || id == "patchy.formats.tga" ||
      id == "patchy.formats.pcx") {
    return {Kind::qt_then_reader, handler};
  }
  return {Kind::reader, handler};
}

quint32 big_endian_32(const char* bytes) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes);
  return (static_cast<quint32>(data[0]) << 24) | (static_cast<quint32>(data[1]) << 16) |
         (static_cast<quint32>(data[2]) << 8) | static_cast<quint32>(data[3]);
}

// At most `limit` bytes from the front of the file; empty when it cannot be opened.
std::vector<std::uint8_t> read_file_front(const QString& path, qint64 limit) {
  ++g_file_opens;
  QFile file(path);
  if (limit <= 0 || !file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto wanted = std::min(limit, file.size());
  if (wanted <= 0) {
    return {};
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(wanted));
  const auto got = file.read(reinterpret_cast<char*>(bytes.data()), wanted);
  if (got <= 0) {
    return {};
  }
  bytes.resize(static_cast<std::size_t>(got));
  return bytes;
}

// A reader's picture decoded at most as large as the stored thumbnail where the format can
// (JPEG decodes reduced; the others decode whole and scale). `over_cap` is set when the image
// is past the pixel cap and was not decoded.
QImage read_reduced(QImageReader& reader, bool auto_transform, bool* over_cap) {
  const auto format = reader.format().toLower();
  const auto full = reader.size();
  if (!full.isValid() || full.isEmpty()) {
    return {};
  }
  const bool decodes_reduced = format == "jpeg" || format == "jpg";
  if (!decodes_reduced && static_cast<qint64>(full.width()) * full.height() > kRecentThumbnailMaxSourcePixels) {
    if (over_cap != nullptr) {
      *over_cap = true;
    }
    return {};
  }
  reader.setAutoTransform(auto_transform);
  if (full.width() > kRecentThumbnailStoredSide || full.height() > kRecentThumbnailStoredSide) {
    reader.setScaledSize(
        full.scaled(kRecentThumbnailStoredSide, kRecentThumbnailStoredSide, Qt::KeepAspectRatio).expandedTo({1, 1}));
  }
  return reader.read();
}

// An encoded image held in memory (a preview a file embeds), through Qt's readers.
QImage decode_embedded(std::span<const std::uint8_t> encoded, bool auto_transform) {
  if (encoded.empty()) {
    return {};
  }
  auto bytes = QByteArray::fromRawData(reinterpret_cast<const char*>(encoded.data()),
                                       static_cast<qsizetype>(encoded.size()));
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return {};
  }
  QImageReader reader(&buffer);
  reader.setDecideFormatFromContent(true);
  if (!reader.canRead()) {
    return {};
  }
  return read_reduced(reader, auto_transform, nullptr);
}

// The JPEG thumbnail a PSD or PSB carries in its image resources (resource 1036), read from the
// front of the file without decoding a layer or the composite. Null for any other file.
QImage psd_embedded_thumbnail(const QString& path) {
  ++g_file_opens;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto head = file.read(kPsdHeaderBytes + 4);
  if (head.size() < kPsdHeaderBytes + 4 || !head.startsWith("8BPS")) {
    return {};
  }
  const auto color_mode_bytes = big_endian_32(head.constData() + kPsdHeaderBytes);
  if (color_mode_bytes > kMaxPsdColorModeBytes || !file.seek(kPsdHeaderBytes + 4 + color_mode_bytes)) {
    return {};
  }
  const auto length_field = file.read(4);
  if (length_field.size() < 4) {
    return {};
  }
  const auto resource_bytes = big_endian_32(length_field.constData());
  if (resource_bytes == 0 || resource_bytes > kMaxPsdResourceBytes) {
    return {};
  }
  const auto resources = file.read(resource_bytes);
  if (resources.size() != static_cast<qsizetype>(resource_bytes)) {
    return {};
  }
  const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(resources.constData()),
                                           static_cast<std::size_t>(resources.size()));
  for (const auto& resource : metadata::parse_irb(span)) {
    if (resource.id != metadata::psd_resource::kThumbnail) {
      continue;
    }
    const auto jpeg = metadata::psd_thumbnail_jpeg(resource.data);
    QImage image;
    if (!jpeg.empty() && image.loadFromData(jpeg.data(), static_cast<int>(jpeg.size()), "JPG")) {
      return image;
    }
  }
  return {};
}

// The picture an OpenRaster or Krita file carries of itself: the application's own render of
// the whole image (mergedimage.png), else its small preview. Nothing of the layers is read.
QImage zip_preview(std::span<const std::uint8_t> bytes, Kind kind) {
  if (kind == Kind::ora ? !ora::sniff(bytes) : !kra::sniff(bytes)) {
    return {};
  }
  static constexpr formats::ZipReaderMessages kMessages{"damaged", "too large", "unsupported", "damaged entry"};
  const formats::ZipReaderLimits limits{kMaxPreviewEntryBytes, 2 * kMaxPreviewEntryBytes, 100000};
  const char* const small_preview = kind == Kind::ora ? "Thumbnails/thumbnail.png" : "preview.png";
  for (const char* const name : {"mergedimage.png", small_preview}) {
    try {
      formats::ZipEntryReader zip(bytes, kMessages, limits);
      const auto entry = zip.extract(name, kMaxPreviewEntryBytes);
      if (!entry.has_value()) {
        continue;
      }
      if (auto image = decode_embedded(*entry, false); !image.isNull()) {
        return image;
      }
    } catch (const std::exception&) {
      // A damaged or oversized entry: the next one, then the slow pass.
    }
  }
  return {};
}

// A camera raw's embedded preview: the JPEG the camera stored, turned the way the develop
// turns the picture. Nothing of the sensor data is decoded.
QImage raw_embedded_preview(std::vector<std::uint8_t> bytes) {
  raw::DevelopSession session(std::move(bytes));
  const auto& info = session.info();
  auto image = decode_embedded(info.thumbnail, false);
  if (image.isNull()) {
    return {};
  }
  int degrees = 0;
  switch (info.orientation_flip) {
    case 3:
      degrees = 180;
      break;
    case 5:
      degrees = 270;
      break;
    case 6:
      degrees = 90;
      break;
    default:
      break;
  }
  // Previews are usually stored unturned; a camera that stored it upright already has the
  // developed picture's orientation, which the two shapes tell apart for a quarter turn.
  const bool quarter_turn = degrees == 90 || degrees == 270;
  const bool output_is_tall = info.output_height > info.output_width;
  const bool preview_is_tall = image.height() > image.width();
  if (degrees != 0 && !(quarter_turn && output_is_tall == preview_is_tall)) {
    image = image.transformed(QTransform().rotate(degrees));
  }
  return image;
}

RecentThumbnailRead picture(QImage image) {
  return {std::move(image), RecentThumbnailOutcome::picture};
}

RecentThumbnailRead outcome(RecentThumbnailOutcome value) {
  return {QImage(), value};
}

#ifdef PATCHY_HAVE_QT_PDF
// Page 1, always (by position, never chosen by content), on white paper, at the stored size.
// A file that needs a password, or does not load, has no picture: nothing is asked.
QImage pdf_first_page(const QString& path) {
  ++g_file_opens;
  QPdfDocument pdf;
  if (pdf.load(path) != QPdfDocument::Error::None || pdf.pageCount() < 1) {
    return {};
  }
  const auto points = pdf.pagePointSize(0);
  if (!points.isValid() || points.width() < 1.0 || points.height() < 1.0) {
    return {};
  }
  const auto size = points
                        .scaled(kRecentThumbnailStoredSide, kRecentThumbnailStoredSide, Qt::KeepAspectRatio)
                        .toSize()
                        .expandedTo({1, 1});
  QPdfDocumentRenderOptions options;
  options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
  const auto page = pdf.render(0, size, options);
  if (page.isNull()) {
    return {};
  }
  QImage paper(page.size(), QImage::Format_RGB32);
  paper.fill(Qt::white);
  QPainter painter(&paper);
  painter.drawImage(0, 0, page);
  painter.end();
  return paper;
}
#endif

// True when the SVG text may make Qt's renderer read another file: an <image> or <feImage>
// whose reference is not data held in the SVG itself, or entity definitions that could hide
// one. Such a file could name a network server, which nothing may reach unasked.
bool svg_reads_other_files(const QByteArray& text) {
  if (text.contains("<!ENTITY")) {
    return true;
  }
  static const QRegularExpression outside_image(
      QStringLiteral(R"(<\s*(?:[\w-]+:)?(?:image|feImage)\b[^>]*?\bhref\s*=\s*["']\s*(?!data:|#))"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  return outside_image.match(QString::fromLatin1(text)).hasMatch();
}

// One gzip member unpacked into at most `limit` bytes; empty when it is damaged or larger. The
// size the trailer claims sizes the buffer, and a stream that does not end exactly there fails,
// so a file that expands without end allocates nothing past the limit.
std::vector<std::uint8_t> gunzip_bounded(std::span<const std::uint8_t> gz, std::size_t limit) {
  if (gz.size() < 18 || gz[0] != 0x1f || gz[1] != 0x8b || gz[2] != 8) {
    return {};
  }
  const auto flags = gz[3];
  std::size_t at = 10;
  if ((flags & 0x04U) != 0) {  // extra field
    if (at + 2 > gz.size()) {
      return {};
    }
    at += 2 + (static_cast<std::size_t>(gz[at]) | (static_cast<std::size_t>(gz[at + 1]) << 8U));
  }
  for (const auto text_flag : {0x08U, 0x10U}) {  // file name, comment
    if ((flags & text_flag) != 0) {
      while (at < gz.size() && gz[at] != 0) {
        ++at;
      }
      ++at;
    }
  }
  if ((flags & 0x02U) != 0) {  // header checksum
    at += 2;
  }
  if (at + 8 >= gz.size()) {
    return {};
  }
  const auto* tail = gz.data() + gz.size() - 4;
  const auto claimed = static_cast<std::size_t>(tail[0]) | (static_cast<std::size_t>(tail[1]) << 8U) |
                       (static_cast<std::size_t>(tail[2]) << 16U) | (static_cast<std::size_t>(tail[3]) << 24U);
  if (claimed == 0 || claimed > limit) {
    return {};
  }
  std::vector<std::uint8_t> out(claimed);
  mz_stream stream{};
  if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
    return {};
  }
  stream.next_in = gz.data() + at;
  stream.avail_in = static_cast<unsigned int>(gz.size() - at - 8);
  stream.next_out = out.data();
  stream.avail_out = static_cast<unsigned int>(out.size());
  const auto status = mz_inflate(&stream, MZ_FINISH);
  const auto produced = static_cast<std::size_t>(stream.total_out);
  mz_inflateEnd(&stream);
  if (status != MZ_STREAM_END || produced != out.size()) {
    return {};
  }
  return out;
}

// The SVG drawn by Qt's renderer at the stored size (vectors stay sharp, small icons included).
QImage svg_picture(const QString& path, qint64 source_bytes) {
  if (source_bytes > kRecentThumbnailMaxSvgBytes) {
    return {};
  }
  auto bytes = read_file_front(path, kRecentThumbnailMaxSvgBytes);
  if (bytes.empty()) {
    return {};
  }
  if (bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) {
    bytes = gunzip_bounded(bytes, static_cast<std::size_t>(kMaxUnpackedSvgBytes));  // .svgz
    if (bytes.empty()) {
      return {};
    }
  }
  const auto text = QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()),
                                            static_cast<qsizetype>(bytes.size()));
  if (svg_reads_other_files(text)) {
    return {};
  }
  QSvgRenderer renderer;
  if (!renderer.load(text) || !renderer.isValid()) {
    return {};
  }
  const auto natural = renderer.defaultSize();
  if (!natural.isValid() || natural.isEmpty()) {
    return {};
  }
  const auto size =
      natural.scaled(kRecentThumbnailStoredSide, kRecentThumbnailStoredSide, Qt::KeepAspectRatio).expandedTo({1, 1});
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
  painter.end();
  return image;
}

void check_canvas(std::uint64_t width, std::uint64_t height) {
  if (width == 0 || height == 0 || width * height > static_cast<std::uint64_t>(kRecentThumbnailMaxDocumentPixels)) {
    throw std::runtime_error("canvas past the thumbnail cap");
  }
}

// The file through Patchy's own reader, with the caps a header can answer checked first.
Document read_document(const Source& source, std::vector<std::uint8_t> owned) {
  const std::span<const std::uint8_t> bytes(owned);
  switch (source.kind) {
    case Kind::psd: {
      // Header: signature, version, reserved, channels, then rows and columns.
      if (bytes.size() < 26) {
        throw std::runtime_error("short PSD");
      }
      const auto* head = reinterpret_cast<const char*>(bytes.data());
      check_canvas(big_endian_32(head + 18), big_endian_32(head + 14));
      // No embedded thumbnail (the quick pass looked): the flat composite the file ends with,
      // when it has a real one, so no layer is decoded.
      psd::ReadOptions options;
      options.preserve_unknown_blocks = false;
      options.prefer_flat_composite = true;
      return psd::DocumentIo::read(bytes, options);
    }
    case Kind::tiff:
      return psd::read_flat_tiff(bytes);
    case Kind::pdn:
      if (const auto header = pdn::read_header(bytes); header.has_value()) {
        check_canvas(static_cast<std::uint64_t>(std::max(header->width, 0)),
                     static_cast<std::uint64_t>(std::max(header->height, 0)));
        const auto decoded = static_cast<std::uint64_t>(header->width) * static_cast<std::uint64_t>(header->height) *
                             4ULL * static_cast<std::uint64_t>(std::max(header->layer_count, 1));
        if (decoded > static_cast<std::uint64_t>(kRecentThumbnailMaxDecodedBytes)) {
          throw std::runtime_error("layers past the thumbnail cap");
        }
      }
      break;
    case Kind::raw: {
      // No embedded preview (the quick pass looked): a draft develop at half size, which
      // skips the demosaic and gives up when quit is asked for.
      raw::DevelopSession session(std::move(owned));
      raw::DevelopParams params;
      params.half_size = true;
      raw::DevelopOptions options;
      options.quality = raw::DevelopQuality::Draft;
      options.cancelled = [] { return background_workers_stop_requested(); };
      return session.develop_document(params, options).document;
    }
    case Kind::xcf: {
      xcf::ReadLimits limits;
      limits.max_canvas_pixels = static_cast<std::uint64_t>(kRecentThumbnailMaxDocumentPixels);
      limits.max_decoded_layer_bytes = static_cast<std::uint64_t>(kRecentThumbnailMaxDecodedBytes);
      // GIMP 3 layer effects without code of the reader's own need a helper process each:
      // left out of a thumbnail.
      limits.run_layer_effects = false;
      const xcf::ScopedReadLimits scope(limits);
      return source.handler->read(bytes).document;
    }
    default:
      break;
  }
  if (source.handler == nullptr) {
    throw std::runtime_error("no reader");
  }
  return source.handler->read(bytes).document;
}

std::uint64_t decoded_canvas_bytes(const Document& document) {
  std::uint64_t channel_bytes = 1;
  switch (document.color_state().bit_depth) {
    case BitDepth::UInt16:
      channel_bytes = 2;
      break;
    case BitDepth::Float32:
      channel_bytes = 4;
      break;
    default:
      break;
  }
  return static_cast<std::uint64_t>(document.width()) * static_cast<std::uint64_t>(document.height()) * 4ULL *
         channel_bytes;
}

RecentThumbnailRead quick_pass(const QString& path, qint64 source_bytes, const Source& source) {
  switch (source.kind) {
    case Kind::psd:
      if (auto embedded = psd_embedded_thumbnail(path); !embedded.isNull()) {
        return picture(std::move(embedded));
      }
      return outcome(RecentThumbnailOutcome::slow);
    case Kind::ora:
    case Kind::kra:
      if (source_bytes <= kRecentThumbnailMaxPreviewHostBytes) {
        const auto bytes = read_file_front(path, source_bytes);
        if (bytes.empty()) {
          return outcome(RecentThumbnailOutcome::unreadable);
        }
        if (auto image = zip_preview(bytes, source.kind); !image.isNull()) {
          return picture(std::move(image));
        }
      }
      return outcome(RecentThumbnailOutcome::slow);
    case Kind::pdn: {
      const auto front = read_file_front(path, kPdnHeaderReadBytes);
      if (front.empty()) {
        return outcome(RecentThumbnailOutcome::unreadable);
      }
      if (const auto header = pdn::read_header(front); header.has_value()) {
        if (auto image = decode_embedded(header->thumbnail_png, false); !image.isNull()) {
          return picture(std::move(image));
        }
      }
      return outcome(RecentThumbnailOutcome::slow);
    }
    case Kind::raw:
      if (source_bytes <= kRecentThumbnailMaxPreviewHostBytes) {
        auto bytes = read_file_front(path, source_bytes);
        if (bytes.empty()) {
          return outcome(RecentThumbnailOutcome::unreadable);
        }
        if (auto image = raw_embedded_preview(std::move(bytes)); !image.isNull()) {
          return picture(std::move(image));
        }
      }
      return outcome(RecentThumbnailOutcome::slow);
    case Kind::xcf:
    case Kind::reader:
    case Kind::pdf:
    case Kind::svg:
      return outcome(RecentThumbnailOutcome::slow);  // nothing is opened in this pass
    case Kind::qt_image:
    case Kind::qt_then_reader:
    case Kind::tiff:
      break;
  }
  ++g_file_opens;
  QImageReader reader(path);
  reader.setDecideFormatFromContent(true);
  if (reader.canRead()) {
    const auto format = reader.format().toLower();
    // Vector and page formats are rendered, not decoded: the slow pass, by their extension.
    if (format != "svg" && format != "svgz" && format != "pdf") {
      bool over_cap = false;
      if (auto image = read_reduced(reader, true, &over_cap); !image.isNull()) {
        return picture(std::move(image));
      }
      if (over_cap) {
        return outcome(RecentThumbnailOutcome::none);
      }
    }
  }
  return outcome(source.kind == Kind::qt_image ? RecentThumbnailOutcome::none : RecentThumbnailOutcome::slow);
}

RecentThumbnailRead slow_pass(const QString& path, qint64 source_bytes, const Source& source) {
  if (background_workers_stop_requested()) {
    return outcome(RecentThumbnailOutcome::unreadable);
  }
  if (source.kind == Kind::pdf) {
#ifdef PATCHY_HAVE_QT_PDF
    ++g_document_reads;
    if (auto page = pdf_first_page(path); !page.isNull()) {
      return picture(std::move(page));
    }
#endif
    return outcome(RecentThumbnailOutcome::none);
  }
  if (source.kind == Kind::svg) {
    if (source_bytes > kRecentThumbnailMaxSvgBytes) {
      return outcome(RecentThumbnailOutcome::none);
    }
    ++g_document_reads;
    if (auto image = svg_picture(path, source_bytes); !image.isNull()) {
      return picture(std::move(image));
    }
    return outcome(RecentThumbnailOutcome::none);
  }
  if (source.kind == Kind::qt_image || source_bytes > kRecentThumbnailMaxDocumentBytes) {
    return outcome(RecentThumbnailOutcome::none);
  }
  auto bytes = read_file_front(path, source_bytes);
  if (bytes.empty()) {
    return outcome(RecentThumbnailOutcome::unreadable);
  }
  ++g_document_reads;
  const auto document = read_document(source, std::move(bytes));
  if (background_workers_stop_requested()) {
    return outcome(RecentThumbnailOutcome::unreadable);
  }
  check_canvas(static_cast<std::uint64_t>(std::max(document.width(), 0)),
               static_cast<std::uint64_t>(std::max(document.height(), 0)));
  if (decoded_canvas_bytes(document) > static_cast<std::uint64_t>(kRecentThumbnailMaxDecodedBytes)) {
    return outcome(RecentThumbnailOutcome::none);
  }
  // The renderer the canvas uses, so the picture is the document as Patchy shows it.
  auto image = qimage_from_document(std::as_const(document), true);
  return image.isNull() ? outcome(RecentThumbnailOutcome::none) : picture(std::move(image));
}

}  // namespace

RecentThumbnailRead read_recent_thumbnail(const QString& path, qint64 source_bytes, RecentThumbnailPass pass) {
  if (source_bytes <= 0 || source_bytes > kRecentThumbnailMaxSourceBytes) {
    return outcome(RecentThumbnailOutcome::none);
  }
  // Every reader is guarded: a damaged file is a file without a picture, whatever it throws.
  Source source;
  try {
    source = source_for(path);
    return pass == RecentThumbnailPass::quick ? quick_pass(path, source_bytes, source)
                                              : slow_pass(path, source_bytes, source);
  } catch (const raw::DevelopCancelled&) {
    return outcome(RecentThumbnailOutcome::unreadable);
  } catch (const std::bad_alloc&) {
    return outcome(RecentThumbnailOutcome::none);
  } catch (const std::exception&) {
    if (pass == RecentThumbnailPass::quick) {
      return outcome(RecentThumbnailOutcome::slow);
    }
    return outcome(source.platform_codec ? RecentThumbnailOutcome::unreadable : RecentThumbnailOutcome::none);
  } catch (...) {
    return outcome(RecentThumbnailOutcome::none);
  }
}

int recent_thumbnail_document_reads_for_testing() noexcept {
  return g_document_reads.load();
}

int recent_thumbnail_file_opens_for_testing() noexcept {
  return g_file_opens.load();
}

}  // namespace patchy::ui
