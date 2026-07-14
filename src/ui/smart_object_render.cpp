#include "ui/smart_object_render.hpp"

#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_filter_effects.hpp"

#include "formats/format_registry.hpp"
#include "formats/image_density_probe.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"

#include <QBuffer>
#include <QByteArray>
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
    const auto url = QUrl(QString::fromStdString(source.external_full_path));
    if (url.isLocalFile()) {
      if (auto found = existing(url.toLocalFile())) {
        return found;
      }
    }
  }
  return std::nullopt;
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
    const QString& parent_document_dir) {
  const auto unfiltered = render_smart_object_unfiltered_layer_preview(
      document, layer, interpolation, parent_document_dir);
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

std::optional<FilterRenderResult> render_smart_object_unfiltered_layer_preview(
    const Document& document, const Layer& layer,
    CanvasWidget::TransformInterpolation interpolation,
    const QString& parent_document_dir) {
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
  const auto image = decode_smart_object_source_image(
      *source, parent_document_dir);
  if (!image.has_value()) {
    return std::nullopt;
  }
  const auto rendered = render_smart_object_pixels(
      *image, *placement, smart_object_warp_from_layer(layer), interpolation);
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
    const SmartFilterStack* stack, Rect document_bounds) {
  auto rendered =
      render_smart_object_pixels(source_image, placement, warp, interpolation);
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
                                                      document_bounds,
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
    bool refresh_native_cache, const QString& parent_document_dir) {
  const auto rendered = render_smart_object_layer_preview(
      std::as_const(document), std::as_const(layer), interpolation, nullptr,
      parent_document_dir);
  if (!rendered.has_value()) {
    return false;
  }

  return install_smart_object_layer_preview(
      document, layer, std::move(*rendered), refresh_native_cache);
}

}  // namespace patchy::ui
