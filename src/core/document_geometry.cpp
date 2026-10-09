#include "core/vector_compound.hpp"
// Document and layer geometry operations (crop, rotate, flip, image/canvas resize, and the
// DocumentChannel geometry counterparts) split out of pixel_tools.cpp as pure moves; bodies
// are verbatim. Helpers shared with the painting half stay in pixel_tools.cpp and are
// declared in core/pixel_tools_internal.hpp (clamp_byte comes from core/blend_math.hpp).

#include "core/pixel_tools.hpp"

#include "core/blend_math.hpp"
#include "core/document_path.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools_internal.hpp"
#include "core/rect_utils.hpp"
#include "core/resample.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace patchy {

namespace {

// Any-depth buffer helpers (docs/high-bit-depth.md): pixels move as whole pixels
// (bytes_per_pixel), never one byte per channel, and new buffers take the source's
// format, so a 16 or 32-bit mask or channel stays at its depth.
PixelBuffer copy_buffer_region(const PixelBuffer& source, std::int32_t x, std::int32_t y, std::int32_t width,
                               std::int32_t height) {
  PixelBuffer region(width, height, source.format());
  const auto pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t row = 0; row < height; ++row) {
    const auto from = source.row(y + row).subspan(static_cast<std::size_t>(x) * pixel_bytes,
                                                  static_cast<std::size_t>(width) * pixel_bytes);
    auto to = region.row(row);
    std::copy(from.begin(), from.end(), to.begin());
  }
  return region;
}

PixelBuffer rotated_buffer_clockwise(const PixelBuffer& source) {
  PixelBuffer rotated(source.height(), source.width(), source.format());
  const auto pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* src = source.pixel(x, y);
      std::copy(src, src + pixel_bytes, rotated.pixel(source.height() - 1 - y, x));
    }
  }
  return rotated;
}

PixelBuffer rotated_buffer_counterclockwise(const PixelBuffer& source) {
  PixelBuffer rotated(source.height(), source.width(), source.format());
  const auto pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* src = source.pixel(x, y);
      std::copy(src, src + pixel_bytes, rotated.pixel(y, source.width() - 1 - x));
    }
  }
  return rotated;
}

// Every sample of a single-channel buffer set to `value` (0..255 coverage) at its depth.
void fill_coverage(PixelBuffer& buffer, std::uint8_t value) {
  if (buffer.format().bit_depth == BitDepth::UInt8 || buffer.empty()) {
    buffer.clear(value);
    return;
  }
  const std::vector<float> row(static_cast<std::size_t>(buffer.width()), static_cast<float>(value));
  for (std::int32_t y = 0; y < buffer.height(); ++y) {
    store_coverage_row(buffer, y, 0, buffer.width(), row);
  }
}

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

  layer.set_pixels(rotated_buffer_clockwise(std::as_const(layer).pixels()));
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

  layer.set_pixels(rotated_buffer_counterclockwise(std::as_const(layer).pixels()));
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
    mask->pixels = PixelBuffer(0, 0, mask->pixels.format());
    return;
  }

  mask->pixels = copy_buffer_region(std::as_const(mask->pixels), intersection.x - old_bounds.x,
                                    intersection.y - old_bounds.y, intersection.width, intersection.height);
  mask->bounds = Rect{intersection.x - crop.x, intersection.y - crop.y, intersection.width, intersection.height};
}

void rotate_layer_mask_clockwise(Layer& layer, std::int32_t document_height) {
  auto& mask = layer.mask();
  if (!mask.has_value() || mask->pixels.empty()) {
    return;
  }

  const auto old_bounds = mask->bounds;
  mask->pixels = rotated_buffer_clockwise(std::as_const(mask->pixels));
  mask->bounds = Rect{document_height - old_bounds.y - old_bounds.height, old_bounds.x, old_bounds.height,
                      old_bounds.width};
}

