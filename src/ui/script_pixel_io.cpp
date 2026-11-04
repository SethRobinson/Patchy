// Pixel exchange for scripts and companion apps (docs/companions.md): doc.selection.getMask
// and setMask, layer.getLayerMask, setLayerMask and removeLayerMask, doc.getCompositePixels,
// and patchy.io.readImage and writeImage. Every name, option key and token here is permanent
// (docs/scripting.md). Large blocks cross the process boundary as image files, never as
// inlined arrays, so a companion's script stays far below the connector's source bound.
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"

#include <QByteArray>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace patchy::ui {
namespace {

// The document size limit (script_api.cpp); a block larger than this is a JS error.
constexpr int kMaxScriptDimension = 30000;

// Throws a JS error naming the first key of `value` that is not in `allowed`.
bool reject_unknown_keys(ScriptEngineHost& host, const QJSValue& value, const QStringList& allowed,
                         const QString& function) {
  QJSValueIterator it(value);
  while (it.hasNext()) {
    it.next();
    if (!allowed.contains(it.name())) {
      host.throw_js_error(ScriptEngineHost::tr("%1: unknown option \"%2\".").arg(function, it.name()));
      return false;
    }
  }
  return true;
}

bool is_integer(const QJSValue& value) {
  if (!value.isNumber()) {
    return false;
  }
  const double number = value.toNumber();
  return std::isfinite(number) && number == std::floor(number) && std::abs(number) <= 1.0e9;
}

// {x, y, width, height} with integer fields and a size within the document limit.
std::optional<QRect> parse_rect(ScriptEngineHost& host, const QJSValue& value, const QString& function) {
  if (!value.isObject() || value.isArray()) {
    host.throw_js_error(ScriptEngineHost::tr("%1 needs a {x, y, width, height} rectangle.").arg(function));
    return std::nullopt;
  }
  const auto x = value.property(QStringLiteral("x"));
  const auto y = value.property(QStringLiteral("y"));
  const auto width = value.property(QStringLiteral("width"));
  const auto height = value.property(QStringLiteral("height"));
  if (!is_integer(x) || !is_integer(y) || !is_integer(width) || !is_integer(height) || width.toInt() < 1 ||
      height.toInt() < 1 || width.toInt() > kMaxScriptDimension || height.toInt() > kMaxScriptDimension) {
    host.throw_js_error(ScriptEngineHost::tr("%1 needs integer x, y, width and height, with a size from 1 to %2.")
                            .arg(function)
                            .arg(kMaxScriptDimension));
    return std::nullopt;
  }
  return QRect(x.toInt(), y.toInt(), width.toInt(), height.toInt());
}

// A {x, y, width, height, data} block with `channels` bytes per pixel. x and y default to 0.
struct PixelBlock {
  QRect rect;
  QByteArray data;
};

std::optional<PixelBlock> parse_block(ScriptEngineHost& host, const QJSValue& value, int channels,
                                      const QStringList& extra_keys, const QString& function) {
  if (!value.isObject() || value.isArray()) {
    host.throw_js_error(ScriptEngineHost::tr("%1 needs a {width, height, data} object.").arg(function));
    return std::nullopt;
  }
  if (!reject_unknown_keys(host, value, QStringList{"x", "y", "width", "height", "data"} + extra_keys, function)) {
    return std::nullopt;
  }
  const auto x = value.property(QStringLiteral("x"));
  const auto y = value.property(QStringLiteral("y"));
  const auto width = value.property(QStringLiteral("width"));
  const auto height = value.property(QStringLiteral("height"));
  if ((!x.isUndefined() && !is_integer(x)) || (!y.isUndefined() && !is_integer(y)) || !is_integer(width) ||
      !is_integer(height) || width.toInt() < 1 || height.toInt() < 1 || width.toInt() > kMaxScriptDimension ||
      height.toInt() > kMaxScriptDimension) {
    host.throw_js_error(ScriptEngineHost::tr("%1 needs integer x, y, width and height, with a size from 1 to %2.")
                            .arg(function)
                            .arg(kMaxScriptDimension));
    return std::nullopt;
  }
  PixelBlock block;
  block.rect = QRect(x.isUndefined() ? 0 : x.toInt(), y.isUndefined() ? 0 : y.toInt(), width.toInt(), height.toInt());
  block.data = value.property(QStringLiteral("data")).toVariant().toByteArray();
  if (block.data.size() != static_cast<qsizetype>(block.rect.width()) * block.rect.height() * channels) {
    host.throw_js_error(channels == 1
                            ? ScriptEngineHost::tr("%1: data must hold width * height bytes.").arg(function)
                            : ScriptEngineHost::tr("%1: data must hold width * height * 4 RGBA bytes.").arg(function));
    return std::nullopt;
  }
  return block;
}

QJSValue block_to_js(QJSEngine* engine, QRect rect, QByteArray data) {
  auto value = rect_to_js(engine, Rect{rect.x(), rect.y(), rect.width(), rect.height()});
  value.setProperty(QStringLiteral("data"), engine->toScriptValue(std::move(data)));
  return value;
}

QByteArray gray8_bytes(const PixelBuffer& plane) {
  QByteArray data(static_cast<qsizetype>(plane.width()) * plane.height(), Qt::Uninitialized);
  for (std::int32_t y = 0; y < plane.height(); ++y) {
    const auto row = plane.row(y);
    std::copy(row.begin(), row.end(), data.data() + static_cast<qsizetype>(y) * plane.width());
  }
  return data;
}

// `block` (1 byte per pixel) laid over `target`, 0 where it does not reach.
PixelBuffer gray8_over(const PixelBlock& block, QRect target) {
  PixelBuffer plane(target.width(), target.height(), PixelFormat::gray8());
  plane.clear(0);
  const auto overlap = block.rect.intersected(target);
  for (int y = overlap.top(); y <= overlap.bottom(); ++y) {
    const auto* source = reinterpret_cast<const std::uint8_t*>(block.data.constData()) +
                         static_cast<qsizetype>(y - block.rect.y()) * block.rect.width() +
                         (overlap.x() - block.rect.x());
    auto row = plane.row(y - target.y());
    std::copy_n(source, overlap.width(), row.begin() + (overlap.x() - target.x()));
  }
  return plane;
}

// Mask density is a percentage in the API (0..100) and a raw 0..255 value in the model.
std::uint8_t density_from_percent(double percent) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(percent * 255.0 / 100.0), 0L, 255L));
}

}  // namespace

