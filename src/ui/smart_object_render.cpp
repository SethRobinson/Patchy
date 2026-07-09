#include "ui/smart_object_render.hpp"

#include "formats/format_registry.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/image_document_io.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QFileInfo>
#include <QImageReader>
#include <QPolygonF>
#include <QString>
#include <QTransform>

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
      if (!image.isNull()) {
        return image;
      }
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
      return document_from_qimage(image, layer_name.empty() ? "Contents" : layer_name);
    }
    case SmartObjectContentsFormat::ReadOnly:
    case SmartObjectContentsFormat::Undecodable:
      return std::nullopt;
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
  const auto image = qt_decode(bytes);
  if (!image.isNull() && image.dotsPerMeterX() > 0) {
    const auto dpi = static_cast<double>(image.dotsPerMeterX()) * 0.0254;
    if (dpi > 1.0 && dpi < 100000.0) {
      return dpi;
    }
  }
  return 72.0;
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

bool refresh_smart_object_layer_preview(Document& document, Layer& layer,
                                        CanvasWidget::TransformInterpolation interpolation) {
  if (!layer_is_smart_object(layer) || !smart_object_lock_reason(layer).empty()) {
    return false;
  }
  const auto placement = smart_object_placement_from_layer(layer);
  if (!placement.has_value()) {
    return false;
  }
  const auto* source = document.metadata().smart_objects.find(placement->uuid);
  if (source == nullptr || source->kind != SmartObjectSourceKind::Embedded) {
    return false;
  }
  const auto image = decode_smart_object_source_image(*source);
  if (!image.has_value()) {
    return false;
  }
  auto rendered = render_smart_object_pixels(*image, *placement, interpolation);
  if (!rendered.has_value()) {
    return false;
  }
  layer.set_pixels(pixels_from_image_rgba(rendered->image));
  layer.set_bounds(rendered->bounds);
  layer.metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
  return true;
}

}  // namespace patchy::ui
