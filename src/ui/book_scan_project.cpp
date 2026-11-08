#include "ui/book_scan_project.hpp"

#include "core/photo_divide.hpp"
#include "support/atomic_file_write.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>

namespace patchy::ui {

namespace {

constexpr const char* kFormatId = "patchy-book-scan";
constexpr int kFormatVersion = 1;
constexpr double kLetterWidth = 8.5;
constexpr double kLetterHeight = 11.0;

// --- enum tokens (permanent) -------------------------------------------------

QString direction_token(BookReadingDirection value) {
  return value == BookReadingDirection::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr");
}
BookReadingDirection direction_from(const QString& token) {
  return token == QLatin1String("rtl") ? BookReadingDirection::RightToLeft : BookReadingDirection::LeftToRight;
}
QString mode_token(BookScanMode value) {
  return value == BookScanMode::OnePage ? QStringLiteral("one-page") : QStringLiteral("two-pages");
}
BookScanMode mode_from(const QString& token) {
  return token == QLatin1String("one-page") ? BookScanMode::OnePage : BookScanMode::TwoPages;
}
QString use_token(BookPageUse value) {
  switch (value) {
    case BookPageUse::LeftOnly:
      return QStringLiteral("left");
    case BookPageUse::RightOnly:
      return QStringLiteral("right");
    case BookPageUse::Both:
      break;
  }
  return QStringLiteral("both");
}
BookPageUse use_from(const QString& token) {
  if (token == QLatin1String("left")) {
    return BookPageUse::LeftOnly;
  }
  if (token == QLatin1String("right")) {
    return BookPageUse::RightOnly;
  }
  return BookPageUse::Both;
}
QString policy_token(BookPageSizePolicy value) {
  switch (value) {
    case BookPageSizePolicy::Largest:
      return QStringLiteral("largest");
    case BookPageSizePolicy::Cover:
      return QStringLiteral("cover");
    case BookPageSizePolicy::Custom:
      return QStringLiteral("custom");
    case BookPageSizePolicy::Median:
      break;
  }
  return QStringLiteral("median");
}
BookPageSizePolicy policy_from(const QString& token) {
  if (token == QLatin1String("largest")) {
    return BookPageSizePolicy::Largest;
  }
  if (token == QLatin1String("cover")) {
    return BookPageSizePolicy::Cover;
  }
  if (token == QLatin1String("custom")) {
    return BookPageSizePolicy::Custom;
  }
  return BookPageSizePolicy::Median;
}
QString output_token(BookOutputMode value) {
  switch (value) {
    case BookOutputMode::Grayscale:
      return QStringLiteral("gray");
    case BookOutputMode::BlackAndWhite:
      return QStringLiteral("bw");
    case BookOutputMode::Color:
      break;
  }
  return QStringLiteral("color");
}
BookOutputMode output_from(const QString& token) {
  if (token == QLatin1String("gray")) {
    return BookOutputMode::Grayscale;
  }
  if (token == QLatin1String("bw")) {
    return BookOutputMode::BlackAndWhite;
  }
  return BookOutputMode::Color;
}
QString kind_token(BookPageRef::Kind value) {
  switch (value) {
    case BookPageRef::Kind::Blank:
      return QStringLiteral("blank");
    case BookPageRef::Kind::FrontCover:
      return QStringLiteral("front-cover");
    case BookPageRef::Kind::BackCover:
      return QStringLiteral("back-cover");
    case BookPageRef::Kind::Scan:
      break;
  }
  return QStringLiteral("scan");
}
BookPageRef::Kind kind_from(const QString& token) {
  if (token == QLatin1String("blank")) {
    return BookPageRef::Kind::Blank;
  }
  if (token == QLatin1String("front-cover")) {
    return BookPageRef::Kind::FrontCover;
  }
  if (token == QLatin1String("back-cover")) {
    return BookPageRef::Kind::BackCover;
  }
  return BookPageRef::Kind::Scan;
}

QJsonObject box_json(const OrientedBox& box) {
  QJsonObject object;
  object[QStringLiteral("cx")] = box.center_x;
  object[QStringLiteral("cy")] = box.center_y;
  object[QStringLiteral("w")] = box.width;
  object[QStringLiteral("h")] = box.height;
  object[QStringLiteral("angle")] = box.angle_degrees;
  return object;
}
OrientedBox box_from(const QJsonObject& object) {
  return {object.value(QStringLiteral("cx")).toDouble(), object.value(QStringLiteral("cy")).toDouble(),
          object.value(QStringLiteral("w")).toDouble(), object.value(QStringLiteral("h")).toDouble(),
          object.value(QStringLiteral("angle")).toDouble()};
}

int normalized_turns(int turns) {
  return ((turns % 4) + 4) % 4;
}

// A relative scan path from book.json stays inside the project folder.
bool safe_relative_file(const QString& file) {
  if (file.isEmpty() || QDir::isAbsolutePath(file) || file.contains(QLatin1Char(':'))) {
    return false;
  }
  const QString clean = QDir::cleanPath(file);
  return !clean.startsWith(QLatin1String("..")) && !clean.startsWith(QLatin1Char('/'));
}

}  // namespace

QByteArray book_project_json(const BookProject& project) {
  QJsonObject root;
  root[QStringLiteral("format")] = QString::fromLatin1(kFormatId);
  root[QStringLiteral("version")] = kFormatVersion;
  root[QStringLiteral("title")] = project.title;
  root[QStringLiteral("author")] = project.author;
  root[QStringLiteral("device")] = project.device_id;
  QJsonObject capture;
  capture[QStringLiteral("dpi")] = project.capture_dpi;
  capture[QStringLiteral("mode")] =
      project.capture_mode == ScanColorMode::Grayscale ? QStringLiteral("gray") : QStringLiteral("color");
  capture[QStringLiteral("bookArea")] = project.scan_book_area;
  if (project.book_area_inches) {
    const QRectF& area = *project.book_area_inches;
    capture[QStringLiteral("areaInches")] = QJsonArray{area.x(), area.y(), area.width(), area.height()};
  }
  root[QStringLiteral("capture")] = capture;
  root[QStringLiteral("upTurns")] = normalized_turns(project.up_turns);
  root[QStringLiteral("direction")] = direction_token(project.direction);
  root[QStringLiteral("mode")] = mode_token(project.mode);
  root[QStringLiteral("includeCovers")] = project.include_covers;
  root[QStringLiteral("finished")] = project.finished_scanning;
  if (project.cover) {
    QJsonObject cover;
    cover[QStringLiteral("file")] = project.cover->file;
    cover[QStringLiteral("dpi")] = project.cover->dpi;
    cover[QStringLiteral("outline")] = box_json(project.cover->layout.outline);
    cover[QStringLiteral("spineLeft")] = project.cover->layout.spine_left;
    cover[QStringLiteral("spineRight")] = project.cover->layout.spine_right;
    cover[QStringLiteral("edited")] = project.cover->user_edited;
    root[QStringLiteral("cover")] = cover;
  }
  QJsonArray scans;
  for (const auto& scan : project.scans) {
    QJsonObject entry;
    entry[QStringLiteral("file")] = scan.file;
    entry[QStringLiteral("dpi")] = scan.dpi;
    entry[QStringLiteral("turns")] = normalized_turns(scan.extra_turns);
    entry[QStringLiteral("spread")] = box_json(scan.layout.spread);
    entry[QStringLiteral("gutter")] = scan.layout.gutter;
    entry[QStringLiteral("edited")] = scan.user_edited;
    entry[QStringLiteral("use")] = use_token(scan.use);
    scans.append(entry);
  }
  root[QStringLiteral("scans")] = scans;
  if (project.custom_order) {
    QJsonArray pages;
    for (const auto& page : *project.custom_order) {
      QJsonObject entry;
      entry[QStringLiteral("kind")] = kind_token(page.kind);
      if (page.kind == BookPageRef::Kind::Scan) {
        entry[QStringLiteral("scan")] = page.scan;
        entry[QStringLiteral("side")] = page.side == BookPageSide::Right ? QStringLiteral("right") : QStringLiteral("left");
      }
      entry[QStringLiteral("turns")] = normalized_turns(page.turns);
      pages.append(entry);
    }
    root[QStringLiteral("pages")] = pages;
  }
  QJsonObject output;
  output[QStringLiteral("sizePolicy")] = policy_token(project.size_policy);
  output[QStringLiteral("customSize")] = QJsonArray{project.custom_size.width, project.custom_size.height};
  output[QStringLiteral("mode")] = output_token(project.output.mode);
  output[QStringLiteral("blackPoint")] = project.output.black_point;
  output[QStringLiteral("whitePoint")] = project.output.white_point;
  output[QStringLiteral("threshold")] = project.output.threshold;
  output[QStringLiteral("dpi")] = project.output_dpi;
  output[QStringLiteral("quality")] = project.quality_id;
  root[QStringLiteral("output")] = output;
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<BookProject> book_project_from_json(const QByteArray& bytes, QString* error) {
  QJsonParseError parse{};
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
  const QJsonObject root = document.object();
  if (parse.error != QJsonParseError::NoError || !document.isObject() ||
      root.value(QStringLiteral("format")).toString() != QLatin1String(kFormatId)) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("BookScanProject", "This folder does not hold a Patchy book scan.");
    }
    return std::nullopt;
  }
  if (root.value(QStringLiteral("version")).toInt() > kFormatVersion) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("BookScanProject", "This book scan was saved by a newer version of Patchy.");
    }
    return std::nullopt;
  }
  BookProject project;
  project.title = root.value(QStringLiteral("title")).toString();
  project.author = root.value(QStringLiteral("author")).toString();
  project.device_id = root.value(QStringLiteral("device")).toString();
  const QJsonObject capture = root.value(QStringLiteral("capture")).toObject();
  project.capture_dpi = std::clamp(capture.value(QStringLiteral("dpi")).toInt(300), 50, 2400);
  project.capture_mode = capture.value(QStringLiteral("mode")).toString() == QLatin1String("gray")
                             ? ScanColorMode::Grayscale
                             : ScanColorMode::Color;
  project.scan_book_area = capture.value(QStringLiteral("bookArea")).toBool(true);
  const QJsonArray area = capture.value(QStringLiteral("areaInches")).toArray();
  if (area.size() == 4) {
    project.book_area_inches = QRectF(area[0].toDouble(), area[1].toDouble(), area[2].toDouble(), area[3].toDouble());
  }
  project.up_turns = normalized_turns(root.value(QStringLiteral("upTurns")).toInt());
  project.direction = direction_from(root.value(QStringLiteral("direction")).toString());
  project.mode = mode_from(root.value(QStringLiteral("mode")).toString());
  project.include_covers = root.value(QStringLiteral("includeCovers")).toBool(true);
  project.finished_scanning = root.value(QStringLiteral("finished")).toBool(false);
  if (root.contains(QStringLiteral("cover"))) {
    const QJsonObject cover = root.value(QStringLiteral("cover")).toObject();
    BookCover entry;
    entry.file = cover.value(QStringLiteral("file")).toString();
    entry.dpi = cover.value(QStringLiteral("dpi")).toDouble(300.0);
    entry.layout.outline = box_from(cover.value(QStringLiteral("outline")).toObject());
    entry.layout.spine_left = cover.value(QStringLiteral("spineLeft")).toDouble(0.49);
    entry.layout.spine_right = cover.value(QStringLiteral("spineRight")).toDouble(0.51);
    entry.user_edited = cover.value(QStringLiteral("edited")).toBool();
    if (safe_relative_file(entry.file) && entry.dpi > 0.0) {
      project.cover = entry;
    }
  }
  for (const auto& value : root.value(QStringLiteral("scans")).toArray()) {
    const QJsonObject object = value.toObject();
    BookScanEntry scan;
    scan.file = object.value(QStringLiteral("file")).toString();
    scan.dpi = object.value(QStringLiteral("dpi")).toDouble(300.0);
    scan.extra_turns = normalized_turns(object.value(QStringLiteral("turns")).toInt());
    scan.layout.spread = box_from(object.value(QStringLiteral("spread")).toObject());
    scan.layout.gutter = std::clamp(object.value(QStringLiteral("gutter")).toDouble(0.5), 0.0, 1.0);
    scan.user_edited = object.value(QStringLiteral("edited")).toBool();
    scan.use = use_from(object.value(QStringLiteral("use")).toString());
    if (!safe_relative_file(scan.file) || scan.dpi <= 0.0) {
      if (error != nullptr) {
        *error = QCoreApplication::translate("BookScanProject", "The book scan's manifest names a scan outside its folder.");
      }
      return std::nullopt;
    }
    project.scans.push_back(scan);
  }
  if (root.contains(QStringLiteral("pages"))) {
    std::vector<BookPageRef> pages;
    for (const auto& value : root.value(QStringLiteral("pages")).toArray()) {
      const QJsonObject object = value.toObject();
      BookPageRef page;
      page.kind = kind_from(object.value(QStringLiteral("kind")).toString());
      page.scan = object.value(QStringLiteral("scan")).toInt(-1);
      page.side = object.value(QStringLiteral("side")).toString() == QLatin1String("right") ? BookPageSide::Right
                                                                                          : BookPageSide::Left;
      page.turns = normalized_turns(object.value(QStringLiteral("turns")).toInt());
      if (page.kind == BookPageRef::Kind::Scan &&
          (page.scan < 0 || page.scan >= static_cast<int>(project.scans.size()))) {
        continue;
      }
      pages.push_back(page);
    }
    project.custom_order = std::move(pages);
  }
  const QJsonObject output = root.value(QStringLiteral("output")).toObject();
  project.size_policy = policy_from(output.value(QStringLiteral("sizePolicy")).toString());
  const QJsonArray custom = output.value(QStringLiteral("customSize")).toArray();
  if (custom.size() == 2) {
    project.custom_size = {custom[0].toDouble(), custom[1].toDouble()};
  }
  project.output.mode = output_from(output.value(QStringLiteral("mode")).toString());
  project.output.black_point = std::clamp(output.value(QStringLiteral("blackPoint")).toInt(0), 0, 254);
  project.output.white_point =
      std::clamp(output.value(QStringLiteral("whitePoint")).toInt(255), project.output.black_point + 1, 255);
  project.output.threshold = std::clamp(output.value(QStringLiteral("threshold")).toInt(160), 1, 255);
  project.output_dpi = std::clamp(output.value(QStringLiteral("dpi")).toInt(0), 0, 2400);
  project.quality_id = output.value(QStringLiteral("quality")).toString(QStringLiteral("high"));
  return project;
}

