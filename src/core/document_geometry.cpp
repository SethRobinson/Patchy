// Document and layer geometry operations (crop, rotate, flip, image/canvas resize, and the
// DocumentChannel geometry counterparts) split out of pixel_tools.cpp as pure moves; bodies
// are verbatim. Helpers shared with the painting half stay in pixel_tools.cpp and are
// declared in core/pixel_tools_internal.hpp (clamp_byte comes from core/blend_math.hpp).

#include "core/pixel_tools.hpp"

#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/pixel_tools_internal.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace patchy {

namespace {

void crop_layer_mask_to_rect(Layer& layer, Rect crop);
void rotate_layer_mask_clockwise(Layer& layer, std::int32_t document_height);
void rotate_layer_mask_counterclockwise(Layer& layer, std::int32_t document_width);

void crop_layer_to_rect(Layer& layer, Rect crop) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      crop_layer_to_rect(child, crop);
    }
    return;
  }
  crop_layer_mask_to_rect(layer, crop);
  if (layer.kind() != LayerKind::Pixel) {
    if (!layer.bounds().empty()) {
      const auto intersection = intersect_rect(layer.bounds(), crop);
      layer.set_bounds(intersection.empty()
                           ? Rect{}
                           : Rect{intersection.x - crop.x, intersection.y - crop.y, intersection.width,
                                  intersection.height});
    }
    return;
  }

  const auto old_bounds = layer.bounds();
  auto& old_pixels = layer.pixels();
  const auto intersection = intersect_rect(old_bounds, crop);
  if (intersection.empty()) {
    PixelBuffer empty(0, 0, old_pixels.format());
    layer.set_pixels(std::move(empty));
    layer.set_bounds({});
    return;
  }

  PixelBuffer cropped(intersection.width, intersection.height, old_pixels.format());
  const auto pixel_bytes = bytes_per_pixel(old_pixels.format());
  for (std::int32_t y = 0; y < intersection.height; ++y) {
    const auto source_y = intersection.y - old_bounds.y + y;
    const auto source_x = intersection.x - old_bounds.x;
    auto source = old_pixels.row(source_y).subspan(static_cast<std::size_t>(source_x) * pixel_bytes,
                                                   static_cast<std::size_t>(intersection.width) * pixel_bytes);
    auto destination = cropped.row(y);
    std::copy(source.begin(), source.end(), destination.begin());
  }

  layer.set_pixels(std::move(cropped));
  layer.set_bounds(Rect{intersection.x - crop.x, intersection.y - crop.y, intersection.width, intersection.height});
}

void rotate_layer_clockwise(Layer& layer, std::int32_t document_height) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      rotate_layer_clockwise(child, document_height);
    }
    return;
  }
  const auto old_bounds = layer.bounds();
  rotate_layer_mask_clockwise(layer, document_height);
  if (layer.kind() != LayerKind::Pixel) {
    if (!old_bounds.empty()) {
      layer.set_bounds(Rect{document_height - old_bounds.y - old_bounds.height, old_bounds.x, old_bounds.height,
                            old_bounds.width});
    }
    return;
  }

  auto& old_pixels = layer.pixels();
  PixelBuffer rotated(old_pixels.height(), old_pixels.width(), old_pixels.format());
  const auto channels = old_pixels.format().channels;
  for (std::int32_t y = 0; y < old_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < old_pixels.width(); ++x) {
      const auto* src = old_pixels.pixel(x, y);
      auto* dst = rotated.pixel(old_pixels.height() - 1 - y, x);
      std::copy(src, src + channels, dst);
    }
  }

  layer.set_pixels(std::move(rotated));
  layer.set_bounds(Rect{document_height - old_bounds.y - old_bounds.height, old_bounds.x, old_bounds.height,
                        old_bounds.width});
}

