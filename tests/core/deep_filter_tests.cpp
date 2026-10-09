// Filters on 16 and 32-bit layers (docs/high-bit-depth.md, Phase 6): every catalogued
// filter agrees with its 8-bit result on a 16-bit copy of the same image, 32-bit
// documents get only the linear-light set, and deep results keep their precision.

#include "core/pixel_depth.hpp"
#include "filters/builtin_filters.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace patchy;

// Gradients, a hard edge, an alpha ramp and a transparent corner.
PixelBuffer filter_test_image() {
  constexpr std::int32_t kWidth = 40;
  constexpr std::int32_t kHeight = 32;
  PixelBuffer pixels(kWidth, kHeight, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 6 + 10);
      px[1] = static_cast<std::uint8_t>(y * 7 + 5);
      px[2] = static_cast<std::uint8_t>(x > 20 ? 220 : 40);
      px[3] = static_cast<std::uint8_t>(x < 6 && y < 6 ? 0 : y > 24 ? 120 + (x * 3) : 255);
    }
  }
  return pixels;
}

struct Difference {
  int max_color{0};
  int max_alpha{0};
  double mean_color{0.0};
};

Difference compare_rgba8(const PixelBuffer& expected, const PixelBuffer& actual) {
  Difference difference;
  std::int64_t total = 0;
  std::int64_t count = 0;
  for (std::int32_t y = 0; y < expected.height(); ++y) {
    for (std::int32_t x = 0; x < expected.width(); ++x) {
      const auto* a = expected.pixel(x, y);
      const auto* b = actual.pixel(x, y);
      const auto channels = expected.format().channels;
      if (channels >= 4) {
        difference.max_alpha = std::max(difference.max_alpha, std::abs(a[3] - b[3]));
      }
      // Compared premultiplied: color matters in proportion to its coverage (the 8-bit
      // path's own byte rounding dominates in nearly transparent pixels).
      const auto alpha_a = channels >= 4 ? a[3] : 255;
      const auto alpha_b = channels >= 4 ? b[3] : 255;
      for (int c = 0; c < 3; ++c) {
        const auto d = static_cast<int>(std::lround(std::abs(a[c] * alpha_a - b[c] * alpha_b) / 255.0));
        difference.max_color = std::max(difference.max_color, d);
        total += d;
        ++count;
      }
    }
  }
  difference.mean_color = count > 0 ? static_cast<double>(total) / static_cast<double>(count) : 0.0;
  return difference;
}

FilterRegistry builtin_registry() {
  FilterRegistry registry;
  register_builtin_filters(registry);
  return registry;
}

FilterInvocation test_invocation(const FilterRegistry& registry, const std::string& id) {
  return registry.default_invocation(id, RgbColor{40, 90, 160}, RgbColor{230, 220, 200});
}

void every_filter_agrees_with_8_bits_on_a_16_bit_copy() {
  const auto registry = builtin_registry();
  const auto image = filter_test_image();
  const auto bounds = Rect::from_size(image.width(), image.height());
  const auto deep = convert_pixel_buffer_depth(image, BitDepth::UInt16, SampleKind::Color);
  int checked = 0;
  for (const auto& filter : registry.filters()) {
    if (!filter.catalog.execute) {
      continue;
    }
    const auto invocation = test_invocation(registry, filter.identifier);
    const auto eight = registry.render(invocation, image, bounds);
    const auto sixteen = registry.render(invocation, deep, bounds);
    CHECK(sixteen.pixels.format().bit_depth == BitDepth::UInt16);
    CHECK(sixteen.bounds.x == eight.bounds.x && sixteen.bounds.y == eight.bounds.y &&
          sixteen.bounds.width == eight.bounds.width && sixteen.bounds.height == eight.bounds.height);
    if (sixteen.pixels.width() != eight.pixels.width() || sixteen.pixels.height() != eight.pixels.height()) {
      std::cerr << "  " << filter.identifier << ": size differs\n";
      CHECK(false);
      continue;
    }
    const auto narrowed = convert_pixel_buffer_depth(sixteen.pixels, BitDepth::UInt8, SampleKind::Color);
    const auto difference = compare_rgba8(eight.pixels, narrowed);
    const auto support = deep_filter_support(filter.identifier, BitDepth::UInt16);
    // A filter computed on the 8-bit copy is exact on 8-bit-exact input; a deep
    // kernel differs only by the 8-bit path's intermediate rounding.
    const auto tolerance = support == DeepFilterSupport::EightBitPrecision ? 0 : 3;
    const auto ok = difference.max_color <= tolerance && difference.max_alpha <= tolerance &&
                    difference.mean_color <= 0.75;
    if (!ok) {
      std::cerr << "  " << filter.identifier << ": max color " << difference.max_color << ", max alpha "
                << difference.max_alpha << ", mean color " << difference.mean_color << "\n";
    }
    CHECK(ok);
    ++checked;
  }
  CHECK(checked >= 40);
}

