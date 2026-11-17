// Pattern tile sampling speed and exactness with large (photo-sized) tiles:
// Bevel & Emboss Texture, Pattern Overlay and pattern fills share
// PatternTileSampler, whose minification box filter averages every texel an
// output pixel covers (desk card local-060: a 2000x2000 photo pattern at low
// Scale kept "Processing..." up for a minute).
#include "core/document.hpp"
#include "core/pattern_resource.hpp"
#include "core/pattern_sampler.hpp"
#include "render/compositor.hpp"

#include "core_test_support.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using patchy::test::solid_rgb;

std::uint64_t splitmix64(std::uint64_t& state) {
  auto z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

// A photo-like tile: smooth gradients plus per-texel noise, so box averages
// land on every rounding case.
patchy::PixelBuffer noisy_tile(std::int32_t width, std::int32_t height, std::uint64_t seed, bool with_alpha) {
  patchy::PixelBuffer tile(width, height, with_alpha ? patchy::PixelFormat::rgba8() : patchy::PixelFormat::rgb8());
  std::uint64_t state = seed;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto bits = splitmix64(state);
      auto* px = tile.pixel(x, y);
      px[0] = static_cast<std::uint8_t>((x * 255 / std::max(1, width - 1) + (bits & 63U)) / 2);
      px[1] = static_cast<std::uint8_t>((y * 255 / std::max(1, height - 1) + ((bits >> 8U) & 127U)) / 2);
      px[2] = static_cast<std::uint8_t>((bits >> 16U) & 255U);
      if (with_alpha) {
        px[3] = static_cast<std::uint8_t>(((bits >> 24U) & 1U) != 0U ? 255U : ((bits >> 32U) & 255U));
      }
    }
  }
  return tile;
}

// A brush-stroke-like ring with soft edges, the shape Seth styled.
patchy::PixelBuffer stroke_ring(std::int32_t width, std::int32_t height) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgba8());
  const auto cx = width * 0.5;
  const auto cy = height * 0.5;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto dx = (x + 0.5 - cx) / (width * 0.42);
      const auto dy = (y + 0.5 - cy) / (height * 0.42);
      const auto distance = std::abs(std::sqrt(dx * dx + dy * dy) - 0.75) * width * 0.42;
      const auto alpha = std::clamp(30.0 - distance, 0.0, 1.0);
      auto* px = pixels.pixel(x, y);
      px[0] = 150;
      px[1] = 60;
      px[2] = 170;
      px[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0));
    }
  }
  return pixels;
}