void rotate_layer_counterclockwise(Layer& layer, std::int32_t document_width) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      rotate_layer_counterclockwise(child, document_width);
    }
    return;
  }
  const auto old_bounds = layer.bounds();
  rotate_layer_mask_counterclockwise(layer, document_width);
  if (layer.kind() != LayerKind::Pixel) {
    if (!old_bounds.empty()) {
      layer.set_bounds(Rect{old_bounds.y, document_width - old_bounds.x - old_bounds.width, old_bounds.height,
                            old_bounds.width});
    }
    return;
  }

  auto& old_pixels = layer.pixels();
  PixelBuffer rotated(old_pixels.height(), old_pixels.width(), old_pixels.format());
  const auto channels = old_pixels.format().channels;
  for (std::int32_t y = 0; y < old_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < old_pixels.width(); ++x) {
      const auto* src = old_pixels.pixel(x, y);
      auto* dst = rotated.pixel(y, old_pixels.width() - 1 - x);
      std::copy(src, src + channels, dst);
    }
  }

  layer.set_pixels(std::move(rotated));
  layer.set_bounds(Rect{old_bounds.y, document_width - old_bounds.x - old_bounds.width, old_bounds.height,
                        old_bounds.width});
}

void flip_pixels_horizontal(PixelBuffer& pixels) {
  if (pixels.empty()) {
    return;
  }
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  std::vector<std::uint8_t> temp(pixel_bytes);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width() / 2; ++x) {
      auto* left = row.data() + static_cast<std::size_t>(x) * pixel_bytes;
      auto* right = row.data() + static_cast<std::size_t>(pixels.width() - 1 - x) * pixel_bytes;
      std::copy(left, left + pixel_bytes, temp.begin());
      std::copy(right, right + pixel_bytes, left);
      std::copy(temp.begin(), temp.end(), right);
    }
  }
}

void flip_pixels_vertical(PixelBuffer& pixels) {
  if (pixels.empty()) {
    return;
  }
  std::vector<std::uint8_t> temp(pixels.stride_bytes());
  for (std::int32_t y = 0; y < pixels.height() / 2; ++y) {
    auto top = pixels.row(y);
    auto bottom = pixels.row(pixels.height() - 1 - y);
    std::copy(top.begin(), top.end(), temp.begin());
    std::copy(bottom.begin(), bottom.end(), top.begin());
    std::copy(temp.begin(), temp.end(), bottom.begin());
  }
}

void crop_layer_mask_to_rect(Layer& layer, Rect crop) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }

  const auto old_bounds = mask->bounds;
  const auto intersection = intersect_rect(old_bounds, crop);
  if (intersection.empty()) {
    mask->bounds = {};
    mask->pixels = PixelBuffer(0, 0, PixelFormat::gray8());
    return;
  }

  PixelBuffer cropped(intersection.width, intersection.height, PixelFormat::gray8());
  for (std::int32_t y = 0; y < intersection.height; ++y) {
    const auto source_y = intersection.y - old_bounds.y + y;
    const auto source_x = intersection.x - old_bounds.x;
    auto source = mask->pixels.row(source_y).subspan(static_cast<std::size_t>(source_x),
                                                     static_cast<std::size_t>(intersection.width));
    auto destination = cropped.row(y);
    std::copy(source.begin(), source.end(), destination.begin());
  }

  mask->pixels = std::move(cropped);
  mask->bounds = Rect{intersection.x - crop.x, intersection.y - crop.y, intersection.width, intersection.height};
}

void rotate_layer_mask_clockwise(Layer& layer, std::int32_t document_height) {
  auto& mask = layer.mask();
  if (!mask.has_value() || mask->pixels.empty()) {
    return;
  }

  const auto old_bounds = mask->bounds;
  PixelBuffer rotated(mask->pixels.height(), mask->pixels.width(), PixelFormat::gray8());
  for (std::int32_t y = 0; y < mask->pixels.height(); ++y) {
    for (std::int32_t x = 0; x < mask->pixels.width(); ++x) {
      *rotated.pixel(mask->pixels.height() - 1 - y, x) = *mask->pixels.pixel(x, y);
    }
  }

  mask->pixels = std::move(rotated);
  mask->bounds = Rect{document_height - old_bounds.y - old_bounds.height, old_bounds.x, old_bounds.height,
                      old_bounds.width};
}

