#include "core/floating_selection.hpp"

#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace patchy {

namespace {

PixelFormat rgba_at_depth(BitDepth depth) noexcept {
  return with_bit_depth(PixelFormat::rgba8(), depth);
}

}  // namespace

LiftedSelection lift_selected_pixels(const PixelBuffer& pixels, Rect bounds, const PixelBuffer& coverage,
                                     Rect coverage_bounds,
                                     const std::optional<std::array<std::uint8_t, 3>>& fill) {
  LiftedSelection result;
  if (pixels.empty() || pixels.format().channels < 3) {
    return result;
  }
  const auto depth = pixels.format().bit_depth;
  const auto domain = deep_domain_for(depth);
  // A hole turns transparent, so a layer without alpha gains it; a filled hole
  // keeps the layer's own format.
  result.remainder = fill.has_value() || pixels.format().channels >= 4
                         ? pixels
                         : PixelBuffer(pixels.width(), pixels.height(), rgba_at_depth(depth));
  if (!fill.has_value() && pixels.format().channels < 4) {
    std::vector<float> row(static_cast<std::size_t>(pixels.width()) * 4U);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      load_rgba_row(pixels, y, 0, pixels.width(), domain, row);
      store_rgba_row(result.remainder, y, 0, pixels.width(), domain, row);
    }
  }
  const bool coverage_usable = !coverage.empty() && coverage.format() == PixelFormat::gray8() &&
                               coverage.width() == coverage_bounds.width &&
                               coverage.height() == coverage_bounds.height;
  const auto rect = coverage_usable ? intersect_rect(intersect_rect(coverage_bounds, bounds),
                                                     Rect{bounds.x, bounds.y, pixels.width(), pixels.height()})
                                    : Rect{};
  if (rect.empty()) {
    return result;
  }
  result.floating_bounds = rect;
  result.floating = PixelBuffer(rect.width, rect.height, rgba_at_depth(depth));

  std::array<float, 3> fill_value{};
  if (fill.has_value()) {
    for (std::size_t c = 0; c < 3U; ++c) {
      const auto byte = static_cast<float>((*fill)[c]);
      fill_value[c] = domain == DeepDomain::Linear
                          ? static_cast<float>(srgb_decode(static_cast<double>(byte) / 255.0) * 255.0)
                          : byte;
    }
  }
  std::vector<float> row(static_cast<std::size_t>(rect.width) * 4U);
  std::vector<float> lifted(row.size());
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    load_rgba_row(result.remainder, y - bounds.y, rect.x - bounds.x, rect.width, domain, row);
    const auto selected = coverage.row(y - coverage_bounds.y);
    for (std::int32_t x = 0; x < rect.width; ++x) {
      const auto i = static_cast<std::size_t>(x) * 4U;
      const auto amount =
          static_cast<float>(selected[static_cast<std::size_t>(rect.x - coverage_bounds.x + x)]) / 255.0F;
      lifted[i] = row[i];
      lifted[i + 1U] = row[i + 1U];
      lifted[i + 2U] = row[i + 2U];
      lifted[i + 3U] = row[i + 3U] * amount;
      if (amount <= 0.0F) {
        continue;
      }
      if (fill.has_value()) {
        for (std::size_t c = 0; c < 3U; ++c) {
          row[i + c] = fill_value[c] * amount + row[i + c] * (1.0F - amount);
        }
      } else {
        row[i + 3U] *= 1.0F - amount;
      }
    }
    store_rgba_row(result.floating, y - rect.y, 0, rect.width, domain, lifted);
    store_rgba_row(result.remainder, y - bounds.y, rect.x - bounds.x, rect.width, domain, row);
  }
  return result;
}

PixelBuffer composite_floating_pixels(const PixelBuffer& base, Rect base_bounds, const PixelBuffer& floating,
                                      Rect floating_bounds, Rect output_bounds, bool preserve_alpha) {
  if (base.empty() || output_bounds.empty()) {
    return {};
  }
  const auto depth = base.format().bit_depth;
  const auto domain = deep_domain_for(depth);
  PixelBuffer output(output_bounds.width, output_bounds.height, base.format());
  output.clear(0);
  base_bounds.width = std::min(base_bounds.width, base.width());
  base_bounds.height = std::min(base_bounds.height, base.height());
  const bool floating_usable = !floating.empty() && floating.format().channels >= 3 &&
                               floating.format().bit_depth == depth;
  if (floating_usable) {
    floating_bounds.width = std::min(floating_bounds.width, floating.width());
    floating_bounds.height = std::min(floating_bounds.height, floating.height());
  }
  const auto base_part = intersect_rect(base_bounds, output_bounds);
  const auto floating_part = floating_usable ? intersect_rect(floating_bounds, output_bounds) : Rect{};

  std::vector<float> row(static_cast<std::size_t>(output_bounds.width) * 4U);
  std::vector<float> top(static_cast<std::size_t>(std::max(0, floating_part.width)) * 4U);
  for (std::int32_t y = output_bounds.y; y < output_bounds.y + output_bounds.height; ++y) {
    std::fill(row.begin(), row.end(), 0.0F);
    if (!base_part.empty() && y >= base_part.y && y < base_part.y + base_part.height) {
      load_rgba_row(base, y - base_bounds.y, base_part.x - base_bounds.x, base_part.width, domain,
                    std::span<float>(row).subspan(static_cast<std::size_t>(base_part.x - output_bounds.x) * 4U,
                                                  static_cast<std::size_t>(base_part.width) * 4U));
    }
    if (!floating_part.empty() && y >= floating_part.y && y < floating_part.y + floating_part.height) {
      load_rgba_row(floating, y - floating_bounds.y, floating_part.x - floating_bounds.x, floating_part.width,
                    domain, top);
      for (std::int32_t x = 0; x < floating_part.width; ++x) {
        const auto* source = top.data() + static_cast<std::size_t>(x) * 4U;
        auto* destination = row.data() + static_cast<std::size_t>(floating_part.x - output_bounds.x + x) * 4U;
        const auto source_alpha = std::clamp(source[3] / kDeepScale, 0.0F, 1.0F);
        if (source_alpha <= 0.0F) {
          continue;
        }
        if (preserve_alpha) {
          for (std::size_t c = 0; c < 3U; ++c) {
            destination[c] = source[c] * source_alpha + destination[c] * (1.0F - source_alpha);
          }
          continue;
        }
        const auto destination_alpha = std::clamp(destination[3] / kDeepScale, 0.0F, 1.0F);
        const auto below = destination_alpha * (1.0F - source_alpha);
        const auto alpha = source_alpha + below;
        for (std::size_t c = 0; c < 3U; ++c) {
          destination[c] = (source[c] * source_alpha + destination[c] * below) / alpha;
        }
        destination[3] = alpha * kDeepScale;
      }
    }
    store_rgba_row(output, y - output_bounds.y, 0, output_bounds.width, domain, row);
  }
  return output;
}

}  // namespace patchy
