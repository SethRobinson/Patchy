#include "core/layer_render_utils.hpp"

#include "core/pixel_buffer.hpp"

#include <algorithm>

namespace photoslop {

Rect outset_rect(Rect rect, int amount) noexcept {
  return Rect{rect.x - amount, rect.y - amount, rect.width + amount * 2, rect.height + amount * 2};
}

Rect clipped_mask_bounds(Rect full_bounds, Rect draw_rect, int sample_padding) noexcept {
  return intersect_rect(full_bounds, outset_rect(draw_rect, std::max(0, sample_padding)));
}

Rect layer_pixel_bounds(const Layer& layer) {
  const auto& source = layer.pixels();
  return layer.bounds().empty() ? Rect::from_size(source.width(), source.height()) : layer.bounds();
}

float layer_mask_alpha_at(const Layer& layer, std::int32_t x, std::int32_t y) {
  const auto& mask = layer.mask();
  if (!mask.has_value() || mask->disabled) {
    return 1.0F;
  }
  if (mask->pixels.empty() || mask->pixels.format() != PixelFormat::gray8()) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }
  if (!mask->bounds.contains(x, y)) {
    return static_cast<float>(mask->default_color) / 255.0F;
  }

  const auto local_x = x - mask->bounds.x;
  const auto local_y = y - mask->bounds.y;
  return static_cast<float>(*mask->pixels.pixel(local_x, local_y)) / 255.0F;
}

std::vector<float> layer_alpha_mask(const Layer& layer, Rect bounds, Rect mask_bounds, std::int32_t sample_offset_x,
                                    std::int32_t sample_offset_y) {
  if (mask_bounds.empty()) {
    return {};
  }

  const auto& source = layer.pixels();
  const auto width = mask_bounds.width;
  const auto height = mask_bounds.height;
  std::vector<float> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0F);
  if (source.empty()) {
    return mask;
  }

  const auto format = source.format();
  const auto source_left = bounds.x - sample_offset_x;
  const auto source_top = bounds.y - sample_offset_y;
  const auto source_right = bounds.x + source.width() - sample_offset_x;
  const auto source_bottom = bounds.y + source.height() - sample_offset_y;
  const auto draw_left = std::max(mask_bounds.x, source_left);
  const auto draw_top = std::max(mask_bounds.y, source_top);
  const auto draw_right = std::min(mask_bounds.x + mask_bounds.width, source_right);
  const auto draw_bottom = std::min(mask_bounds.y + mask_bounds.height, source_bottom);
  if (draw_left >= draw_right || draw_top >= draw_bottom) {
    return mask;
  }

  if (format.channels < 4) {
    for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
      auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
      for (std::int32_t x = draw_left; x < draw_right; ++x) {
        *output++ = layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
      }
    }
    return mask;
  }

  const auto* bytes = source.data().data();
  const auto stride = source.stride_bytes();
  for (std::int32_t y = draw_top; y < draw_bottom; ++y) {
    const auto sy = y + sample_offset_y - bounds.y;
    const auto* source_row = bytes + static_cast<std::size_t>(sy) * stride;
    auto* output = mask.data() + static_cast<std::size_t>(y - mask_bounds.y) * width + (draw_left - mask_bounds.x);
    for (std::int32_t x = draw_left; x < draw_right; ++x) {
      const auto sx = x + sample_offset_x - bounds.x;
      const auto* pixel = source_row + static_cast<std::size_t>(sx) * format.channels;
      *output++ = (static_cast<float>(pixel[3]) / 255.0F) *
                  layer_mask_alpha_at(layer, x + sample_offset_x, y + sample_offset_y);
    }
  }
  return mask;
}

}  // namespace photoslop
