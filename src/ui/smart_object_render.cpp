#include "ui/smart_object_render.hpp"

#include "color/color_management.hpp"
#include "core/psd_source_colors.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_filter_effects.hpp"

#include "formats/format_registry.hpp"
#include "formats/image_density_probe.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPolygonF>
#include <QString>
#include <QTransform>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <string>

namespace patchy::ui {
namespace {

std::span<const std::uint8_t> source_bytes(const SmartObjectSource& source) {
  if (source.kind != SmartObjectSourceKind::Embedded || source.file_bytes == nullptr) {
    return {};
  }
  return {source.file_bytes->data(), source.file_bytes->size()};
}

std::string lower_extension(const std::string& filename) {
  const auto dot = filename.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= filename.size()) {
    return {};
  }
  auto extension = filename.substr(dot + 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

// Formats Qt can both decode and re-encode; the Edit Contents commit path re-embeds
// these as a flattened image in the original format.
bool qt_round_trip_extension(const std::string& extension) {
  static constexpr std::array<const char*, 7> kExtensions = {"png", "jpg", "jpeg", "tif",
                                                             "tiff", "bmp", "webp"};
  return std::any_of(kExtensions.begin(), kExtensions.end(),
                     [&extension](const char* candidate) { return extension == candidate; });
}

bool qt_can_decode(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  QByteArray raw = QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()),
                                           static_cast<qsizetype>(bytes.size()));
  QBuffer buffer(&raw);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  return reader.canRead();
}

QImage qt_decode(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {};
  }
  return QImage::fromData(reinterpret_cast<const uchar*>(bytes.data()), static_cast<int>(bytes.size()));
}

bool image_has_transparency(const QImage& image) {
  for (int y = 0; y < image.height(); ++y) {
    const auto* line = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      if (line[x * 4 + 3] != 255) {  // RGBA8888 byte order
        return true;
      }
    }
  }
  return false;
}

const FormatHandler* registry_handler_for(const SmartObjectSource& source,
                                          std::span<const std::uint8_t> bytes) {
  const auto& registry = builtin_format_registry();
  const auto extension = lower_extension(source.filename);
  if (!extension.empty()) {
    if (const auto* handler = registry.find_by_extension(extension);
        handler != nullptr && handler->read && (!handler->sniff || handler->sniff(bytes))) {
      return handler;
    }
  }
  for (const auto& handler : registry.handlers()) {
    if (handler.read && handler.sniff && handler.sniff(bytes)) {
      return &handler;
    }
  }
  return nullptr;
}

}  // namespace

QImage smart_object_image_for_document(const QImage& image, const Document& document) {
  const auto& space = document.metadata().psd_native_color_space;
  if (image.isNull() || space == nullptr || space->mode != 1) {
    return image;
  }
  auto result = image.convertToFormat(QImage::Format_RGBA8888);
  PixelBuffer rgb(result.width(), result.height(), PixelFormat::rgb8());
  for (int y = 0; y < result.height(); ++y) {
    const auto* input = result.constScanLine(y);
    auto output = rgb.row(y);
    for (int x = 0; x < result.width(); ++x) {
      std::copy_n(input + x * 4, 3, output.data() + x * 3);
    }
  }
  const auto to_rgb = GrayToRgbTransform::from_icc_profile(space->profile);
  const auto gray = to_rgb.has_value()
      ? rgb_to_native_color_space(rgb, ColorMode::Grayscale, space->profile) : std::nullopt;
  for (int y = 0; y < result.height(); ++y) {
    auto* output = result.scanLine(y);
    if (gray.has_value()) {
      auto converted = rgb.row(y);
      to_rgb->convert(gray->row(y).data(), converted.data(), static_cast<std::size_t>(result.width()));
      for (int x = 0; x < result.width(); ++x) {
        std::copy_n(converted.data() + x * 3, 3, output + x * 4);
      }
    } else {
      // Untagged/invalid-profile gray imports use neutral RGB. Keep the existing
      // alpha and use deterministic luminance for colored embedded sources.
      for (int x = 0; x < result.width(); ++x) {
        auto* pixel = output + x * 4;
        const auto value = static_cast<std::uint8_t>((30 * pixel[0] + 59 * pixel[1] + 11 * pixel[2] + 50) / 100);
        pixel[0] = pixel[1] = pixel[2] = value;
      }
    }
  }
  return result;
}

