#include "ui/image_document_io.hpp"

#include "core/blend_math.hpp"
#include "core/rect_utils.hpp"
#include "render/layer_compositor.hpp"
#include "support/string_utils.hpp"

#include <QColor>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

class QImageCompositeTarget {
public:
  QImageCompositeTarget(QImage& destination, bool preserve_alpha, std::int32_t origin_x, std::int32_t origin_y)
      : destination_(destination), preserve_alpha_(preserve_alpha), origin_x_(origin_x), origin_y_(origin_y) {}

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (alpha <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    const auto da = preserve_alpha_ ? static_cast<float>(dst[3]) / 255.0F : 1.0F;
    const auto out_a = preserve_alpha_ ? (alpha + da * (1.0F - alpha)) : 1.0F;
    const std::array<std::uint8_t, 3> src = {color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> dst_rgb = {dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(src, dst_rgb, mode, alpha, da);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    if (preserve_alpha_) {
      dst[3] = clamp_byte(out_a * 255.0F);
    }
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    const auto image_x = x - origin_x_;
    const auto image_y = y - origin_y_;
    if (amount <= 0.0F || image_x < 0 || image_y < 0 || image_x >= destination_.width() ||
        image_y >= destination_.height()) {
      return;
    }

    auto* dst = destination_.scanLine(image_y) + static_cast<std::size_t>(image_x) * (preserve_alpha_ ? 4U : 3U);
    if (preserve_alpha_ && dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  QImage& destination_;
  bool preserve_alpha_{false};
  std::int32_t origin_x_{0};
  std::int32_t origin_y_{0};
};

std::string lower_extension(std::string_view extension) {
  return normalized_extension(extension, false);
}

double sanitized_ppi(double value) noexcept {
  return std::isfinite(value) && value > 0.0 ? value : 300.0;
}

double ppi_from_dots_per_meter(int dots_per_meter) noexcept {
  return dots_per_meter > 0 ? static_cast<double>(dots_per_meter) * 0.0254 : 300.0;
}

int dots_per_meter_from_ppi(double ppi) noexcept {
  return std::clamp(static_cast<int>(std::lround(sanitized_ppi(ppi) / 0.0254)), 1, 1000000);
}

void apply_document_resolution(QImage& image, const Document& document) {
  if (image.isNull()) {
    return;
  }
  image.setDotsPerMeterX(dots_per_meter_from_ppi(document.print_settings().horizontal_ppi));
  image.setDotsPerMeterY(dots_per_meter_from_ppi(document.print_settings().vertical_ppi));
}

QImage render_document_rect(const Document& document, QRect document_rect, bool preserve_alpha,
                            const std::vector<render_detail::LayerBoundsOverride>* overrides) {
  const auto normalized = document_rect.normalized();
  const auto requested = Rect{normalized.x(), normalized.y(), normalized.width(), normalized.height()};
  const auto clip = intersect_rect(Rect::from_size(document.width(), document.height()), requested);
  if (clip.empty()) {
    return {};
  }

  QImage image(clip.width, clip.height, preserve_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  if (preserve_alpha) {
    image.fill(Qt::transparent);
  } else {
    image.fill(Qt::white);
  }

  QImageCompositeTarget target(image, preserve_alpha, clip.x, clip.y);
  for (const auto& layer : document.layers()) {
    render_detail::composite_layer(target, layer, clip, overrides, false);
  }
  apply_document_resolution(image, document);
  return image;
}

}  // namespace

PixelBuffer pixels_from_image_rgba(const QImage& image) {
  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  PixelBuffer pixels(converted.width(), converted.height(), PixelFormat::rgba8());
  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      const auto color = converted.pixelColor(x, y);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      px[3] = static_cast<std::uint8_t>(color.alpha());
    }
  }
  return pixels;
}

Document document_from_qimage(const QImage& image, std::string layer_name) {
  if (image.isNull()) {
    throw std::invalid_argument("Cannot import a null image");
  }

  const auto has_alpha = image.hasAlphaChannel();
  const auto converted = image.convertToFormat(has_alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  auto pixels = has_alpha ? pixels_from_image_rgba(converted)
                          : PixelBuffer(converted.width(), converted.height(), PixelFormat::rgb8());

  if (!has_alpha) {
    for (int y = 0; y < converted.height(); ++y) {
      for (int x = 0; x < converted.width(); ++x) {
        const auto color = converted.pixelColor(x, y);
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
      }
    }
  }

  if (layer_name.empty()) {
    layer_name = "Imported Image";
  }
  Document document(converted.width(), converted.height(), has_alpha ? PixelFormat::rgba8() : PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = ppi_from_dots_per_meter(image.dotsPerMeterX());
  document.print_settings().vertical_ppi = ppi_from_dots_per_meter(image.dotsPerMeterY());
  document.add_pixel_layer(std::move(layer_name), std::move(pixels));
  return document;
}

QImage qimage_from_document(const Document& document, bool preserve_alpha) {
  return render_document_rect(document, QRect(0, 0, document.width(), document.height()), preserve_alpha, nullptr);
}

QImage qimage_from_document_rect(const Document& document, QRect document_rect, bool preserve_alpha) {
  return render_document_rect(document, document_rect, preserve_alpha, nullptr);
}

QImage qimage_from_document_rect_with_layer_bounds(
    const Document& document, QRect document_rect, bool preserve_alpha,
    const std::vector<std::pair<LayerId, Rect>>& layer_bounds) {
  std::vector<render_detail::LayerBoundsOverride> overrides;
  overrides.reserve(layer_bounds.size());
  for (const auto& [layer_id, bounds] : layer_bounds) {
    overrides.push_back(render_detail::LayerBoundsOverride{layer_id, bounds});
  }
  return render_document_rect(document, document_rect, preserve_alpha, &overrides);
}

QImage qimage_from_document_rect_with_layer_bounds(const Document& document, QRect document_rect, bool preserve_alpha,
                                                   LayerId layer_id, Rect layer_bounds) {
  const std::vector<std::pair<LayerId, Rect>> layer_bounds_overrides{{layer_id, layer_bounds}};
  return qimage_from_document_rect_with_layer_bounds(document, document_rect, preserve_alpha, layer_bounds_overrides);
}

bool image_format_preserves_alpha(std::string_view extension) noexcept {
  const auto lower = lower_extension(extension);
  return lower == "png" || lower == "tif" || lower == "tiff" || lower == "webp";
}

}  // namespace patchy::ui
