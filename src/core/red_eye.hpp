#pragma once

// The Red Eye tool's correction (docs/healing.md, "Red Eye"). The user picks the eye with a
// click or a drag box; a fixed color rule marks red pixels, the connected red area at or
// nearest the user's point is kept, and that area is desaturated and darkened. Nothing here
// looks for faces or eyes, scans a whole image, or uses a learned model: that is the legal
// boundary recorded in docs/legal-constraints.md ("Red Eye").

#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy {

struct RedEyeOptions {
  // 1..100. Lowers the redness a pixel needs to count as pupil, so a larger value takes in
  // more of the pinkish rim around a red pupil.
  int pupil_size{50};
  // 1..100. How much darker the corrected pupil gets (50 halves its brightness).
  int darken_amount{50};
};

// The pixels one correction changes: a weight per pixel (0..255) over `bounds`, in buffer
// coordinates. Empty when no red pixel was found.
struct RedEyeMask {
  Rect bounds{};
  std::vector<std::uint8_t> weight;  // bounds.width * bounds.height, row-major
  std::int64_t pupil_pixels{0};      // pixels of the connected red area (the feather ring excluded)

  [[nodiscard]] bool empty() const noexcept { return pupil_pixels == 0; }
  [[nodiscard]] std::uint8_t at(std::int32_t x, std::int32_t y) const noexcept;
};

// Side of the square a plain click works in, for a document of the given size.
[[nodiscard]] std::int32_t red_eye_click_box_size(std::int32_t document_width,
                                                  std::int32_t document_height) noexcept;

// Whether one display-encoded 8-bit RGB value counts as red at `pupil_size`.
[[nodiscard]] bool red_eye_is_red(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept;

// Finds the connected red area (8-connected) inside `region` that holds (seed_x, seed_y), or
// failing that the red pixel nearest it within `search_radius`. `region` and the seed are in
// buffer coordinates; the region is clipped to the buffer. Pixels under half alpha never
// count. The mask carries a one-pixel feather ring around the area.
[[nodiscard]] RedEyeMask find_red_eye(const PixelBuffer& pixels, Rect region, std::int32_t seed_x,
                                      std::int32_t seed_y, std::int32_t search_radius,
                                      const RedEyeOptions& options);

// Desaturates and darkens the masked pixels at the buffer's depth (8-bit integer math; 16
// and 32-bit through typed pixels, 32-bit in linear light). `coverage` (buffer coordinates,
// 0..1; empty = everywhere) scales each weight, which is how a selection limits the change.
// Alpha is never changed. Returns the changed bounds in buffer coordinates.
Rect apply_red_eye(PixelBuffer& pixels, const RedEyeMask& mask, const RedEyeOptions& options,
                   const std::function<float(std::int32_t, std::int32_t)>& coverage = {},
                   const PaletteSnapContext* palette_snap = nullptr);

}  // namespace patchy