// ---------------------------------------------------------------------------
// ScriptEngineHost: the session's selection as coverage

PixelBuffer ScriptEngineHost::selection_coverage(std::int64_t session_id, QRect rect) const {
  auto* canvas = session_canvas(session_id);
  if (canvas == nullptr || rect.isEmpty()) {
    return {};
  }
  return canvas->selection_coverage_buffer(rect);
}

void ScriptEngineHost::set_selection_coverage(std::int64_t session_id, QRect bounds, const PixelBuffer& coverage) {
  auto* canvas = session_canvas(session_id);
  if (canvas == nullptr) {
    return;
  }
  bool any = false;
  for (std::int32_t y = 0; y < coverage.height() && !any; ++y) {
    const auto row = coverage.row(y);
    any = std::any_of(row.begin(), row.end(), [](std::uint8_t value) { return value != 0U; });
  }
  if (!any) {
    canvas->clear_selection();
    return;
  }
  canvas->set_selection_from_coverage(bounds, coverage);
}

// ---------------------------------------------------------------------------
// doc.selection.getMask / setMask

QJSValue ScriptSelectionObject::getMask(const QJSValue& rect) const {
  const ScriptApiCall api_call(host_);
  const auto* document = host_.session_document_const(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return {};
  }
  const QRect canvas(0, 0, document->width(), document->height());
  QRect area;
  if (rect.isUndefined() || rect.isNull()) {
    if (!host_.has_selection(session_id_)) {
      return QJSValue(QJSValue::NullValue);
    }
    area = host_.selection_region(session_id_).boundingRect().intersected(canvas);
  } else {
    const auto requested = parse_rect(host_, rect, QStringLiteral("getMask"));
    if (!requested.has_value()) {
      return {};
    }
    area = requested->intersected(canvas);
    if (area.isEmpty()) {
      host_.throw_js_error(ScriptEngineHost::tr("%1: the rectangle is outside the canvas.").arg(QStringLiteral("getMask")));
      return {};
    }
  }
  if (area.isEmpty()) {
    return QJSValue(QJSValue::NullValue);
  }
  return block_to_js(host_.engine(), area, gray8_bytes(host_.selection_coverage(session_id_, area)));
}