void rotate_layer_mask_counterclockwise(Layer& layer, std::int32_t document_width) {
  auto& mask = layer.mask();
  if (!mask.has_value() || mask->pixels.empty()) {
    return;
  }

  const auto old_bounds = mask->bounds;
  mask->pixels = rotated_buffer_counterclockwise(std::as_const(mask->pixels));
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

void shift_layer_mask_to_canvas(Layer& layer, CanvasResizeOffset offset, std::int32_t width, std::int32_t height,
                                bool crop_mask = true) {
  auto& mask = layer.mask();
  if (!mask.has_value() || width <= 0 || height <= 0) {
    return;
  }

  const auto shifted_bounds =
      Rect{mask->bounds.x + offset.x, mask->bounds.y + offset.y, mask->bounds.width, mask->bounds.height};
  if (!crop_mask) {
    mask->bounds = shifted_bounds;
    return;
  }
  const auto clipped = intersect_rect(shifted_bounds, Rect{0, 0, width, height});
  if (clipped.empty() || mask->pixels.empty()) {
    mask->bounds = {};
    mask->pixels = PixelBuffer(0, 0, mask->pixels.format());
    return;
  }

  mask->pixels = copy_buffer_region(std::as_const(mask->pixels), clipped.x - shifted_bounds.x,
                                    clipped.y - shifted_bounds.y, clipped.width, clipped.height);
  mask->bounds = clipped;
}

void resize_layer_to_canvas(Layer& layer, std::int32_t width, std::int32_t height, CanvasResizeOffset offset,
                            EditColor extension_color, bool crop_layer = true) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      resize_layer_to_canvas(child, width, height, offset, extension_color, crop_layer);
    }
    shift_layer_mask_to_canvas(layer, offset, width, height, crop_layer);
    if (!layer.bounds().empty()) {
      const auto bounds = layer.bounds();
      layer.set_bounds(Rect{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height});
    }
    return;
  }
  if (layer.kind() != LayerKind::Pixel || width <= 0 || height <= 0) {
    shift_layer_mask_to_canvas(layer, offset, width, height, crop_layer);
    if (!layer.bounds().empty()) {
      const auto bounds = layer.bounds();
      layer.set_bounds(Rect{bounds.x + offset.x, bounds.y + offset.y, bounds.width, bounds.height});
    }
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto shifted_bounds =
      Rect{old_bounds.x + offset.x, old_bounds.y + offset.y, old_bounds.width, old_bounds.height};
  auto resized_bounds = Rect::from_size(width, height);
  if (!crop_layer) {
    shift_layer_mask_to_canvas(layer, offset, width, height, false);
    layer.set_bounds(shifted_bounds);
    // Ordinary layers need only a translation. Painting already expands their
    // buffers on demand; resizing the canvas must not rewrite their pixels.
    if (layer.name() != "Background") {
      return;
    }
    // Keep the Background's existing off-canvas data while filling new space.
    resized_bounds = unite_rect(resized_bounds, shifted_bounds);
    if (resized_bounds.x == shifted_bounds.x && resized_bounds.y == shifted_bounds.y &&
        resized_bounds.width == shifted_bounds.width && resized_bounds.height == shifted_bounds.height) {
      return;
    }
  }
  const auto& source = std::as_const(layer).pixels();
  PixelBuffer resized(resized_bounds.width, resized_bounds.height,
                      canvas_resized_format_for_layer(layer, source, extension_color));
  fill_resized_layer_background(resized, layer, extension_color);

  if (!source.empty()) {
    for (std::int32_t sy = 0; sy < source.height(); ++sy) {
      const auto destination_y = shifted_bounds.y + sy - resized_bounds.y;
      if (destination_y < 0 || destination_y >= resized_bounds.height) {
        continue;
      }
      for (std::int32_t sx = 0; sx < source.width(); ++sx) {
        const auto destination_x = shifted_bounds.x + sx - resized_bounds.x;
        if (destination_x < 0 || destination_x >= resized_bounds.width) {
          continue;
        }
        copy_resized_layer_pixel(source, resized, sx, sy, destination_x, destination_y);
      }
    }
  }

  if (crop_layer) {
    shift_layer_mask_to_canvas(layer, offset, width, height);
  }
  layer.set_pixels(std::move(resized));
  layer.set_bounds(resized_bounds);
}

