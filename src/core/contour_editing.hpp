#pragma once

#include "core/layer.hpp"

#include <cstddef>
#include <optional>

namespace patchy {

// The editing model behind the contour editor (GitHub issue 726): plain
// functions on a StyleContour's point list. They never evaluate the curve;
// build_style_contour_lut (core/style_contour.hpp) stays the one evaluator, so
// what the editor draws is what the effects render.
//
// Rules the functions keep:
// - Points stay in input order, at least one level apart, inside 0..255.
// - A contour keeps at least kMinContourPoints and at most kMaxContourPoints
//   points (Photoshop 2027 loads and saves a 16-point contour unchanged).
// - A point the user places or moves lands on whole levels (the editor's
//   fields and a .shc file hold whole numbers); points nobody touched keep the
//   exact values a file gave them.
inline constexpr std::size_t kMinContourPoints = 2;
inline constexpr std::size_t kMaxContourPoints = 16;

// The name an edited curve carries (the PSD writer's fallback for an unnamed
// custom curve, psd_layer_styles.cpp).
inline constexpr const char* kCustomContourName = "Custom";

// The explicit point list an editor works on: Linear (no points) becomes the
// (0,0)-(255,255) ramp; anything else is clamped to 0..255, sorted by input,
// and equal inputs collapse to the last one given, exactly as the renderer
// reads the list. The name is kept.
[[nodiscard]] StyleContour editable_style_contour(const StyleContour& contour);

// Adds a point at (x, y), rounded to whole levels. Returns its index, or
// nullopt when the contour is full or another point already sits on that input
// level. The new point is smooth.
std::optional<std::size_t> add_contour_point(StyleContour& contour, float x, float y);

// Moves point `index` to (x, y), rounded to whole levels; the input is held
// one level clear of both neighbours, so points never cross. Returns whether
// the point changed.
bool move_contour_point(StyleContour& contour, std::size_t index, float x, float y);

// Removes point `index`. Refused (false) at kMinContourPoints.
bool remove_contour_point(StyleContour& contour, std::size_t index);

// Sets the corner flag of point `index`. Returns whether it changed.
bool set_contour_point_corner(StyleContour& contour, std::size_t index, bool corner);

// Whether two contours draw the same curve: both Linear, or the same points
// and corner flags. Names are ignored.
[[nodiscard]] bool style_contours_same_shape(const StyleContour& lhs, const StyleContour& rhs) noexcept;

}  // namespace patchy
