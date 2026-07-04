#include "core/brush_tip.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

[[nodiscard]] BrushTip halve_brush_tip(const BrushTip& source) {
  BrushTip half;
  half.default_spacing = source.default_spacing;
  half.width = std::max(1, (source.width + 1) / 2);
  half.height = std::max(1, (source.height + 1) / 2);
  half.mask.resize(static_cast<std::size_t>(half.width) * static_cast<std::size_t>(half.height));
  for (std::int32_t y = 0; y < half.height; ++y) {
    const auto src_y0 = std::min(y * 2, source.height - 1);
    const auto src_y1 = std::min(y * 2 + 1, source.height - 1);
    for (std::int32_t x = 0; x < half.width; ++x) {
      const auto src_x0 = std::min(x * 2, source.width - 1);
      const auto src_x1 = std::min(x * 2 + 1, source.width - 1);
      const auto sum = static_cast<int>(source.mask[static_cast<std::size_t>(src_y0) * source.width + src_x0]) +
                       static_cast<int>(source.mask[static_cast<std::size_t>(src_y0) * source.width + src_x1]) +
                       static_cast<int>(source.mask[static_cast<std::size_t>(src_y1) * source.width + src_x0]) +
                       static_cast<int>(source.mask[static_cast<std::size_t>(src_y1) * source.width + src_x1]);
      half.mask[static_cast<std::size_t>(y) * half.width + x] = static_cast<std::uint8_t>((sum + 2) / 4);
    }
  }
  return half;
}

}  // namespace

BrushTipMipChain build_brush_tip_mips(BrushTip tip) {
  BrushTipMipChain chain;
  if (tip.empty() ||
      tip.mask.size() != static_cast<std::size_t>(tip.width) * static_cast<std::size_t>(tip.height)) {
    return chain;
  }
  chain.levels.push_back(std::move(tip));
  while (std::max(chain.levels.back().width, chain.levels.back().height) > 2) {
    chain.levels.push_back(halve_brush_tip(chain.levels.back()));
  }
  return chain;
}

ScaledBrushTip make_scaled_brush_tip(const BrushTipMipChain& mips, int target_size) {
  ScaledBrushTip scaled;
  if (mips.empty()) {
    return scaled;
  }

  const auto& source = mips.levels.front();
  target_size = std::max(1, target_size);
  const auto source_max = std::max(source.width, source.height);
  const auto scale = static_cast<double>(target_size) / static_cast<double>(source_max);
  scaled.width = std::max(1, static_cast<std::int32_t>(std::lround(source.width * scale)));
  scaled.height = std::max(1, static_cast<std::int32_t>(std::lround(source.height * scale)));
  scaled.anchor_x = static_cast<double>(scaled.width) / 2.0;
  scaled.anchor_y = static_cast<double>(scaled.height) / 2.0;
  scaled.mask.resize(static_cast<std::size_t>(scaled.width) * static_cast<std::size_t>(scaled.height));

  // Pick the smallest mip that is still at least the target resolution, so bilinear sampling
  // reads a source no more than ~2x the destination in each axis.
  std::size_t level = 0;
  while (level + 1 < mips.levels.size() &&
         std::max(mips.levels[level + 1].width, mips.levels[level + 1].height) >= target_size) {
    ++level;
  }
  const auto& src = mips.levels[level];

  const auto x_ratio = static_cast<double>(src.width) / static_cast<double>(scaled.width);
  const auto y_ratio = static_cast<double>(src.height) / static_cast<double>(scaled.height);
  for (std::int32_t y = 0; y < scaled.height; ++y) {
    const auto sample_y = (static_cast<double>(y) + 0.5) * y_ratio - 0.5;
    const auto y0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_y)), 0, src.height - 1);
    const auto y1 = std::min(y0 + 1, src.height - 1);
    const auto ty = std::clamp(sample_y - static_cast<double>(y0), 0.0, 1.0);
    for (std::int32_t x = 0; x < scaled.width; ++x) {
      const auto sample_x = (static_cast<double>(x) + 0.5) * x_ratio - 0.5;
      const auto x0 = std::clamp(static_cast<std::int32_t>(std::floor(sample_x)), 0, src.width - 1);
      const auto x1 = std::min(x0 + 1, src.width - 1);
      const auto tx = std::clamp(sample_x - static_cast<double>(x0), 0.0, 1.0);
      const auto top =
          static_cast<double>(src.mask[static_cast<std::size_t>(y0) * src.width + x0]) * (1.0 - tx) +
          static_cast<double>(src.mask[static_cast<std::size_t>(y0) * src.width + x1]) * tx;
      const auto bottom =
          static_cast<double>(src.mask[static_cast<std::size_t>(y1) * src.width + x0]) * (1.0 - tx) +
          static_cast<double>(src.mask[static_cast<std::size_t>(y1) * src.width + x1]) * tx;
      scaled.mask[static_cast<std::size_t>(y) * scaled.width + x] =
          static_cast<std::uint8_t>(std::clamp(std::lround(top * (1.0 - ty) + bottom * ty), 0L, 255L));
    }
  }
  return scaled;
}