// Rotated crop mapping: the crop box is `crop` rotated by the angle about its
// center in document space. Result pixel q samples the document at
// p = center + R(theta) * (q - result_center); `forward` is the document ->
// result affine ({a,b,c,d,tx,ty} like transform_vector_path) used for text
// transforms, smart-object placements, vector data, and bounds.
struct RotatedCropMap {
  double cos_theta{1.0};
  double sin_theta{0.0};
  double document_center_x{0.0};
  double document_center_y{0.0};
  double result_center_x{0.0};
  double result_center_y{0.0};
  std::array<double, 6> forward{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
};

[[nodiscard]] RotatedCropMap rotated_crop_map(Rect crop, double angle_degrees) {
  RotatedCropMap map;
  const auto radians = angle_degrees * (std::numbers::pi / 180.0);
  map.cos_theta = std::cos(radians);
  map.sin_theta = std::sin(radians);
  map.document_center_x = static_cast<double>(crop.x) + static_cast<double>(crop.width) / 2.0;
  map.document_center_y = static_cast<double>(crop.y) + static_cast<double>(crop.height) / 2.0;
  map.result_center_x = static_cast<double>(crop.width) / 2.0;
  map.result_center_y = static_cast<double>(crop.height) / 2.0;
  map.forward = {map.cos_theta,
                 -map.sin_theta,
                 map.sin_theta,
                 map.cos_theta,
                 map.result_center_x - map.cos_theta * map.document_center_x -
                     map.sin_theta * map.document_center_y,
                 map.result_center_y + map.sin_theta * map.document_center_x -
                     map.cos_theta * map.document_center_y};
  return map;
}

// Integer bounding box of `bounds` mapped through the forward affine.
[[nodiscard]] Rect rotated_crop_bounds(Rect bounds, const RotatedCropMap& map) {
  const auto& m = map.forward;
  const std::array<std::pair<double, double>, 4> corners = {
      std::pair<double, double>{static_cast<double>(bounds.x), static_cast<double>(bounds.y)},
      {static_cast<double>(bounds.x + bounds.width), static_cast<double>(bounds.y)},
      {static_cast<double>(bounds.x), static_cast<double>(bounds.y + bounds.height)},
      {static_cast<double>(bounds.x + bounds.width), static_cast<double>(bounds.y + bounds.height)}};
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
  bool first = true;
  for (const auto& [x, y] : corners) {
    const auto mapped_x = m[0] * x + m[2] * y + m[4];
    const auto mapped_y = m[1] * x + m[3] * y + m[5];
    min_x = first ? mapped_x : std::min(min_x, mapped_x);
    min_y = first ? mapped_y : std::min(min_y, mapped_y);
    max_x = first ? mapped_x : std::max(max_x, mapped_x);
    max_y = first ? mapped_y : std::max(max_y, mapped_y);
    first = false;
  }
  const auto left = static_cast<std::int32_t>(std::floor(min_x));
  const auto top = static_cast<std::int32_t>(std::floor(min_y));
  const auto right = static_cast<std::int32_t>(std::ceil(max_x));
  const auto bottom = static_cast<std::int32_t>(std::ceil(max_y));
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

// Samples `source` (document-space `source_bounds`) into `destination`
// (result-space `destination_bounds`) through the inverse crop mapping.
// Bilinear with clamped edges for 8-bit (the convention a bilinear Image Size
// enlargement shares, core/resample.cpp), nearest for other depths; destination pixels whose source
// point falls outside the buffer keep their pre-filled value. A 3->4 channel
// promotion writes opaque alpha, like copy_resized_layer_pixel.
void sample_rotated_crop_pixels(const PixelBuffer& source, Rect source_bounds, PixelBuffer& destination,
                                Rect destination_bounds, const RotatedCropMap& map) {
  if (source.empty() || destination.empty()) {
    return;
  }
  const auto bilinear = source.format().bit_depth == BitDepth::UInt8 &&
                        destination.format().bit_depth == BitDepth::UInt8;
  const auto deep = source.format().bit_depth != BitDepth::UInt8 &&
                    source.format().bit_depth == destination.format().bit_depth &&
                    (source.format().channels >= 3) == (destination.format().channels >= 3);
  const auto gray = source.format().channels < 3;
  const auto domain =
      source.format().bit_depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
  const auto source_channels = source.format().channels;
  const auto destination_channels = destination.format().channels;
  const auto shared_channels = std::min(source_channels, destination_channels);
  const auto pixel_bytes = bytes_per_pixel(source.format());
  for (std::int32_t y = 0; y < destination.height(); ++y) {
    for (std::int32_t x = 0; x < destination.width(); ++x) {
      const auto qx = static_cast<double>(destination_bounds.x + x) + 0.5 - map.result_center_x;
      const auto qy = static_cast<double>(destination_bounds.y + y) + 0.5 - map.result_center_y;
      const auto document_x = map.document_center_x + map.cos_theta * qx - map.sin_theta * qy;
      const auto document_y = map.document_center_y + map.sin_theta * qx + map.cos_theta * qy;
      const auto local_x = document_x - static_cast<double>(source_bounds.x);
      const auto local_y = document_y - static_cast<double>(source_bounds.y);
      if (local_x < 0.0 || local_y < 0.0 || local_x >= static_cast<double>(source.width()) ||
          local_y >= static_cast<double>(source.height())) {
        continue;
      }
      auto* dst = destination.pixel(x, y);
      if (deep) {
        // 16/32-bit: bilinear on float samples in the buffer's own domain, so no
        // transfer is applied (Encoded for 16 bits, Linear for 32).
        const auto sample_x = local_x - 0.5;
        const auto sample_y = local_y - 0.5;
        const auto x0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_x)), 0, source.width() - 1);
        const auto x1 = std::clamp(x0 + 1, 0, source.width() - 1);
        const auto tx = static_cast<float>(std::clamp(sample_x - static_cast<double>(x0), 0.0, 1.0));
        const auto y0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_y)), 0, source.height() - 1);
        const auto y1 = std::clamp(y0 + 1, 0, source.height() - 1);
        const auto ty = static_cast<float>(std::clamp(sample_y - static_cast<double>(y0), 0.0, 1.0));
        std::array<float, 4> corners[4]{};
        const std::array<std::pair<std::int32_t, std::int32_t>, 4> at{{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}};
        for (std::size_t i = 0; i < 4U; ++i) {
          if (gray) {
            load_coverage_row(source, at[i].second, at[i].first, 1, std::span<float>(corners[i]).first(1));
          } else {
            load_rgba_row(source, at[i].second, at[i].first, 1, domain, corners[i]);
          }
        }
        std::array<float, 4> mixed{};
        for (std::size_t c = 0; c < 4U; ++c) {
          const auto top = corners[0][c] * (1.0F - tx) + corners[1][c] * tx;
          const auto bottom = corners[2][c] * (1.0F - tx) + corners[3][c] * tx;
          mixed[c] = top * (1.0F - ty) + bottom * ty;
        }
        if (gray) {
          store_coverage_row(destination, y, x, 1, std::span<const float>(mixed).first(1));
        } else {
          store_rgba_row(destination, y, x, 1, domain, mixed);
        }
        continue;
      }
      if (!bilinear) {
        const auto sx = std::clamp(static_cast<std::int32_t>(local_x), 0, source.width() - 1);
        const auto sy = std::clamp(static_cast<std::int32_t>(local_y), 0, source.height() - 1);
        const auto* src = source.pixel(sx, sy);
        std::copy(src, src + pixel_bytes, dst);
        continue;
      }
      const auto sample_x = local_x - 0.5;
      const auto sample_y = local_y - 0.5;
      const auto x0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_x)), 0, source.width() - 1);
      const auto x1 = std::clamp(x0 + 1, 0, source.width() - 1);
      const auto tx = std::clamp(sample_x - static_cast<double>(x0), 0.0, 1.0);
      const auto y0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_y)), 0, source.height() - 1);
      const auto y1 = std::clamp(y0 + 1, 0, source.height() - 1);
      const auto ty = std::clamp(sample_y - static_cast<double>(y0), 0.0, 1.0);
      const auto* top_left = source.pixel(x0, y0);
      const auto* top_right = source.pixel(x1, y0);
      const auto* bottom_left = source.pixel(x0, y1);
      const auto* bottom_right = source.pixel(x1, y1);
      for (std::uint16_t channel = 0; channel < shared_channels; ++channel) {
        const auto top =
            static_cast<double>(top_left[channel]) * (1.0 - tx) + static_cast<double>(top_right[channel]) * tx;
        const auto bottom =
            static_cast<double>(bottom_left[channel]) * (1.0 - tx) + static_cast<double>(bottom_right[channel]) * tx;
        dst[channel] = clamp_byte(static_cast<float>(top * (1.0 - ty) + bottom * ty));
      }
      if (destination_channels >= 4 && source_channels < 4) {
        dst[3] = 255;
      }
    }
  }
}

