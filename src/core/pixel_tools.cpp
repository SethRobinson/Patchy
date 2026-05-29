#include "core/pixel_tools.hpp"

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

[[nodiscard]] Rect canvas_rect(const Document& document) noexcept {
  return Rect{0, 0, document.width(), document.height()};
}

[[nodiscard]] Rect normalized_rect(Rect rect) noexcept {
  if (rect.width < 0) {
    rect.x += rect.width;
    rect.width = -rect.width;
  }
  if (rect.height < 0) {
    rect.y += rect.height;
    rect.height = -rect.height;
  }
  return rect;
}

[[nodiscard]] float selection_coverage(const EditOptions& options, std::int32_t x, std::int32_t y) noexcept {
  if (options.selection_coverage) {
    return std::clamp(options.selection_coverage(x, y), 0.0F, 1.0F);
  }
  if (options.selection_mask) {
    return options.selection_mask(x, y) ? 1.0F : 0.0F;
  }
  return !options.selection.has_value() || options.selection->contains(x, y) ? 1.0F : 0.0F;
}

[[nodiscard]] bool selection_allows(const EditOptions& options, std::int32_t x, std::int32_t y) noexcept {
  return selection_coverage(options, x, y) > 0.0F;
}

std::uint8_t clamp_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float brush_coverage(double distance_squared, int radius, int softness) {
  if (radius <= 0) {
    return distance_squared <= 0.0 ? 1.0F : 0.0F;
  }

  const auto radius_squared = static_cast<double>(radius) * static_cast<double>(radius);
  if (distance_squared > radius_squared) {
    return 0.0F;
  }

  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return 1.0F;
  }

  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  const auto inner_radius = std::max(0.0, static_cast<double>(radius) - edge_width);
  const auto distance = std::sqrt(distance_squared);
  if (distance <= inner_radius) {
    return 1.0F;
  }
  const auto t = std::clamp((distance - inner_radius) / edge_width, 0.0, 1.0);
  const auto smooth = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  return static_cast<float>(1.0 - smooth);
}

[[nodiscard]] Layer* editable_layer(Document& document, LayerId layer_id) noexcept {
  auto* layer = document.find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return nullptr;
  }
  auto& pixels = layer->pixels();
  if (pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3) {
    return nullptr;
  }
  return layer;
}

void write_pixel(PixelBuffer& pixels, std::uint8_t* px, const EditOptions& options, bool erase, float coverage = 1.0F) {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  if (coverage <= 0.0F) {
    return;
  }

  const auto channels = pixels.format().channels;
  const auto locked_alpha = options.lock_transparent_pixels && channels >= 4;
  if (locked_alpha && px[3] == 0) {
    return;
  }
  if (erase) {
    const auto erase_alpha = (static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F) * coverage;
    if (channels >= 4) {
      if (locked_alpha) {
        return;
      }
      px[3] = clamp_byte(static_cast<float>(px[3]) * (1.0F - erase_alpha));
    } else {
      px[0] =
          clamp_byte(static_cast<float>(options.secondary.r) * erase_alpha + static_cast<float>(px[0]) * (1.0F - erase_alpha));
      px[1] =
          clamp_byte(static_cast<float>(options.secondary.g) * erase_alpha + static_cast<float>(px[1]) * (1.0F - erase_alpha));
      px[2] =
          clamp_byte(static_cast<float>(options.secondary.b) * erase_alpha + static_cast<float>(px[2]) * (1.0F - erase_alpha));
    }
    return;
  }

  const auto source_alpha = (static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F) * coverage;
  if (channels >= 4 && !locked_alpha && px[3] == 0) {
    px[0] = options.primary.r;
    px[1] = options.primary.g;
    px[2] = options.primary.b;
    px[3] = std::max<std::uint8_t>(1, clamp_byte(source_alpha * 255.0F));
    return;
  }
  if (channels >= 4) {
    if (locked_alpha) {
      if (source_alpha >= 0.999F) {
        px[0] = options.primary.r;
        px[1] = options.primary.g;
        px[2] = options.primary.b;
      } else {
        px[0] = clamp_byte(static_cast<float>(options.primary.r) * source_alpha +
                           static_cast<float>(px[0]) * (1.0F - source_alpha));
        px[1] = clamp_byte(static_cast<float>(options.primary.g) * source_alpha +
                           static_cast<float>(px[1]) * (1.0F - source_alpha));
        px[2] = clamp_byte(static_cast<float>(options.primary.b) * source_alpha +
                           static_cast<float>(px[2]) * (1.0F - source_alpha));
      }
      return;
    }
    const auto destination_alpha = static_cast<float>(px[3]) / 255.0F;
    const auto out_alpha = source_alpha + destination_alpha * (1.0F - source_alpha);
    if (out_alpha <= 0.0F) {
      return;
    }
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      const auto destination_premultiplied = static_cast<float>(px[channel]) * destination_alpha;
      const auto source_value = channel == 0 ? static_cast<float>(options.primary.r)
                                             : channel == 1 ? static_cast<float>(options.primary.g)
                                                            : static_cast<float>(options.primary.b);
      px[channel] =
          clamp_byte((source_value * source_alpha + destination_premultiplied * (1.0F - source_alpha)) / out_alpha);
    }
    px[3] = clamp_byte(out_alpha * 255.0F);
    return;
  }
  if (source_alpha >= 0.999F) {
    px[0] = options.primary.r;
    px[1] = options.primary.g;
    px[2] = options.primary.b;
  } else {
    px[0] = clamp_byte(static_cast<float>(options.primary.r) * source_alpha + static_cast<float>(px[0]) * (1.0F - source_alpha));
    px[1] = clamp_byte(static_cast<float>(options.primary.g) * source_alpha + static_cast<float>(px[1]) * (1.0F - source_alpha));
    px[2] = clamp_byte(static_cast<float>(options.primary.b) * source_alpha + static_cast<float>(px[2]) * (1.0F - source_alpha));
  }
}

