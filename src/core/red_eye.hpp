#pragma once

// The Red Eye tool's correction (docs/red-eye.md). The user picks the eye with a
// click or a drag box; fixed color rules weigh how flash-red each pixel is, the red area at
// or nearest the user's point is grown from there (hysteresis, holes such as the glint
// filled, a soft edge), and each pixel in it is desaturated and darkened in proportion to
// its own redness. Nothing here looks for faces or eyes, scans a whole image, or uses a
// learned model: that is the legal boundary recorded in docs/legal-constraints.md ("Red Eye").

#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy {

struct RedEyeOptions {
  // 1..100. Widens what counts as pupil red (less saturated, more magenta or pinkish), so a
  // larger value takes in more of the rim and of pink or purple red-eye.
  int pupil_size{50};
  // 1..100. How much darker the corrected pupil gets (50 keeps about 60% of the pupil's
  // green/blue brightness, 100 a quarter, so its texture survives).
  int darken_amount{50};
};

// The area one correction works on, in buffer coordinates over `bounds`: 255 inside the grown
// red area (holes filled), a three-pixel soft ring outside it falling to 0. How much each
// pixel changes also depends on its own color (apply_red_eye).
struct RedEyeMask {
  Rect bounds{};
  std::vector<std::uint8_t> weight;  // bounds.width * bounds.height, row-major
  std::int64_t pupil_pixels{0};      // pixels of the grown red area (holes and ring excluded)

  [[nodiscard]] bool empty() const noexcept { return pupil_pixels == 0; }
  [[nodiscard]] std::uint8_t at(std::int32_t x, std::int32_t y) const noexcept;
};

// Side of the square a plain click works in, for a document of the given size.
[[nodiscard]] std::int32_t red_eye_click_box_size(std::int32_t document_width,
                                                  std::int32_t document_height) noexcept;

// How flash-red one display-encoded 8-bit RGB value is at `pupil_size`, 0..256.
[[nodiscard]] int red_eye_redness(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept;

// Whether one display-encoded 8-bit RGB value counts as pupil red at `pupil_size` (redness
// past the seed level).
[[nodiscard]] bool red_eye_is_red(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept;

// Finds the red area inside `region` at (seed_x, seed_y), or at the strongly red pixel nearest
// it within `search_radius`, and grows it. `region` and the seed are in buffer coordinates.
// With `limit_to_disk` (a click) the area must also stay inside the disk inscribed in the
// region and fill under 70% of it; a drag box is its own limit. An area that grows irregular
// or elongated (into lashes or skin) is regrown with a stricter threshold. Pixels under half
// alpha never count.
[[nodiscard]] RedEyeMask find_red_eye(const PixelBuffer& pixels, Rect region, std::int32_t seed_x,
                                      std::int32_t seed_y, std::int32_t search_radius,
                                      const RedEyeOptions& options, bool limit_to_disk = true);

// Desaturates and darkens the masked pixels at the buffer's depth (8-bit integer math; 16 and
// 32-bit through typed pixels on encoded values). Each pixel moves toward the mean of its
// green and blue by its tint, and darkens by its redness, both scaled by the mask; inside the
// area any color cast (a glint's pink or purple halo) is desaturated too, so a white glint
// keeps its brightness. `coverage` (buffer coordinates, 0..1; empty = everywhere) scales the
// mask, which is how a selection limits the change. Alpha is never changed. Returns the
// changed bounds in buffer coordinates.
Rect apply_red_eye(PixelBuffer& pixels, const RedEyeMask& mask, const RedEyeOptions& options,
                   const std::function<float(std::int32_t, std::int32_t)>& coverage = {},
                   const PaletteSnapContext* palette_snap = nullptr);

}  // namespace patchy