void thirty_two_bit_documents_get_the_linear_light_filters() {
  const auto registry = builtin_registry();
  // An opaque uniform HDR color (3.0, 0.5, 0.02 in linear light): every offered filter
  // except Clouds leaves it as it is, values above 1.0 included.
  constexpr std::array<float, 3> kColor{3.0F * 255.0F, 0.5F * 255.0F, 0.02F * 255.0F};
  PixelBuffer uniform(24, 20, with_bit_depth(PixelFormat::rgba8(), BitDepth::Float32));
  for (std::int32_t y = 0; y < uniform.height(); ++y) {
    for (std::int32_t x = 0; x < uniform.width(); ++x) {
      store_pixel(uniform.format(), uniform.pixel(x, y), {kColor[0], kColor[1], kColor[2], 255.0F});
    }
  }
  const auto bounds = Rect::from_size(uniform.width(), uniform.height());
  int offered = 0;
  for (const auto& filter : registry.filters()) {
    if (!filter.catalog.execute) {
      continue;
    }
    const auto invocation = test_invocation(registry, filter.identifier);
    if (deep_filter_support(filter.identifier, BitDepth::Float32) == DeepFilterSupport::Unsupported) {
      bool threw = false;
      try {
        static_cast<void>(registry.render(invocation, uniform, bounds));
      } catch (const std::invalid_argument&) {
        threw = true;
      }
      CHECK(threw);
      continue;
    }
    ++offered;
    const auto result = registry.render(invocation, uniform, bounds);
    CHECK(result.pixels.format().bit_depth == BitDepth::Float32);
    if (filter.identifier == "patchy.filters.clouds") {
      continue;
    }
    float worst = 0.0F;
    for (std::int32_t y = 0; y < result.pixels.height(); ++y) {
      for (std::int32_t x = 0; x < result.pixels.width(); ++x) {
        const auto values = load_pixel(result.pixels.format(), result.pixels.pixel(x, y));
        if (values[3] < 255.0F * 0.999F) {
          continue;  // the transparent margin a blur grows into
        }
        for (std::size_t c = 0; c < 3U; ++c) {
          worst = std::max(worst, std::abs(values[c] - kColor[c]) / kColor[c]);
        }
      }
    }
    if (worst > 0.001F) {
      std::cerr << "  " << filter.identifier << ": relative change " << worst << "\n";
    }
    CHECK(worst <= 0.001F);
  }
  CHECK(offered >= 9);
  CHECK(deep_filter_support("patchy.filters.gaussian_blur", BitDepth::Float32) == DeepFilterSupport::Native);
  CHECK(deep_filter_support("patchy.filters.median", BitDepth::Float32) == DeepFilterSupport::Unsupported);
  CHECK(deep_filter_support("patchy.filters.median", BitDepth::UInt16) == DeepFilterSupport::EightBitPrecision);
  CHECK(deep_filter_support("patchy.filters.median", BitDepth::UInt8) == DeepFilterSupport::Native);
}

