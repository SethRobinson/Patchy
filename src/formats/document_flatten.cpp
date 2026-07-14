#include "formats/document_flatten.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_render_utils.hpp"
#include "core/palette.hpp"
#include "render/layer_compositor.hpp"

#include <array>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace patchy {

namespace {

// Same straight-alpha RGBA8 compositor target the BMP writer uses (stateless; kept local
// to each formats TU rather than promoted into render/).
class Rgba8FlattenTarget {
public:
  explicit Rgba8FlattenTarget(PixelBuffer& destination) : destination_(destination) {}

  void composite_color(std::int32_t x, std::int32_t y, RgbColor color, float alpha, BlendMode mode) {
    alpha = clamp_unit(alpha);
    if (alpha <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    const auto destination_alpha = static_cast<float>(dst[3]) / 255.0F;
    const std::array<std::uint8_t, 3> source_rgb{color.red, color.green, color.blue};
    const std::array<std::uint8_t, 3> destination_rgb{dst[0], dst[1], dst[2]};
    const auto blended = composite_blended_rgb(source_rgb, destination_rgb, mode, alpha, destination_alpha);
    dst[0] = blended[0];
    dst[1] = blended[1];
    dst[2] = blended[2];
    dst[3] = clamp_byte((alpha + destination_alpha * (1.0F - alpha)) * 255.0F);
  }

  void composite_special_fill_color(std::int32_t x, std::int32_t y, RgbColor color,
                                    float source_coverage, float fill_opacity, float layer_opacity,
                                    BlendMode mode) {
    if (source_coverage <= 0.0F || fill_opacity <= 0.0F || layer_opacity <= 0.0F || x < 0 || y < 0 ||
        x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    const auto result = composite_special_fill_rgb(
        {color.red, color.green, color.blue}, {dst[0], dst[1], dst[2]}, mode, source_coverage,
        fill_opacity, layer_opacity, static_cast<float>(dst[3]) / 255.0F);
    dst[0] = result.color[0]; dst[1] = result.color[1]; dst[2] = result.color[2];
    dst[3] = clamp_byte(result.alpha * 255.0F);
  }

  [[nodiscard]] render_detail::CompositeSample sample_color(std::int32_t x, std::int32_t y) const noexcept {
    if (x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return {};
    }
    const auto* pixel = destination_.pixel(x, y);
    return render_detail::CompositeSample{RgbColor{pixel[0], pixel[1], pixel[2]},
                                          static_cast<float>(pixel[3]) / 255.0F};
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentSettings& settings, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    const auto adjusted = apply_adjustment_to_color(RgbColor{dst[0], dst[1], dst[2]}, settings);
    dst[0] = clamp_byte(static_cast<float>(adjusted.red) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] = clamp_byte(static_cast<float>(adjusted.green) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(adjusted.blue) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

  void adjust_color(std::int32_t x, std::int32_t y, const AdjustmentLut& lut, float amount) {
    amount = clamp_unit(amount);
    if (amount <= 0.0F || x < 0 || y < 0 || x >= destination_.width() || y >= destination_.height()) {
      return;
    }
    auto* dst = destination_.pixel(x, y);
    if (dst[3] == 0) {
      return;
    }
    dst[0] = clamp_byte(static_cast<float>(lut.red[dst[0]]) * amount + static_cast<float>(dst[0]) * (1.0F - amount));
    dst[1] =
        clamp_byte(static_cast<float>(lut.green[dst[1]]) * amount + static_cast<float>(dst[1]) * (1.0F - amount));
    dst[2] = clamp_byte(static_cast<float>(lut.blue[dst[2]]) * amount + static_cast<float>(dst[2]) * (1.0F - amount));
  }

private:
  PixelBuffer& destination_;
};

}  // namespace

PixelBuffer flatten_document_rgba8(const Document& document) {
  if (document.width() <= 0 || document.height() <= 0) {
    throw std::runtime_error("Cannot flatten an empty document");
  }
  if (!render_detail::layers_have_rendered_blend_if(document.layers())) {
    if (auto masked = document_alpha_rgba8(document); masked.has_value()) {
      return std::move(*masked);
    }
  }
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgba8());
  output.clear(0);
  Rgba8FlattenTarget target(output);
  const auto canvas = Rect::from_size(document.width(), document.height());
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  return output;
}

IndexedFlattenResult indexed_flatten_for_palette_mode(const Document& document) {
  if (!document.palette_editing().has_value() || document.palette_editing()->palette.colors.empty()) {
    throw std::runtime_error("Document is not in palette mode");
  }
  const auto& editing = *document.palette_editing();
  const auto& colors = editing.palette.colors;
  const auto flattened = flatten_document_rgba8(document);

  PaletteLut lut;
  lut.build(colors);
  std::unordered_map<std::uint32_t, int> exact_index;
  exact_index.reserve(colors.size());
  for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
    exact_index.emplace(palette_color_key(colors[static_cast<std::size_t>(index)]), index);
  }

  IndexedFlattenResult result;
  result.width = flattened.width();
  result.height = flattened.height();
  result.palette = colors;

  const auto threshold = editing.alpha_threshold;
  bool needs_transparent = false;
  for (std::int32_t y = 0; y < flattened.height() && !needs_transparent; ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      if (flattened.pixel(x, y)[3] < threshold) {
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
  for (std::int32_t y = 0; y < flattened.height(); ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      const auto* px = flattened.pixel(x, y);
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
  const auto flattened = flatten_document_rgba8(document);
  bool has_transparency = false;
  for (std::int32_t y = 0; y < flattened.height() && !has_transparency; ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      if (flattened.pixel(x, y)[3] < alpha_threshold) {
        has_transparency = true;
        break;
      }
    }
  }

  auto palette =
      quantize_to_palette(flattened, has_transparency ? std::max<std::size_t>(1, max_colors - 1) : max_colors,
                          alpha_threshold);
  if (palette.colors.empty()) {
    palette.colors.push_back(RgbColor{0, 0, 0});
  }
  PaletteLut lut;
  lut.build(palette.colors);
  std::unordered_map<std::uint32_t, int> exact_index;
  exact_index.reserve(palette.colors.size());
  for (int index = 0; index < static_cast<int>(palette.colors.size()); ++index) {
    exact_index.emplace(palette_color_key(palette.colors[static_cast<std::size_t>(index)]), index);
  }

  IndexedFlattenResult result;
  result.width = flattened.width();
  result.height = flattened.height();
  result.palette = std::move(palette.colors);
  if (has_transparency) {
    result.transparent_index = static_cast<int>(result.palette.size());
    result.palette.push_back(RgbColor{0, 0, 0});
  }

  result.indexes.resize(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height));
  for (std::int32_t y = 0; y < flattened.height(); ++y) {
    for (std::int32_t x = 0; x < flattened.width(); ++x) {
      const auto* px = flattened.pixel(x, y);
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