void rotate_layer_mask_counterclockwise(Layer& layer, std::int32_t document_width) {
  auto& mask = layer.mask();
  if (!mask.has_value() || mask->pixels.empty()) {
    return;
  }

  const auto old_bounds = mask->bounds;
  PixelBuffer rotated(mask->pixels.height(), mask->pixels.width(), PixelFormat::gray8());
  for (std::int32_t y = 0; y < mask->pixels.height(); ++y) {
    for (std::int32_t x = 0; x < mask->pixels.width(); ++x) {
      *rotated.pixel(y, mask->pixels.width() - 1 - x) = *mask->pixels.pixel(x, y);
    }
  }

  mask->pixels = std::move(rotated);
  mask->bounds = Rect{old_bounds.y, document_width - old_bounds.x - old_bounds.width, old_bounds.height,
                      old_bounds.width};
}

void flip_layer_mask_horizontal(Layer& layer, Rect layer_bounds) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }
  flip_pixels_horizontal(mask->pixels);
  mask->bounds.x = layer_bounds.x + layer_bounds.width - (mask->bounds.x - layer_bounds.x) - mask->bounds.width;
}

void flip_layer_mask_vertical(Layer& layer, Rect layer_bounds) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }
  flip_pixels_vertical(mask->pixels);
  mask->bounds.y = layer_bounds.y + layer_bounds.height - (mask->bounds.y - layer_bounds.y) - mask->bounds.height;
}

struct CanvasResizeOffset {
  std::int32_t x{0};
  std::int32_t y{0};
};

[[nodiscard]] std::int32_t canvas_anchor_axis_offset(std::int32_t old_extent, std::int32_t new_extent,
                                                     int anchor_position) noexcept {
  const auto delta = new_extent - old_extent;
  switch (anchor_position) {
    case 0:
      return 0;
    case 1:
      return delta / 2;
    case 2:
      return delta;
    default:
      return 0;
  }
}

[[nodiscard]] CanvasResizeOffset canvas_resize_offset(CanvasAnchor anchor, std::int32_t old_width,
                                                      std::int32_t old_height, std::int32_t new_width,
                                                      std::int32_t new_height) noexcept {
  int column = 1;
  int row = 1;
  switch (anchor) {
    case CanvasAnchor::TopLeft:
      column = 0;
      row = 0;
      break;
    case CanvasAnchor::Top:
      column = 1;
      row = 0;
      break;
    case CanvasAnchor::TopRight:
      column = 2;
      row = 0;
      break;
    case CanvasAnchor::Left:
      column = 0;
      row = 1;
      break;
    case CanvasAnchor::Center:
      column = 1;
      row = 1;
      break;
    case CanvasAnchor::Right:
      column = 2;
      row = 1;
      break;
    case CanvasAnchor::BottomLeft:
      column = 0;
      row = 2;
      break;
    case CanvasAnchor::Bottom:
      column = 1;
      row = 2;
      break;
    case CanvasAnchor::BottomRight:
      column = 2;
      row = 2;
      break;
  }
  return CanvasResizeOffset{canvas_anchor_axis_offset(old_width, new_width, column),
                            canvas_anchor_axis_offset(old_height, new_height, row)};
}