SmartObjectContentsFormat classify_smart_object_contents(const SmartObjectSource& source) {
  const auto bytes = source_bytes(source);
  if (bytes.empty()) {
    return SmartObjectContentsFormat::Undecodable;
  }
  if (psd::DocumentIo::can_read(bytes)) {
    return SmartObjectContentsFormat::PsdDocument;
  }
  const bool qt_readable = qt_can_decode(bytes);
  if (qt_readable && qt_round_trip_extension(lower_extension(source.filename))) {
    return SmartObjectContentsFormat::QtImage;
  }
  if (qt_readable) {
    return SmartObjectContentsFormat::ReadOnly;  // e.g. GIF: decodes, no lossless re-encode
  }
  if (registry_handler_for(source, bytes) != nullptr) {
    return SmartObjectContentsFormat::ReadOnly;
  }
  return SmartObjectContentsFormat::Undecodable;
}

std::optional<QImage> decode_smart_object_source_image(const SmartObjectSource& source) {
  const auto bytes = source_bytes(source);
  if (bytes.empty()) {
    return std::nullopt;
  }
  if (psd::DocumentIo::can_read(bytes)) {
    try {
      psd::ReadOptions options;
      options.preserve_unknown_blocks = false;
      // Photoshop's stored composite when present, so an untouched child renders as PS would.
      options.prefer_flat_composite = true;
      const auto child = psd::DocumentIo::read(bytes, options);
      auto image = qimage_from_document(child, true).convertToFormat(QImage::Format_RGBA8888);
      if (image.isNull()) {
        return std::nullopt;
      }
      if (image_has_transparency(image)) {
        return image;
      }
      // The stored composite claims a fully opaque canvas, which a 3-channel composite
      // claims even when the layers are transparent: Photoshop always ships a
      // "Transparency" alpha for transparent canvases, but pre-July-2026 Patchy wrote
      // 3-channel composites matted onto black. Verify against the layered render and
      // prefer it when it disagrees; genuinely opaque children keep the stored
      // composite (PS-exact for Photoshop sources).
      try {
        psd::ReadOptions layered_options;
        layered_options.preserve_unknown_blocks = false;
        const auto layered = psd::DocumentIo::read(bytes, layered_options);
        auto layered_image = qimage_from_document(layered, true).convertToFormat(QImage::Format_RGBA8888);
        if (!layered_image.isNull() && image_has_transparency(layered_image)) {
          return layered_image;
        }
      } catch (const std::exception&) {
      }
      return image;
    } catch (const std::exception&) {
    }
    return std::nullopt;
  }
  if (auto image = qt_decode(bytes); !image.isNull()) {
    return image.convertToFormat(QImage::Format_RGBA8888);
  }
  if (const auto* handler = registry_handler_for(source, bytes); handler != nullptr) {
    try {
      const auto result = handler->read(bytes);
      auto image = qimage_from_document(result.document, true).convertToFormat(QImage::Format_RGBA8888);
      if (!image.isNull()) {
        return image;
      }
    } catch (const std::exception&) {
    }
  }
  return std::nullopt;
}

