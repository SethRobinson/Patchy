#include "formats/document_flatten.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "core/pixel_depth.hpp"
#include "render/compositor.hpp"
#include "render/composite_targets.hpp"
#include "render/native_cmyk.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace patchy {

// 16 and 32-bit documents: the deep flatten narrowed to display RGBA8 (16-bit values to
// 8, 32-bit linear values sRGB-encoded), the values flatten_rgb8 and the canvas show. The
// 8-bit walk refuses deep layers, so every caller (previews, script flatten, writers)
// would otherwise throw.
[[nodiscard]] PixelBuffer deep_flatten_display_rgba8(const Document& document) {
  return convert_pixel_buffer_depth(Compositor{}.flatten_rgba_deep(document), BitDepth::UInt8, SampleKind::Color);
}

PixelBuffer flatten_document_rgba8(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Cannot flatten an empty document"));
  }
  if (document.color_state().bit_depth != BitDepth::UInt8) {
    return deep_flatten_display_rgba8(document);
  }
  if (auto native = render_native_cmyk8(document, Rect::from_size(document.width(), document.height()))) {
    return std::move(*native);
  }
  if (!render_detail::layers_have_rendered_blend_if(document.layers())) {
    if (auto masked = document_alpha_rgba8(document); masked.has_value()) {
      return std::move(*masked);
    }
  }
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgba8());
  output.clear(0);
  Rgba8PixelBufferTarget target(output);
  const auto canvas = Rect::from_size(document.width(), document.height());
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  return output;
}

PixelBuffer flatten_document_rgba8_rect(const Document& document, Rect rect) {
  const auto canvas = Rect::from_size(document.width(), document.height());
  rect = intersect_rect(rect, canvas);
  if (rect.empty()) {
    return {};
  }
  if (rect == canvas) {
    return flatten_document_rgba8(document);
  }
  if (document.color_state().bit_depth != BitDepth::UInt8) {
    const auto full = deep_flatten_display_rgba8(document);
    PixelBuffer cropped(rect.width, rect.height, PixelFormat::rgba8());
    for (std::int32_t y = 0; y < rect.height; ++y) {
      std::memcpy(cropped.row(y).data(), full.row(rect.y + y).data() + static_cast<std::size_t>(rect.x) * 4U,
                  static_cast<std::size_t>(rect.width) * 4U);
    }
    return cropped;
  }
  PixelBuffer output(rect.width, rect.height, PixelFormat::rgba8());
  output.clear(0);
  Rgba8PixelBufferTarget target(output, rect.x, rect.y);
  render_detail::composite_layers(target, document.layers(), rect, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  return output;
}

IndexedFlattenResult indexed_flatten_for_palette_mode(const Document& document) {
  if (!document.palette_editing().has_value() || document.palette_editing()->palette.colors.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Document is not in palette mode"));
  }
  const auto& editing = *document.palette_editing();
  return indexed_rgba8_with_palette(flatten_document_rgba8(document), editing.palette.colors,
                                    editing.alpha_threshold);
}

IndexedFlattenResult indexed_rgba8_with_palette(const PixelBuffer& rgba, std::span<const RgbColor> colors,
                                                std::uint8_t alpha_threshold) {
  if (colors.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Indexed mapping needs a palette"));
  }
  PaletteLut lut;
  lut.build(colors);
  std::unordered_map<std::uint32_t, int> exact_index;
  exact_index.reserve(colors.size());
  for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
    exact_index.emplace(palette_color_key(colors[static_cast<std::size_t>(index)]), index);
  }

  IndexedFlattenResult result;
  result.width = rgba.width();
  result.height = rgba.height();
  result.palette.assign(colors.begin(), colors.end());

  const auto threshold = alpha_threshold;
  bool needs_transparent = false;
  for (std::int32_t y = 0; y < rgba.height() && !needs_transparent; ++y) {
    for (std::int32_t x = 0; x < rgba.width(); ++x) {
      if (rgba.pixel(x, y)[3] < threshold) {
        needs_transparent = true;
        break;
      }
    }
  }
  if (needs_transparent && result.palette.size() < 256) {
    result.transparent_index = static_cast<int>(result.palette.size());
    result.palette.push_back(RgbColor{0, 0, 0});
  }

  result.indexes.resize(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height));
  for (std::int32_t y = 0; y < rgba.height(); ++y) {
    for (std::int32_t x = 0; x < rgba.width(); ++x) {
      const auto* px = rgba.pixel(x, y);
      auto& out =
          result.indexes[static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width) +
                         static_cast<std::size_t>(x)];
      if (result.transparent_index >= 0 && px[3] < threshold) {
        out = static_cast<std::uint8_t>(result.transparent_index);
        continue;
      }
      const auto color = RgbColor{px[0], px[1], px[2]};
      const auto found = exact_index.find(palette_color_key(color));
      out = found != exact_index.end() ? static_cast<std::uint8_t>(found->second)
                                       : static_cast<std::uint8_t>(lut.index_for(px[0], px[1], px[2]));
    }
  }
  return result;
}