void save_book_project(const BookProject& project) {
  const QByteArray bytes = book_project_json(project);
  const QString path = QDir(project.folder).filePath(QString::fromLatin1(kBookProjectFileName));
  try {
    write_file_bytes_atomically(to_filesystem_path(path),
                                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                              static_cast<std::size_t>(bytes.size())),
                                "cannot create the manifest", "cannot write the manifest");
  } catch (const std::exception&) {
    throw std::runtime_error(
        QCoreApplication::translate("BookScanProject", "The book scan's manifest could not be written to %1.").arg(QDir::toNativeSeparators(path)).toStdString());
  }
}

std::optional<BookProject> load_book_project(const QString& folder, QString* error) {
  QFile file(QDir(folder).filePath(QString::fromLatin1(kBookProjectFileName)));
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QCoreApplication::translate("BookScanProject", "This folder does not hold a Patchy book scan.");
    }
    return std::nullopt;
  }
  auto project = book_project_from_json(file.readAll(), error);
  if (project) {
    project->folder = QDir(folder).absolutePath();
  }
  return project;
}

int pages_per_scan(const BookProject& project, const BookScanEntry& scan) {
  if (project.mode == BookScanMode::OnePage) {
    return 1;
  }
  return scan.use == BookPageUse::Both ? 2 : 1;
}