void crop_layer_mask_rotated(Layer& layer, Rect crop, const RotatedCropMap& map) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }
  const auto result_canvas = Rect::from_size(crop.width, crop.height);
  const auto destination_bounds = intersect_rect(rotated_crop_bounds(mask->bounds, map), result_canvas);
  if (destination_bounds.empty() || mask->pixels.empty()) {
    mask->bounds = {};
    mask->pixels = PixelBuffer(0, 0, mask->pixels.format());
    return;
  }
  PixelBuffer rotated(destination_bounds.width, destination_bounds.height, mask->pixels.format());
  fill_coverage(rotated, mask->default_color);
  sample_rotated_crop_pixels(mask->pixels, mask->bounds, rotated, destination_bounds, map);
  mask->pixels = std::move(rotated);
  mask->bounds = destination_bounds;
}

void crop_layer_to_rect_rotated(Layer& layer, Rect crop, const RotatedCropMap& map, EditColor extension_color) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      crop_layer_to_rect_rotated(child, crop, map, extension_color);
    }
    return;
  }
  crop_layer_mask_rotated(layer, crop, map);
  const auto result_canvas = Rect::from_size(crop.width, crop.height);
  if (layer.kind() != LayerKind::Pixel) {
    if (!layer.bounds().empty()) {
      const auto moved = intersect_rect(rotated_crop_bounds(layer.bounds(), map), result_canvas);
      layer.set_bounds(moved.empty() ? Rect{} : moved);
    }
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto& source = std::as_const(layer).pixels();
  if (layer.name() == "Background" && source.format().channels >= 3) {
    // Canvas-sized rebuild with the canvas-resize fill rules under the exposed
    // corners, mirroring the expanding path.
    PixelBuffer rotated(crop.width, crop.height, canvas_resized_format_for_layer(layer, source, extension_color));
    fill_resized_layer_background(rotated, layer, extension_color);
    sample_rotated_crop_pixels(source, old_bounds, rotated, result_canvas, map);
    layer.set_pixels(std::move(rotated));
    layer.set_bounds(Rect{0, 0, crop.width, crop.height});
    return;
  }

  const auto destination_bounds = intersect_rect(rotated_crop_bounds(old_bounds, map), result_canvas);
  if (destination_bounds.empty() || source.empty()) {
    PixelBuffer empty(0, 0, source.format());
    layer.set_pixels(std::move(empty));
    layer.set_bounds({});
    return;
  }
  // Rotation exposes area inside the bounding box, so opaque 8-bit formats
  // gain an alpha channel (the transparent-extension format rule).
  PixelBuffer rotated(destination_bounds.width, destination_bounds.height,
                      canvas_resized_format_for_layer(layer, source, EditColor{0, 0, 0, 0}));
  rotated.clear(0);
  sample_rotated_crop_pixels(source, old_bounds, rotated, destination_bounds, map);
  layer.set_pixels(std::move(rotated));
  layer.set_bounds(destination_bounds);
}

void crop_document_channel_rotated(DocumentChannel& channel, Rect crop, const RotatedCropMap& map) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer rotated(crop.width, crop.height, source.format());
  fill_coverage(rotated, channel.kind() == DocumentChannelKind::Spot ? 255 : 0);
  sample_rotated_crop_pixels(source, Rect{0, 0, source.width(), source.height()}, rotated,
                             Rect::from_size(crop.width, crop.height), map);
  channel.set_pixels(std::move(rotated));
}

// Crop that may extend past the canvas: content outside `crop` is discarded and
// buffers stay tight, except a pixel layer literally named "Background", which
// is rebuilt canvas-sized through resize_layer_to_canvas so the canvas-resize
// fill rules put extension_color under the exposed area.
void crop_layer_to_rect_expanding(Layer& layer, Rect crop, EditColor extension_color) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      crop_layer_to_rect_expanding(child, crop, extension_color);
    }
    return;
  }
  if (layer.kind() == LayerKind::Pixel && layer.name() == "Background") {
    resize_layer_to_canvas(layer, crop.width, crop.height, CanvasResizeOffset{-crop.x, -crop.y}, extension_color);
    return;
  }
  crop_layer_to_rect(layer, crop);
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

// Image Size resampling lives in core/resample.cpp (docs/resampling.md); `method` is
// already resolved from Automatic by resize_image_and_layers.
void resize_layer_mask_image(Layer& layer, std::int32_t old_width, std::int32_t old_height,
                             std::int32_t new_width, std::int32_t new_height, ResampleMethod method) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }

  const auto new_bounds = scale_document_rect(mask->bounds, old_width, old_height, new_width, new_height);
  if (mask->feather > 0.0 && old_width > 0 && old_height > 0) {
    // The feather is in pixels, so it scales with the image (mean of the axes
    // for a non-uniform resize).
    mask->feather *= (static_cast<double>(new_width) / old_width + static_cast<double>(new_height) / old_height) * 0.5;
  }
  if (mask->pixels.empty()) {
    mask->bounds = new_bounds;
    return;
  }

  mask->pixels = resample_pixels(mask->pixels, new_bounds.width, new_bounds.height, method);
  mask->bounds = new_bounds;
}

void resize_layer_image(Layer& layer, std::int32_t old_width, std::int32_t old_height, std::int32_t new_width,
                        std::int32_t new_height, ResampleMethod method) {
  resize_layer_mask_image(layer, old_width, old_height, new_width, new_height, method);
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      resize_layer_image(child, old_width, old_height, new_width, new_height, method);
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

  auto scaled = resample_pixels(source, new_bounds.width, new_bounds.height, method);
  layer.set_pixels(std::move(scaled));
  layer.set_bounds(new_bounds);
}