void shift_layer_mask_to_canvas(Layer& layer, CanvasResizeOffset offset, std::int32_t width, std::int32_t height) {
  auto& mask = layer.mask();
  if (!mask.has_value() || width <= 0 || height <= 0) {
    return;
  }

  const auto shifted_bounds =
      Rect{mask->bounds.x + offset.x, mask->bounds.y + offset.y, mask->bounds.width, mask->bounds.height};
  const auto clipped = intersect_rect(shifted_bounds, Rect{0, 0, width, height});
  if (clipped.empty() || mask->pixels.empty()) {
    mask->bounds = {};
    mask->pixels = PixelBuffer(0, 0, PixelFormat::gray8());
    return;
  }

  PixelBuffer shifted(clipped.width, clipped.height, PixelFormat::gray8());
  for (std::int32_t y = 0; y < clipped.height; ++y) {
    const auto source_y = clipped.y - shifted_bounds.y + y;
    const auto source_x = clipped.x - shifted_bounds.x;
    const auto source =
        mask->pixels.row(source_y).subspan(static_cast<std::size_t>(source_x), static_cast<std::size_t>(clipped.width));
    auto destination = shifted.row(y);
    std::copy(source.begin(), source.end(), destination.begin());
  }

  mask->pixels = std::move(shifted);
  mask->bounds = clipped;
}

void resize_layer_to_canvas(Layer& layer, std::int32_t width, std::int32_t height, CanvasResizeOffset offset,
                            EditColor extension_color) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      resize_layer_to_canvas(child, width, height, offset, extension_color);
    }
    shift_layer_mask_to_canvas(layer, offset, width, height);
    if (!layer.bounds().empty()) {
      const auto bounds = layer.bounds();
      layer.set_bounds(Rect{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height});
    }
    return;
  }
  if (layer.kind() != LayerKind::Pixel || width <= 0 || height <= 0) {
    shift_layer_mask_to_canvas(layer, offset, width, height);
    if (!layer.bounds().empty()) {
      const auto bounds = layer.bounds();
      layer.set_bounds(Rect{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height});
    }
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto& source = layer.pixels();
  PixelBuffer resized(width, height, canvas_resized_format_for_layer(layer, source, extension_color));
  fill_resized_layer_background(resized, layer, extension_color);

  if (!source.empty()) {
    for (std::int32_t sy = 0; sy < source.height(); ++sy) {
      const auto document_y = old_bounds.y + sy + offset.y;
      if (document_y < 0 || document_y >= height) {
        continue;
      }
      for (std::int32_t sx = 0; sx < source.width(); ++sx) {
        const auto document_x = old_bounds.x + sx + offset.x;
        if (document_x < 0 || document_x >= width) {
          continue;
        }
        copy_resized_layer_pixel(source, resized, sx, sy, document_x, document_y);
      }
    }
  }

  shift_layer_mask_to_canvas(layer, offset, width, height);
  layer.set_pixels(std::move(resized));
  layer.set_bounds(Rect{0, 0, width, height});
}

[[nodiscard]] std::int32_t scaled_dimension_edge(std::int32_t edge, std::int32_t old_extent,
                                                 std::int32_t new_extent) noexcept {
  if (old_extent <= 0) {
    return edge;
  }
  return static_cast<std::int32_t>(
      std::llround((static_cast<double>(edge) * static_cast<double>(new_extent)) / static_cast<double>(old_extent)));
}

[[nodiscard]] Rect scale_document_rect(Rect rect, std::int32_t old_width, std::int32_t old_height,
                                       std::int32_t new_width, std::int32_t new_height) noexcept {
  rect = normalized_rect(rect);
  if (rect.empty()) {
    return {};
  }

  const auto left = scaled_dimension_edge(rect.x, old_width, new_width);
  const auto top = scaled_dimension_edge(rect.y, old_height, new_height);
  auto right = scaled_dimension_edge(rect.x + rect.width, old_width, new_width);
  auto bottom = scaled_dimension_edge(rect.y + rect.height, old_height, new_height);
  if (right <= left) {
    right = left + 1;
  }
  if (bottom <= top) {
    bottom = top + 1;
  }
  return Rect{left, top, right - left, bottom - top};
}