std::vector<BookPageRef> generated_page_order(const BookProject& project) {
  std::vector<BookPageRef> pages;
  const bool covers = project.include_covers && project.cover.has_value();
  if (covers) {
    pages.push_back({BookPageRef::Kind::FrontCover, -1, BookPageSide::Left, 0});
  }
  const bool rtl = project.direction == BookReadingDirection::RightToLeft;
  for (int index = 0; index < static_cast<int>(project.scans.size()); ++index) {
    const auto& scan = project.scans[static_cast<std::size_t>(index)];
    if (project.mode == BookScanMode::OnePage) {
      pages.push_back({BookPageRef::Kind::Scan, index, BookPageSide::Left, 0});
      continue;
    }
    const BookPageSide first = rtl ? BookPageSide::Right : BookPageSide::Left;
    const BookPageSide second = rtl ? BookPageSide::Left : BookPageSide::Right;
    for (const BookPageSide side : {first, second}) {
      const bool used = scan.use == BookPageUse::Both ||
                        (scan.use == BookPageUse::LeftOnly && side == BookPageSide::Left) ||
                        (scan.use == BookPageUse::RightOnly && side == BookPageSide::Right);
      if (used) {
        pages.push_back({BookPageRef::Kind::Scan, index, side, 0});
      }
    }
  }
  if (covers) {
    pages.push_back({BookPageRef::Kind::BackCover, -1, BookPageSide::Left, 0});
  }
  return pages;
}

