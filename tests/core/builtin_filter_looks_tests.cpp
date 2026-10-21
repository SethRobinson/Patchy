// Built-in Looks (docs/builtin-looks.md): persisted ids, catalog
// validity, size scaling, and one render pin per Look so a later filter change
// that alters a shipped Look shows up here.
#include "core/pixel_depth.hpp"
#include "filters/builtin_filter_looks.hpp"
#include "filters/filter_registry.hpp"
#include "test_harness.hpp"
#include "core_test_support.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Persisted ids in display order. Append new Looks; never rename, reorder
// silently, reuse or remove an id once it has shipped.
const std::vector<std::string_view>& expected_builtin_look_ids() {
  static const std::vector<std::string_view> ids = {
      "patchy.looks.warm_film",
      "patchy.looks.cool_cinema",
      "patchy.looks.faded_matte",
      "patchy.looks.bold_black_and_white",
      "patchy.looks.sepia_print",
      "patchy.looks.cross_process",
      "patchy.looks.silver_contrast",
      "patchy.looks.vintage_print",
      "patchy.looks.cyanotype",
      "patchy.looks.dreamy_glow",
      "patchy.looks.crisp_detail",
      "patchy.looks.watercolor",
      "patchy.looks.ink_sketch",
      "patchy.looks.pencil_drawing",
      "patchy.looks.comic",
      "patchy.looks.oil_paint",
      "patchy.looks.halftone_print",
      "patchy.looks.film_grain",
      "patchy.looks.toy_camera",
      "patchy.looks.miniature",
  };
  return ids;
}

// A small opaque photo-like RGB source: smooth gradients (sky and skin-like
// ramps), a hard-edged disc and bar for the edge filters, and fine texture for
// the median and noise paths. Deterministic integer math only.
patchy::PixelBuffer make_look_source(std::int32_t width, std::int32_t height) {
  patchy::PixelBuffer pixels(width, height, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      const auto hash = (static_cast<std::uint32_t>(x) * 73856093U) ^ (static_cast<std::uint32_t>(y) * 19349663U);
      const int texture = static_cast<int>((hash >> 7) % 17U) - 8;
      int r = 40 + (170 * x) / width + texture;
      int g = 70 + (120 * y) / height + texture;
      int b = 200 - (150 * y) / height + texture / 2;
      const auto dx = x - width / 3;
      const auto dy = y - height / 2;
      if (dx * dx + dy * dy < (height * height) / 12) {
        r = 225 + texture;
        g = 170 + texture;
        b = 130 + texture;
      }
      if (x > (2 * width) / 3 && x < (2 * width) / 3 + width / 10) {
        r = 20 + texture;
        g = 25 + texture;
        b = 30 + texture;
      }
      px[0] = static_cast<std::uint8_t>(std::clamp(r, 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(g, 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(b, 0, 255));
    }
  }
  return pixels;
}

patchy::FilterRegistry make_registry() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  return registry;
}

void builtin_looks_have_stable_unique_ids() {
  const auto& looks = patchy::builtin_filter_looks();
  const auto& expected = expected_builtin_look_ids();
  CHECK(looks.size() == expected.size());
  std::set<std::string_view> ids;
  for (std::size_t index = 0; index < looks.size() && index < expected.size(); ++index) {
    CHECK(looks[index].id == expected[index]);
    CHECK(ids.insert(looks[index].id).second);
    CHECK(patchy::find_builtin_filter_look(looks[index].id) == &looks[index]);
  }
  CHECK(patchy::find_builtin_filter_look("patchy.looks.not_shipped") == nullptr);
  // Built-in ids never look like the canonical UUIDs that name user Looks, so
  // a user record can never shadow or overwrite one.
  for (const auto& look : looks) {
    CHECK(look.id.starts_with("patchy.looks."));
    CHECK(look.id.size() != 36U);
  }
}