void ensure_alpha_for_erase(Layer& layer) {
  auto& source = layer.pixels();
  if (source.format().channels >= 4) {
    return;
  }

  const auto old_bounds = layer.bounds();
  PixelBuffer rgba(source.width(), source.height(), PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* src = source.pixel(x, y);
      auto* dst = rgba.pixel(x, y);
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = 255;
    }
  }
  layer.set_pixels(std::move(rgba));
  layer.set_bounds(old_bounds);
}

EditColor lerp_color(EditColor a, EditColor b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  const auto lerp = [t](std::uint8_t lhs, std::uint8_t rhs) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(lhs) +
                                                            (static_cast<double>(rhs) - static_cast<double>(lhs)) * t),
                                                0L, 255L));
  };
  return EditColor{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

std::array<std::uint8_t, 4> sample_layer_rgba(const Layer& layer, std::int32_t document_x, std::int32_t document_y) {
  const auto bounds = layer.bounds();
  if (!bounds.contains(document_x, document_y)) {
    return {0, 0, 0, 0};
  }

  const auto& pixels = layer.pixels();
  const auto* px = pixels.pixel(document_x - bounds.x, document_y - bounds.y);
  return {px[0], px[1], px[2],
          pixels.format().channels >= 4 ? px[3] : static_cast<std::uint8_t>(255)};
}

void ensure_smudge_sample(SmudgeState& state, int diameter) {
  diameter = std::max(1, diameter);
  const auto required_size = static_cast<std::size_t>(diameter) * static_cast<std::size_t>(diameter) * 4U;
  if (state.diameter != diameter || state.sample_rgba.size() != required_size) {
    state.diameter = diameter;
    state.sample_rgba.assign(required_size, 0);
    state.initialized = false;
  }
}

void capture_smudge_sample(SmudgeState& state, const Layer& layer, std::int32_t center_x, std::int32_t center_y,
                           int radius) {
  ensure_smudge_sample(state, radius * 2 + 1);
  for (int y = 0; y < state.diameter; ++y) {
    for (int x = 0; x < state.diameter; ++x) {
      const auto color = sample_layer_rgba(layer, center_x + x - radius, center_y + y - radius);
      auto* dst = state.sample_rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(state.diameter) +
                                              static_cast<std::size_t>(x)) *
                                                 4U;
      std::copy(color.begin(), color.end(), dst);
    }
  }
  state.initialized = true;
}

bool same_pixel(const std::uint8_t* px, const std::vector<std::uint8_t>& target, std::uint16_t channels) {
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    if (px[channel] != target[channel]) {
      return false;
    }
  }
  return true;
}

void crop_layer_mask_to_rect(Layer& layer, Rect crop);
void rotate_layer_mask_clockwise(Layer& layer, std::int32_t document_height);
void rotate_layer_mask_counterclockwise(Layer& layer, std::int32_t document_width);