std::vector<BookPageRef> book_page_order(const BookProject& project) {
  return project.custom_order ? *project.custom_order : generated_page_order(project);
}

std::vector<BookPageSize> measured_page_sizes(const BookProject& project) {
  std::vector<BookPageSize> sizes;
  for (const auto& scan : project.scans) {
    if (scan.dpi <= 0.0) {
      continue;
    }
    if (project.mode == BookScanMode::OnePage) {
      sizes.push_back({scan.layout.spread.width / scan.dpi, scan.layout.spread.height / scan.dpi});
      continue;
    }
    for (const BookPageSide side : {BookPageSide::Left, BookPageSide::Right}) {
      const bool used = scan.use == BookPageUse::Both ||
                        (scan.use == BookPageUse::LeftOnly && side == BookPageSide::Left) ||
                        (scan.use == BookPageUse::RightOnly && side == BookPageSide::Right);
      if (used) {
        const OrientedBox box = spread_page_box(scan.layout, side);
        sizes.push_back({box.width / scan.dpi, box.height / scan.dpi});
      }
    }
  }
  return sizes;
}

BookPageSize resolved_page_size(const BookProject& project) {
  BookPageSize size;
  const auto measured = measured_page_sizes(project);
  switch (project.size_policy) {
    case BookPageSizePolicy::Median:
      size = median_page_size(measured);
      break;
    case BookPageSizePolicy::Largest:
      size = largest_page_size(measured);
      break;
    case BookPageSizePolicy::Cover:
      if (project.cover) {
        size = cover_page_size(project.cover->layout, project.cover->dpi);
      }
      break;
    case BookPageSizePolicy::Custom:
      size = project.custom_size;
      break;
  }
  if ((size.width <= 0.0 || size.height <= 0.0) && project.cover) {
    size = cover_page_size(project.cover->layout, project.cover->dpi);
  }
  if (size.width <= 0.0 || size.height <= 0.0) {
    size = {kLetterWidth, kLetterHeight};
  }
  return size;
}

