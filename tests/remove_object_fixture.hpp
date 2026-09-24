#pragma once

// A procedural stand-in for the banner image of GitHub issue #23 (Remove
// Object looked smoothed and lifted toward the background): a light noisy
// background with thin colored scribble lines, a dark horizontal band across
// it, a lighter grungy stripe inside the band, white glyph blocks on the band
// (two "." squares and a tall "1" bar), with the "1" close to the band's right
// end so white background sits within the old 24 px tone-match reach of the
// hole. Qt-free so the core and UI suites share it. Deterministic (splitmix64
// noise), RGBA8 interleaved, row-major.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy::test {

struct BannerFixture {
  std::int32_t width{224};
  std::int32_t height{120};
  // The dark band's rows and columns (inclusive) and its base luminance.
  std::int32_t band_top{40};
  std::int32_t band_bottom{80};
  std::int32_t band_left{0};
  std::int32_t band_right{189};
  std::uint8_t band_value{26};
  // The grungy stripe inside the band (inclusive rows) and its base value.
  std::int32_t stripe_top{56};
  std::int32_t stripe_bottom{63};
  std::uint8_t stripe_value{112};
  // The "1" bar (inclusive) and the hole drawn around it: 35 x 35, so the
  // adaptive tone-match radius is 8 while the white background begins 8 px
  // right of the hole (inside the old radius of 24).
  std::int32_t bar_left{165};
  std::int32_t bar_right{170};
  std::int32_t bar_top{46};
  std::int32_t bar_bottom{74};
  std::int32_t hole_x{148};
  std::int32_t hole_y{43};
  std::int32_t hole_width{35};
  std::int32_t hole_height{35};
  std::vector<std::uint8_t> pixels;

  [[nodiscard]] const std::uint8_t* pixel(std::int32_t x, std::int32_t y) const {
    return pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
  }
  [[nodiscard]] bool in_hole(std::int32_t x, std::int32_t y) const {
    return x >= hole_x && y >= hole_y && x < hole_x + hole_width && y < hole_y + hole_height;
  }
  [[nodiscard]] bool in_stripe(std::int32_t y) const { return y >= stripe_top && y <= stripe_bottom; }
  // Row-major 8-bit coverage of the hole over its own bounds (all 255).
  [[nodiscard]] std::vector<std::uint8_t> hole_mask() const {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(hole_width) * static_cast<std::size_t>(hole_height),
                                     255U);
  }
};

inline std::uint64_t banner_fixture_splitmix64(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

inline BannerFixture make_banner_fixture() {
  BannerFixture f;
  f.pixels.assign(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height) * 4U, 255U);
  const auto noise = [](std::int32_t x, std::int32_t y, std::uint32_t salt, std::int32_t amplitude) {
    const auto draw = banner_fixture_splitmix64((static_cast<std::uint64_t>(y) << 32U) ^
                                                static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) ^
                                                (static_cast<std::uint64_t>(salt) << 48U));
    return static_cast<std::int32_t>(draw % static_cast<std::uint64_t>(2 * amplitude + 1)) - amplitude;
  };
  const auto clamp_byte = [](std::int32_t value) {
    return static_cast<std::uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
  };
  for (std::int32_t y = 0; y < f.height; ++y) {
    for (std::int32_t x = 0; x < f.width; ++x) {
      auto* px = f.pixels.data() + (static_cast<std::size_t>(y) * f.width + x) * 4U;
      // Light noisy paper.
      std::int32_t r = 242 + noise(x, y, 1, 8);
      std::int32_t g = 238 + noise(x, y, 2, 8);
      std::int32_t b = 236 + noise(x, y, 3, 8);
      // Thin pink and gray scribble lines (two sine tracks, 2 px thick).
      for (std::int32_t line = 0; line < 3; ++line) {
        const auto phase = static_cast<double>(line) * 2.1;
        const auto centre = 14.0 + line * 38.0 + 9.0 * ((x % 41) / 41.0 - 0.5) + 6.0 * ((x * (line + 3)) % 17) / 17.0;
        const auto dy = static_cast<double>(y) - centre - phase;
        if (dy >= -1.0 && dy <= 1.0 && x % 3 != line) {
          r = line == 1 ? 150 : 236;
          g = line == 1 ? 150 : 120;
          b = line == 1 ? 150 : 170;
        }
      }
      const bool in_band = y >= f.band_top && y <= f.band_bottom && x >= f.band_left && x <= f.band_right;
      if (in_band) {
        const auto base = static_cast<std::int32_t>(f.band_value) + noise(x, y, 4, 3);
        r = base;
        g = base;
        b = base + 2;
        if (f.in_stripe(y)) {
          // Grungy stripe: a lighter gray with coarse speckle.
          const auto grunge = static_cast<std::int32_t>(f.stripe_value) + noise(x / 2, y, 5, 22);
          r = grunge;
          g = grunge;
          b = grunge - 4;
        }
        // Two "." squares and the "1" bar in white.
        const bool dot_a = x >= 60 && x <= 66 && y >= 66 && y <= 72;
        const bool dot_b = x >= 120 && x <= 126 && y >= 66 && y <= 72;
        const bool bar = x >= f.bar_left && x <= f.bar_right && y >= f.bar_top && y <= f.bar_bottom;
        const bool bar_foot = y >= f.bar_bottom - 2 && y <= f.bar_bottom && x >= f.bar_left - 3 && x <= f.bar_right + 3;
        if (dot_a || dot_b || bar || bar_foot) {
          r = g = b = 250;
        }
      }
      px[0] = clamp_byte(r);
      px[1] = clamp_byte(g);
      px[2] = clamp_byte(b);
      px[3] = 255;
    }
  }
  return f;
}

}  // namespace patchy::test