template <typename Callback>
void visit_pixel_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Callback&& callback) {
  const auto dx = std::abs(x1 - x0);
  const auto sx = x0 < x1 ? 1 : -1;
  const auto dy = -std::abs(y1 - y0);
  const auto sy = y0 < y1 ? 1 : -1;
  auto error = dx + dy;

  while (true) {
    callback(x0, y0);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const auto doubled_error = error * 2;
    if (doubled_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

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

[[nodiscard]] PixelFormat canvas_resized_format_for_layer(const Layer& layer, const PixelBuffer& source,
                                                          EditColor extension_color = EditColor{255, 255, 255,
                                                                                                 255}) noexcept {
  if (source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return source.format();
  }
  if (source.format().channels >= 4 || layer.name() != "Background" || extension_color.a < 255) {
    return PixelFormat::rgba8();
  }
  return source.format();
}

void fill_resized_layer_background(PixelBuffer& pixels, const Layer& layer,
                                   EditColor extension_color = EditColor{255, 255, 255, 255}) {
  pixels.clear(0);
  if (layer.name() != "Background" || pixels.format().bit_depth != BitDepth::UInt8 || pixels.format().channels < 3 ||
      pixels.empty()) {
    return;
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = extension_color.r;
      px[1] = extension_color.g;
      px[2] = extension_color.b;
      if (pixels.format().channels >= 4) {
        px[3] = extension_color.a;
      }
    }
  }
}

void copy_resized_layer_pixel(const PixelBuffer& source, PixelBuffer& destination, std::int32_t sx, std::int32_t sy,
                              std::int32_t dx, std::int32_t dy) {
  const auto* src = source.pixel(sx, sy);
  auto* dst = destination.pixel(dx, dy);
  if (source.format().bit_depth == BitDepth::UInt8 && destination.format().bit_depth == BitDepth::UInt8 &&
      source.format().channels >= 3 && destination.format().channels >= 3) {
    const auto channel_count = std::min(source.format().channels, destination.format().channels);
    for (std::uint16_t channel = 0; channel < channel_count; ++channel) {
      dst[channel] = src[channel];
    }
    if (destination.format().channels >= 4 && source.format().channels < 4) {
      dst[3] = 255;
    }
    return;
  }

  const auto bytes = std::min(bytes_per_pixel(source.format()), bytes_per_pixel(destination.format()));
  std::copy(src, src + bytes, dst);
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

}  // namespace

std::vector<GradientStop> normalized_gradient_stops(const std::vector<GradientStop>& stops) {
  std::vector<GradientStop> normalized;
  normalized.reserve(stops.size());
  for (const auto& stop : stops) {
    auto copy = stop;
    copy.location = std::clamp(copy.location, 0.0F, 1.0F);
    normalized.push_back(copy);
  }
  if (normalized.empty()) {
    normalized.push_back(GradientStop{0.0F, EditColor{0, 0, 0, 255}});
    normalized.push_back(GradientStop{1.0F, EditColor{255, 255, 255, 255}});
  } else if (normalized.size() == 1U) {
    normalized.push_back(GradientStop{1.0F, normalized.front().color});
  }
  std::sort(normalized.begin(), normalized.end(), [](const GradientStop& lhs, const GradientStop& rhs) {
    return lhs.location < rhs.location;
  });
  return normalized;
}

EditColor gradient_color_at(const std::vector<GradientStop>& sorted_stops, float opacity, bool reverse,
                            double position) {
  if (sorted_stops.empty()) {
    return EditColor{};
  }
  position = std::clamp(reverse ? 1.0 - position : position, 0.0, 1.0);
  opacity = std::clamp(opacity, 0.0F, 1.0F);
  const auto apply_opacity = [opacity](EditColor color) {
    color.a = static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<float>(color.a) * opacity), 0L, 255L));
    return color;
  };
  if (position <= sorted_stops.front().location) {
    return apply_opacity(sorted_stops.front().color);
  }
  if (position >= sorted_stops.back().location) {
    return apply_opacity(sorted_stops.back().color);
  }
  for (std::size_t index = 1; index < sorted_stops.size(); ++index) {
    const auto& right = sorted_stops[index];
    const auto& left = sorted_stops[index - 1U];
    if (position <= right.location) {
      const auto span = std::max(0.0001F, right.location - left.location);
      const auto t = (position - left.location) / static_cast<double>(span);
      return apply_opacity(lerp_color(left.color, right.color, t));
    }
  }
  return apply_opacity(sorted_stops.back().color);
}

void expand_layer_to_include_rect(Layer& layer, Rect document_rect) {
  document_rect = normalized_rect(document_rect);
  if (document_rect.empty()) {
    return;
  }

  auto& source = layer.pixels();
  if (source.empty()) {
    PixelBuffer expanded(document_rect.width, document_rect.height, PixelFormat::rgba8());
    expanded.clear(0);
    layer.set_pixels(std::move(expanded));
    layer.set_bounds(document_rect);
    return;
  }

  const auto old_bounds = layer.bounds();
  const auto new_bounds = unite_rect(old_bounds, document_rect);
  if (new_bounds.x == old_bounds.x && new_bounds.y == old_bounds.y && new_bounds.width == old_bounds.width &&
      new_bounds.height == old_bounds.height) {
    return;
  }

  const auto destination_format = canvas_resized_format_for_layer(layer, source);
  PixelBuffer expanded(new_bounds.width, new_bounds.height, destination_format);
  fill_resized_layer_background(expanded, layer);

  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      copy_resized_layer_pixel(source, expanded, x, y, old_bounds.x - new_bounds.x + x, old_bounds.y - new_bounds.y + y);
    }
  }

  layer.set_pixels(std::move(expanded));
  layer.set_bounds(new_bounds);
}