std::optional<QImage> decode_smart_object_source_image(
    const SmartObjectSource& source, const QString& parent_document_dir) {
  if (source.kind == SmartObjectSourceKind::Embedded) {
    return decode_smart_object_source_image(source);
  }
  if (source.kind != SmartObjectSourceKind::ExternalFile) {
    return std::nullopt;
  }
  const auto path = resolve_smart_object_external_path(source, parent_document_dir);
  if (!path.has_value()) {
    return std::nullopt;
  }
  QFile file(*path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const auto bytes = file.readAll();
  if (bytes.isEmpty()) {
    return std::nullopt;
  }
  SmartObjectSource probe = source;
  probe.kind = SmartObjectSourceKind::Embedded;
  probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(
      bytes.begin(), bytes.end());
  return decode_smart_object_source_image(probe);
}

std::optional<Document> decode_smart_object_source_document(const SmartObjectSource& source) {
  const auto bytes = source_bytes(source);
  if (bytes.empty()) {
    return std::nullopt;
  }
  switch (classify_smart_object_contents(source)) {
    case SmartObjectContentsFormat::PsdDocument:
      try {
        // Defaults preserve unknown blocks (and nested smart objects) so the child
        // re-serializes faithfully on commit.
        return psd::DocumentIo::read(bytes);
      } catch (const std::exception&) {
        return std::nullopt;
      }
    case SmartObjectContentsFormat::QtImage: {
      const auto image = qt_decode(bytes);
      if (image.isNull()) {
        return std::nullopt;
      }
      const auto layer_name =
          QFileInfo(QString::fromStdString(source.filename)).completeBaseName().toStdString();
      // Deliberately no promote_flat_alpha_to_layer_mask here: the child stays one
      // plain RGBA layer so the commit flatten is trivially faithful.
      auto child = document_from_qimage(image, layer_name.empty() ? "Contents" : layer_name);
      apply_imported_image_density(child, bytes, image);
      return child;
    }
    case SmartObjectContentsFormat::ReadOnly:
    case SmartObjectContentsFormat::Undecodable:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<QString> resolve_smart_object_external_path(const SmartObjectSource& source,
                                                          const QString& parent_document_dir) {
  const auto existing = [](const QString& candidate) -> std::optional<QString> {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
      return QFileInfo(candidate).absoluteFilePath();
    }
    return std::nullopt;
  };
  if (!parent_document_dir.isEmpty() && !source.external_rel_path.empty()) {
    if (auto found = existing(QDir(parent_document_dir)
                                  .absoluteFilePath(QString::fromStdString(source.external_rel_path)))) {
      return found;
    }
  }
  // A bare filename fallback next to the parent covers files whose stored relative
  // path pointed elsewhere (the tent-file case after the folder was copied).
  if (!parent_document_dir.isEmpty() && !source.filename.empty()) {
    if (auto found =
            existing(QDir(parent_document_dir).absoluteFilePath(QString::fromStdString(source.filename)))) {
      return found;
    }
  }
  if (auto found = existing(QString::fromStdString(source.external_original_path))) {
    return found;
  }
  if (!source.external_full_path.empty()) {
    // Photoshop stores the URI undecorated (no percent-encoding), so the text after
    // "file://" is the path itself; a name holding '#' or '%' would not survive a
    // QUrl parse. Encoded URIs from other writers take the QUrl route below.
    const auto uri = QString::fromStdString(source.external_full_path);
    if (uri.startsWith(QStringLiteral("file://"))) {
      auto literal = uri.mid(7);
      if (literal.size() > 2 && literal.at(0) == QLatin1Char('/') && literal.at(2) == QLatin1Char(':')) {
        literal.remove(0, 1);  // "/D:/art" is the Windows drive path "D:/art"
      }
      if (QDir::isAbsolutePath(literal)) {
        if (auto found = existing(literal)) {
          return found;
        }
      }
    }
    const auto url = QUrl(uri);
    if (url.isLocalFile()) {
      if (auto found = existing(url.toLocalFile())) {
        return found;
      }
    }
  }
  return std::nullopt;
}

std::string smart_object_filetype_for_extension(const QString& extension) {
  if (extension == QStringLiteral("psb")) {
    return "8BPB";
  }
  if (extension == QStringLiteral("psd")) {
    return "8BPS";
  }
  if (extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg")) {
    return "JPEG";
  }
  if (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) {
    return "TIFF";
  }
  if (extension == QStringLiteral("bmp")) {
    return "BMP ";
  }
  if (extension == QStringLiteral("svg") || extension == QStringLiteral("svgz")) {
    return "SVG ";
  }
  return "png ";
}

void stamp_smart_object_link(SmartObjectSource& source, const QFileInfo& file) {
  const auto modified = file.lastModified().toUTC();
  source.external_mod_year = modified.date().year();
  source.external_mod_month = static_cast<std::uint8_t>(modified.date().month());
  source.external_mod_day = static_cast<std::uint8_t>(modified.date().day());
  source.external_mod_hour = static_cast<std::uint8_t>(modified.time().hour());
  source.external_mod_minute = static_cast<std::uint8_t>(modified.time().minute());
  source.external_mod_seconds = static_cast<double>(modified.time().second());
  source.external_file_size = static_cast<std::uint64_t>(file.size());
}

bool smart_object_link_changed_on_disk(const SmartObjectSource& source, const QFileInfo& file) {
  if (source.external_file_size != 0U && static_cast<std::uint64_t>(file.size()) != source.external_file_size) {
    return true;
  }
  if (source.external_mod_year == 0) {
    return false;  // no stamp recorded
  }
  const auto matches = [&source](const QDateTime& when) {
    return when.date().year() == source.external_mod_year && when.date().month() == source.external_mod_month &&
           when.date().day() == source.external_mod_day && when.time().hour() == source.external_mod_hour &&
           when.time().minute() == source.external_mod_minute &&
           when.time().second() == static_cast<int>(source.external_mod_seconds);
  };
  const auto modified = file.lastModified();
  return !matches(modified.toUTC()) && !matches(modified.toLocalTime());
}

void set_smart_object_link_target(SmartObjectSource& source, const QFileInfo& file, const QString& document_dir) {
  const auto absolute = file.absoluteFilePath();
  source.filename = file.fileName().toStdString();
  source.filetype = smart_object_filetype_for_extension(file.suffix().toLower());
  // Photoshop stores the URI undecorated: forward slashes, no percent-encoding, spaces
  // and non-ASCII characters as they are ("file:///D:/art/logo one.svg").
  source.external_full_path =
      (absolute.startsWith(QStringLiteral("//")) ? QUrl::fromLocalFile(absolute).toString()
                                                 : QStringLiteral("file://") +
                                                       (absolute.startsWith(QLatin1Char('/')) ? QString()
                                                                                              : QStringLiteral("/")) +
                                                       absolute)
          .toStdString();
  source.external_original_path = QDir::toNativeSeparators(absolute).toStdString();
  source.external_rel_path =
      (document_dir.isEmpty() ? file.fileName() : QDir(document_dir).relativeFilePath(absolute)).toStdString();
  stamp_smart_object_link(source, file);
  source.dirty = true;
}

bool refresh_smart_object_link_relative_paths(SmartObjectStore& store, const QString& current_document_dir,
                                              const QString& saved_document_dir) {
  if (saved_document_dir.isEmpty()) {
    return false;
  }
  bool changed = false;
  for (auto& block : store.blocks) {
    for (auto& source : block.sources) {
      if (source.kind != SmartObjectSourceKind::ExternalFile) {
        continue;
      }
      const auto resolved = resolve_smart_object_external_path(source, current_document_dir);
      if (!resolved.has_value()) {
        continue;
      }
      auto relative = QDir(saved_document_dir).relativeFilePath(*resolved).toStdString();
      if (relative == source.external_rel_path) {
        continue;
      }
      source.external_rel_path = std::move(relative);
      source.dirty = true;
      changed = true;
    }
  }
  return changed;
}

SmartObjectSource* find_smart_object_link_for_file(SmartObjectStore& store, const QFileInfo& file,
                                                   const QString& document_dir) {
  for (auto& block : store.blocks) {
    for (auto& source : block.sources) {
      if (source.kind != SmartObjectSourceKind::ExternalFile) {
        continue;
      }
      const auto resolved = resolve_smart_object_external_path(source, document_dir);
      if (resolved.has_value() && QFileInfo(*resolved) == file) {
        return &source;
      }
    }
  }
  return nullptr;
}

std::optional<SmartObjectSource> load_smart_object_file_probe(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const auto raw = file.readAll();
  if (raw.isEmpty()) {
    return std::nullopt;
  }
  const QFileInfo info(path);
  SmartObjectSource probe;
  probe.kind = SmartObjectSourceKind::Embedded;
  probe.filename = info.fileName().toStdString();
  probe.filetype = smart_object_filetype_for_extension(info.suffix().toLower());
  probe.file_bytes = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.end());
  return probe;
}

bool smart_object_contents_are_vector(const SmartObjectSource& source) {
  const auto bytes = source_bytes(source);
  if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      psd::DocumentIo::can_read(bytes)) {
    return false;
  }
  QByteArray raw = QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()),
                                           static_cast<qsizetype>(bytes.size()));
  QBuffer buffer(&raw);
  buffer.open(QIODevice::ReadOnly);
  const auto format = QImageReader(&buffer).format();
  return format == "svg" || format == "svgz";
}