void copy_nearest_scaled_pixels(const PixelBuffer& source, PixelBuffer& scaled) {
  const auto pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < scaled.height(); ++y) {
    const auto sy =
        std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(y) * source.height()) / scaled.height()), 0,
                   source.height() - 1);
    for (std::int32_t x = 0; x < scaled.width(); ++x) {
      const auto sx =
          std::clamp(static_cast<std::int32_t>((static_cast<std::int64_t>(x) * source.width()) / scaled.width()), 0,
                     source.width() - 1);
      const auto* src = source.pixel(sx, sy);
      auto* dst = scaled.pixel(x, y);
      std::copy(src, src + pixel_bytes, dst);
    }
  }
}

[[nodiscard]] PixelBuffer scale_pixels_resampled(const PixelBuffer& source, std::int32_t width,
                                                 std::int32_t height) {
  PixelBuffer scaled(width, height, source.format());
  if (source.empty() || width <= 0 || height <= 0) {
    return scaled;
  }

  if (source.format().bit_depth != BitDepth::UInt8) {
    copy_nearest_scaled_pixels(source, scaled);
    return scaled;
  }

  const auto channels = source.format().channels;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto source_y =
        ((static_cast<double>(y) + 0.5) * static_cast<double>(source.height()) / static_cast<double>(height)) - 0.5;
    const auto y0 = std::clamp(static_cast<std::int32_t>(std::floor(source_y)), 0, source.height() - 1);
    const auto y1 = std::clamp(y0 + 1, 0, source.height() - 1);
    const auto ty = std::clamp(source_y - static_cast<double>(y0), 0.0, 1.0);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto source_x =
          ((static_cast<double>(x) + 0.5) * static_cast<double>(source.width()) / static_cast<double>(width)) - 0.5;
      const auto x0 = std::clamp(static_cast<std::int32_t>(std::floor(source_x)), 0, source.width() - 1);
      const auto x1 = std::clamp(x0 + 1, 0, source.width() - 1);
      const auto tx = std::clamp(source_x - static_cast<double>(x0), 0.0, 1.0);
      const auto* top_left = source.pixel(x0, y0);
      const auto* top_right = source.pixel(x1, y0);
      const auto* bottom_left = source.pixel(x0, y1);
      const auto* bottom_right = source.pixel(x1, y1);
      auto* dst = scaled.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        const auto top =
            static_cast<double>(top_left[channel]) * (1.0 - tx) + static_cast<double>(top_right[channel]) * tx;
        const auto bottom =
            static_cast<double>(bottom_left[channel]) * (1.0 - tx) + static_cast<double>(bottom_right[channel]) * tx;
        dst[channel] = clamp_byte(static_cast<float>(top * (1.0 - ty) + bottom * ty));
      }
    }
  }
  return scaled;
}

void resize_layer_mask_image(Layer& layer, std::int32_t old_width, std::int32_t old_height,
                             std::int32_t new_width, std::int32_t new_height) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }

  const auto new_bounds = scale_document_rect(mask->bounds, old_width, old_height, new_width, new_height);
  if (mask->pixels.empty()) {
    mask->bounds = new_bounds;
    return;
  }

  mask->pixels = scale_pixels_resampled(mask->pixels, new_bounds.width, new_bounds.height);
  mask->bounds = new_bounds;
}

void resize_layer_image(Layer& layer, std::int32_t old_width, std::int32_t old_height, std::int32_t new_width,
                        std::int32_t new_height) {
  resize_layer_mask_image(layer, old_width, old_height, new_width, new_height);
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      resize_layer_image(child, old_width, old_height, new_width, new_height);
    }
    if (!layer.bounds().empty()) {
      layer.set_bounds(scale_document_rect(layer.bounds(), old_width, old_height, new_width, new_height));
    }
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto new_bounds = scale_document_rect(old_bounds, old_width, old_height, new_width, new_height);
  if (layer.kind() != LayerKind::Pixel) {
    if (!old_bounds.empty()) {
      layer.set_bounds(new_bounds);
    }
    return;
  }

  const auto& source = layer.pixels();
  if (source.empty() || new_bounds.empty()) {
    PixelBuffer empty(0, 0, source.format());
    layer.set_pixels(std::move(empty));
    layer.set_bounds({});
    return;
  }

  auto scaled = scale_pixels_resampled(source, new_bounds.width, new_bounds.height);
  layer.set_pixels(std::move(scaled));
  layer.set_bounds(new_bounds);
}