Rect paint_brush(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y, const EditOptions& options,
                 bool erase) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  const auto dab_rect = intersect_rect(Rect{x - radius, y - radius, radius * 2 + 1, radius * 2 + 1}, canvas_rect(document));
  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, dab_rect);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  Rect dirty;

  for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
    const auto local_y = py - bounds.y;
    if (local_y < 0 || local_y >= pixels.height()) {
      continue;
    }

    auto row = pixels.row(local_y);
    for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
      const auto selected_coverage = selection_coverage(options, px_doc, py);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      const auto dx = px_doc - x;
      const auto dy = py - y;
      const auto coverage =
          brush_coverage(static_cast<double>(dx * dx + dy * dy), radius, options.brush_softness) * selected_coverage;
      if (coverage <= 0.0F) {
        continue;
      }

      const auto local_x = px_doc - bounds.x;
      if (local_x < 0 || local_x >= pixels.width()) {
        continue;
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        continue;
      }
      auto effective_coverage = coverage;
      if (options.stroke_coverage_gate) {
        effective_coverage = options.stroke_coverage_gate(px_doc, py, coverage);
        if (effective_coverage <= 0.0F) {
          continue;
        }
      }

      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      write_pixel(pixels, px, options, erase, effective_coverage);
      dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
    }
  }
  return dirty;
}

Rect paint_brush_segment(Document& document, LayerId layer_id, double x0, double y0, double x1, double y1,
                         const EditOptions& options, bool erase) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  if (erase) {
    ensure_alpha_for_erase(*layer);
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  if (radius == 0) {
    const auto start_x = static_cast<std::int32_t>(std::floor(x0));
    const auto start_y = static_cast<std::int32_t>(std::floor(y0));
    const auto end_x = static_cast<std::int32_t>(std::floor(x1));
    const auto end_y = static_cast<std::int32_t>(std::floor(y1));
    const auto path_rect = intersect_rect(Rect{std::min(start_x, end_x),
                                               std::min(start_y, end_y),
                                               std::abs(end_x - start_x) + 1,
                                               std::abs(end_y - start_y) + 1},
                                          canvas_rect(document));
    if (path_rect.empty()) {
      return {};
    }

    if (!erase && !options.lock_transparent_pixels) {
      expand_layer_to_include_rect(*layer, path_rect);
    }

    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    Rect dirty;
    visit_pixel_line(start_x, start_y, end_x, end_y, [&](std::int32_t px_doc, std::int32_t py) {
      if (!canvas_rect(document).contains(px_doc, py)) {
        return;
      }
      auto effective_coverage = selection_coverage(options, px_doc, py);
      if (effective_coverage <= 0.0F) {
        return;
      }
      const auto local_x = px_doc - bounds.x;
      const auto local_y = py - bounds.y;
      if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
        return;
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        return;
      }
      if (options.stroke_coverage_gate) {
        effective_coverage = options.stroke_coverage_gate(px_doc, py, effective_coverage);
        if (effective_coverage <= 0.0F) {
          return;
        }
      }

      auto row = pixels.row(local_y);
      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      write_pixel(pixels, px, options, erase, effective_coverage);
      dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
    });
    return dirty;
  }

  const auto left = static_cast<std::int32_t>(std::floor(std::min(x0, x1) - static_cast<double>(radius)));
  const auto top = static_cast<std::int32_t>(std::floor(std::min(y0, y1) - static_cast<double>(radius)));
  const auto right =
      static_cast<std::int32_t>(std::ceil(std::max(x0, x1) + static_cast<double>(radius))) + 1;
  const auto bottom =
      static_cast<std::int32_t>(std::ceil(std::max(y0, y1) + static_cast<double>(radius))) + 1;
  const auto stroke_rect = intersect_rect(Rect{left, top, right - left, bottom - top}, canvas_rect(document));
  if (stroke_rect.empty()) {
    return {};
  }

  if (!erase && !options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, stroke_rect);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  const auto segment_length_squared = dx * dx + dy * dy;
  Rect dirty;

  for (std::int32_t py = stroke_rect.y; py < stroke_rect.y + stroke_rect.height; ++py) {
    const auto local_y = py - bounds.y;
    if (local_y < 0 || local_y >= pixels.height()) {
      continue;
    }

    auto row = pixels.row(local_y);
    for (std::int32_t px_doc = stroke_rect.x; px_doc < stroke_rect.x + stroke_rect.width; ++px_doc) {
      const auto along =
          segment_length_squared <= std::numeric_limits<double>::epsilon()
              ? 0.0
              : std::clamp(((static_cast<double>(px_doc) - x0) * dx + (static_cast<double>(py) - y0) * dy) /
                                segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = x0 + dx * along;
      const auto closest_y = y0 + dy * along;
      const auto distance_x = static_cast<double>(px_doc) - closest_x;
      const auto distance_y = static_cast<double>(py) - closest_y;
      const auto coverage = brush_coverage(distance_x * distance_x + distance_y * distance_y, radius,
                                           options.brush_softness);
      if (coverage <= 0.0F) {
        continue;
      }
      const auto selected_coverage = selection_coverage(options, px_doc, py);
      if (selected_coverage <= 0.0F) {
        continue;
      }

      const auto local_x = px_doc - bounds.x;
      if (local_x < 0 || local_x >= pixels.width()) {
        continue;
      }
      if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
        continue;
      }
      auto effective_coverage = coverage * selected_coverage;
      if (options.stroke_coverage_gate) {
        effective_coverage = options.stroke_coverage_gate(px_doc, py, effective_coverage);
        if (effective_coverage <= 0.0F) {
          continue;
        }
      }

      auto* px = row.data() + static_cast<std::size_t>(local_x) * channels;
      write_pixel(pixels, px, options, erase, effective_coverage);
      dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
    }
  }
  return dirty;
}

