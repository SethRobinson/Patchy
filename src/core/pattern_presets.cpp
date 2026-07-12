#include "core/pattern_presets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace patchy {

namespace {

// Determinism: tiles are pinned byte-exactly by pattern_presets_generate_stable_tiles
// and embedded into user PSDs, so generators restrict themselves to integer hashing
// plus IEEE-exact double ops (+ - * / sqrt, floor, lround). No std::sin/pow — libm
// results vary across toolchains (same rule as the layer-style EDT).

// Deterministic lattice hash -> [0,1), wrapped so tiles stay seamless.
[[nodiscard]] double wrapped_hash01(std::int32_t x, std::int32_t y, std::int32_t period_x,
                                    std::int32_t period_y, std::uint32_t seed) noexcept {
  const auto wrap = [](std::int32_t v, std::int32_t period) {
    const auto m = v % period;
    return m < 0 ? m + period : m;
  };
  auto h = static_cast<std::uint32_t>(wrap(x, period_x)) * 0x8DA6B343U +
           static_cast<std::uint32_t>(wrap(y, period_y)) * 0xD8163841U + seed * 0xCB1AB31FU;
  h ^= h >> 13U;
  h *= 0x7FEB352DU;
  h ^= h >> 15U;
  return static_cast<double>(h & 0x00FFFFFFU) / static_cast<double>(0x01000000U);
}

[[nodiscard]] double smooth01(double t) noexcept {
  return t * t * (3.0 - 2.0 * t);
}

// Smooth periodic wave over t in cycles: 0 at integer t, 1 at half cycles
// (a deterministic stand-in for (1-cos(2*pi*t))/2).
[[nodiscard]] double wave01(double t) noexcept {
  const auto fraction = t - std::floor(t);
  return fraction < 0.5 ? smooth01(fraction * 2.0) : smooth01(2.0 - fraction * 2.0);
}

// Wrapped value noise: lattice nodes every cell_x/cell_y pixels, tile-periodic.
// cell sizes must divide the tile dimensions.
[[nodiscard]] double wrapped_value_noise(double x, double y, std::int32_t cell_x, std::int32_t cell_y,
                                         std::int32_t tile_w, std::int32_t tile_h,
                                         std::uint32_t seed) noexcept {
  const auto lattice_w = tile_w / cell_x;
  const auto lattice_h = tile_h / cell_y;
  const auto lx = x / static_cast<double>(cell_x);
  const auto ly = y / static_cast<double>(cell_y);
  const auto x0 = static_cast<std::int32_t>(std::floor(lx));
  const auto y0 = static_cast<std::int32_t>(std::floor(ly));
  const auto tx = smooth01(lx - static_cast<double>(x0));
  const auto ty = smooth01(ly - static_cast<double>(y0));
  const auto top = wrapped_hash01(x0, y0, lattice_w, lattice_h, seed) * (1.0 - tx) +
                   wrapped_hash01(x0 + 1, y0, lattice_w, lattice_h, seed) * tx;
  const auto bottom = wrapped_hash01(x0, y0 + 1, lattice_w, lattice_h, seed) * (1.0 - tx) +
                      wrapped_hash01(x0 + 1, y0 + 1, lattice_w, lattice_h, seed) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

[[nodiscard]] std::uint8_t to_byte(double value) noexcept {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

struct TileBuilder {
  PixelBuffer buffer;

  TileBuilder(std::int32_t width, std::int32_t height)
      : buffer(width, height, PixelFormat::rgba8()) {}

  void set(std::int32_t x, std::int32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto* px = buffer.pixel(x, y);
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255U;
  }

  void set_gray(std::int32_t x, std::int32_t y, double value) {
    const auto v = to_byte(value);
    set(x, y, v, v, v);
  }
};

PixelBuffer generate_checkerboard() {
  TileBuilder tile(16, 16);
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      tile.set_gray(x, y, (((x >> 3) ^ (y >> 3)) & 1) != 0 ? 192.0 : 64.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_diagonal_stripes() {
  TileBuilder tile(32, 32);
  for (std::int32_t y = 0; y < 32; ++y) {
    for (std::int32_t x = 0; x < 32; ++x) {
      tile.set_gray(x, y, ((x + y) & 7) < 4 ? 70.0 : 185.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_polka_dots() {
  TileBuilder tile(32, 32);
  const std::array<std::array<double, 2>, 2> centers{{{8.0, 8.0}, {24.0, 24.0}}};
  const double radius = 5.5;
  for (std::int32_t y = 0; y < 32; ++y) {
    for (std::int32_t x = 0; x < 32; ++x) {
      double coverage = 0.0;
      for (const auto& center : centers) {
        auto dx = std::abs((static_cast<double>(x) + 0.5) - center[0]);
        auto dy = std::abs((static_cast<double>(y) + 0.5) - center[1]);
        dx = std::min(dx, 32.0 - dx);
        dy = std::min(dy, 32.0 - dy);
        const auto distance = std::sqrt(dx * dx + dy * dy);
        coverage = std::max(coverage, std::clamp(radius + 0.5 - distance, 0.0, 1.0));
      }
      tile.set_gray(x, y, 215.0 + (60.0 - 215.0) * coverage);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_grid() {
  TileBuilder tile(32, 32);
  for (std::int32_t y = 0; y < 32; ++y) {
    for (std::int32_t x = 0; x < 32; ++x) {
      const auto on_line = (x % 16) == 0 || (y % 16) == 0;
      tile.set_gray(x, y, on_line ? 85.0 : 205.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_fine_grain() {
  TileBuilder tile(64, 64);
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      const auto noise = wrapped_hash01(x, y, 64, 64, 101U);
      tile.set_gray(x, y, 128.0 + (noise - 0.5) * 90.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_canvas_weave() {
  TileBuilder tile(64, 64);
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      const auto horizontal_over = (((x >> 3) + (y >> 3)) & 1) == 0;
      const auto across = horizontal_over ? (static_cast<double>(y % 8) + 0.5) / 8.0
                                          : (static_cast<double>(x % 8) + 0.5) / 8.0;
      const auto thread = wave01(across);  // rounded thread profile: 0 at edges, 1 mid-thread
      const auto fiber = (wrapped_hash01(x, y, 64, 64, 202U) - 0.5) * 14.0;
      tile.set_gray(x, y, 142.0 + 52.0 * thread + fiber);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_wood_grain() {
  TileBuilder tile(128, 128);
  for (std::int32_t y = 0; y < 128; ++y) {
    for (std::int32_t x = 0; x < 128; ++x) {
      const auto warp = wrapped_value_noise(static_cast<double>(x), static_cast<double>(y), 32, 16,
                                            128, 128, 303U);
      const auto rings = wave01(5.0 * static_cast<double>(x) / 128.0 + 1.1 * warp);
      const auto streak = (wrapped_hash01(x, y, 128, 128, 304U) - 0.5) * 12.0;
      const auto t = rings * rings * (3.0 - 2.0 * rings);
      const auto r = 107.0 + (156.0 - 107.0) * t + streak;
      const auto g = 74.0 + (122.0 - 74.0) * t + streak * 0.8;
      const auto b = 43.0 + (79.0 - 43.0) * t + streak * 0.6;
      tile.set(x, y, to_byte(r), to_byte(g), to_byte(b));
    }
  }
  return tile.buffer;
}

PixelBuffer generate_brushed_metal() {
  TileBuilder tile(128, 128);
  for (std::int32_t y = 0; y < 128; ++y) {
    for (std::int32_t x = 0; x < 128; ++x) {
      const auto coarse = wrapped_value_noise(static_cast<double>(x), static_cast<double>(y), 16, 2,
                                              128, 128, 405U);
      const auto fine = wrapped_value_noise(static_cast<double>(x), static_cast<double>(y), 8, 2,
                                            128, 128, 406U);
      const auto grain = wrapped_hash01(x, y, 128, 128, 407U);
      const auto v = 0.55 * coarse + 0.3 * fine + 0.15 * grain;
      tile.set_gray(x, y, 150.0 + (v - 0.5) * 96.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_bumps() {
  TileBuilder tile(64, 64);
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      const auto low = wrapped_value_noise(static_cast<double>(x), static_cast<double>(y), 16, 16,
                                           64, 64, 508U);
      const auto high = wrapped_value_noise(static_cast<double>(x), static_cast<double>(y), 8, 8,
                                            64, 64, 509U);
      const auto v = 0.7 * low + 0.3 * high;
      tile.set_gray(x, y, 70.0 + v * 130.0);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_bricks() {
  TileBuilder tile(64, 32);
  for (std::int32_t y = 0; y < 32; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      const auto row = y / 16;
      const auto shifted_x = (x + (row & 1) * 16) % 32;
      const auto brick_column = ((x + (row & 1) * 16) / 32 + row * 7) % 8;
      const auto in_mortar = shifted_x < 2 || (y % 16) < 2;
      if (in_mortar) {
        tile.set(x, y, 198U, 193U, 186U);
        continue;
      }
      const auto jitter = (wrapped_hash01(brick_column, row, 8, 2, 610U) - 0.5) * 26.0;
      const auto shade = (static_cast<double>(y % 16) - 2.0) / 13.0 * -8.0;
      tile.set(x, y, to_byte(165.0 + jitter + shade), to_byte(88.0 + jitter * 0.6 + shade),
               to_byte(72.0 + jitter * 0.4 + shade));
    }
  }
  return tile.buffer;
}

PixelBuffer generate_scales() {
  TileBuilder tile(64, 64);
  const double radius = 11.5;
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      // Scales hang downward from centers on every 8-px row line; alternate
      // rows offset by half the 16-px spacing. The front-most (lowest) row
      // containing the pixel wins.
      double value = 205.0;
      for (std::int32_t row_delta = 0; row_delta <= 1; ++row_delta) {
        const auto row = static_cast<std::int32_t>(std::floor(static_cast<double>(y) / 8.0)) - row_delta;
        const auto center_y = static_cast<double>(row) * 8.0;
        const auto dy = (static_cast<double>(y) + 0.5) - center_y;
        if (dy < 0.0 || dy > radius) {
          continue;
        }
        const auto offset = ((row % 2) + 2) % 2 == 0 ? 0.0 : 8.0;
        auto dx = (static_cast<double>(x) + 0.5) - offset;
        dx = dx - std::floor(dx / 16.0) * 16.0;  // nearest center along x, wrapped
        dx = std::min(dx, 16.0 - dx);
        const auto distance = std::sqrt(dx * dx + dy * dy);
        if (distance > radius) {
          continue;
        }
        const auto normalized = distance / radius;
        auto shade = 205.0 - 95.0 * normalized * normalized;
        if (distance > radius - 1.4) {
          shade -= 55.0 * ((distance - (radius - 1.4)) / 1.4);
        }
        value = shade;
        break;  // row_delta 0 is the front scale
      }
      tile.set_gray(x, y, value);
    }
  }
  return tile.buffer;
}

PixelBuffer generate_basketweave() {
  TileBuilder tile(64, 64);
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 64; ++x) {
      const auto block_x = x >> 4;
      const auto block_y = y >> 4;
      const auto horizontal = ((block_x + block_y) & 1) == 0;
      const auto across = horizontal ? y % 8 : x % 8;
      const auto along = horizontal ? x % 16 : y % 16;
      double value = 168.0;
      if (across == 0) {
        value = 95.0;  // groove between planks
      } else {
        const auto profile = wave01((static_cast<double>(across) + 0.5) / 8.0);
        value = 148.0 + 44.0 * profile;
        if (along == 0) {
          value -= 40.0;  // block seam
        }
      }
      tile.set_gray(x, y, value);
    }
  }
  return tile.buffer;
}

constexpr std::array<PatternPreset, 12> kBuiltinPatternPresets{{
    {"c4a11e00-0001-4b1d-9c3e-7a7c9e55b001", "Checkerboard"},
    {"c4a11e00-0002-4b1d-9c3e-7a7c9e55b002", "Diagonal Stripes"},
    {"c4a11e00-0003-4b1d-9c3e-7a7c9e55b003", "Polka Dots"},
    {"c4a11e00-0004-4b1d-9c3e-7a7c9e55b004", "Grid"},
    {"c4a11e00-0005-4b1d-9c3e-7a7c9e55b005", "Fine Grain"},
    {"c4a11e00-0006-4b1d-9c3e-7a7c9e55b006", "Canvas Weave"},
    {"c4a11e00-0007-4b1d-9c3e-7a7c9e55b007", "Wood Grain"},
    {"c4a11e00-0008-4b1d-9c3e-7a7c9e55b008", "Brushed Metal"},
    {"c4a11e00-0009-4b1d-9c3e-7a7c9e55b009", "Bumps"},
    {"c4a11e00-000a-4b1d-9c3e-7a7c9e55b00a", "Bricks"},
    {"c4a11e00-000b-4b1d-9c3e-7a7c9e55b00b", "Scales"},
    {"c4a11e00-000c-4b1d-9c3e-7a7c9e55b00c", "Basketweave"},
}};

}  // namespace

std::span<const PatternPreset> builtin_pattern_presets() noexcept {
  return kBuiltinPatternPresets;
}

const PatternPreset* find_builtin_pattern_preset(std::string_view id) noexcept {
  for (const auto& preset : kBuiltinPatternPresets) {
    if (id == preset.id) {
      return &preset;
    }
  }
  return nullptr;
}

PixelBuffer generate_builtin_pattern_tile(std::string_view id) {
  const auto* preset = find_builtin_pattern_preset(id);
  if (preset == nullptr) {
    return {};
  }
  const auto index = static_cast<std::size_t>(preset - kBuiltinPatternPresets.data());
  switch (index) {
    case 0U: return generate_checkerboard();
    case 1U: return generate_diagonal_stripes();
    case 2U: return generate_polka_dots();
    case 3U: return generate_grid();
    case 4U: return generate_fine_grain();
    case 5U: return generate_canvas_weave();
    case 6U: return generate_wood_grain();
    case 7U: return generate_brushed_metal();
    case 8U: return generate_bumps();
    case 9U: return generate_bricks();
    case 10U: return generate_scales();
    case 11U: return generate_basketweave();
    default: return {};
  }
}

PatternResource builtin_pattern_resource(std::string_view id) {
  PatternResource resource;
  const auto* preset = find_builtin_pattern_preset(id);
  if (preset == nullptr) {
    return resource;
  }
  resource.id = preset->id;
  resource.name = preset->english_name;
  resource.tile = generate_builtin_pattern_tile(id);
  resource.provenance = PatternProvenance::Authored;
  return resource;
}

namespace {

// Bundled photo textures (Poly Haven, CC0; provenance in NOTICE-THIRD-PARTY.md).
// Append-only: ids and canonical names persist in user PSDs and library
// sidecars. All current entries shipped with pattern-library defaults version 2.
constexpr std::array<PhotoPatternPreset, 20> kPhotoPatternPresets{{
    {"f0705a00-0001-4c8b-9e3d-2a5b6c77e001", "Fine Wood Grain", "fine_grained_wood.png", 2},
    {"f0705a00-0002-4c8b-9e3d-2a5b6c77e002", "Dark Walnut", "dark_wood.png", 2},
    {"f0705a00-0003-4c8b-9e3d-2a5b6c77e003", "Oak Veneer", "oak_veneer_01.png", 2},
    {"f0705a00-0004-4c8b-9e3d-2a5b6c77e004", "Weathered Wood", "rough_wood.png", 2},
    {"f0705a00-0005-4c8b-9e3d-2a5b6c77e005", "Old Planks", "old_planks_02.png", 2},
    {"f0705a00-0006-4c8b-9e3d-2a5b6c77e006", "Medieval Wood", "medieval_wood.png", 2},
    {"f0705a00-0007-4c8b-9e3d-2a5b6c77e007", "Tree Bark", "bark_brown_01.png", 2},
    {"f0705a00-0008-4c8b-9e3d-2a5b6c77e008", "Weathered Marble", "marble_rock_01.png", 2},
    {"f0705a00-0009-4c8b-9e3d-2a5b6c77e009", "Slate Slabs", "slab_tiles.png", 2},
    {"f0705a00-000a-4c8b-9e3d-2a5b6c77e00a", "Granite Blocks", "japanese_stone_wall.png", 2},
    {"f0705a00-000b-4c8b-9e3d-2a5b6c77e00b", "Rock Face", "rock_face.png", 2},
    {"f0705a00-000c-4c8b-9e3d-2a5b6c77e00c", "Coarse Rust", "rust_coarse_01.png", 2},
    {"f0705a00-000d-4c8b-9e3d-2a5b6c77e00d", "Steel Plate", "metal_plate.png", 2},
    {"f0705a00-000e-4c8b-9e3d-2a5b6c77e00e", "Brown Leather", "brown_leather.png", 2},
    {"f0705a00-000f-4c8b-9e3d-2a5b6c77e00f", "Denim Weave", "denim_fabric.png", 2},
    {"f0705a00-0010-4c8b-9e3d-2a5b6c77e010", "Burlap", "hessian_230.png", 2},
    {"f0705a00-0011-4c8b-9e3d-2a5b6c77e011", "Rippled Sand", "damp_sand.png", 2},
    {"f0705a00-0012-4c8b-9e3d-2a5b6c77e012", "Snow", "snow_02.png", 2},
    {"f0705a00-0013-4c8b-9e3d-2a5b6c77e013", "Cracked Earth", "mud_cracked_dry_03.png", 2},
    {"f0705a00-0014-4c8b-9e3d-2a5b6c77e014", "Mossy Forest Floor", "forest_leaves_02.png", 2},
}};

}  // namespace

std::span<const PhotoPatternPreset> photo_pattern_presets() noexcept {
  return kPhotoPatternPresets;
}

const PhotoPatternPreset* find_photo_pattern_preset(std::string_view id) noexcept {
  for (const auto& preset : kPhotoPatternPresets) {
    if (id == preset.id) {
      return &preset;
    }
  }
  return nullptr;
}

}  // namespace patchy