void builtin_looks_are_named_supported_and_normalized() {
  const auto registry = make_registry();
  std::set<std::string> names;
  for (const auto& look : patchy::builtin_filter_looks()) {
    CHECK(look.name != nullptr && std::string_view(look.name).size() > 0U);
    CHECK(look.description != nullptr && std::string_view(look.description).size() > 0U);
    CHECK(names.insert(look.name).second);
    CHECK(!look.recipe.entries.empty());
    CHECK(look.recipe.entries.size() <= 8U);
    CHECK(registry.supports(look.recipe));
    for (const auto& entry : look.recipe.entries) {
      CHECK(entry.enabled);
      CHECK(entry.opacity > 0.0 && entry.opacity <= 1.0);
      // Stored complete, so the gallery shows the values the Look renders with.
      const auto normalized = registry.normalize(entry.invocation);
      CHECK(normalized.has_value());
      CHECK(normalized.has_value() && normalized->parameters == entry.invocation.parameters);
      // No captured-color filters: a Look must not depend on the swatches.
      CHECK(entry.invocation.filter_id != "patchy.filters.clouds");
    }
  }
}

void builtin_looks_scale_pixel_distances_to_the_image() {
  const auto registry = make_registry();
  const auto* glow = patchy::find_builtin_filter_look("patchy.looks.dreamy_glow");
  CHECK(glow != nullptr);
  if (glow == nullptr) {
    return;
  }
  const auto radius = [](const patchy::FilterRecipe& recipe) {
    const auto& value = recipe.entries.front().invocation.parameters.at("radius");
    return std::get<double>(value);
  };
  const auto reference = patchy::builtin_look_recipe_for_size(
      registry, *glow, patchy::kBuiltinLookReferenceLongEdge, 2000);
  const auto half = patchy::builtin_look_recipe_for_size(registry, *glow, 1500, 2000);
  const auto doubled = patchy::builtin_look_recipe_for_size(registry, *glow, 8000, 5000);
  CHECK(reference.has_value() && half.has_value() && doubled.has_value());
  if (reference && half && doubled) {
    CHECK(std::abs(radius(*reference) - radius(glow->recipe)) < 1e-9);
    CHECK(std::abs(radius(*half) - radius(glow->recipe) * 0.5) < 1e-9);
    CHECK(std::abs(radius(*doubled) - radius(glow->recipe) * 2.0) < 1e-9);
    // Opacity, blend mode and non-spatial parameters never scale.
    CHECK(half->entries.front().opacity == glow->recipe.entries.front().opacity);
    CHECK(half->entries.front().blend_mode == glow->recipe.entries.front().blend_mode);
  }
  CHECK(!patchy::builtin_look_recipe_for_size(registry, *glow, 0, 0).has_value());
}

// A 4x3 grid of block means per channel, one byte each, as hex: a coarse,
// toolchain-tolerant picture of a render.
std::string render_signature(const patchy::PixelBuffer& pixels) {
  constexpr int kColumns = 4;
  constexpr int kRows = 3;
  static constexpr char kHex[] = "0123456789abcdef";
  std::string signature;
  for (int row = 0; row < kRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const auto x0 = pixels.width() * column / kColumns;
      const auto x1 = pixels.width() * (column + 1) / kColumns;
      const auto y0 = pixels.height() * row / kRows;
      const auto y1 = pixels.height() * (row + 1) / kRows;
      for (int channel = 0; channel < 3; ++channel) {
        std::uint64_t sum = 0;
        for (auto y = y0; y < y1; ++y) {
          for (auto x = x0; x < x1; ++x) {
            sum += pixels.pixel(x, y)[channel];
          }
        }
        const auto count = static_cast<std::uint64_t>((x1 - x0) * (y1 - y0));
        const auto mean = static_cast<unsigned>((sum + count / 2U) / count);
        signature += kHex[(mean >> 4U) & 15U];
        signature += kHex[mean & 15U];
      }
    }
  }
  return signature;
}

bool signatures_match(std::string_view actual, std::string_view expected, int tolerance) {
  if (actual.size() != expected.size() || actual.size() % 2U != 0U) {
    return false;
  }
  for (std::size_t index = 0; index < actual.size(); index += 2U) {
    const auto lhs = std::stoi(std::string(actual.substr(index, 2)), nullptr, 16);
    const auto rhs = std::stoi(std::string(expected.substr(index, 2)), nullptr, 16);
    if (std::abs(lhs - rhs) > tolerance) {
      return false;
    }
  }
  return true;
}