Rect paint_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                         std::int32_t y1, const EditOptions& options, bool erase) {
  return paint_brush_segment(document, layer_id, static_cast<double>(x0), static_cast<double>(y0),
                             static_cast<double>(x1), static_cast<double>(y1), options, erase);
}

Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options) {
  SmudgeState state;
  return smudge_brush_segment(document, layer_id, x0, y0, x1, y1, options, state);
}

Rect smudge_brush_segment(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options, SmudgeState& state) {
  const auto dx = x1 - x0;
  const auto dy = y1 - y0;
  if (dx == 0 && dy == 0) {
    auto* layer = editable_layer(document, layer_id);
    if (layer != nullptr) {
      const auto radius = std::max(1, options.brush_size) / 2;
      capture_smudge_sample(state, *layer, x0, y0, radius);
    }
    return {};
  }

  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  const auto radius = std::max(1, options.brush_size) / 2;
  const auto diameter = radius * 2 + 1;
  const auto left = std::min(x0, x1) - radius;
  const auto top = std::min(y0, y1) - radius;
  const auto right = std::max(x0, x1) + radius + 1;
  const auto bottom = std::max(y0, y1) + radius + 1;
  auto stroke_rect = intersect_rect(Rect{left, top, right - left, bottom - top}, canvas_rect(document));
  if (options.selection.has_value()) {
    stroke_rect = intersect_rect(stroke_rect, *options.selection);
  }
  if (stroke_rect.empty()) {
    return {};
  }

  if (!options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, stroke_rect);
  }

  stroke_rect = intersect_rect(stroke_rect, layer->bounds());
  if (stroke_rect.empty()) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto color_channels = std::min<std::uint16_t>(channels, 3);
  const auto strength = static_cast<float>(std::clamp<int>(options.primary.a, 1, 255)) / 255.0F;
  if (!state.initialized || state.diameter != diameter) {
    capture_smudge_sample(state, *layer, x0, y0, radius);
  }

  Rect dirty;
  const auto stamp_at = [&](std::int32_t center_x, std::int32_t center_y) {
    const auto dab_rect = intersect_rect(
        Rect{center_x - radius, center_y - radius, diameter, diameter}, stroke_rect);
    if (dab_rect.empty()) {
      return;
    }

    for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
      auto row = pixels.row(py - bounds.y);
      for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
        const auto sample_x = px_doc - (center_x - radius);
        const auto sample_y = py - (center_y - radius);
        const auto distance_x = px_doc - center_x;
        const auto distance_y = py - center_y;
        auto coverage = brush_coverage(static_cast<double>(distance_x * distance_x + distance_y * distance_y),
                                       radius, options.brush_softness);
        coverage *= selection_coverage(options, px_doc, py);
        if (coverage <= 0.0F) {
          continue;
        }

        if (options.stroke_pixel_gate && !options.stroke_pixel_gate(px_doc, py)) {
          continue;
        }

        auto* dst = row.data() + static_cast<std::size_t>(px_doc - bounds.x) * channels;
        if (options.lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
          continue;
        }

        const auto* src =
            state.sample_rgba.data() +
            (static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(state.diameter) +
             static_cast<std::size_t>(sample_x)) *
                4U;
        const auto amount = std::clamp(strength * coverage, 0.0F, 1.0F);
        bool changed = false;
        for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
          const auto value = clamp_byte(static_cast<float>(src[channel]) * amount +
                                        static_cast<float>(dst[channel]) * (1.0F - amount));
          changed = changed || value != dst[channel];
          dst[channel] = value;
        }
        if (channels >= 4 && !options.lock_transparent_pixels) {
          const auto alpha = clamp_byte(static_cast<float>(src[3]) * amount +
                                        static_cast<float>(dst[3]) * (1.0F - amount));
          changed = changed || alpha != dst[3];
          dst[3] = alpha;
        }
        if (changed) {
          dirty = unite_rect(dirty, Rect{px_doc, py, 1, 1});
        }
      }
    }

    const auto pickup = std::clamp(1.0F - strength, 0.0F, 1.0F);
    if (pickup <= 0.0F) {
      return;
    }
    for (std::int32_t py = dab_rect.y; py < dab_rect.y + dab_rect.height; ++py) {
      for (std::int32_t px_doc = dab_rect.x; px_doc < dab_rect.x + dab_rect.width; ++px_doc) {
        const auto sample_x = px_doc - (center_x - radius);
        const auto sample_y = py - (center_y - radius);
        const auto distance_x = px_doc - center_x;
        const auto distance_y = py - center_y;
        auto coverage = brush_coverage(static_cast<double>(distance_x * distance_x + distance_y * distance_y),
                                       radius, options.brush_softness);
        coverage *= selection_coverage(options, px_doc, py);
        if (coverage <= 0.0F) {
          continue;
        }

        const auto current = sample_layer_rgba(*layer, px_doc, py);
        auto* dst =
            state.sample_rgba.data() +
            (static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(state.diameter) +
             static_cast<std::size_t>(sample_x)) *
                4U;
        const auto amount = std::clamp(pickup * coverage, 0.0F, 1.0F);
        for (std::size_t channel = 0; channel < 4U; ++channel) {
          dst[channel] = clamp_byte(static_cast<float>(current[channel]) * amount +
                                    static_cast<float>(dst[channel]) * (1.0F - amount));
        }
      }
    }
  };

  const auto distance = std::sqrt(static_cast<double>(dx) * static_cast<double>(dx) +
                                  static_cast<double>(dy) * static_cast<double>(dy));
  const auto spacing = std::max(1.0, static_cast<double>(radius) * 0.2);
  const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
  auto last_center_x = std::numeric_limits<std::int32_t>::min();
  auto last_center_y = std::numeric_limits<std::int32_t>::min();
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    const auto center_x = static_cast<std::int32_t>(std::lround(static_cast<double>(x0) + static_cast<double>(dx) * t));
    const auto center_y = static_cast<std::int32_t>(std::lround(static_cast<double>(y0) + static_cast<double>(dy) * t));
    if (last_center_x == center_x && last_center_y == center_y) {
      continue;
    }
    stamp_at(center_x, center_y);
    last_center_x = center_x;
    last_center_y = center_y;
  }
  return dirty;
}