// A 16-bit ramp holds 1024 distinct values across 1024 pixels; a deep blur keeps far
// more than the 256 an 8-bit result can hold, and an 8-bit-precision filter leaves the
// samples it does not change at full precision.
void sixteen_bit_filters_keep_precision() {
  const auto registry = builtin_registry();
  PixelBuffer ramp(1024, 3, with_bit_depth(PixelFormat::rgb8(), BitDepth::UInt16));
  for (std::int32_t y = 0; y < ramp.height(); ++y) {
    for (std::int32_t x = 0; x < ramp.width(); ++x) {
      const auto value = static_cast<float>(x) * 255.0F / 1023.0F;
      store_pixel(ramp.format(), ramp.pixel(x, y), {value, value, value, 255.0F});
    }
  }
  const auto bounds = Rect::from_size(ramp.width(), ramp.height());
  const auto distinct_values = [](const PixelBuffer& pixels) {
    std::set<std::uint16_t> values;
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      std::uint16_t value = 0;
      std::memcpy(&value, pixels.pixel(x, 1), sizeof(value));
      values.insert(value);
    }
    return values.size();
  };
  CHECK(distinct_values(ramp) >= 1000U);
  auto blur = registry.default_invocation("patchy.filters.gaussian_blur");
  blur.parameters["radius"] = 2.0;
  const auto blurred = registry.render(blur, ramp, bounds, false);
  CHECK(distinct_values(blurred.pixels) >= 900U);
  // Median (8-bit precision) leaves a smooth ramp alone apart from rounding.
  const auto median = registry.render(registry.default_invocation("patchy.filters.median"), ramp, bounds, false);
  CHECK(distinct_values(median.pixels) >= 900U);
}

// The 8-bit-precision path folds an edit back as a move of the changed samples.
void eight_bit_edits_fold_back_at_depth() {
  PixelBuffer pixels(4, 1, with_bit_depth(PixelFormat::rgba8(), BitDepth::UInt16));
  const std::array<float, 4> kValue{100.3F, 50.6F, 200.2F, 255.0F};
  for (std::int32_t x = 0; x < 4; ++x) {
    store_pixel(pixels.format(), pixels.pixel(x, 0), kValue);
  }
  apply_eight_bit_edit_at_depth(pixels, [](PixelBuffer& narrowed) {
    narrowed.pixel(1, 0)[0] = static_cast<std::uint8_t>(narrowed.pixel(1, 0)[0] + 10);
    narrowed.pixel(2, 0)[1] = 255;
    narrowed.pixel(3, 0)[2] = 0;
  });
  const auto at = [&](std::int32_t x) { return load_pixel(pixels.format(), pixels.pixel(x, 0)); };
  const auto close_to = [](float a, float b) { return std::abs(a - b) <= 1.0F / 257.0F; };
  CHECK(close_to(at(0)[0], kValue[0]) && close_to(at(0)[1], kValue[1]) && close_to(at(0)[2], kValue[2]));
  CHECK(close_to(at(1)[0], kValue[0] + 10.0F));
  CHECK(close_to(at(1)[1], kValue[1]));
  CHECK(at(2)[1] == 255.0F);
  CHECK(at(3)[2] == 0.0F);
  CHECK(pixels.format().bit_depth == BitDepth::UInt16);
}

}  // namespace

std::vector<patchy::test::TestCase> deep_filter_tests() {
  return {
      {"deep_filters_every_filter_agrees_with_8_bits_on_a_16_bit_copy",
       every_filter_agrees_with_8_bits_on_a_16_bit_copy},
      {"deep_filters_thirty_two_bit_documents_get_the_linear_light_filters",
       thirty_two_bit_documents_get_the_linear_light_filters},
      {"deep_filters_sixteen_bit_filters_keep_precision", sixteen_bit_filters_keep_precision},
      {"deep_filters_eight_bit_edits_fold_back_at_depth", eight_bit_edits_fold_back_at_depth},
  };
}