void resize_document_channel_image(DocumentChannel& channel, std::int32_t width, std::int32_t height) {
  const auto& source = std::as_const(channel).pixels();
  channel.set_pixels(scale_pixels_resampled(source, width, height));
}

void resize_document_channel_canvas(DocumentChannel& channel, std::int32_t width, std::int32_t height,
                                    CanvasResizeOffset offset) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer resized(width, height, PixelFormat::gray8());
  resized.clear(channel.kind() == DocumentChannelKind::Spot ? 255 : 0);
  for (std::int32_t source_y = 0; source_y < source.height(); ++source_y) {
    const auto destination_y = source_y + offset.y;
    if (destination_y < 0 || destination_y >= height) {
      continue;
    }
    for (std::int32_t source_x = 0; source_x < source.width(); ++source_x) {
      const auto destination_x = source_x + offset.x;
      if (destination_x < 0 || destination_x >= width) {
        continue;
      }
      *resized.pixel(destination_x, destination_y) = *source.pixel(source_x, source_y);
    }
  }
  channel.set_pixels(std::move(resized));
}

void crop_document_channel(DocumentChannel& channel, Rect crop) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer cropped(crop.width, crop.height, PixelFormat::gray8());
  for (std::int32_t y = 0; y < crop.height; ++y) {
    const auto source_row = source.row(crop.y + y).subspan(static_cast<std::size_t>(crop.x),
                                                           static_cast<std::size_t>(crop.width));
    auto destination_row = cropped.row(y);
    std::copy(source_row.begin(), source_row.end(), destination_row.begin());
  }
  channel.set_pixels(std::move(cropped));
}

void rotate_document_channel_clockwise(DocumentChannel& channel) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer rotated(source.height(), source.width(), PixelFormat::gray8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      *rotated.pixel(source.height() - 1 - y, x) = *source.pixel(x, y);
    }
  }
  channel.set_pixels(std::move(rotated));
}

void rotate_document_channel_counterclockwise(DocumentChannel& channel) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer rotated(source.height(), source.width(), PixelFormat::gray8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      *rotated.pixel(y, source.width() - 1 - x) = *source.pixel(x, y);
    }
  }
  channel.set_pixels(std::move(rotated));
}

}  // namespace

namespace {

// Live-shape annotations survive positive axis-aligned scale + translate;
// everything else drops them (the path stays the exact source of truth, the
// keyShapeInvalidated rule).
bool matrix_keeps_live_shapes(const std::array<double, 6>& matrix) {
  return matrix[1] == 0.0 && matrix[2] == 0.0 && matrix[0] > 0.0 && matrix[3] > 0.0;
}

void transform_origination_or_drop(VectorShapeContent& content,
                                   const std::array<double, 6>& matrix) {
  if (content.origination.empty()) {
    return;
  }
  if (!matrix_keeps_live_shapes(matrix)) {
    content.origination.clear();
    return;
  }
  const double sx = matrix[0];
  const double sy = matrix[3];
  const double tx = matrix[4];
  const double ty = matrix[5];
  const double uniform_scale = (sx + sy) / 2.0;
  for (auto& params : content.origination) {
    params.left = params.left * sx + tx;
    params.right = params.right * sx + tx;
    params.top = params.top * sy + ty;
    params.bottom = params.bottom * sy + ty;
    for (std::size_t i = 0; i < params.box_corners.size(); i += 2) {
      params.box_corners[i] = params.box_corners[i] * sx + tx;
      params.box_corners[i + 1] = params.box_corners[i + 1] * sy + ty;
    }
    for (auto& radius : params.corner_radii) {
      radius *= uniform_scale;
    }
    params.line_start_x = params.line_start_x * sx + tx;
    params.line_end_x = params.line_end_x * sx + tx;
    params.line_start_y = params.line_start_y * sy + ty;
    params.line_end_y = params.line_end_y * sy + ty;
    params.line_weight *= uniform_scale;
    params.arrow_width *= uniform_scale;
    params.arrow_length *= uniform_scale;
    params.transform[4] = params.transform[4] * sx + tx;
    params.transform[5] = params.transform[5] * sy + ty;
  }
}

}  // namespace