void resize_document_channel_image(DocumentChannel& channel, std::int32_t width, std::int32_t height,
                                   ResampleMethod method) {
  const auto& source = std::as_const(channel).pixels();
  channel.set_pixels(resample_pixels(source, width, height, method));
}

void resize_document_channel_canvas(DocumentChannel& channel, std::int32_t width, std::int32_t height,
                                    CanvasResizeOffset offset) {
  const auto& source = std::as_const(channel).pixels();
  PixelBuffer resized(width, height, source.format());
  fill_coverage(resized, channel.kind() == DocumentChannelKind::Spot ? 255 : 0);
  const auto pixel_bytes = bytes_per_pixel(source.format());
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
      const auto* src = source.pixel(source_x, source_y);
      std::copy(src, src + pixel_bytes, resized.pixel(destination_x, destination_y));
    }
  }
  channel.set_pixels(std::move(resized));
}

void crop_document_channel(DocumentChannel& channel, Rect crop) {
  channel.set_pixels(copy_buffer_region(std::as_const(channel).pixels(), crop.x, crop.y, crop.width, crop.height));
}

void rotate_document_channel_clockwise(DocumentChannel& channel) {
  channel.set_pixels(rotated_buffer_clockwise(std::as_const(channel).pixels()));
}

void rotate_document_channel_counterclockwise(DocumentChannel& channel) {
  channel.set_pixels(rotated_buffer_counterclockwise(std::as_const(channel).pixels()));
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

// Text layers store their text-local -> document mapping in patchy.text.transform. Any
// operation that remaps DOCUMENT space (image/canvas resize, crop, canvas rotate, layer
// flip, seam offset) must compose its matrix onto that mapping, or the next metadata
// re-render (an edit commit, --append-text, a PSD save) puts the text back where -- and
// how big -- it was before the operation, which is exactly how the pinball poster's saved
// TySh ended up pointing ~900 px away from its raster (August 2026). A layer without the
// key implicitly maps text space via translate(bounds): that must be materialized under a
// matrix with a linear part (scale/rotate/flip), while a pure translation already rides
// in the bounds the operation moves, so absent keys stay absent there. The matrix uses
// the same (a, b, c, d, tx, ty) layout as transform_vector_path. Call BEFORE mutating the
// layer, so the implicit case reads pre-operation bounds.
void compose_text_layer_transform(Layer& layer, const std::array<double, 6>& matrix) {
  if (!layer_is_text(std::as_const(layer))) {
    return;
  }
  const bool pure_translation =
      matrix[0] == 1.0 && matrix[1] == 0.0 && matrix[2] == 0.0 && matrix[3] == 1.0;
  const auto& metadata = std::as_const(layer).metadata();
  std::optional<LayerAffineTransform> stored;
  if (const auto found = metadata.find(kLayerMetadataTextTransform); found != metadata.end()) {
    stored = parse_layer_affine_transform(found->second);
  }
  if (!stored.has_value()) {
    if (pure_translation) {
      return;
    }
    const auto bounds = std::as_const(layer).bounds();
    stored = LayerAffineTransform{
        1.0, 0.0, 0.0, 1.0, static_cast<double>(bounds.x), static_cast<double>(bounds.y)};
  }
  const LayerAffineTransform outer{matrix[0], matrix[1], matrix[2],
                                   matrix[3], matrix[4], matrix[5]};
  layer.metadata_without_content_bump()[kLayerMetadataTextTransform] =
      serialize_layer_affine_transform(compose_layer_affine_transform(outer, *stored));
}

void compose_document_text_transforms(std::vector<Layer>& layers,
                                      const std::array<double, 6>& matrix) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      compose_document_text_transforms(layer.children(), matrix);
      continue;
    }
    compose_text_layer_transform(layer, matrix);
  }
}

// The placed-layer counterpart of compose_text_layer_transform: a smart object's SoLd
// 'Trnf' quad is DOCUMENT space, so an operation that remaps document space has to map
// it or the next re-render (a move, a transform) and the saved SoLd both put the placed
// content back at its pre-operation rectangle. Photoshop maps placements the same way,
// which is why Image Size on a smart object stays non-destructive there. The pixels the
// caller resamples/rotates/crops remain the preview; marking the block dirty makes the
// writer regenerate the SoLd patch-in-place from this quad. Layers whose SoLd never
// parsed have no quad to map -- the UI refuses document geometry for those instead.
void compose_smart_object_placement(Layer& layer, const std::array<double, 6>& matrix) {
  if (!layer_is_smart_object(std::as_const(layer))) {
    return;
  }
  const auto placement = smart_object_placement_from_layer(std::as_const(layer));
  if (!placement.has_value()) {
    return;
  }
  store_smart_object_placement(layer, transformed_smart_object_placement(*placement, matrix));
  mark_layer_smart_object_block_dirty(layer);
  layer.metadata()[kLayerMetadataSmartObjectRasterStatus] = kSmartObjectRasterStatusPatchy;
}

void compose_document_smart_object_placements(std::vector<Layer>& layers,
                                              const std::array<double, 6>& matrix) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      compose_document_smart_object_placements(layer.children(), matrix);
      continue;
    }
    compose_smart_object_placement(layer, matrix);
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
    transform_vector_part_appearance(content, matrix, stroke_scale);
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
  auto* layer = pixel_layer_any_depth(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  const auto bounds = layer->bounds();
  flip_pixels_horizontal(layer->pixels());
  flip_layer_mask_horizontal(*layer, bounds);
  const double center_x = bounds.x + bounds.width / 2.0;
  compose_text_layer_transform(*layer, {-1.0, 0.0, 0.0, 1.0, 2.0 * center_x, 0.0});
  transform_layer_vector_data(document, *layer, {-1.0, 0.0, 0.0, 1.0, 2.0 * center_x, 0.0},
                              Rect::from_size(document.width(), document.height()));
  return layer->bounds();
}

