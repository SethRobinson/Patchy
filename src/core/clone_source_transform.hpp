#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace patchy {

// The Clone Stamp's source transform (GitHub issue 424, docs/healing.md "Clone source
// settings"): a mirror, a uniform scale and a rotation applied to what the stroke samples,
// about the Alt-clicked source point. The flips mirror the source first, so the rotation
// still turns what the user sees clockwise. At 100%, 0 degrees and no flip the mapping is
// the plain offset and callers keep their untransformed path, so the output stays
// byte-identical.
struct CloneSourceTransform {
  double scale_percent{100.0};
  // Positive turns the cloned content clockwise on screen (y down).
  double rotation_degrees{0.0};
  // Mirror the cloned content left to right, and top to bottom, about the source point.
  bool flip_horizontal{false};
  bool flip_vertical{false};

  static constexpr double kMinScalePercent = 10.0;
  static constexpr double kMaxScalePercent = 400.0;
  static constexpr double kMaxRotationDegrees = 180.0;

  [[nodiscard]] bool is_identity() const noexcept;
  // Scale clamped to its range, rotation folded into (-180, 180], the flips kept.
  [[nodiscard]] CloneSourceTransform normalized() const noexcept;
  friend bool operator==(const CloneSourceTransform&, const CloneSourceTransform&) = default;
};

// Destination pixel to source position. The destination anchor is the pixel the source
// point lands on (source point minus offset); a destination pixel's distance from it is
// turned back through the transform and added to the source point. Positions are in
// pixel units times 256 (Q8): the integer part names the top-left pixel of the bilinear
// neighborhood, the low 8 bits its weights. Integer math throughout (the inverse matrix
// is rounded once to Q24 when the mapping is built; quarter turns are exact), so the
// result is the same on every toolchain (AGENTS.md determinism rule).
class CloneSourceMapping {
public:
  CloneSourceMapping() = default;
  CloneSourceMapping(std::int32_t source_x, std::int32_t source_y, std::int32_t offset_x, std::int32_t offset_y,
                     const CloneSourceTransform& transform) noexcept;

  // True when the transform is the identity: source = destination + offset exactly.
  [[nodiscard]] bool identity() const noexcept { return identity_; }
  [[nodiscard]] std::int32_t offset_x() const noexcept { return offset_x_; }
  [[nodiscard]] std::int32_t offset_y() const noexcept { return offset_y_; }

  // The Q8 source position for destination pixel (x, y).
  [[nodiscard]] std::array<std::int64_t, 2> map_q8(std::int32_t x, std::int32_t y) const noexcept;
  // The source pixel nearest to where destination pixel (x, y) samples.
  [[nodiscard]] std::array<std::int32_t, 2> map_nearest(std::int32_t x, std::int32_t y) const noexcept;

private:
  std::int32_t source_x_{0};
  std::int32_t source_y_{0};
  std::int32_t offset_x_{0};
  std::int32_t offset_y_{0};
  bool identity_{true};
  // The inverse transform, Q24: source delta = m * destination delta.
  std::int64_t m00_{1 << 24};
  std::int64_t m01_{0};
  std::int64_t m10_{0};
  std::int64_t m11_{1 << 24};
};

// The bilinear neighborhood of a Q8 source position in a width x height source: the
// two columns and rows (clamped to the edge) and the weights of the second ones, 0..256.
// nullopt when the pixel nearest the position lies outside the source, which the Clone
// Stamp skips as it skips an out-of-canvas offset.
struct CloneSourceTaps {
  std::int32_t x0{0};
  std::int32_t y0{0};
  std::int32_t x1{0};
  std::int32_t y1{0};
  std::uint32_t wx{0};
  std::uint32_t wy{0};
};
[[nodiscard]] std::optional<CloneSourceTaps> clone_source_taps(std::int64_t x_q8, std::int64_t y_q8, std::int32_t width,
                                                              std::int32_t height) noexcept;

// Samples straight (unpremultiplied) RGBA8 rows at `taps`: weighted in premultiplied
// form with integer weights, so a transparent neighbor adds no color fringe, then
// unpremultiplied with rounding.
[[nodiscard]] std::array<std::uint8_t, 4> sample_clone_source_rgba8(const std::uint8_t* pixels, std::size_t stride,
                                                                  const CloneSourceTaps& taps) noexcept;

}  // namespace patchy