// One pin per Look on the 8-bit source at its scaled size, so a filter change
// that alters a shipped Look shows up. The signature (block means, +-2 levels)
// holds on every toolchain. The exact digest is the Windows release gate's:
// several kernels round floating point differently elsewhere (arm64 clang
// contracts multiply-adds). A mismatch prints every actual value before
// failing, so a deliberate change re-pins from one run.
void builtin_looks_render_pins_8_bit() {
  struct Pin {
    std::string_view id;
    std::uint64_t digest;
    std::string_view signature;
  };
  const auto registry = make_registry();
  const auto source = make_look_source(160, 120);
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  const std::vector<Pin> pins = {
      {"patchy.looks.warm_film", 0xf62c8dadf083084dULL,
       "4a5da38f74a46b497bb960a48b9e82e3bd917c6e61bc8b7c52ae519cb75f758141c0af52"},
      {"patchy.looks.cool_cinema", 0x8c4a2414320bd6ddULL,
       "22378f6e58a2472b72903a90727f77e9b49564595a9d686f2b8a3b7da554526c2f988b3c"},
      {"patchy.looks.faded_matte", 0x87d754a32378cc9fULL,
       "64719c8e7e9e726182a5729f8f978cc8aa957e7774a88a886ba16f95a778778362aba272"},
      {"patchy.looks.bold_black_and_white", 0x7c1771d83260a3f4ULL,
       "464646727272454545666666838383c5c5c56464648080806d6d6d9898986464648c8c8c"},
      {"patchy.looks.sepia_print", 0x8a4d16c02b7ea595ULL,
       "6b604e9b8b6f6e6350867860a69577ead2a78e8066a191738e8066bdaa878a7c63a89778"},
      {"patchy.looks.cross_process", 0x99300e56b29d5af5ULL,
       "3953a98371af5c417db363b2899f90eac8a3726a67bb8e8d53ae5f9bbd716c7e49c4b669"},
      {"patchy.looks.silver_contrast", 0x3c6ab47e0caa21a3ULL,
       "3e4a827c6e99523e65a062988a978ee2cbba6e6763ae8e8b5b9d619db57f6c784ebcb377"},
      {"patchy.looks.vintage_print", 0x883d1e8342df4035ULL,
       "5158758b798f68566f875d7a878978d9b79a7f756d977b745c825797a27271795691865c"},
      {"patchy.looks.cyanotype", 0xf1802a6bb7867b13ULL,
       "223c6a4c61872a395e4f62876476979eaabf46527165769651658a7384a24a56747c8aa5"},
      {"patchy.looks.dreamy_glow", 0x09e7bdacb3f5d8bbULL,
       "4b6ac1997aba6e4b8cc562ba8da792edbd947c706ac58d8a4dbd599cc0636f8642c5b453"},
      {"patchy.looks.crisp_detail", 0x5ba5d7aeea2dfff2ULL,
       "455dae8a6ca8644581b557a9859580e1aa82736660b47c7947ab4f8dae58647b3db5a24a"},
      {"patchy.looks.watercolor", 0xc125795ba28b4fb5ULL,
       "5d70b7997daf745a8cbb6ab2999f8df1b5947f736dba8b875eb4649cb26a748853baab5f"},
      {"patchy.looks.ink_sketch", 0xb775c6a196fd34b9ULL,
       "fdfdfdfbfbfbe4e4e4f7f7f7fafafaffffffe3e3e3f8f8f8fdfdfdfefefee5e5e5f8f8f8"},
      {"patchy.looks.pencil_drawing", 0x53a489c6abe9babdULL,
       "fefefefefefef5f5f5fafafafefefefffffff4f4f4fafafafffffffffffff5f5f5f9f9f9"},
      {"patchy.looks.comic", 0xe7ec22ea54c9eaacULL,
       "1936a15b3b88471e679e278e577154b47d444c3d369f5653158f1c5b8c1f415a0f998013"},
      {"patchy.looks.oil_paint", 0x19d0e8367eedba05ULL,
       "4962b38c6fac674883b658ab85937fdaa37e736662b57d7c46a84f8ca955667b40b19f48"},
      {"patchy.looks.halftone_print", 0xc44804d73b536370ULL,
       "2837cd7050bb592596d32bc37e8f68eeb8656b5752d35e6229c03370c33d55852bd0b02d"},
      {"patchy.looks.film_grain", 0x589430600498aa18ULL,
       "435aa7896ca560427bb157a6879582e3af8870635eb27d7a49a8518fae5c63783db5a450"},
      {"patchy.looks.toy_camera", 0x1763008f246b2ce9ULL,
       "1d2d6c755397552b766d2564646c51f0ad6f71605487524d1c661b749c30526b1a695c14"},
      {"patchy.looks.miniature", 0x9e784c8db150dd2cULL,
       "3255c98c64b964358acc45bb82997cf6af75736259c678742dbb3889ba3f5f7d24c4aa2b"},
  };
  CHECK(pins.size() == patchy::builtin_filter_looks().size());
  bool all_match = true;
  for (const auto& pin : pins) {
    const auto* look = patchy::find_builtin_filter_look(pin.id);
    CHECK(look != nullptr);
    if (look == nullptr) {
      all_match = false;
      continue;
    }
    const auto recipe =
        patchy::builtin_look_recipe_for_size(registry, *look, source.width(), source.height());
    CHECK(recipe.has_value());
    if (!recipe.has_value()) {
      all_match = false;
      continue;
    }
    const auto result = registry.render(*recipe, source, bounds, false);
    CHECK(result.bounds.x == 0 && result.bounds.y == 0 && result.bounds.width == source.width() &&
          result.bounds.height == source.height());
    CHECK(result.pixels.format() == source.format());
    CHECK(!std::equal(result.pixels.data().begin(), result.pixels.data().end(), source.data().begin()));
    const auto digest = patchy::test::fnv1a_hash_bytes(result.pixels.data());
    const auto signature = render_signature(result.pixels);
#if defined(_MSC_VER) && defined(_M_X64)
    const bool digest_matches = digest == pin.digest;
#else
    const bool digest_matches = true;
#endif
    if (!digest_matches || !signatures_match(signature, pin.signature, 2)) {
      std::cout << "look pin {\"" << pin.id << "\", 0x" << std::hex << digest << std::dec << "ULL, \""
                << signature << "\"},\n";
      all_match = false;
    }
  }
  CHECK(all_match);
}

