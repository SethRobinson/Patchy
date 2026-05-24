#include "ui/image_document_io.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace photoslop::ui {

namespace {

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

std::uint8_t blend_channel(std::uint8_t src, std::uint8_t dst, BlendMode mode) {
  switch (mode) {
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return src;
    case BlendMode::Multiply:
      return static_cast<std::uint8_t>((static_cast<int>(src) * static_cast<int>(dst)) / 255);
    case BlendMode::Screen:
      return static_cast<std::uint8_t>(255 - ((255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::Overlay:
      if (dst < 128) {
        return static_cast<std::uint8_t>((2 * static_cast<int>(src) * static_cast<int>(dst)) / 255);
      }
      return static_cast<std::uint8_t>(255 - (2 * (255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::Darken:
      return std::min(src, dst);
    case BlendMode::Lighten:
      return std::max(src, dst);
    case BlendMode::ColorDodge:
      return src == 255 ? 255
                        : static_cast<std::uint8_t>(
                              std::min(255, (static_cast<int>(dst) * 255) / (255 - static_cast<int>(src))));
    case BlendMode::ColorBurn:
      return src == 0 ? 0
                      : static_cast<std::uint8_t>(
                            255 - std::min(255, ((255 - static_cast<int>(dst)) * 255) / static_cast<int>(src)));
    case BlendMode::HardLight:
      if (src < 128) {
        return static_cast<std::uint8_t>((2 * static_cast<int>(src) * static_cast<int>(dst)) / 255);
      }
      return static_cast<std::uint8_t>(255 - (2 * (255 - static_cast<int>(src)) * (255 - static_cast<int>(dst))) / 255);
    case BlendMode::Difference:
      return static_cast<std::uint8_t>(std::abs(static_cast<int>(dst) - static_cast<int>(src)));
  }
  return src;
}

std::string lower_extension(std::string_view extension) {
  std::string result(extension);
  if (!result.empty() && result.front() == '.') {
    result.erase(result.begin());
  }
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

void composite_layer(QImage& destination, const Layer& layer, bool preserve_alpha);

void composite_pixel_layer(QImage& destination, const Layer& layer, bool preserve_alpha) {
  if (!layer.visible() || layer.opacity() <= 0.0F || layer.kind() != LayerKind::Pixel) {
    return;
  }

  const auto& source = layer.pixels();
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return;
  }

  const auto bounds = layer.bounds();
  const auto left = std::max(0, bounds.x);
  const auto top = std::max(0, bounds.y);
  const auto right = std::min(destination.width(), bounds.x + source.width());
  const auto bottom = std::min(destination.height(), bounds.y + source.height());
  if (left >= right || top >= bottom) {
    return;
  }

  for (int y = top; y < bottom; ++y) {
    auto* destination_row = destination.scanLine(y);
    for (int x = left; x < right; ++x) {
      const auto sx = x - bounds.x;
      const auto sy = y - bounds.y;
      const auto* src = source.pixel(sx, sy);
      const auto source_alpha = source.format().channels >= 4 ? static_cast<float>(src[3]) / 255.0F : 1.0F;
      const auto sa = source_alpha * layer.opacity();
      if (sa <= 0.0F) {
        continue;
      }

      auto* dst = destination_row + static_cast<std::size_t>(x) * (preserve_alpha ? 4U : 3U);
      const auto da = preserve_alpha ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
      const auto out_a = preserve_alpha ? (sa + da * (1.0F - sa)) : 1.0F;
      const std::array<float, 3> dst_channels = {static_cast<float>(dst[0]), static_cast<float>(dst[1]),
                                                 static_cast<float>(dst[2])};
      std::array<float, 3> out_channels = {};
      for (int channel = 0; channel < 3; ++channel) {
        const auto src_channel = blend_channel(src[channel], static_cast<std::uint8_t>(dst_channels[channel]),
                                               layer.blend_mode());
        if (preserve_alpha && out_a > 0.0F) {
          out_channels[channel] =
              (static_cast<float>(src_channel) * sa + dst_channels[channel] * da * (1.0F - sa)) / out_a;
        } else {
          out_channels[channel] =
              static_cast<float>(src_channel) * sa + dst_channels[channel] * (1.0F - sa);
        }
      }

      dst[0] = clamp_byte(out_channels[0]);
      dst[1] = clamp_byte(out_channels[1]);
      dst[2] = clamp_byte(out_channels[2]);
      if (preserve_alpha) {
        dst[3] = clamp_byte(out_a * 255.0F);
      }
    }
  }
}

void composite_layer(QImage& destination, const Layer& layer, bool preserve_alpha) {
  if (layer.kind() == LayerKind::Group) {
    for (const auto& child : layer.children()) {
      composite_layer(destination, child, preserve_alpha);
    }
    return;
  }
  composite_pixel_layer(destination, layer, preserve_alpha);
}

}  // namespace

Document document_from_qimage(const QImage& image, std::string layer_name) {
  if (image.isNull()) {
    throw std::invalid_argument("Cannot import a null image");
  }

  const auto has_alpha = image.hasAlphaChannel();
  const auto converted = image.convertToFormat(has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  PixelBuffer pixels(converted.width(), converted.height(), has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());

  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      const auto color = converted.pixelColor(x, y);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (has_alpha) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }

  if (layer_name.empty()) {
    layer_name = "Imported Image";
  }
  Document document(converted.width(), converted.height(), has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  document.add_pixel_layer(std::move(layer_name), std::move(pixels));
  return document;
}

QImage qimage_from_document(const Document& document, bool preserve_alpha) {
  QImage image(document.width(), document.height(), preserve_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  if (preserve_alpha) {
    image.fill(Qt::transparent);
  } else {
    image.fill(Qt::white);
  }

  for (const auto& layer : document.layers()) {
    composite_layer(image, layer, preserve_alpha);
  }
  return image;
}

bool image_format_preserves_alpha(std::string_view extension) noexcept {
  const auto lower = lower_extension(extension);
  return lower == "png" || lower == "tif" || lower == "tiff" || lower == "webp";
}

}  // namespace photoslop::ui