patchy::Document bevel_texture_document(float scale) {
  patchy::Document document(1000, 667, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Photo", solid_rgb(1000, 667, 180, 140, 90));
  patchy::Layer styled(document.allocate_layer_id(), "Layer 2", stroke_ring(340, 230));
  auto& layer = document.add_layer(std::move(styled));
  layer.set_bounds(patchy::Rect{430, 300, 340, 230});
  patchy::PatternResource photo;
  photo.id = "local-060-photo";
  photo.name = "Large photo";
  photo.tile = noisy_tile(2000, 2000, 0x060ULL, true);
  document.metadata().patterns.adopt(photo);
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.pattern_id = photo.id;
  bevel.texture.scale = scale;
  bevel.texture.depth = 0.52F;
  bevel.texture.link_with_layer = true;
  layer.layer_style().bevels.push_back(bevel);
  return document;
}

double render_seconds(const patchy::Document& document) {
  const auto start = std::chrono::steady_clock::now();
  const auto flattened = patchy::Compositor{}.flatten_rgb8(document);
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(flattened.width() == document.width());
  return seconds;
}

// The summed-area path gives the same bytes as the texel-by-texel box average
// it replaces for large footprints: odd and even tile sizes, opaque and
// translucent tiles, negative coordinates and anchors (wrapping), every Scale
// from the threshold down to the slider's 1%.
void pattern_sampler_tile_sums_match_direct_box_average() {
  patchy::Document anchor_document(4, 4, patchy::PixelFormat::rgb8());
  auto& anchor = anchor_document.add_pixel_layer("Anchor", solid_rgb(4, 4, 0, 0, 0));
  patchy::set_layer_effects_reference_point(anchor, 13.25, -7.5);
  struct TileCase {
    std::int32_t width;
    std::int32_t height;
    bool alpha;
  };
  std::size_t compared = 0;
  std::size_t mismatched = 0;
  for (const auto tile_case : {TileCase{37, 23, true}, TileCase{64, 64, false}, TileCase{301, 157, true},
                               TileCase{1, 1, true}, TileCase{2, 3, false}}) {
    const auto tile = noisy_tile(tile_case.width, tile_case.height,
                                 static_cast<std::uint64_t>(tile_case.width * 1000 + tile_case.height),
                                 tile_case.alpha);
    for (const auto scale : {0.249F, 0.2F, 0.137F, 0.1F, 0.05F, 0.033F, 0.0123F, 0.01F}) {
      for (const auto linked : {false, true}) {
        const patchy::PatternTileSampler sampler(tile, anchor, scale, 0.0F, linked, linked ? -2.5F : 3.0F,
                                                 linked ? 1.75F : -4.0F);
        CHECK(sampler.uses_tile_sums());
        std::uint64_t state = static_cast<std::uint64_t>(scale * 100000.0F) + (linked ? 7U : 0U);
        for (int index = 0; index < 400; ++index) {
          const auto x = static_cast<std::int32_t>(splitmix64(state) % 4001U) - 2000;
          const auto y = static_cast<std::int32_t>(splitmix64(state) % 4001U) - 2000;
          const auto fast = sampler.sample(x, y);
          const auto direct = sampler.sample_box_direct(x, y);
          ++compared;
          if (fast.color.red != direct.color.red || fast.color.green != direct.color.green ||
              fast.color.blue != direct.color.blue || std::abs(fast.alpha - direct.alpha) > 1e-6F) {
            ++mismatched;
            if (mismatched <= 5) {
              std::cout << "[INFO] mismatch tile " << tile_case.width << "x" << tile_case.height << " scale "
                        << scale << " at " << x << "," << y << ": " << int{fast.color.red} << "/"
                        << int{direct.color.red} << " " << int{fast.color.green} << "/" << int{direct.color.green}
                        << " " << int{fast.color.blue} << "/" << int{direct.color.blue} << " " << fast.alpha
                        << "/" << direct.alpha << "\n";
            }
          }
        }
      }
    }
  }
  std::cout << "[INFO] compared " << compared << " samples, " << mismatched << " mismatched\n";
  CHECK(mismatched == 0U);

  // At and above 25% the sampler keeps the texel loop (no table needed).
  const auto small = noisy_tile(16, 16, 5U, true);
  CHECK(!patchy::PatternTileSampler(small, anchor, 0.25F, 0.0F, false, 0.0F, 0.0F).uses_tile_sums());
  CHECK(!patchy::PatternTileSampler(small, anchor, 0.5F, 0.0F, false, 0.0F, 0.0F).uses_tile_sums());
  CHECK(!patchy::PatternTileSampler(small, anchor, 1.58F, 0.0F, false, 0.0F, 0.0F).uses_tile_sums());
}

// One table per tile: samplers built for the same pixels share it (the
// preview renders of a slider drag reuse it), and changed pixels get a new one.
void pattern_sampler_tile_sums_are_shared_per_tile() {
  auto tile = noisy_tile(48, 40, 11U, true);
  const auto first = patchy::shared_pattern_tile_sums(tile);
  const auto copy = tile;  // shares the storage
  CHECK(patchy::shared_pattern_tile_sums(copy) == first);
  CHECK(patchy::shared_pattern_tile_sums(tile) == first);
  const auto old_red = tile.pixel(0, 0)[0];
  tile.pixel(0, 0)[0] = static_cast<std::uint8_t>(old_red == 255U ? 254U : old_red + 1U);  // detaches
  const auto changed = patchy::shared_pattern_tile_sums(tile);
  CHECK(changed != first);
  std::array<std::uint32_t, 4> before{};
  std::array<std::uint32_t, 4> after{};
  first->prefix(1, 1, before);
  changed->prefix(1, 1, after);
  CHECK(after[0] != before[0]);
  CHECK(before[0] == old_red);
}

// Bevel & Emboss Texture with a 2000x2000 photo tile on a 1000x667 document
// renders quickly at every Scale, low Scale included. Before the summed-area
// table, 1% took 8.4 s per render on metroid (5%: 0.44 s); now every Scale is
// in the tens of milliseconds. The bound is loose for slow hosts.
void pattern_sampler_large_photo_bevel_texture_renders_fast() {
  double slowest = 0.0;
  for (const auto scale : {0.01F, 0.02F, 0.05F, 0.1F, 0.25F, 1.0F, 1.58F, 4.0F}) {
    const auto document = bevel_texture_document(scale);
    const auto seconds = render_seconds(document);
    slowest = std::max(slowest, seconds);
    std::cout << "[INFO] bevel texture 2000x2000 tile at " << scale * 100.0F << "%: " << seconds * 1000.0
              << " ms\n";
  }
  CHECK(slowest < 1.5);
}

}  // namespace

std::vector<patchy::test::TestCase> pattern_sampler_tests() {
  return {
      {"pattern_sampler_tile_sums_match_direct_box_average", pattern_sampler_tile_sums_match_direct_box_average},
      {"pattern_sampler_tile_sums_are_shared_per_tile", pattern_sampler_tile_sums_are_shared_per_tile},
      {"pattern_sampler_large_photo_bevel_texture_renders_fast",
       pattern_sampler_large_photo_bevel_texture_renders_fast},
  };
}
