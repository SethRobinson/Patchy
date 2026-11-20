#include "render/native_adjustment_stack.hpp"

#include "color/color_management.hpp"
#include "core/layer_metadata.hpp"
#include "core/psd_source_colors.hpp"
#include "core/smart_object.hpp"
#include "render/layer_compositor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace patchy {

const PsdNativeLayerColors* render_detail::retained_adjustment_base(const Document& document,
                                                                  std::size_t components) {
  const auto& layers = document.layers();
  const auto& space = document.metadata().psd_native_color_space;
  if (!space || layers.size() < 2 || (space->depth != BitDepth::UInt8 && space->depth != BitDepth::UInt16) ||
      space->depth != document.color_state().bit_depth || !document.color_state().embedded_icc_profile.empty())
    return nullptr;
  const auto& base = layers.front();
  const auto& native = base.psd_native_colors();
  const auto& pixels = base.pixels();
  const auto canvas = Rect::from_size(document.width(), document.height());
  if (!native || native->space != space || !native->imported || !base.visible() || base.kind() != LayerKind::Pixel ||
      base.vector_shape() || layer_is_text(base) || layer_is_smart_object(base) || base.mask() || base.vector_mask() ||
      base.blend_mode() != BlendMode::Normal || base.opacity() != 1.0F || base.fill_opacity() != 1.0F ||
      base.bounds() != canvas || pixels.format() != PixelFormat{ColorMode::RGB, space->depth, 3} ||
      pixels.width() != canvas.width || pixels.height() != canvas.height || native->width != canvas.width ||
      native->height != canvas.height || components > native->planes.size()) return nullptr;
  const auto plane_bytes = static_cast<std::size_t>(canvas.width) * canvas.height * bytes_per_channel(space->depth);
  const auto& original = native->imported->pixels();
  if (pixels.format() != original.format() || pixels.data().data() != original.data().data() ||
      !std::all_of(native->planes.begin(), native->planes.begin() + static_cast<std::ptrdiff_t>(components),
                   [plane_bytes](const auto& plane) { return plane.size() == plane_bytes; })) return nullptr;
  return native.get();
}

namespace {
// Photoshop processes deep Posterize on a 0..32768 lattice. Exporting the
// resulting planes to 16-bit Lab TIFF pins the bucket boundaries and output bias.
std::uint16_t posterized_lab_sample(std::uint16_t value, int levels, bool deep) {
  if (!deep) return posterize_channel_value(static_cast<std::uint8_t>(value), levels);
  const int v15 = (value + 1) / 2;
  const int bucket = std::min(v15 * levels / 32768, levels - 1);
  const auto level = static_cast<unsigned>(bucket * 32768 / (levels - 1));
  return static_cast<std::uint16_t>((level * 65535U + 16384U) / 32768U);
}

struct LabAdjustment {
  const Layer& layer;
  Rect bounds;
  float opacity;
  int posterize_levels{0};
  int threshold_sample{0};
  bool lightness_only{false};
  std::optional<AdjustmentLut> byte_channels;
  std::optional<DeepAdjuster> deep_channels;

  std::array<std::uint16_t, 3> apply(const std::uint16_t* sample, bool deep) const {
    std::array<std::uint16_t, 3> mapped{};
    if (threshold_sample != 0) {
      const auto lightness = static_cast<std::uint16_t>(sample[0] >= threshold_sample ? (deep ? 65535 : 255) : 0);
      const auto neutral = static_cast<std::uint16_t>(deep ? 32768 : 128);
      return {lightness, neutral, neutral};
    }
    if (byte_channels) {
      mapped = {byte_channels->red[sample[0]], byte_channels->green[sample[1]], byte_channels->blue[sample[2]]};
    } else if (deep_channels) {
      const auto out = deep_channels->apply({sample[0] / 257.0F, sample[1] / 257.0F, sample[2] / 257.0F});
      for (std::size_t c = 0; c < mapped.size(); ++c)
        mapped[c] = static_cast<std::uint16_t>(std::lround(out[c] * 257.0F));
    } else {
      for (std::size_t c = 0; c < mapped.size(); ++c)
        mapped[c] = posterize_levels ? posterized_lab_sample(sample[c], posterize_levels, deep) :
            static_cast<std::uint16_t>((deep ? 65535 : 255) - sample[c]);
    }
    if (lightness_only) {
      mapped[1] = sample[1];
      mapped[2] = sample[2];
    }
    return mapped;
  }
};
}

