#include "core/contour_presets.hpp"

#include "core/style_contour.hpp"

#include <cmath>
#include <vector>

namespace patchy {

namespace {

StyleContourPoint pt(float x, float y, bool corner = false) {
  StyleContourPoint point;
  point.x = x;
  point.y = y;
  point.corner = corner;
  return point;
}

StyleContour make_contour(const char* preset_name, std::vector<StyleContourPoint> points) {
  StyleContour contour;
  contour.name = preset_name;
  contour.points = std::move(points);
  return contour;
}

// Original point values shaped after the classic contour roster. Corner points
// make polyline segments (Cone/Sawtooth); everything else rides the natural
// cubic in build_style_contour_lut.
std::vector<ContourPreset> build_presets() {
  std::vector<ContourPreset> presets;
  presets.push_back({"contour.linear", "Linear", make_contour("Linear", {pt(0, 0), pt(255, 255)})});
  presets.push_back({"contour.cone", "Cone",
                     make_contour("Cone", {pt(0, 0, true), pt(128, 255, true), pt(255, 0, true)})});
  presets.push_back({"contour.cone_inverted", "Cone - Inverted",
                     make_contour("Cone - Inverted", {pt(0, 255, true), pt(128, 0, true), pt(255, 255, true)})});
  presets.push_back({"contour.cove_deep", "Cove - Deep",
                     make_contour("Cove - Deep", {pt(0, 0), pt(72, 220), pt(255, 255)})});
  presets.push_back({"contour.cove_shallow", "Cove - Shallow",
                     make_contour("Cove - Shallow", {pt(0, 0), pt(184, 44), pt(255, 255)})});
  presets.push_back({"contour.gaussian", "Gaussian",
                     make_contour("Gaussian",
                                  {pt(0, 0), pt(64, 20), pt(128, 128), pt(192, 236), pt(255, 255)})});
  presets.push_back({"contour.half_round", "Half Round",
                     make_contour("Half Round", {pt(0, 0), pt(80, 190), pt(160, 246), pt(255, 255)})});
  presets.push_back({"contour.ring", "Ring",
                     make_contour("Ring", {pt(0, 0), pt(128, 255), pt(255, 0)})});
  presets.push_back({"contour.ring_double", "Ring - Double",
                     make_contour("Ring - Double",
                                  {pt(0, 0), pt(64, 255), pt(128, 0), pt(192, 255), pt(255, 0)})});
  presets.push_back({"contour.rolling_slope", "Rolling Slope - Descending",
                     make_contour("Rolling Slope - Descending",
                                  {pt(0, 255), pt(64, 224), pt(192, 32), pt(255, 0)})});
  presets.push_back({"contour.rounded_steps", "Rounded Steps",
                     make_contour("Rounded Steps",
                                  {pt(0, 0), pt(88, 116), pt(128, 128), pt(168, 140), pt(255, 255)})});
  presets.push_back({"contour.sawtooth", "Sawtooth",
                     make_contour("Sawtooth",
                                  {pt(0, 0, true), pt(124, 255, true), pt(132, 0, true), pt(255, 255, true)})});
  return presets;
}

bool same_shape(const StyleContour& lhs, const StyleContour& rhs) {
  if (style_contour_is_linear(lhs) && style_contour_is_linear(rhs)) {
    return true;
  }
  if (lhs.points.size() != rhs.points.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.points.size(); ++i) {
    const auto& a = lhs.points[i];
    const auto& b = rhs.points[i];
    if (std::abs(a.x - b.x) > 1e-4F || std::abs(a.y - b.y) > 1e-4F || a.corner != b.corner) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::span<const ContourPreset> builtin_contour_presets() {
  static const std::vector<ContourPreset> presets = build_presets();
  return presets;
}

const ContourPreset* find_builtin_contour_preset(std::string_view id) {
  for (const auto& preset : builtin_contour_presets()) {
    if (id == preset.id) {
      return &preset;
    }
  }
  return nullptr;
}

const ContourPreset* find_builtin_contour_preset(const StyleContour& contour) {
  for (const auto& preset : builtin_contour_presets()) {
    if (same_shape(preset.contour, contour)) {
      return &preset;
    }
  }
  return nullptr;
}

}  // namespace patchy
