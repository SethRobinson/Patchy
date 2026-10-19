#include "core/paint_mode.hpp"

#include <algorithm>

namespace patchy {

bool blend_mode_is_paint_mode(BlendMode mode) noexcept {
  return mode != BlendMode::PassThrough;
}

std::array<std::uint8_t, 4> composite_paint_rgba8(std::array<std::uint8_t, 4> original,
                                                  std::array<std::uint8_t, 3> paint, float alpha, PaintMode mode,
                                                  std::int32_t x, std::int32_t y, bool has_alpha,
                                                  bool lock_alpha) noexcept {
  alpha = clamp_unit(alpha);
  auto out = original;
  if (alpha <= 0.0F) {
    return out;
  }
  const bool alpha_fixed = !has_alpha || lock_alpha;
  const auto destination_alpha = static_cast<float>(original[3]) / 255.0F;
  switch (mode.kind) {
    case PaintModeKind::Clear:
      if (!alpha_fixed) {
        out[3] = clamp_byte(static_cast<float>(original[3]) * (1.0F - alpha));
      }
      return out;
    case PaintModeKind::Behind: {
      if (alpha_fixed || original[3] == 255) {
        return out;
      }
      if (original[3] == 0) {
        out = {paint[0], paint[1], paint[2], std::max<std::uint8_t>(1, clamp_byte(alpha * 255.0F))};
        return out;
      }
      const auto out_alpha = destination_alpha + alpha * (1.0F - destination_alpha);
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = clamp_byte((static_cast<float>(original[c]) * destination_alpha +
                             static_cast<float>(paint[c]) * alpha * (1.0F - destination_alpha)) /
                            out_alpha);
      }
      out[3] = clamp_byte(out_alpha * 255.0F);
      return out;
    }
    case PaintModeKind::Blend:
      break;
  }

  auto blend = mode.blend;
  if (blend == BlendMode::Dissolve) {
    alpha = dissolve_coverage(x, y, alpha, DissolveField::Paint);
    if (alpha <= 0.0F) {
      return out;
    }
    blend = BlendMode::Normal;
  }
  const std::array<std::uint8_t, 3> destination{original[0], original[1], original[2]};
  if (alpha_fixed) {
    const auto blended = blend_rgb(paint, destination, blend);
    for (std::size_t c = 0; c < 3U; ++c) {
      out[c] = clamp_byte(static_cast<float>(blended[c]) * alpha + static_cast<float>(original[c]) * (1.0F - alpha));
    }
    return out;
  }
  if (original[3] == 0) {
    out = {paint[0], paint[1], paint[2], std::max<std::uint8_t>(1, clamp_byte(alpha * 255.0F))};
    return out;
  }
  const auto color = composite_blended_rgb(paint, destination, blend, alpha, destination_alpha);
  out = {color[0], color[1], color[2], clamp_byte((alpha + destination_alpha * (1.0F - alpha)) * 255.0F)};
  return out;
}

std::array<float, 4> composite_paint_deep(std::array<float, 4> original, DeepChannels paint, float alpha,
                                          PaintMode mode, std::int32_t x, std::int32_t y, bool has_alpha,
                                          bool lock_alpha, DeepDomain domain) noexcept {
  alpha = clamp_unit(alpha);
  auto out = original;
  if (alpha <= 0.0F) {
    return out;
  }
  const bool alpha_fixed = !has_alpha || lock_alpha;
  const auto destination_alpha = std::clamp(original[3] / 255.0F, 0.0F, 1.0F);
  switch (mode.kind) {
    case PaintModeKind::Clear:
      if (!alpha_fixed) {
        out[3] = original[3] * (1.0F - alpha);
      }
      return out;
    case PaintModeKind::Behind: {
      if (alpha_fixed || destination_alpha >= 1.0F) {
        return out;
      }
      if (destination_alpha <= 0.0F) {
        out = {paint[0], paint[1], paint[2], alpha * 255.0F};
        return out;
      }
      const auto out_alpha = destination_alpha + alpha * (1.0F - destination_alpha);
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] = (original[c] * destination_alpha + paint[c] * alpha * (1.0F - destination_alpha)) / out_alpha;
      }
      out[3] = out_alpha * 255.0F;
      return out;
    }
    case PaintModeKind::Blend:
      break;
  }

  auto blend = mode.blend;
  if (blend == BlendMode::Dissolve) {
    alpha = dissolve_coverage(x, y, alpha, DissolveField::Paint);
    if (alpha <= 0.0F) {
      return out;
    }
    blend = BlendMode::Normal;
  }
  const DeepChannels destination{original[0], original[1], original[2]};
  if (alpha_fixed) {
    const auto blended = blend_rgb_deep(paint, destination, blend, domain);
    for (std::size_t c = 0; c < 3U; ++c) {
      out[c] = blended[c] * alpha + original[c] * (1.0F - alpha);
    }
    return out;
  }
  if (destination_alpha <= 0.0F) {
    out = {paint[0], paint[1], paint[2], alpha * 255.0F};
    return out;
  }
  const auto color = composite_blended_rgb_deep(paint, destination, blend, alpha, destination_alpha, domain);
  out = {color[0], color[1], color[2], (alpha + destination_alpha * (1.0F - alpha)) * 255.0F};
  return out;
}

}  // namespace patchy