PixelBuffer book_pixels_from_image(const QImage& image) {
  if (image.isNull()) {
    return {};
  }
  const bool gray = image.isGrayscale();
  const QImage converted = image.convertToFormat(gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
  PixelBuffer pixels(converted.width(), converted.height(), gray ? PixelFormat::gray8() : PixelFormat::rgb8());
  const auto row_bytes = static_cast<std::size_t>(converted.width()) * (gray ? 1U : 3U);
  for (int y = 0; y < converted.height(); ++y) {
    std::memcpy(pixels.row(y).data(), converted.constScanLine(y), row_bytes);
  }
  return pixels;
}

QImage book_image_from_pixels(const PixelBuffer& pixels) {
  if (pixels.empty()) {
    return {};
  }
  const bool gray = pixels.format().channels == 1;
  QImage image(pixels.width(), pixels.height(), gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
  const auto row_bytes = static_cast<std::size_t>(pixels.width()) * (gray ? 1U : 3U);
  for (int y = 0; y < pixels.height(); ++y) {
    std::memcpy(image.scanLine(y), pixels.row(y).data(), row_bytes);
  }
  return image;
}

PixelBuffer load_upright_scan(const BookProject& project, const QString& relative_file, int turns) {
  if (!safe_relative_file(relative_file)) {
    return {};
  }
  QImageReader reader(QDir(project.folder).filePath(relative_file));
  reader.setAutoTransform(false);
  const PixelBuffer pixels = book_pixels_from_image(reader.read());
  if (pixels.empty()) {
    return {};
  }
  const int normalized = normalized_turns(turns);
  return normalized == 0 ? pixels : rotated_quarter_turns(pixels, normalized);
}

void BookScanCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

std::shared_ptr<const PixelBuffer> BookScanCache::get(const QString& file, int turns) {
  const int normalized = normalized_turns(turns);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->file == file && it->turns == normalized) {
        Entry hit = *it;
        entries_.erase(it);
        entries_.push_back(hit);
        return hit.pixels;
      }
    }
  }
  auto pixels = std::make_shared<const PixelBuffer>(load_upright_scan(project_, file, normalized));
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.push_back({file, normalized, pixels});
  while (entries_.size() > kCapacity) {
    entries_.erase(entries_.begin());
  }
  return pixels;
}