IndexedFlattenResult indexed_flatten_quantized(const Document& document, std::size_t max_colors,
                                               std::uint8_t alpha_threshold) {
  return indexed_rgba8_quantized(flatten_document_rgba8(document), max_colors, alpha_threshold);
}

IndexedFlattenResult indexed_flatten_quantized(const Document& document, std::uint8_t alpha_threshold,
                                               const QuantizeOptions& options) {
  return indexed_rgba8_quantized(flatten_document_rgba8(document), alpha_threshold, options);
}

IndexedFlattenResult indexed_rgba8_quantized(const PixelBuffer& rgba, std::size_t max_colors,
                                             std::uint8_t alpha_threshold) {
  return indexed_rgba8_quantized(rgba, alpha_threshold, QuantizeOptions{max_colors, false, 100});
}

IndexedFlattenResult indexed_rgba8_quantized(const PixelBuffer& source, std::uint8_t alpha_threshold,
                                             const QuantizeOptions& options) {
  const auto max_colors = options.max_colors;
  const auto& rgba = source;
  bool has_transparency = false;
  for (std::int32_t y = 0; y < rgba.height() && !has_transparency; ++y) {
    for (std::int32_t x = 0; x < rgba.width(); ++x) {
      if (rgba.pixel(x, y)[3] < alpha_threshold) {
        has_transparency = true;
        break;
      }
    }
  }

  auto palette =
      quantize_to_palette(rgba, has_transparency ? std::max<std::size_t>(1, max_colors - 1) : max_colors,
                          alpha_threshold);
  if (palette.colors.empty()) {
    palette.colors.push_back(RgbColor{0, 0, 0});
  }
  PaletteLut lut;
  lut.build(palette.colors);
  // Diffusion rewrites a copy to palette colors first (deterministic integer error); the
  // exact lookup below then finds every visible pixel.
  std::optional<PixelBuffer> diffused;
  if (options.diffuse && options.diffusion_amount > 0) {
    diffused.emplace(rgba);
    (void)diffused->data();
    (void)apply_palette_to_pixels(*diffused, lut, PaletteDither::FloydSteinberg, alpha_threshold,
                                  options.diffusion_amount);
  }
  const auto& mapped = diffused.has_value() ? *diffused : rgba;
  std::unordered_map<std::uint32_t, int> exact_index;
  exact_index.reserve(palette.colors.size());
  for (int index = 0; index < static_cast<int>(palette.colors.size()); ++index) {
    exact_index.emplace(palette_color_key(palette.colors[static_cast<std::size_t>(index)]), index);
  }

  IndexedFlattenResult result;
  result.width = rgba.width();
  result.height = rgba.height();
  result.palette = std::move(palette.colors);
  if (has_transparency) {
    result.transparent_index = static_cast<int>(result.palette.size());
    result.palette.push_back(RgbColor{0, 0, 0});
  }

  result.indexes.resize(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height));
  for (std::int32_t y = 0; y < mapped.height(); ++y) {
    for (std::int32_t x = 0; x < mapped.width(); ++x) {
      const auto* px = mapped.pixel(x, y);
      auto& out = result.indexes[static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width) +
                                 static_cast<std::size_t>(x)];
      if (result.transparent_index >= 0 && px[3] < alpha_threshold) {
        out = static_cast<std::uint8_t>(result.transparent_index);
        continue;
      }
      const auto found = exact_index.find(palette_color_key(RgbColor{px[0], px[1], px[2]}));
      out = found != exact_index.end() ? static_cast<std::uint8_t>(found->second)
                                       : static_cast<std::uint8_t>(lut.index_for(px[0], px[1], px[2]));
    }
  }
  return result;
}

}  // namespace patchy