void transform_layer_vector_data(Document& document, Layer& layer,
                                 const std::array<double, 6>& matrix, Rect canvas_after,
                                 double stroke_scale) {
  bool touched = false;
  if (const auto* shape = layer.vector_shape(); shape != nullptr) {
    auto content = *shape;
    transform_vector_path(content.path, matrix);
    transform_origination_or_drop(content, matrix);
    if (stroke_scale != 1.0 && stroke_scale > 0.0) {
      content.stroke.width *= stroke_scale;
    }
    layer.set_vector_shape(std::move(content));
    layer.metadata()[kLayerMetadataVectorRasterStatus] = kVectorRasterStatusPatchy;
    update_vector_shape_raster(layer, canvas_after, &document.metadata().patterns);
    touched = true;
  }
  if (const auto* mask = layer.vector_mask(); mask != nullptr) {
    auto updated = *mask;
    transform_vector_path(updated.path, matrix);
    layer.set_vector_mask(std::move(updated));
    update_vector_mask_raster(layer, canvas_after);
    touched = true;
  }
  if (touched) {
    mark_layer_vector_block_dirty(layer);
  }
}

void transform_document_vector_data(Document& document, const std::array<double, 6>& matrix,
                                    Rect canvas_after, double stroke_scale) {
  const auto walk = [&document, &matrix, canvas_after, stroke_scale](auto&& self,
                                                                     std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (layer.kind() == LayerKind::Group) {
        self(self, layer.children());
        continue;
      }
      transform_layer_vector_data(document, layer, matrix, canvas_after, stroke_scale);
    }
  };
  walk(walk, document.layers());
  for (auto& path : document.paths()) {
    auto updated = path.path();
    transform_vector_path(updated, matrix);
    path.set_path(std::move(updated));  // marks the path dirty for the writer
  }
}

Rect flip_layer_horizontal(Document& document, LayerId layer_id) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto channels = pixels.format().channels;
  const auto bounds = layer->bounds();
  std::vector<std::uint8_t> temp(channels);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width() / 2; ++x) {
      auto* left = row.data() + static_cast<std::size_t>(x) * channels;
      auto* right = row.data() + static_cast<std::size_t>(pixels.width() - 1 - x) * channels;
      std::copy(left, left + channels, temp.begin());
      std::copy(right, right + channels, left);
      std::copy(temp.begin(), temp.end(), right);
    }
  }
  flip_layer_mask_horizontal(*layer, bounds);
  const double center_x = bounds.x + bounds.width / 2.0;
  transform_layer_vector_data(document, *layer, {-1.0, 0.0, 0.0, 1.0, 2.0 * center_x, 0.0},
                              Rect::from_size(document.width(), document.height()));
  return layer->bounds();
}