Rect flip_layer_vertical(Document& document, LayerId layer_id) {
  auto* layer = pixel_layer_any_depth(document, layer_id);
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
  compose_text_layer_transform(*layer, {1.0, 0.0, 0.0, -1.0, 0.0, 2.0 * center_y});
  transform_layer_vector_data(document, *layer, {1.0, 0.0, 0.0, -1.0, 0.0, 2.0 * center_y},
                              Rect::from_size(document.width(), document.height()));
  return layer->bounds();
}

void resize_image_and_layers(Document& document, std::int32_t width, std::int32_t height, ResampleMethod method) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const auto old_width = document.width();
  const auto old_height = document.height();
  if (old_width <= 0 || old_height <= 0) {
    resize_canvas_and_layers(document, width, height);
    return;
  }

  // One kernel for every layer, mask and channel of this resize.
  method = resolve_automatic_resample_method(method, old_width, old_height, width, height);
  const auto sx = static_cast<double>(width) / old_width;
  const auto sy = static_cast<double>(height) / old_height;
  // Before the raster loop: the implicit-transform case reads pre-resize bounds.
  compose_document_text_transforms(document.layers(), {sx, 0.0, 0.0, sy, 0.0, 0.0});
  compose_document_smart_object_placements(document.layers(), {sx, 0.0, 0.0, sy, 0.0, 0.0});
  for (auto& layer : document.layers()) {
    resize_layer_image(layer, old_width, old_height, width, height, method);
  }
  for (auto& channel : document.channels()) {
    resize_document_channel_image(channel, width, height, method);
  }
  document.resize_canvas(width, height);
  transform_document_vector_data(document, {sx, 0.0, 0.0, sy, 0.0, 0.0},
                                 Rect::from_size(width, height), (sx + sy) / 2.0);
}

Rect canvas_resize_frame(Rect reference, CanvasAnchor anchor, std::int32_t new_width,
                         std::int32_t new_height) noexcept {
  const auto offset = canvas_resize_offset(anchor, reference.width, reference.height, new_width, new_height);
  return Rect{reference.x - offset.x, reference.y - offset.y, new_width, new_height};
}

void resize_canvas_and_layers(Document& document, std::int32_t width, std::int32_t height, CanvasAnchor anchor,
                              EditColor extension_color, bool crop_layers) {
  if (width <= 0 || height <= 0) {
    return;
  }
  resize_canvas_to_frame(document, canvas_resize_frame(canvas_rect(document), anchor, width, height),
                         extension_color, crop_layers);
}

void resize_canvas_to_frame(Document& document, Rect frame, EditColor extension_color, bool crop_layers) {
  const auto width = frame.width;
  const auto height = frame.height;
  if (width <= 0 || height <= 0) {
    return;
  }

  const CanvasResizeOffset offset{-frame.x, -frame.y};
  compose_document_text_transforms(
      document.layers(),
      {1.0, 0.0, 0.0, 1.0, static_cast<double>(offset.x), static_cast<double>(offset.y)});
  compose_document_smart_object_placements(
      document.layers(),
      {1.0, 0.0, 0.0, 1.0, static_cast<double>(offset.x), static_cast<double>(offset.y)});
  for (auto& channel : document.channels()) {
    resize_document_channel_canvas(channel, width, height, offset);
  }
  document.resize_canvas(width, height);
  for (auto& layer : document.layers()) {
    resize_layer_to_canvas(layer, width, height, offset, extension_color, crop_layers);
  }
  transform_document_vector_data(document,
                                 {1.0, 0.0, 0.0, 1.0, static_cast<double>(offset.x),
                                  static_cast<double>(offset.y)},
                                 Rect::from_size(width, height));
}

namespace {

// Appends the ids to remove under `layers`. Returns true when every entry (of at least
// one) is slated, so the parent group goes as a whole instead of child by child.
bool collect_layers_outside_canvas(const std::vector<Layer>& layers, Rect canvas, std::vector<LayerId>& ids) {
  bool all_removed = !layers.empty();
  for (const auto& layer : layers) {
    bool remove = false;
    if (layer.kind() == LayerKind::Group) {
      std::vector<LayerId> child_ids;
      remove = collect_layers_outside_canvas(layer.children(), canvas, child_ids);
      if (!remove) {
        ids.insert(ids.end(), child_ids.begin(), child_ids.end());
      }
    } else {
      const auto bounds = layer.bounds();
      remove = !bounds.empty() && intersect_rect(bounds, canvas).empty();
    }
    if (remove) {
      ids.push_back(layer.id());
    } else {
      all_removed = false;
    }
  }
  return all_removed;
}

}  // namespace

std::size_t remove_layers_outside_canvas(Document& document) {
  return remove_layers_outside_canvas(document, canvas_rect(document));
}

std::size_t remove_layers_outside_canvas(Document& document, Rect canvas) {
  std::vector<LayerId> ids;
  (void)collect_layers_outside_canvas(std::as_const(document).layers(), canvas, ids);
  std::size_t removed = 0;
  for (const auto id : ids) {
    // Document::remove_layer keeps the active layer valid and drops orphaned Smart
    // Filter caches, which a raw erase from the tree would not.
    if (document.remove_layer(id)) {
      ++removed;
    }
  }
  return removed;
}

bool crop_document(Document& document, Rect crop) {
  return crop_document(document, intersect_rect(crop, canvas_rect(document)), EditColor{255, 255, 255, 255});
}