void ScriptSelectionObject::setMask(const QJSValue& mask, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  const auto block = parse_block(host_, mask, 1, {}, QStringLiteral("setMask"));
  if (!block.has_value()) {
    return;
  }
  QString operation = QStringLiteral("replace");
  if (!options.isUndefined() && !options.isNull()) {
    if (!options.isObject() || options.isArray()) {
      host_.throw_js_error(ScriptEngineHost::tr("%1 options must be an object.").arg(QStringLiteral("setMask")));
      return;
    }
    if (!reject_unknown_keys(host_, options, {QStringLiteral("operation")}, QStringLiteral("setMask"))) {
      return;
    }
    const auto value = options.property(QStringLiteral("operation"));
    if (!value.isUndefined()) {
      operation = value.toString();
    }
  }
  const QStringList operations{"replace", "add", "subtract", "intersect"};
  if (!operations.contains(operation)) {
    host_.throw_js_error(
        ScriptEngineHost::tr("setMask: operation must be \"replace\", \"add\", \"subtract\" or \"intersect\"."));
    return;
  }
  const auto* document = host_.session_document_const(session_id_);
  if (document == nullptr) {
    host_.throw_js_error(ScriptEngineHost::tr("The document is no longer open."));
    return;
  }
  const QRect canvas(0, 0, document->width(), document->height());
  const QRect current =
      host_.has_selection(session_id_) ? host_.selection_region(session_id_).boundingRect() : QRect();
  const QRect target = (operation == QLatin1String("replace") ? block->rect : block->rect.united(current))
                           .intersected(canvas);
  if (target.isEmpty()) {
    // Nothing of the mask lands on the canvas: replace and intersect leave nothing selected.
    if (operation == QLatin1String("replace") || operation == QLatin1String("intersect")) {
      host_.deselect(session_id_);
    }
    return;
  }
  auto result = gray8_over(*block, target);
  if (operation != QLatin1String("replace")) {
    const auto existing = host_.selection_coverage(session_id_, target);
    for (std::int32_t y = 0; y < result.height(); ++y) {
      auto row = result.row(y);
      const auto old_row = existing.row(y);
      for (std::size_t x = 0; x < row.size(); ++x) {
        const unsigned m = row[x];
        const unsigned c = old_row[x];
        if (operation == QLatin1String("add")) {
          row[x] = static_cast<std::uint8_t>(std::max(m, c));
        } else if (operation == QLatin1String("subtract")) {
          row[x] = static_cast<std::uint8_t>((c * (255U - m) + 127U) / 255U);
        } else {
          row[x] = static_cast<std::uint8_t>((c * m + 127U) / 255U);
        }
      }
    }
  }
  host_.set_selection_coverage(session_id_, target, result);
}

// ---------------------------------------------------------------------------
// layer.getLayerMask / setLayerMask / removeLayerMask

