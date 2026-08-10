#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <utility>

namespace patchy {

// Spot Healing source geometry: ONE coherent rigid mapping for the whole
// stroke footprint, chosen from the mask's SHAPE alone. The footprint is
// reflected across the line just past its nearest boundary (or translated
// clear of itself when the reflection would land back inside), so the sampled
// texture stays spatially coherent instead of smearing per pixel. Candidate
// directions are tried in a FIXED order with mask-geometry validity counts;
// pixel content is never read, so there is no patch search or
// synthesis-by-example (Adobe's PatchMatch family US 8285055 / US 8340463 /
// US 8355592 stays untouched, into 2031). The tone blend on top is the
// classic healing membrane of the expired US 6587592 (core/heal_membrane.hpp).
// See docs/legal-constraints.md before changing anything here.
struct SpotHealSourceMap {
  bool valid{false};
  bool mirrored{false};  // reflection across the rim line; false = translation
  // Unit outward direction (toward the sampling side) and the anchor point on
  // the reflection line, both in document space; `shift` is the translation
  // distance when not mirrored.
  double direction_x{0.0};
  double direction_y{0.0};
  double anchor_x{0.0};
  double anchor_y{0.0};
  double shift{0.0};

  // Document-space source cell for a document-space cell.
  [[nodiscard]] std::pair<std::int32_t, std::int32_t> map(std::int32_t x, std::int32_t y) const;
};

// `mask` is row-major 8-bit coverage over `bounds` (0 = outside the
// footprint). `bounds` must already be clipped to the canvas. `margin` is how
// far past the boundary the mapped region starts. Deterministic: integer
// centroid/extent math plus IEEE sqrt of integer inputs, fixed candidate
// order, first-fewest-violations tie-break.
[[nodiscard]] SpotHealSourceMap spot_heal_source_map(const std::uint8_t* mask, Rect bounds,
                                                     std::int32_t canvas_width, std::int32_t canvas_height,
                                                     std::int32_t margin = 2);

}  // namespace patchy