Rect flip_layer_vertical(Document& document, LayerId layer_id) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  std::vector<std::uint8_t> temp(pixels.stride_bytes());
  for (std::int32_t y = 0; y < pixels.height() / 2; ++y) {
    auto top = pixels.row(y);
    auto bottom = pixels.row(pixels.height() - 1 - y);
    std::copy(top.begin(), top.end(), temp.begin());
    std::copy(bottom.begin(), bottom.end(), top.begin());
    std::copy(temp.begin(), temp.end(), bottom.begin());
  }
  flip_layer_mask_vertical(*layer, bounds);
  const double center_y = bounds.y + bounds.height / 2.0;
  transform_layer_vector_data(document, *layer, {1.0, 0.0, 0.0, -1.0, 0.0, 2.0 * center_y},
                              Rect::from_size(document.width(), document.height()));
  return layer->bounds();
}

void resize_image_and_layers(Document& document, std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const auto old_width = document.width();
  const auto old_height = document.height();
  if (old_width <= 0 || old_height <= 0) {
    resize_canvas_and_layers(document, width, height);
    return;
  }

  for (auto& layer : document.layers()) {
    resize_layer_image(layer, old_width, old_height, width, height);
  }
  for (auto& channel : document.channels()) {
    resize_document_channel_image(channel, width, height);
  }
  document.resize_canvas(width, height);
  const auto sx = static_cast<double>(width) / old_width;
  const auto sy = static_cast<double>(height) / old_height;
  transform_document_vector_data(document, {sx, 0.0, 0.0, sy, 0.0, 0.0},
                                 Rect::from_size(width, height), (sx + sy) / 2.0);
}

void resize_canvas_and_layers(Document& document, std::int32_t width, std::int32_t height, CanvasAnchor anchor,
                              EditColor extension_color) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const auto offset = canvas_resize_offset(anchor, document.width(), document.height(), width, height);
  for (auto& channel : document.channels()) {
    resize_document_channel_canvas(channel, width, height, offset);
  }
  document.resize_canvas(width, height);
  for (auto& layer : document.layers()) {
    resize_layer_to_canvas(layer, width, height, offset, extension_color);
  }
  transform_document_vector_data(document,
                                 {1.0, 0.0, 0.0, 1.0, static_cast<double>(offset.x),
                                  static_cast<double>(offset.y)},
                                 Rect::from_size(width, height));
}

bool crop_document(Document& document, Rect crop) {
  crop = intersect_rect(crop, canvas_rect(document));
  if (crop.empty()) {
    return false;
  }

  for (auto& layer : document.layers()) {
    crop_layer_to_rect(layer, crop);
  }
  for (auto& channel : document.channels()) {
    crop_document_channel(channel, crop);
  }
  document.resize_canvas(crop.width, crop.height);
  transform_document_vector_data(document,
                                 {1.0, 0.0, 0.0, 1.0, static_cast<double>(-crop.x),
                                  static_cast<double>(-crop.y)},
                                 Rect::from_size(crop.width, crop.height));
  return true;
}

void rotate_document_clockwise(Document& document) {
  const auto old_width = document.width();
  const auto old_height = document.height();
  for (auto& layer : document.layers()) {
    rotate_layer_clockwise(layer, old_height);
  }
  for (auto& channel : document.channels()) {
    rotate_document_channel_clockwise(channel);
  }
  document.resize_canvas(old_height, old_width);
  // Edge coordinates rotate as (x, y) -> (H - y, x).
  transform_document_vector_data(document,
                                 {0.0, 1.0, -1.0, 0.0, static_cast<double>(old_height), 0.0},
                                 Rect::from_size(old_height, old_width));
}

void rotate_document_counterclockwise(Document& document) {
  const auto old_width = document.width();
  const auto old_height = document.height();
  for (auto& layer : document.layers()) {
    rotate_layer_counterclockwise(layer, old_width);
  }
  for (auto& channel : document.channels()) {
    rotate_document_channel_counterclockwise(channel);
  }
  document.resize_canvas(old_height, old_width);
  // Edge coordinates rotate as (x, y) -> (y, W - x).
  transform_document_vector_data(document,
                                 {0.0, -1.0, 1.0, 0.0, 0.0, static_cast<double>(old_width)},
                                 Rect::from_size(old_height, old_width));
}

}  // namespace patchy