// Every Look runs at 16 bits (docs/high-bit-depth.md), keeps the depth, and lands
// close to its 8-bit result: the deep path is the same Look, not a different one.
void builtin_looks_render_at_16_bit() {
  const auto registry = make_registry();
  const auto source = make_look_source(160, 120);
  const auto deep_source = patchy::convert_pixel_buffer_depth(source, patchy::BitDepth::UInt16, patchy::SampleKind::Color);
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  for (const auto& look : patchy::builtin_filter_looks()) {
    const auto recipe =
        patchy::builtin_look_recipe_for_size(registry, look, source.width(), source.height());
    CHECK(recipe.has_value());
    if (!recipe.has_value()) {
      continue;
    }
    const auto shallow = registry.render(*recipe, source, bounds, false);
    const auto deep = registry.render(*recipe, deep_source, bounds, false);
    CHECK(deep.pixels.format().bit_depth == patchy::BitDepth::UInt16);
    CHECK(deep.pixels.width() == source.width() && deep.pixels.height() == source.height());
    const auto narrowed = patchy::convert_pixel_buffer_depth(deep.pixels, patchy::BitDepth::UInt8, patchy::SampleKind::Color);
    std::uint64_t total = 0;
    const auto lhs = narrowed.data();
    const auto rhs = shallow.pixels.data();
    CHECK(lhs.size() == rhs.size());
    for (std::size_t index = 0; index < lhs.size() && index < rhs.size(); ++index) {
      total += static_cast<std::uint64_t>(std::abs(static_cast<int>(lhs[index]) - static_cast<int>(rhs[index])));
    }
    const auto mean = lhs.empty() ? 0.0 : static_cast<double>(total) / static_cast<double>(lhs.size());
    if (mean > 12.0) {
      std::cout << "look " << look.id << " 16-bit mean difference " << mean << "\n";
    }
    CHECK(mean <= 12.0);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> builtin_filter_looks_tests() {
  return {
      {"builtin_looks_have_stable_unique_ids", builtin_looks_have_stable_unique_ids},
      {"builtin_looks_are_named_supported_and_normalized", builtin_looks_are_named_supported_and_normalized},
      {"builtin_looks_scale_pixel_distances_to_the_image", builtin_looks_scale_pixel_distances_to_the_image},
      {"builtin_looks_render_pins_8_bit", builtin_looks_render_pins_8_bit},
      {"builtin_looks_render_at_16_bit", builtin_looks_render_at_16_bit},
  };
}