QJSValue ScriptLayerObject::getLayerMask() const {
  const ScriptApiCall api_call(host_);
  const auto* layer = read_layer();
  if (layer == nullptr) {
    return {};
  }
  const auto& mask = layer->mask();
  if (!mask.has_value()) {
    return QJSValue(QJSValue::NullValue);
  }
  const auto plane = mask->pixels.format().bit_depth != BitDepth::UInt8
                         ? convert_pixel_buffer_depth(mask->pixels, BitDepth::UInt8, SampleKind::Coverage)
                         : mask->pixels;
  const QRect rect(mask->bounds.x, mask->bounds.y, plane.width(), plane.height());
  auto value = block_to_js(host_.engine(), rect, plane.empty() ? QByteArray() : gray8_bytes(plane));
  value.setProperty(QStringLiteral("defaultColor"), static_cast<int>(mask->default_color));
  value.setProperty(QStringLiteral("density"), static_cast<double>(mask->density) * 100.0 / 255.0);
  value.setProperty(QStringLiteral("feather"), mask->feather);
  value.setProperty(QStringLiteral("disabled"), mask->disabled);
  return value;
}

void ScriptLayerObject::setLayerMask(const QJSValue& mask) {
  const ScriptApiCall api_call(host_);
  const QString function = QStringLiteral("setLayerMask");
  if (!mask.isObject() || mask.isArray()) {
    host_.throw_js_error(ScriptEngineHost::tr("setLayerMask needs a mask object."));
    return;
  }
  const auto* document = host_.session_document_const(session_id_);
  const auto* current = read_layer();
  if (document == nullptr || current == nullptr) {
    return;
  }
  if (current->kind() != LayerKind::Pixel && current->kind() != LayerKind::Adjustment &&
      current->kind() != LayerKind::Group) {
    host_.throw_js_error(ScriptEngineHost::tr("setLayerMask needs a pixel, adjustment, or group layer."));
    return;
  }
  if (layer_effectively_locks_image_pixels(document->layers(), layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("Layer pixels are locked."));
    return;
  }
  const QStringList settings{"defaultColor", "density", "feather"};
  const bool from_selection = mask.property(QStringLiteral("fromSelection")).toBool();
  LayerMask result;
  if (from_selection) {
    if (!reject_unknown_keys(host_, mask, QStringList{"fromSelection", "invert"} + settings, function)) {
      return;
    }
    if (!host_.has_selection(session_id_)) {
      host_.throw_js_error(ScriptEngineHost::tr("setLayerMask: fromSelection needs a selection."));
      return;
    }
    const QRect canvas(0, 0, document->width(), document->height());
    const auto area = host_.selection_region(session_id_).boundingRect().intersected(canvas);
    auto coverage = host_.selection_coverage(session_id_, area);
    const bool invert = mask.property(QStringLiteral("invert")).toBool();
    if (invert) {
      for (std::int32_t y = 0; y < coverage.height(); ++y) {
        for (auto& value : coverage.row(y)) {
          value = static_cast<std::uint8_t>(255U - value);
        }
      }
    }
    result.bounds = Rect{area.x(), area.y(), area.width(), area.height()};
    result.pixels = std::move(coverage);
    result.default_color = invert ? 255 : 0;
  } else {
    const auto block = parse_block(host_, mask, 1, settings, function);
    if (!block.has_value()) {
      return;
    }
    result.bounds = Rect{block->rect.x(), block->rect.y(), block->rect.width(), block->rect.height()};
    result.pixels = gray8_over(*block, block->rect);
    result.default_color = 0;
  }
  const auto default_color = mask.property(QStringLiteral("defaultColor"));
  if (!default_color.isUndefined()) {
    if (!is_integer(default_color) || (default_color.toInt() != 0 && default_color.toInt() != 255)) {
      host_.throw_js_error(ScriptEngineHost::tr("setLayerMask: defaultColor must be 0 or 255."));
      return;
    }
    result.default_color = static_cast<std::uint8_t>(default_color.toInt());
  }
  const auto density = mask.property(QStringLiteral("density"));
  if (!density.isUndefined()) {
    if (!density.isNumber() || !std::isfinite(density.toNumber()) || density.toNumber() < 0.0 ||
        density.toNumber() > 100.0) {
      host_.throw_js_error(ScriptEngineHost::tr("setLayerMask: %1 must be a number from %2 to %3.")
                               .arg(QStringLiteral("density"))
                               .arg(0)
                               .arg(100));
      return;
    }
    result.density = density_from_percent(density.toNumber());
  }
  const auto feather = mask.property(QStringLiteral("feather"));
  if (!feather.isUndefined()) {
    if (!feather.isNumber() || !std::isfinite(feather.toNumber()) || feather.toNumber() < 0.0 ||
        feather.toNumber() > 1000.0) {
      host_.throw_js_error(ScriptEngineHost::tr("setLayerMask: %1 must be a number from %2 to %3.")
                               .arg(QStringLiteral("feather"))
                               .arg(0)
                               .arg(1000));
      return;
    }
    result.feather = feather.toNumber();
  }
  result.pixels = coverage_at_document_depth(*document, std::move(result.pixels));
  const Rect canvas = Rect::from_size(document->width(), document->height());
  auto* layer = write_layer();
  if (layer == nullptr) {
    return;
  }
  layer->set_mask(std::move(result));
  sync_layer_mask_feather_canvas(*layer, canvas);
  host_.note_structure_changed(session_id_);
}