namespace {

// One separable box-blur pass with the given radius, in place. Values are averaged over the
// clamped window; the caller has already padded the buffer so nothing is lost at the borders.
void box_blur_pass(std::vector<std::uint8_t>& mask, std::int32_t width, std::int32_t height,
                   std::int32_t radius) {
  if (radius <= 0) {
    return;
  }
  std::vector<std::uint8_t> scratch(mask.size());
  // Horizontal.
  for (std::int32_t y = 0; y < height; ++y) {
    const auto* row = mask.data() + static_cast<std::size_t>(y) * width;
    auto* out = scratch.data() + static_cast<std::size_t>(y) * width;
    std::int64_t sum = 0;
    for (std::int32_t x = -radius; x <= radius; ++x) {
      sum += row[std::clamp(x, 0, width - 1)];
    }
    const auto window = 2 * radius + 1;
    for (std::int32_t x = 0; x < width; ++x) {
      out[x] = static_cast<std::uint8_t>((sum + window / 2) / window);
      sum += row[std::clamp(x + radius + 1, 0, width - 1)];
      sum -= row[std::clamp(x - radius, 0, width - 1)];
    }
  }
  // Vertical.
  for (std::int32_t x = 0; x < width; ++x) {
    std::int64_t sum = 0;
    for (std::int32_t y = -radius; y <= radius; ++y) {
      sum += scratch[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * width + x];
    }
    const auto window = 2 * radius + 1;
    for (std::int32_t y = 0; y < height; ++y) {
      mask[static_cast<std::size_t>(y) * width + x] =
          static_cast<std::uint8_t>((sum + window / 2) / window);
      sum += scratch[static_cast<std::size_t>(std::clamp(y + radius + 1, 0, height - 1)) * width + x];
      sum -= scratch[static_cast<std::size_t>(std::clamp(y - radius, 0, height - 1)) * width + x];
    }
  }
}

}  // namespace

void soften_scaled_brush_tip(ScaledBrushTip& tip, int feather_pixels) {
  if (tip.empty() || feather_pixels <= 0) {
    return;
  }
  const auto pad = feather_pixels;
  const auto padded_width = tip.width + 2 * pad;
  const auto padded_height = tip.height + 2 * pad;
  std::vector<std::uint8_t> padded(static_cast<std::size_t>(padded_width) * padded_height, 0);
  for (std::int32_t y = 0; y < tip.height; ++y) {
    std::copy_n(tip.mask.data() + static_cast<std::size_t>(y) * tip.width, tip.width,
                padded.data() + static_cast<std::size_t>(y + pad) * padded_width + pad);
  }
  const auto pass_radius = std::max(1, feather_pixels / 3);
  for (int pass = 0; pass < 3; ++pass) {
    box_blur_pass(padded, padded_width, padded_height, pass_radius);
  }
  tip.width = padded_width;
  tip.height = padded_height;
  tip.anchor_x += static_cast<double>(pad);
  tip.anchor_y += static_cast<double>(pad);
  tip.mask = std::move(padded);
}

}  // namespace patchy