Rect draw_line(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
               const EditOptions& options, bool erase) {
  return paint_brush_segment(document, layer_id, x0, y0, x1, y1, options, erase);
}

Rect draw_rectangle(Document& document, LayerId layer_id, Rect rect, const EditOptions& options, bool erase) {
  rect = normalized_rect(rect);
  if (rect.empty()) {
    return {};
  }

  if (options.fill_shapes) {
    return erase ? clear_rect(document, layer_id, rect, options) : fill_rect(document, layer_id, rect, options);
  }

  Rect dirty;
  dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x, rect.y, rect.x + rect.width - 1, rect.y, options, erase));
  dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x + rect.width - 1, rect.y, rect.x + rect.width - 1,
                                      rect.y + rect.height - 1, options, erase));
  dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x + rect.width - 1, rect.y + rect.height - 1, rect.x,
                                      rect.y + rect.height - 1, options, erase));
  dirty = unite_rect(dirty, draw_line(document, layer_id, rect.x, rect.y + rect.height - 1, rect.x, rect.y, options, erase));
  return dirty;
}

Rect draw_ellipse(Document& document, LayerId layer_id, Rect rect, const EditOptions& options, bool erase) {
  rect = normalized_rect(rect);
  if (rect.empty()) {
    return {};
  }

  if (options.fill_shapes) {
    auto* layer = editable_layer(document, layer_id);
    if (layer == nullptr) {
      return {};
    }
    if (erase) {
      ensure_alpha_for_erase(*layer);
    }
    auto affected = intersect_rect(rect, canvas_rect(document));
    if (options.selection.has_value()) {
      affected = intersect_rect(affected, *options.selection);
    }
    if (!erase && !options.lock_transparent_pixels) {
      expand_layer_to_include_rect(*layer, affected);
    }

    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    affected = intersect_rect(affected, bounds);
    if (affected.empty()) {
      return {};
    }

    const auto rx = std::max(1.0, static_cast<double>(rect.width) / 2.0);
    const auto ry = std::max(1.0, static_cast<double>(rect.height) / 2.0);
    const auto cx = static_cast<double>(rect.x) + rx;
    const auto cy = static_cast<double>(rect.y) + ry;
    bool wrote = false;
    for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
      auto row = pixels.row(y - bounds.y);
      for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
        const auto selected_coverage = selection_coverage(options, x, y);
        if (selected_coverage <= 0.0F) {
          continue;
        }
        const auto nx = (static_cast<double>(x) + 0.5 - cx) / rx;
        const auto ny = (static_cast<double>(y) + 0.5 - cy) / ry;
        if (nx * nx + ny * ny > 1.0) {
          continue;
        }
        auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
        write_pixel(pixels, px, options, erase, selected_coverage);
        wrote = true;
      }
    }
    return wrote ? affected : Rect{};
  }

  constexpr int kSamples = 720;
  const auto rx = std::max(1.0, static_cast<double>(rect.width) / 2.0);
  const auto ry = std::max(1.0, static_cast<double>(rect.height) / 2.0);
  const auto cx = static_cast<double>(rect.x) + rx;
  const auto cy = static_cast<double>(rect.y) + ry;
  auto previous_x = static_cast<std::int32_t>(std::round(cx + rx));
  auto previous_y = static_cast<std::int32_t>(std::round(cy));

  Rect dirty;
  for (int i = 1; i <= kSamples; ++i) {
    const auto angle = (static_cast<double>(i) / static_cast<double>(kSamples)) * 2.0 * 3.14159265358979323846;
    const auto current_x = static_cast<std::int32_t>(std::round(cx + std::cos(angle) * rx));
    const auto current_y = static_cast<std::int32_t>(std::round(cy + std::sin(angle) * ry));
    dirty = unite_rect(dirty, draw_line(document, layer_id, previous_x, previous_y, current_x, current_y, options, erase));
    previous_x = current_x;
    previous_y = current_y;
  }
  return dirty;
}