std::optional<QImage> render_smart_object_vector_contents(const SmartObjectSource& source,
                                                          const SmartObjectPlacement& placement) {
  if (!smart_object_contents_are_vector(source)) {
    return std::nullopt;
  }
  // The quad's own edge lengths: the top edge carries the artwork's width, the left
  // edge its height, whatever the rotation.
  const auto& quad = placement.transform;
  const double width = std::hypot(quad[2] - quad[0], quad[3] - quad[1]);
  const double height = std::hypot(quad[6] - quad[0], quad[7] - quad[1]);
  if (!std::isfinite(width) || !std::isfinite(height) || width < 0.5 || height < 0.5) {
    return std::nullopt;
  }
  // A side that is a whole number of pixels renders at exactly that size (an
  // axis-aligned placement on whole pixels then needs no resampling at all).
  const auto side = [](double length) {
    constexpr double kMaxSide = 16384.0;
    const double limited = length < kMaxSide ? length : kMaxSide;
    const double rounded = std::round(limited);
    return static_cast<int>(std::abs(limited - rounded) < 1e-6 ? rounded : std::ceil(limited));
  };
  const auto bytes = source_bytes(source);
  QByteArray raw = QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()),
                                           static_cast<qsizetype>(bytes.size()));
  QBuffer buffer(&raw);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  reader.setScaledSize(QSize(side(width), side(height)));
  auto image = reader.read();
  if (image.isNull()) {
    return std::nullopt;
  }
  return image.convertToFormat(QImage::Format_RGBA8888);
}