std::optional<BookPageSource> book_page_source(const BookProject& project, const BookPageRef& page,
                                               const BookPageSize& size) {
  switch (page.kind) {
    case BookPageRef::Kind::Scan: {
      if (page.scan < 0 || page.scan >= static_cast<int>(project.scans.size())) {
        return std::nullopt;
      }
      const auto& scan = project.scans[static_cast<std::size_t>(page.scan)];
      const double width_px = size.width * scan.dpi;
      const double height_px = size.height * scan.dpi;
      BookPageSource source;
      source.file = scan.file;
      source.dpi = scan.dpi;
      source.turns = normalized_turns(project.up_turns + scan.extra_turns);
      source.box = project.mode == BookScanMode::OnePage
                       ? centered_page_box(scan.layout.spread, width_px, height_px)
                       : uniform_spread_page_box(scan.layout, page.side, width_px, height_px);
      return source;
    }
    case BookPageRef::Kind::FrontCover:
    case BookPageRef::Kind::BackCover: {
      if (!project.cover) {
        return std::nullopt;
      }
      // Seen from outside, a left-to-right book has its front cover on the right.
      const bool front = page.kind == BookPageRef::Kind::FrontCover;
      const bool rtl = project.direction == BookReadingDirection::RightToLeft;
      const BookPageSide half = (front != rtl) ? BookPageSide::Right : BookPageSide::Left;
      BookPageSource source;
      source.file = project.cover->file;
      source.dpi = project.cover->dpi;
      source.turns = normalized_turns(project.up_turns);
      source.box = centered_page_box(cover_half_box(project.cover->layout, half), size.width * source.dpi,
                                     size.height * source.dpi);
      return source;
    }
    case BookPageRef::Kind::Blank:
      break;
  }
  return std::nullopt;
}

PixelBuffer render_book_page(const BookProject& project, const BookPageRef& page, const BookPageSize& size, int dpi,
                             BookScanCache& cache, bool apply_output) {
  PixelBuffer cut;
  const auto output_size = [&](double source_dpi) {
    const double out_dpi = dpi > 0 ? dpi : source_dpi;
    return std::pair<std::int32_t, std::int32_t>{
        std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(size.width * out_dpi))),
        std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(size.height * out_dpi)))};
  };
  if (page.kind == BookPageRef::Kind::Blank) {
    const auto [out_width, out_height] = output_size(project.capture_dpi);
    const bool gray = project.capture_mode == ScanColorMode::Grayscale || project.output.mode != BookOutputMode::Color;
    cut = PixelBuffer(out_width, out_height, gray ? PixelFormat::gray8() : PixelFormat::rgb8());
    cut.clear(255);
  } else {
    const auto source = book_page_source(project, page, size);
    if (!source) {
      return {};
    }
    const auto pixels = cache.get(source->file, source->turns);
    if (!pixels || pixels->empty()) {
      return {};
    }
    const auto [out_width, out_height] = output_size(source->dpi);
    cut = extract_book_page(*pixels, source->box, out_width, out_height);
  }
  if (cut.empty()) {
    return {};
  }
  if (normalized_turns(page.turns) != 0) {
    cut = rotated_quarter_turns(cut, page.turns);
  }
  return apply_output ? apply_book_output(cut, project.output) : cut;
}

}  // namespace patchy::ui
