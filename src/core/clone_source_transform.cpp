#include "core/clone_source_transform.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kQ24 = 16777216.0;

}  // namespace

bool CloneSourceTransform::is_identity() const noexcept {
  const auto value = normalized();
  return value.scale_percent == 100.0 && value.rotation_degrees == 0.0 && !value.flip_horizontal &&
         !value.flip_vertical;
}

CloneSourceTransform CloneSourceTransform::normalized() const noexcept {
  CloneSourceTransform value;
  value.scale_percent = std::isfinite(scale_percent) ? std::clamp(scale_percent, kMinScalePercent, kMaxScalePercent)
                                                     : 100.0;
  auto rotation = std::isfinite(rotation_degrees) ? std::fmod(rotation_degrees, 360.0) : 0.0;
  if (rotation > 180.0) {
    rotation -= 360.0;
  } else if (rotation <= -180.0) {
    rotation += 360.0;
  }
  value.rotation_degrees = rotation == 0.0 ? 0.0 : rotation;  // no -0
  value.flip_horizontal = flip_horizontal;
  value.flip_vertical = flip_vertical;
  return value;
}

CloneSourceMapping::CloneSourceMapping(std::int32_t source_x, std::int32_t source_y, std::int32_t offset_x,
                                       std::int32_t offset_y, const CloneSourceTransform& transform) noexcept
    : source_x_(source_x), source_y_(source_y), offset_x_(offset_x), offset_y_(offset_y) {
  const auto value = transform.normalized();
  identity_ = value.is_identity();
  if (identity_) {
    return;
  }
  // Quarter turns take exact sines and cosines, so 90 degrees at 100% maps whole pixels.
  double c = 0.0;
  double s = 0.0;
  const auto turns = value.rotation_degrees / 90.0;
  if (turns == std::floor(turns)) {
    switch ((static_cast<int>(turns) % 4 + 4) % 4) {
      case 0:
        c = 1.0;
        break;
      case 1:
        s = 1.0;
        break;
      case 2:
        c = -1.0;
        break;
      default:
        s = -1.0;
        break;
    }
  } else {
    const auto radians = value.rotation_degrees * kPi / 180.0;
    c = std::cos(radians);
    s = std::sin(radians);
  }
  // The forward transform mirrors, scales, then turns by the rotation; its inverse is the
  // opposite turn divided by the scale, then the same mirror (a flip is its own inverse),
  // which negates the matrix's row for each flipped axis.
  const auto inverse_scale = 100.0 / value.scale_percent;
  m00_ = std::llround(c * inverse_scale * kQ24);
  m01_ = std::llround(s * inverse_scale * kQ24);
  m10_ = -m01_;
  m11_ = m00_;
  if (value.flip_horizontal) {
    m00_ = -m00_;
    m01_ = -m01_;
  }
  if (value.flip_vertical) {
    m10_ = -m10_;
    m11_ = -m11_;
  }
}

std::array<std::int64_t, 2> CloneSourceMapping::map_q8(std::int32_t x, std::int32_t y) const noexcept {
  if (identity_) {
    return {(static_cast<std::int64_t>(x) + offset_x_) * 256, (static_cast<std::int64_t>(y) + offset_y_) * 256};
  }
  // Distance from the destination anchor (the pixel the source point lands on).
  const auto dx = static_cast<std::int64_t>(x) - (static_cast<std::int64_t>(source_x_) - offset_x_);
  const auto dy = static_cast<std::int64_t>(y) - (static_cast<std::int64_t>(source_y_) - offset_y_);
  // Q24 times whole pixels, down to Q8 by an arithmetic shift (floor; C++20).
  const auto sx = static_cast<std::int64_t>(source_x_) * 256 + ((m00_ * dx + m01_ * dy) >> 16);
  const auto sy = static_cast<std::int64_t>(source_y_) * 256 + ((m10_ * dx + m11_ * dy) >> 16);
  return {sx, sy};
}

std::array<std::int32_t, 2> CloneSourceMapping::map_nearest(std::int32_t x, std::int32_t y) const noexcept {
  const auto q8 = map_q8(x, y);
  return {static_cast<std::int32_t>((q8[0] + 128) >> 8), static_cast<std::int32_t>((q8[1] + 128) >> 8)};
}

std::optional<CloneSourceTaps> clone_source_taps(std::int64_t x_q8, std::int64_t y_q8, std::int32_t width,
                                                 std::int32_t height) noexcept {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  const auto nearest_x = (x_q8 + 128) >> 8;
  const auto nearest_y = (y_q8 + 128) >> 8;
  if (nearest_x < 0 || nearest_y < 0 || nearest_x >= width || nearest_y >= height) {
    return std::nullopt;
  }
  const auto x0 = x_q8 >> 8;
  const auto y0 = y_q8 >> 8;
  const auto clamp_x = [width](std::int64_t value) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, 0, width - 1));
  };
  const auto clamp_y = [height](std::int64_t value) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, 0, height - 1));
  };
  CloneSourceTaps taps;
  taps.x0 = clamp_x(x0);
  taps.x1 = clamp_x(x0 + 1);
  taps.y0 = clamp_y(y0);
  taps.y1 = clamp_y(y0 + 1);
  taps.wx = static_cast<std::uint32_t>(x_q8 & 255);
  taps.wy = static_cast<std::uint32_t>(y_q8 & 255);
  return taps;
}

std::array<std::uint8_t, 4> sample_clone_source_rgba8(const std::uint8_t* pixels, std::size_t stride,
                                                      const CloneSourceTaps& taps) noexcept {
  const auto at = [pixels, stride](std::int32_t x, std::int32_t y) {
    return pixels + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4U;
  };
  const std::array<const std::uint8_t*, 4> corners = {at(taps.x0, taps.y0), at(taps.x1, taps.y0), at(taps.x0, taps.y1),
                                                      at(taps.x1, taps.y1)};
  const std::array<std::uint64_t, 4> weights = {
      static_cast<std::uint64_t>(256U - taps.wx) * (256U - taps.wy), static_cast<std::uint64_t>(taps.wx) * (256U - taps.wy),
      static_cast<std::uint64_t>(256U - taps.wx) * taps.wy, static_cast<std::uint64_t>(taps.wx) * taps.wy};
  std::uint64_t alpha = 0;
  std::array<std::uint64_t, 3> color{};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const auto weighted_alpha = weights[i] * corners[i][3];
    alpha += weighted_alpha;
    for (std::size_t c = 0; c < 3U; ++c) {
      color[c] += weighted_alpha * corners[i][c];
    }
  }
  std::array<std::uint8_t, 4> out{0, 0, 0, static_cast<std::uint8_t>((alpha + 32768U) >> 16)};
  if (alpha > 0) {
    for (std::size_t c = 0; c < 3U; ++c) {
      out[c] = static_cast<std::uint8_t>(std::min<std::uint64_t>(255U, (color[c] + alpha / 2U) / alpha));
    }
  }
  return out;
}

}  // namespace patchy