Rect flood_fill(Document& document, LayerId layer_id, std::int32_t x, std::int32_t y, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr || !canvas_rect(document).contains(x, y) || !selection_allows(options, x, y)) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto local_start_x = x - bounds.x;
  const auto local_start_y = y - bounds.y;
  if (local_start_x < 0 || local_start_y < 0 || local_start_x >= pixels.width() || local_start_y >= pixels.height()) {
    return {};
  }

  const auto channels = pixels.format().channels;
  std::vector<std::uint8_t> target(channels);
  const auto* start_pixel = pixels.pixel(local_start_x, local_start_y);
  for (std::uint16_t channel = 0; channel < channels; ++channel) {
    target[channel] = start_pixel[channel];
  }
  if (options.lock_transparent_pixels && channels >= 4 && target[3] == 0) {
    return {};
  }

  std::vector<std::uint8_t> replacement{options.primary.r, options.primary.g, options.primary.b};
  if (channels >= 4) {
    replacement.push_back(options.lock_transparent_pixels ? target[3] : std::max<std::uint8_t>(1, options.primary.a));
  }
  if (same_pixel(replacement.data(), target, channels)) {
    return {};
  }

  std::queue<std::pair<std::int32_t, std::int32_t>> queue;
  queue.emplace(local_start_x, local_start_y);
  Rect dirty;
  while (!queue.empty()) {
    const auto [local_x, local_y] = queue.front();
    queue.pop();
    if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
      continue;
    }

    const auto doc_x = local_x + bounds.x;
    const auto doc_y = local_y + bounds.y;
    if (!selection_allows(options, doc_x, doc_y)) {
      continue;
    }

    auto* px = pixels.pixel(local_x, local_y);
    if (!same_pixel(px, target, channels)) {
      continue;
    }

    for (std::uint16_t channel = 0; channel < channels; ++channel) {
      px[channel] = replacement[channel];
    }
    dirty = unite_rect(dirty, Rect{doc_x, doc_y, 1, 1});
    queue.emplace(local_x + 1, local_y);
    queue.emplace(local_x - 1, local_y);
    queue.emplace(local_x, local_y + 1);
    queue.emplace(local_x, local_y - 1);
  }
  return dirty;
}

Rect fill_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto affected = intersect_rect(normalized_rect(rect), canvas_rect(document));
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (!options.lock_transparent_pixels) {
    expand_layer_to_include_rect(*layer, affected);
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  affected = intersect_rect(affected, bounds);
  if (affected.empty()) {
    return {};
  }

  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      const auto selected_coverage = selection_coverage(options, x, y);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      write_pixel(pixels, px, options, false, selected_coverage);
    }
  }
  return affected;
}