bool crop_document(Document& document, Rect crop, double angle_degrees, EditColor extension_color) {
  // Sub-threshold angles take the exact translation path (no resampling), the
  // same tolerance Free Transform uses for "did the angle change".
  if (std::abs(angle_degrees) < 0.01) {
    return crop_document(document, crop, extension_color);
  }
  if (crop.empty()) {
    return false;
  }

  const auto map = rotated_crop_map(crop, angle_degrees);
  compose_document_text_transforms(document.layers(), map.forward);
  compose_document_smart_object_placements(document.layers(), map.forward);
  for (auto& layer : document.layers()) {
    crop_layer_to_rect_rotated(layer, crop, map, extension_color);
  }
  for (auto& channel : document.channels()) {
    crop_document_channel_rotated(channel, crop, map);
  }
  document.resize_canvas(crop.width, crop.height);
  transform_document_vector_data(document, map.forward, Rect::from_size(crop.width, crop.height));
  return true;
}

bool crop_document(Document& document, Rect crop, EditColor extension_color) {
  if (crop.empty()) {
    return false;
  }

  const auto canvas = canvas_rect(document);
  const auto contained = crop.x >= 0 && crop.y >= 0 && crop.x + crop.width <= canvas.width &&
                         crop.y + crop.height <= canvas.height;
  compose_document_text_transforms(
      document.layers(),
      {1.0, 0.0, 0.0, 1.0, static_cast<double>(-crop.x), static_cast<double>(-crop.y)});
  compose_document_smart_object_placements(
      document.layers(),
      {1.0, 0.0, 0.0, 1.0, static_cast<double>(-crop.x), static_cast<double>(-crop.y)});
  for (auto& layer : document.layers()) {
    if (contained) {
      crop_layer_to_rect(layer, crop);
    } else {
      crop_layer_to_rect_expanding(layer, crop, extension_color);
    }
  }
  for (auto& channel : document.channels()) {
    if (contained) {
      crop_document_channel(channel, crop);
    } else {
      // crop_document_channel indexes the source unclamped, so out-of-canvas
      // rects go through the offset blit that zero/spot-fills the new area.
      resize_document_channel_canvas(channel, crop.width, crop.height, CanvasResizeOffset{-crop.x, -crop.y});
    }
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
  compose_document_text_transforms(document.layers(),
                                   {0.0, 1.0, -1.0, 0.0, static_cast<double>(old_height), 0.0});
  compose_document_smart_object_placements(
      document.layers(), {0.0, 1.0, -1.0, 0.0, static_cast<double>(old_height), 0.0});
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
  compose_document_text_transforms(document.layers(),
                                   {0.0, -1.0, 1.0, 0.0, 0.0, static_cast<double>(old_width)});
  compose_document_smart_object_placements(
      document.layers(), {0.0, -1.0, 1.0, 0.0, 0.0, static_cast<double>(old_width)});
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

bool rotate_document_arbitrary(Document& document, double clockwise_degrees, EditColor extension_color) {
  if (!std::isfinite(clockwise_degrees)) {
    return false;
  }
  const auto width = document.width();
  const auto height = document.height();
  if (width <= 0 || height <= 0) {
    return false;
  }
  double degrees = std::fmod(clockwise_degrees, 360.0);
  if (degrees > 180.0) {
    degrees -= 360.0;
  } else if (degrees <= -180.0) {
    degrees += 360.0;
  }
  if (std::abs(degrees) < 0.01) {
    return true;
  }
  // The result is the bounding box of the rotated canvas, centered where the canvas was.
  const auto radians = degrees * (std::numbers::pi / 180.0);
  const auto cos_abs = std::abs(std::cos(radians));
  const auto sin_abs = std::abs(std::sin(radians));
  const auto new_width =
      std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(width * cos_abs + height * sin_abs)));
  const auto new_height =
      std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(width * sin_abs + height * cos_abs)));
  const Rect crop{static_cast<std::int32_t>(std::floor((width - new_width) / 2.0)),
                  static_cast<std::int32_t>(std::floor((height - new_height) / 2.0)), new_width, new_height};
  // The rotated crop "straightens" a box rotated by its angle: a box rotated clockwise on
  // screen comes out with the image turned counterclockwise, so a clockwise canvas rotation
  // is the crop of a box rotated the other way.
  return crop_document(document, crop, -degrees, extension_color);
}