double smart_object_source_dpi(const SmartObjectSource& source) {
  const auto bytes = source_bytes(source);
  if (bytes.empty()) {
    return 72.0;
  }
  if (psd::DocumentIo::can_read(bytes)) {
    try {
      psd::ReadOptions options;
      options.preserve_unknown_blocks = false;
      options.prefer_flat_composite = true;
      return psd::DocumentIo::read(bytes, options).print_settings().horizontal_ppi;
    } catch (const std::exception&) {
      return 72.0;
    }
  }
  // Same policy as opening the file: an explicit recorded density counts, an
  // untagged source places at Photoshop's 72 PPI convention (never Qt's
  // screen-derived dotsPerMeter default).
  const auto probe = formats::probe_image_density(bytes);
  if (probe.density.has_value()) {
    const auto dpi = probe.density->horizontal_ppi;
    if (dpi > 1.0 && dpi < 100000.0) {
      return dpi;
    }
    return 72.0;
  }
  if (probe.container == formats::ImageDensityContainer::Unrecognized) {
    if (const auto explicit_density = explicit_qimage_density_ppi(qt_decode(bytes));
        explicit_density.has_value() && explicit_density->first > 1.0 &&
        explicit_density->first < 100000.0) {
      return explicit_density->first;
    }
  }
  return 72.0;
}

std::optional<TransformedImage> render_smart_object_pixels(
    const QImage& source_image, const SmartObjectPlacement& placement,
    const std::optional<SmartObjectWarp>& warp, CanvasWidget::TransformInterpolation interpolation) {
  if (warp.has_value() && !warp->mesh_xs.empty()) {
    WarpMeshGrid mesh;
    mesh.u_order = warp->u_order;
    mesh.v_order = warp->v_order;
    mesh.xs = warp->mesh_xs;
    mesh.ys = warp->mesh_ys;
    // Commit-quality grid: 4 px cells, clamped (preview paths may pass coarser later).
    const auto grid = build_warp_surface_grid(mesh, placement.transform, source_image.width(),
                                              source_image.height(), 4.0, 128);
    if (grid.has_value()) {
      auto rendered = resample_warped_rgba8(source_image, *grid, interpolation);
      if (!rendered.image.isNull()) {
        return rendered;
      }
    }
    return std::nullopt;
  }
  return render_smart_object_pixels(source_image, placement, interpolation);
}

