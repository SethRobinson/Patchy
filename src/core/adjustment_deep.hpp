#pragma once

// Adjustment layers on 16 and 32-bit documents (docs/high-bit-depth.md). Values are on
// the deep scale (255 = full). The 8-bit path (apply_adjustment_to_color, the 256-entry
// LUTs) stays the only one for 8-bit documents.
//
// Channel-wise kinds run their continuous transfer (Levels, Curves as the natural cubic
// itself, Brightness/Contrast, Exposure, Color Balance, Invert, Posterize, Threshold).
// Hue/Saturation and ink-space adjustments, whose calibrated 8-bit math mixes channels
// through integer stages, are lifted: the 8-bit function evaluated on the eight byte
// colors around the value and blended trilinearly, which reproduces the 8-bit result
// exactly on byte values and stays continuous between them.
//
// In the Linear domain (32-bit documents) Exposure acts on the linear values; every
// other kind acts on their display encoding.

#include "core/adjustment_layer.hpp"
#include "core/blend_math_deep.hpp"
#include "core/pixel_depth.hpp"

#include <vector>

namespace patchy {

// The natural cubic through Curves control points (build_curve_lut's spline) evaluated
// at a real-valued input on the 0..255 scale, without the final rounding.
class CurveSpline {
public:
  explicit CurveSpline(const CurveControlPoints& points);
  [[nodiscard]] double evaluate(double input) const;

private:
  CurveControlPoints points_;
  std::vector<double> second_derivatives_;
};

// One adjustment prepared for repeated application (the Curves splines are built once).
class DeepAdjuster {
public:
  DeepAdjuster(const AdjustmentSettings& settings, DeepDomain domain);
  [[nodiscard]] DeepChannels apply(DeepChannels color) const;
  [[nodiscard]] const AdjustmentSettings& settings() const noexcept { return settings_; }

private:
  [[nodiscard]] DeepChannels apply_encoded(DeepChannels color) const;

  AdjustmentSettings settings_;
  DeepDomain domain_;
  std::vector<CurveSpline> curves_;  // composite, red, green, blue (Curves only)
};

[[nodiscard]] DeepChannels apply_adjustment_deep(DeepChannels color, const AdjustmentSettings& settings,
                                                 DeepDomain domain);

}  // namespace patchy