Rect clear_rect(Document& document, LayerId layer_id, Rect rect, const EditOptions& options) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }
  ensure_alpha_for_erase(*layer);

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  auto affected = intersect_rect(intersect_rect(normalized_rect(rect), canvas_rect(document)), bounds);
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (affected.empty()) {
    return {};
  }

  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      const auto selected_coverage = selection_coverage(options, x, y);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      write_pixel(pixels, px, options, true, selected_coverage);
    }
  }
  return affected;
}

Rect draw_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                   std::int32_t y1, const EditOptions& options, const GradientOptions& gradient) {
  auto* layer = editable_layer(document, layer_id);
  if (layer == nullptr) {
    return {};
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  auto affected = intersect_rect(bounds, canvas_rect(document));
  if (options.selection.has_value()) {
    affected = intersect_rect(affected, *options.selection);
  }
  if (affected.empty()) {
    return {};
  }

  const auto dx = static_cast<double>(x1 - x0);
  const auto dy = static_cast<double>(y1 - y0);
  const auto length_squared = dx * dx + dy * dy;
  const auto radius = std::sqrt(length_squared);
  auto stops = normalized_gradient_stops(gradient.stops.empty()
                                             ? std::vector<GradientStop>{
                                                   GradientStop{0.0F, options.primary},
                                                   GradientStop{1.0F, options.secondary}}
                                             : gradient.stops);
  auto gradient_options = options;
  for (std::int32_t y = affected.y; y < affected.y + affected.height; ++y) {
    auto row = pixels.row(y - bounds.y);
    for (std::int32_t x = affected.x; x < affected.x + affected.width; ++x) {
      const auto selected_coverage = selection_coverage(options, x, y);
      if (selected_coverage <= 0.0F) {
        continue;
      }
      double t = 0.0;
      switch (gradient.method) {
        case GradientMethod::Radial:
          t = radius <= 0.0 ? 0.0
                            : std::sqrt(static_cast<double>(x - x0) * static_cast<double>(x - x0) +
                                        static_cast<double>(y - y0) * static_cast<double>(y - y0)) /
                                  radius;
          break;
        case GradientMethod::Linear:
          t = length_squared <= 0.0
                  ? 0.0
                  : (((static_cast<double>(x - x0) * dx) + (static_cast<double>(y - y0) * dy)) / length_squared);
          break;
      }
      const auto color = gradient_color_at(stops, gradient.opacity, gradient.reverse, t);
      if (color.a == 0) {
        continue;
      }
      gradient_options.primary = color;
      auto* px = row.data() + static_cast<std::size_t>(x - bounds.x) * channels;
      write_pixel(pixels, px, gradient_options, false, selected_coverage);
    }
  }
  return affected;
}

Rect draw_linear_gradient(Document& document, LayerId layer_id, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1, const EditOptions& options) {
  GradientOptions gradient;
  gradient.method = GradientMethod::Linear;
  gradient.opacity = 1.0F;
  gradient.stops.push_back(GradientStop{0.0F, options.primary});
  gradient.stops.push_back(GradientStop{1.0F, options.secondary});
  return draw_gradient(document, layer_id, x0, y0, x1, y1, options, gradient);
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
  document.resize_canvas(width, height);
}

void resize_canvas_and_layers(Document& document, std::int32_t width, std::int32_t height, CanvasAnchor anchor,
                              EditColor extension_color) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const auto offset = canvas_resize_offset(anchor, document.width(), document.height(), width, height);
  document.resize_canvas(width, height);
  for (auto& layer : document.layers()) {
    resize_layer_to_canvas(layer, width, height, offset, extension_color);
  }
}

bool crop_document(Document& document, Rect crop) {
  crop = intersect_rect(crop, canvas_rect(document));
  if (crop.empty()) {
    return false;
  }

  for (auto& layer : document.layers()) {
    crop_layer_to_rect(layer, crop);
  }
  document.resize_canvas(crop.width, crop.height);
  return true;
}

void rotate_document_clockwise(Document& document) {
  const auto old_width = document.width();
  const auto old_height = document.height();
  for (auto& layer : document.layers()) {
    rotate_layer_clockwise(layer, old_height);
  }
  document.resize_canvas(old_height, old_width);
}

void rotate_document_counterclockwise(Document& document) {
  const auto old_width = document.width();
  const auto old_height = document.height();
  for (auto& layer : document.layers()) {
    rotate_layer_counterclockwise(layer, old_width);
  }
  document.resize_canvas(old_height, old_width);
}

}  // namespace patchy