std::optional<TransformedImage> render_smart_object_pixels(
    const QImage& source_image, const SmartObjectPlacement& placement,
    CanvasWidget::TransformInterpolation interpolation) {
  if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0) {
    return std::nullopt;
  }
  const auto width = static_cast<qreal>(source_image.width());
  const auto height = static_cast<qreal>(source_image.height());
  const QPolygonF source_quad({QPointF(0.0, 0.0), QPointF(width, 0.0), QPointF(width, height),
                               QPointF(0.0, height)});
  const auto& quad = placement.transform;
  const QPolygonF target_quad({QPointF(quad[0], quad[1]), QPointF(quad[2], quad[3]),
                               QPointF(quad[4], quad[5]), QPointF(quad[6], quad[7])});
  QTransform source_to_document;
  if (!QTransform::quadToQuad(source_quad, target_quad, source_to_document)) {
    return std::nullopt;
  }
  return resample_transformed_rgba8(source_image, source_to_document, interpolation);
}

std::optional<SmartObjectLayerPreview> render_smart_object_layer_preview(
    const Document& document, const Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    const SmartFilterStack* override_stack,
    const QString& parent_document_dir,
    SmartObjectSourceRenderCache* cache) {
  const auto unfiltered = render_smart_object_unfiltered_layer_preview(
      document, layer, interpolation, parent_document_dir, cache);
  if (!unfiltered.has_value()) {
    return std::nullopt;
  }
  SmartObjectLayerPreview result;
  result.unfiltered = *unfiltered;
  const auto* stack = override_stack != nullptr ? override_stack
                                                 : layer.smart_filter_stack();
  try {
    result.rendered = stack == nullptr
                          ? result.unfiltered
                          : render_smart_filter_stack(result.unfiltered.pixels,
                                                      result.unfiltered.bounds,
                                                      Rect::from_size(
                                                          document.width(),
                                                          document.height()),
                                                      *stack);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return result;
}

std::optional<SmartObjectLinkProblem> smart_object_link_problem(
    const Document& document, const Layer& layer, const QString& parent_document_dir) {
  if (!layer_is_smart_object(layer)) {
    return std::nullopt;
  }
  const auto* source = document.metadata().smart_objects.find(smart_object_source_uuid(layer));
  if (source == nullptr || source->kind != SmartObjectSourceKind::ExternalFile) {
    return std::nullopt;
  }
  const auto path = resolve_smart_object_external_path(*source, parent_document_dir);
  if (!path.has_value()) {
    return SmartObjectLinkProblem::missing;
  }
  const auto contents = load_smart_object_file_probe(*path);
  if (!contents.has_value() || !decode_smart_object_source_image(*contents).has_value()) {
    return SmartObjectLinkProblem::unreadable;
  }
  return std::nullopt;
}

std::optional<FilterRenderResult> render_smart_object_unfiltered_layer_preview(
    const Document& document, const Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    const QString& parent_document_dir,
    SmartObjectSourceRenderCache* cache) {
  const auto lock = smart_object_lock_reason(layer);
  if (!layer_is_smart_object(layer) ||
      (!lock.empty() && lock != "external")) {
    return std::nullopt;
  }
  const auto placement = smart_object_placement_from_layer(layer);
  if (!placement.has_value()) {
    return std::nullopt;
  }
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  if (source == nullptr) {
    return std::nullopt;
  }
  // The cache entry, when a caller renders several layers from one store: the file
  // read and the natural-size decode happen once per source, every layer after the
  // first only rasterizes at its own placement.
  SmartObjectSourceRenderCache::Entry local_entry;
  auto& entry = cache != nullptr ? cache->entries[placement->uuid] : local_entry;
  const SmartObjectSource* contents = source;
  if (source->kind == SmartObjectSourceKind::ExternalFile) {
    // Linked contents live on disk: read them once so the vector pass has the bytes.
    if (!entry.resolved) {
      entry.resolved = true;
      if (const auto path = resolve_smart_object_external_path(*source, parent_document_dir); path.has_value()) {
        entry.linked_contents = load_smart_object_file_probe(*path);
      }
    }
    if (!entry.linked_contents.has_value()) {
      return std::nullopt;
    }
    contents = &*entry.linked_contents;
  }
  if (!entry.image.has_value()) {
    entry.image = decode_smart_object_source_image(*contents);
    if (!entry.image.has_value()) {
      return std::nullopt;
    }
    entry.image = smart_object_image_for_document(*entry.image, document);
  }
  auto image = entry.image;
  const auto warp = smart_object_warp_from_layer(layer);
  if (!warp.has_value() || warp->mesh_xs.empty()) {
    // Vector artwork rasterizes at the placement's scale; the warp grid is built
    // for the natural size, so warped placements keep the natural image.
    if (auto vector = render_smart_object_vector_contents(*contents, *placement); vector.has_value()) {
      image = smart_object_image_for_document(*vector, document);
    }
  }
  const auto rendered = render_smart_object_pixels(
      *image, *placement, warp, interpolation);
  if (!rendered.has_value()) {
    return std::nullopt;
  }
  return FilterRenderResult{pixels_from_image_rgba(rendered->image),
                            rendered->bounds};
}

std::optional<SmartObjectLayerPreview> render_smart_object_image_preview(
    const QImage& source_image, const SmartObjectPlacement& placement,
    const std::optional<SmartObjectWarp>& warp,
    CanvasWidget::TransformInterpolation interpolation,
    const SmartFilterStack* stack, const Document& document) {
  auto rendered =
      render_smart_object_pixels(smart_object_image_for_document(source_image, document), placement, warp, interpolation);
  if (!rendered.has_value()) {
    return std::nullopt;
  }
  SmartObjectLayerPreview result;
  result.unfiltered =
      FilterRenderResult{pixels_from_image_rgba(rendered->image), rendered->bounds};
  try {
    result.rendered = stack == nullptr
                          ? result.unfiltered
                          : render_smart_filter_stack(result.unfiltered.pixels,
                                                      result.unfiltered.bounds,
                                                      Rect::from_size(document.width(), document.height()),
                                                      *stack);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return result;
}

bool install_smart_object_layer_preview(Document& document, Layer& layer,
                                        SmartObjectLayerPreview preview,
                                        bool refresh_native_cache) {
  auto filter_effects = document.metadata().smart_filter_effects;
  if (const auto* stack = std::as_const(layer).smart_filter_stack();
      stack != nullptr) {
    const auto placed_uuid = smart_object_placed_uuid(std::as_const(layer));
    if (placed_uuid.empty()) {
      return false;
    }
    if (refresh_native_cache) {
      auto record = psd::author_filter_effects_record(
          placed_uuid, Rect::from_size(document.width(), document.height()),
          preview.unfiltered.pixels, preview.unfiltered.bounds, stack->mask);
      if (!record.has_value() ||
          !filter_effects.upsert_authored(std::move(*record))) {
        return false;
      }
    } else {
      const auto* record = filter_effects.find_unique(placed_uuid);
      if (record == nullptr || !record->semantic_supported()) {
        return false;
      }
    }
  }

  document.metadata().smart_filter_effects = std::move(filter_effects);
  layer.set_pixels(std::move(preview.rendered.pixels));
  layer.set_bounds(preview.rendered.bounds);
  layer.metadata()[kLayerMetadataSmartObjectRasterStatus] =
      kSmartObjectRasterStatusPatchy;
  return true;
}

bool refresh_smart_object_layer_preview(
    Document& document, Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    bool refresh_native_cache, const QString& parent_document_dir,
    SmartObjectSourceRenderCache* cache) {
  auto rendered = render_smart_object_layer_preview(
      std::as_const(document), std::as_const(layer), interpolation, nullptr,
      parent_document_dir, cache);
  if (!rendered.has_value()) {
    return false;
  }

  return install_smart_object_layer_preview(
      document, layer, std::move(*rendered), refresh_native_cache);
}

}  // namespace patchy::ui