namespace {

// Wrap-rolls a buffer by (dx, dy) within its own dimensions: content pushed past one edge
// re-enters at the opposite edge. dx/dy must already be normalized into [0, extent).
[[nodiscard]] PixelBuffer wrap_rolled_pixels(const PixelBuffer& source, std::int32_t dx, std::int32_t dy) {
  PixelBuffer rolled(source.width(), source.height(), source.format());
  if (source.empty()) {
    return rolled;
  }
  const auto pixel_bytes = bytes_per_pixel(source.format());
  const auto prefix_bytes =
      static_cast<std::ptrdiff_t>(static_cast<std::size_t>(source.width() - dx) * pixel_bytes);
  const auto offset_bytes = static_cast<std::ptrdiff_t>(static_cast<std::size_t>(dx) * pixel_bytes);
  for (std::int32_t y = 0; y < source.height(); ++y) {
    const auto source_row = source.row(y);
    auto destination_row = rolled.row((y + dy) % source.height());
    std::copy(source_row.begin(), source_row.begin() + prefix_bytes, destination_row.begin() + offset_bytes);
    std::copy(source_row.begin() + prefix_bytes, source_row.end(), destination_row.begin());
  }
  return rolled;
}

// Expands the layer mask to a canvas-sized plane (default_color everywhere the stored
// rect does not cover), then wrap-rolls it with the content.
void wrap_offset_layer_mask(Layer& layer, std::int32_t width, std::int32_t height, std::int32_t dx,
                            std::int32_t dy) {
  auto& mask = layer.mask();
  if (!mask.has_value()) {
    return;
  }
  PixelBuffer plane(width, height, mask->pixels.format());
  fill_coverage(plane, mask->default_color);
  if (!mask->pixels.empty()) {
    const auto pixel_bytes = bytes_per_pixel(mask->pixels.format());
    const auto intersection = intersect_rect(mask->bounds, Rect{0, 0, width, height});
    for (std::int32_t y = 0; y < intersection.height; ++y) {
      const auto source_row =
          std::as_const(mask->pixels)
              .row(intersection.y - mask->bounds.y + y)
              .subspan(static_cast<std::size_t>(intersection.x - mask->bounds.x) * pixel_bytes,
                       static_cast<std::size_t>(intersection.width) * pixel_bytes);
      auto destination_row =
          plane.row(intersection.y + y)
              .subspan(static_cast<std::size_t>(intersection.x) * pixel_bytes,
                       static_cast<std::size_t>(intersection.width) * pixel_bytes);
      std::copy(source_row.begin(), source_row.end(), destination_row.begin());
    }
  }
  mask->pixels = wrap_rolled_pixels(plane, dx, dy);
  mask->bounds = Rect{0, 0, width, height};
}

// Object-like layers (text, placed records, shape layers) cannot wrap across an edge
// without rasterizing, so they translate whole; their masks travel with them unclipped
// so the inverse offset restores them exactly.
void translate_layer_and_mask(Layer& layer, std::int32_t dx, std::int32_t dy) {
  if (auto& mask = layer.mask(); mask.has_value()) {
    mask->bounds.x += dx;
    mask->bounds.y += dy;
  }
  if (!layer.bounds().empty()) {
    const auto bounds = layer.bounds();
    layer.set_bounds(Rect{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height});
  }
}

// roll_dx/roll_dy are the wrap deltas normalized into [0, extent); raw_dx/raw_dy keep the
// caller's sign so translated (non-wrappable) layers invert exactly under the opposite
// offset instead of drifting by a full canvas span.
void wrap_offset_layer(Layer& layer, std::int32_t width, std::int32_t height, std::int32_t roll_dx,
                       std::int32_t roll_dy, std::int32_t raw_dx, std::int32_t raw_dy) {
  if (layer.kind() == LayerKind::Group) {
    for (auto& child : layer.children()) {
      wrap_offset_layer(child, width, height, roll_dx, roll_dy, raw_dx, raw_dy);
    }
    wrap_offset_layer_mask(layer, width, height, roll_dx, roll_dy);
    return;
  }
  if (layer.kind() == LayerKind::Adjustment) {
    // The mask is what positions an adjustment's effect; its extent semantics stay as-is.
    wrap_offset_layer_mask(layer, width, height, roll_dx, roll_dy);
    return;
  }
  if (layer.kind() != LayerKind::Pixel || layer.vector_shape() != nullptr) {
    // Shape layers keep their raster cache in sync via the vector pass in
    // wrap_offset_document (the translated path re-rasters pixels and bounds).
    translate_layer_and_mask(layer, raw_dx, raw_dy);
    return;
  }

  wrap_offset_layer_mask(layer, width, height, roll_dx, roll_dy);
  const auto old_bounds = layer.bounds();
  const bool canvas_sized = old_bounds.x == 0 && old_bounds.y == 0 && old_bounds.width == width &&
                            old_bounds.height == height && layer.pixels().width() == width &&
                            layer.pixels().height() == height;
  if (!canvas_sized) {
    // Normalize to the canvas rect first (off-canvas content clips, like a crop; new area
    // is transparent) so the roll is a pure permutation of canvas pixels.
    resize_layer_to_canvas(layer, width, height, CanvasResizeOffset{0, 0}, EditColor{0, 0, 0, 0});
  }
  layer.set_pixels(wrap_rolled_pixels(layer.pixels(), roll_dx, roll_dy));
  layer.set_bounds(Rect{0, 0, width, height});
}

}  // namespace

void wrap_offset_document(Document& document, std::int32_t dx, std::int32_t dy) {
  const auto width = document.width();
  const auto height = document.height();
  if (width <= 0 || height <= 0 || (dx == 0 && dy == 0)) {
    return;
  }
  const auto roll_dx = ((dx % width) + width) % width;
  const auto roll_dy = ((dy % height) + height) % height;
  // Text layers translate whole (raw deltas, no wrap), so their transforms follow the
  // same raw offset and the inverse offset restores them exactly.
  compose_document_text_transforms(
      document.layers(), {1.0, 0.0, 0.0, 1.0, static_cast<double>(dx), static_cast<double>(dy)});
  // Smart-object placements deliberately do NOT compose here: a smart object is a
  // LayerKind::Pixel layer, so wrap_offset_layer ROLLS its preview across the canvas
  // edges instead of translating it whole, and a quad translated by the raw delta would
  // stop describing those pixels. Seam offset therefore still refuses documents
  // containing smart objects (main_window_layer_ops.cpp) until placed records take the
  // translate_layer_and_mask path the comment above already claims for them.
  for (auto& layer : document.layers()) {
    wrap_offset_layer(layer, width, height, roll_dx, roll_dy, dx, dy);
  }
  for (auto& channel : document.channels()) {
    channel.set_pixels(wrap_rolled_pixels(std::as_const(channel).pixels(), roll_dx, roll_dy));
  }
  transform_document_vector_data(document,
                                 {1.0, 0.0, 0.0, 1.0, static_cast<double>(dx), static_cast<double>(dy)},
                                 Rect::from_size(width, height));
}

}  // namespace patchy