bool ScriptLayerObject::removeLayerMask() {
  const ScriptApiCall api_call(host_);
  const auto* document = host_.session_document_const(session_id_);
  const auto* current = read_layer();
  if (document == nullptr || current == nullptr || !current->mask().has_value()) {
    return false;
  }
  if (layer_effectively_locks_image_pixels(document->layers(), layer_id_)) {
    host_.throw_js_error(ScriptEngineHost::tr("Layer pixels are locked."));
    return false;
  }
  auto* layer = write_layer();
  if (layer == nullptr) {
    return false;
  }
  layer->clear_mask();
  host_.note_structure_changed(session_id_);
  return true;
}

// ---------------------------------------------------------------------------
// doc.getCompositePixels

QJSValue ScriptDocumentObject::getCompositePixels(const QJSValue& rect) const {
  const ScriptApiCall api_call(host_);
  const auto* document = read_document();
  if (document == nullptr) {
    return {};
  }
  const QRect canvas(0, 0, document->width(), document->height());
  QRect area = canvas;
  if (!rect.isUndefined() && !rect.isNull()) {
    const auto requested = parse_rect(host_, rect, QStringLiteral("getCompositePixels"));
    if (!requested.has_value()) {
      return {};
    }
    area = requested->intersected(canvas);
    if (area.isEmpty()) {
      host_.throw_js_error(
          ScriptEngineHost::tr("%1: the rectangle is outside the canvas.").arg(QStringLiteral("getCompositePixels")));
      return {};
    }
  }
  // 16 and 32-bit documents come back encoded to 8-bit display values (docs/high-bit-depth-formats.md).
  const auto pixels =
      flatten_document_rgba8_rect(*document, Rect{area.x(), area.y(), area.width(), area.height()});
  QByteArray data(static_cast<qsizetype>(area.width()) * area.height() * 4, Qt::Uninitialized);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto row = pixels.row(y);
    std::copy(row.begin(), row.end(), data.data() + static_cast<qsizetype>(y) * area.width() * 4);
  }
  return block_to_js(host_.engine(), area, std::move(data));
}

// ---------------------------------------------------------------------------
// patchy.io.readImage / writeImage