std::optional<PixelBuffer> render_native_lab_adjustments(
    const Document& document, Rect clip, BitDepth depth,
    const std::vector<render_detail::LayerBoundsOverride>* overrides, LabRenderOutput output) {
  const auto& space = document.metadata().psd_native_color_space;
  if (!space || space->mode != 9 || space->depth != depth || render_detail::raster_view_context ||
      (overrides && !overrides->empty())) return std::nullopt;
  const auto* base = render_detail::retained_adjustment_base(document, 3);
  if (!base) return std::nullopt;
  const auto canvas = Rect::from_size(document.width(), document.height());
  const bool deep = depth == BitDepth::UInt16;
  std::vector<LabAdjustment> adjustments;
  for (const auto& layer : document.layers()) {
    if (!layer.visible() || layer.opacity() <= 0.0F) continue;
    if (layer.clipped() || layer.restricted_channels() != 0 || !layer.layer_style().empty() ||
        !layer.layer_style().satins.empty() || render_detail::layer_knockout_mode(layer) != 0 ||
        render_detail::layer_has_rendered_blend_if(layer)) return std::nullopt;
    if (&layer == &document.layers().front()) continue;
    const auto& source = layer.psd_native_colors();
    if (layer.kind() != LayerKind::Adjustment || layer.blend_mode() != BlendMode::Normal ||
        !source || source->space != space) return std::nullopt;
    auto settings = adjustment_settings_from_layer(layer);
    if (!settings) return std::nullopt;
    LabAdjustment prepared{layer, layer.bounds().empty() ? canvas : layer.bounds(),
        layer.opacity() * render_detail::layer_fill_opacity_for_render(layer), 0, 0, false, {}, {}};
    if (settings->kind == AdjustmentKind::Levels || settings->kind == AdjustmentKind::Curves ||
        settings->kind == AdjustmentKind::BrightnessContrast) {
      // Lab has three independent channels. Photoshop ignores record zero even
      // when its curve or Levels values are nonidentity; slots 1, 2, 3 are L*, a*, b*.
      set_levels_master_record(settings->levels, {});
      settings->curves.rgb = {{0, 0}, {255, 255}};
      prepared.lightness_only = settings->kind == AdjustmentKind::BrightnessContrast &&
                                source->brightness_contrast_lightness_only;
      if (deep) prepared.deep_channels.emplace(*settings, DeepDomain::Encoded);
      else prepared.byte_channels = build_adjustment_lut(*settings);
    } else if (settings->kind == AdjustmentKind::Threshold) {
      const int level = settings->threshold.level;
      // The first white sample on a full 16-bit L* ramp is the lower wire
      // code that rounds to the requested level in Photoshop's 0..32768 range.
      prepared.threshold_sample = deep ? 2 * ((level * 32768 + 127) / 255) - 1 : level;
    } else if (settings->kind == AdjustmentKind::Posterize) {
      prepared.posterize_levels = settings->posterize.levels;
    } else if (settings->kind != AdjustmentKind::Invert) return std::nullopt;
    adjustments.push_back(std::move(prepared));
  }
  if (adjustments.empty()) return std::nullopt;
  clip = intersect_rect(canvas, clip);
  if (clip.empty()) return PixelBuffer{};
  const bool native = output == LabRenderOutput::NativeLab;
  const auto channels = static_cast<std::uint8_t>(native ? 3 : 4);
  PixelBuffer result(clip.width, clip.height, {native ? ColorMode::Lab : ColorMode::RGB, depth, channels});
  const auto width = static_cast<std::size_t>(clip.width);
  std::vector<std::uint16_t> lab_row(width * 3U), rgb_row(width * 3U);
  static const auto transform = LabToRgbTransform::create();
  if (!native && !transform) return std::nullopt;
  for (int y = 0; y < clip.height; ++y) {
    const int dy = clip.y + y;
    for (int x = 0; x < clip.width; ++x) {
      const int dx = clip.x + x;
      const auto offset = (static_cast<std::size_t>(dy) * canvas.width + dx) * (deep ? 2U : 1U);
      auto* sample = lab_row.data() + static_cast<std::size_t>(x) * 3U;
      for (std::size_t c = 0; c < 3; ++c) {
        const auto& plane = base->planes[c];
        sample[c] = deep ? static_cast<std::uint16_t>((plane[offset] << 8U) | plane[offset + 1U]) : plane[offset];
      }
      for (const auto& adjustment : adjustments) {
        if (!adjustment.bounds.contains(dx, dy)) continue;
        const auto amount = adjustment.opacity * layer_mask_alpha_at(adjustment.layer, dx, dy);
        const auto mapped = adjustment.apply(sample, deep);
        for (std::size_t c = 0; c < 3; ++c) {
          sample[c] = static_cast<std::uint16_t>(std::lround(sample[c] + (mapped[c] - sample[c]) * amount));
        }
      }
      if (!native) {
        // PSD Lab16 chroma has 256 codes per a*/b* unit, including neutral at
        // 0x8000. Little CMS Lab4 uses 257, including neutral at 0x8080.
        if (deep) for (std::size_t c = 1; c < 3; ++c)
          sample[c] = static_cast<std::uint16_t>(std::min(65535U, (sample[c] * 257U + 128U) / 256U));
        if (!deep) for (std::size_t c = 0; c < 3; ++c) sample[c] *= 257U;
      }
    }
    if (!native) transform->convert16(lab_row.data(), rgb_row.data(), width);
    const auto& row = native ? lab_row : rgb_row;
    auto* dest = result.row(y).data();
    for (std::size_t x = 0; x < width; ++x) {
      std::array<std::uint16_t, 4> values{row[x * 3], row[x * 3 + 1], row[x * 3 + 2], 65535};
      if (deep) {
        std::memcpy(dest + x * channels * 2U, values.data(), channels * 2U);
      } else {
        for (std::size_t c = 0; c < channels; ++c) {
          dest[x * channels + c] = static_cast<std::uint8_t>(native ? values[c] : (values[c] + 128U) / 257U);
        }
      }
    }
  }
  return result;
}

}  // namespace patchy