QJSValue ScriptIoObject::readImage(const QString& path, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  bool grayscale = false;
  if (!options.isUndefined() && !options.isNull()) {
    if (!options.isObject() || options.isArray()) {
      host_.throw_js_error(ScriptEngineHost::tr("%1 options must be an object.").arg(QStringLiteral("readImage")));
      return {};
    }
    if (!reject_unknown_keys(host_, options, {QStringLiteral("grayscale")}, QStringLiteral("readImage"))) {
      return {};
    }
    grayscale = options.property(QStringLiteral("grayscale")).toBool();
  }
  QImageReader reader(path);
  reader.setAutoTransform(false);
  const auto size = reader.size();
  if (size.isValid() && (size.width() > kMaxScriptDimension || size.height() > kMaxScriptDimension)) {
    host_.throw_js_error(
        ScriptEngineHost::tr("readImage: %1 is larger than %2 pixels on a side.").arg(path).arg(kMaxScriptDimension));
    return {};
  }
  const auto image = reader.read();
  if (image.isNull()) {
    host_.throw_js_error(ScriptEngineHost::tr("readImage: could not read %1 (%2).").arg(path, reader.errorString()));
    return {};
  }
  if (image.width() > kMaxScriptDimension || image.height() > kMaxScriptDimension) {
    host_.throw_js_error(
        ScriptEngineHost::tr("readImage: %1 is larger than %2 pixels on a side.").arg(path).arg(kMaxScriptDimension));
    return {};
  }
  // Grayscale is the luminance of the color with alpha ignored: how a saved mask reads back.
  const int channels = grayscale ? 1 : 4;
  const auto decoded = image.convertToFormat(grayscale ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888);
  const auto row_bytes = static_cast<qsizetype>(decoded.width()) * channels;
  QByteArray data(row_bytes * decoded.height(), Qt::Uninitialized);
  for (int y = 0; y < decoded.height(); ++y) {
    std::copy_n(decoded.constScanLine(y), row_bytes, data.data() + y * row_bytes);
  }
  return block_to_js(host_.engine(), QRect(0, 0, decoded.width(), decoded.height()), std::move(data));
}

void ScriptIoObject::writeImage(const QString& path, const QJSValue& imageData) {
  const ScriptApiCall api_call(host_);
  if (!imageData.isObject() || imageData.isArray()) {
    host_.throw_js_error(
        ScriptEngineHost::tr("%1 needs a {width, height, data} object.").arg(QStringLiteral("writeImage")));
    return;
  }
  // 4 bytes per pixel writes RGBA; 1 byte per pixel (a mask from getMask or getLayerMask)
  // writes grayscale.
  const auto width = imageData.property(QStringLiteral("width")).toInt();
  const auto height = imageData.property(QStringLiteral("height")).toInt();
  const auto bytes = imageData.property(QStringLiteral("data")).toVariant().toByteArray().size();
  const int channels = width > 0 && height > 0 && bytes == static_cast<qsizetype>(width) * height ? 1 : 4;
  const auto block = parse_block(host_, imageData, channels, {"defaultColor", "density", "feather", "disabled"},
                                 QStringLiteral("writeImage"));
  if (!block.has_value()) {
    return;
  }
  QByteArray format = QFileInfo(path).suffix().toLower().toLatin1();
  if (format.isEmpty()) {
    format = "png";
  }
  if (!QImageWriter::supportedImageFormats().contains(format)) {
    host_.throw_js_error(ScriptEngineHost::tr("writeImage: unsupported image format \"%1\".")
                             .arg(QString::fromLatin1(format)));
    return;
  }
  QImage image(block->rect.width(), block->rect.height(),
               channels == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGBA8888);
  if (image.isNull()) {
    host_.throw_js_error(ScriptEngineHost::tr("writeImage: could not allocate the image."));
    return;
  }
  const auto row_bytes = static_cast<qsizetype>(block->rect.width()) * channels;
  for (int y = 0; y < image.height(); ++y) {
    std::copy_n(block->data.constData() + y * row_bytes, row_bytes, image.scanLine(y));
  }
  // A sibling temporary and a replacing rename: a failed write keeps the previous file.
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !image.save(&file, format.constData()) || !file.commit()) {
    host_.throw_js_error(ScriptEngineHost::tr("writeImage: could not write %1.").arg(path));
  }
}

}  // namespace patchy::ui
