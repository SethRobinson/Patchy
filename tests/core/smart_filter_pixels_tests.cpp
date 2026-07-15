#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::require_layer_named;
using patchy::test::require_smart_filter_stack;
using patchy::test::solid_rgba;
using patchy::test::test_dust_and_scratches_smart_filter_stack;
using patchy::test::test_gaussian_smart_filter_stack;
using patchy::test::test_high_pass_smart_filter_stack;
using patchy::test::test_median_smart_filter_stack;
using patchy::test::test_motion_blur_smart_filter_stack;
using patchy::test::test_surface_blur_smart_filter_stack;
using patchy::test::test_unsharp_mask_smart_filter_stack;

const std::uint8_t* filter_result_pixel(const patchy::FilterRenderResult& result,
                                        std::int32_t document_x,
                                        std::int32_t document_y) {
  const auto local_x = document_x - result.bounds.x;
  const auto local_y = document_y - result.bounds.y;
  CHECK(local_x >= 0 && local_x < result.bounds.width);
  CHECK(local_y >= 0 && local_y < result.bounds.height);
  return result.pixels.pixel(local_x, local_y);
}

void check_filter_result_equal(const patchy::FilterRenderResult& expected,
                               const patchy::FilterRenderResult& actual) {
  CHECK(actual.bounds.x == expected.bounds.x);
  CHECK(actual.bounds.y == expected.bounds.y);
  CHECK(actual.bounds.width == expected.bounds.width);
  CHECK(actual.bounds.height == expected.bounds.height);
  CHECK(actual.pixels.format() == expected.pixels.format());
  CHECK(actual.pixels.data().size() == expected.pixels.data().size());
  CHECK(std::equal(expected.pixels.data().begin(), expected.pixels.data().end(),
                   actual.pixels.data().begin()));
}

void smart_filter_gaussian_matches_photoshop_calibrated_kernels() {
  struct KernelCase {
    double radius;
    std::vector<std::uint8_t> alpha;
    std::int32_t vertical_support;
  };
  const std::array<KernelCase, 4> cases{{
      {0.5, {55, 145, 55}, 1},
      {1.0, {2, 18, 60, 96, 60, 18, 2}, 3},
      {2.5, {1, 5, 11, 20, 31, 39, 42, 39, 31, 20, 11, 5, 1}, 5},
      {4.5, {1, 2, 3, 5, 7, 9, 12, 16, 19, 21, 22, 22, 22, 21, 19, 16, 12,
             9, 7, 5, 3, 2, 1}, 9},
  }};

  constexpr std::int32_t kCanvasSize = 65;
  constexpr std::int32_t kLineX = 32;
  constexpr std::int32_t kLineTop = 16;
  constexpr std::int32_t kLineBottom = 48;
  const patchy::Rect source_bounds{17, 29, kCanvasSize, kCanvasSize};
  for (const auto& test_case : cases) {
    auto source = solid_rgba(kCanvasSize, kCanvasSize, 0, 0, 0, 0);
    for (std::int32_t y = kLineTop; y <= kLineBottom; ++y) {
      auto* pixel = source.pixel(kLineX, y);
      pixel[0] = 255;
      pixel[1] = 255;
      pixel[2] = 255;
      pixel[3] = 255;
    }
    for (std::int32_t coordinate = 0; coordinate < kCanvasSize; ++coordinate) {
      CHECK(source.pixel(coordinate, 0)[3] == 0);
      CHECK(source.pixel(coordinate, kCanvasSize - 1)[3] == 0);
      CHECK(source.pixel(0, coordinate)[3] == 0);
      CHECK(source.pixel(kCanvasSize - 1, coordinate)[3] == 0);
    }

    const auto result = patchy::render_smart_filter_stack(
        source, source_bounds, test_gaussian_smart_filter_stack(test_case.radius));
    const auto support = static_cast<std::int32_t>(test_case.alpha.size() / 2U);
    CHECK(result.bounds.x == source_bounds.x + kLineX - support);
    CHECK(result.bounds.y ==
          source_bounds.y + kLineTop - test_case.vertical_support);
    CHECK(result.bounds.width == static_cast<std::int32_t>(test_case.alpha.size()));
    CHECK(result.bounds.height ==
          kLineBottom - kLineTop + 1 + test_case.vertical_support * 2);
    // The compatibility overload treats this source rectangle as the complete
    // filter canvas. Transparent interior space is alpha-trimmed to the
    // calibrated visible support.
    CHECK(result.bounds.x >= source_bounds.x && result.bounds.y >= source_bounds.y);
    CHECK(result.bounds.x + result.bounds.width <= source_bounds.x + source_bounds.width);
    CHECK(result.bounds.y + result.bounds.height <= source_bounds.y + source_bounds.height);

    const auto sample_y = source_bounds.y + (kLineTop + kLineBottom) / 2;
    for (std::size_t index = 0; index < test_case.alpha.size(); ++index) {
      const auto document_x = result.bounds.x + static_cast<std::int32_t>(index);
      const auto* pixel = filter_result_pixel(result, document_x, sample_y);
      CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
      CHECK(pixel[3] == test_case.alpha[index]);
    }
  }
}

void smart_filter_gaussian_repeats_canvas_edges_and_blurs_premultiplied() {
  const auto edge_result = patchy::render_smart_filter_stack(
      solid_rgba(1, 1, 255, 255, 255, 255), patchy::Rect{8, 11, 1, 1},
      test_gaussian_smart_filter_stack(1.0));
  CHECK(edge_result.bounds.x == 8 && edge_result.bounds.y == 11);
  CHECK(edge_result.bounds.width == 1 && edge_result.bounds.height == 1);
  const auto* repeated_edge = filter_result_pixel(edge_result, 8, 11);
  CHECK(repeated_edge[0] == 255 && repeated_edge[1] == 255 && repeated_edge[2] == 255);
  CHECK(repeated_edge[3] == 255);

  // A native FEid cache covers the document, not just the placed raster.
  // Photoshop 27.8 therefore grows an isolated opaque pixel to the measured
  // radius-1 support inside that transparent document canvas.
  const auto document_result = patchy::render_smart_filter_stack(
      solid_rgba(1, 1, 255, 255, 255, 255), patchy::Rect{8, 11, 1, 1},
      patchy::Rect{0, 0, 20, 20}, test_gaussian_smart_filter_stack(1.0));
  CHECK(document_result.bounds.x == 5 && document_result.bounds.y == 8);
  CHECK(document_result.bounds.width == 7 && document_result.bounds.height == 7);

  auto half_plane = solid_rgba(65, 65, 0, 0, 0, 0);
  for (std::int32_t y = 0; y < half_plane.height(); ++y) {
    for (std::int32_t x = 0; x <= 31; ++x) {
      auto* pixel = half_plane.pixel(x, y);
      pixel[0] = 255;
      pixel[1] = 255;
      pixel[2] = 255;
      pixel[3] = 255;
    }
  }
  const auto step_result = patchy::render_smart_filter_stack(
      half_plane, patchy::Rect{0, 0, 65, 65},
      test_gaussian_smart_filter_stack(4.5));
  CHECK(step_result.bounds.x == 0 && step_result.bounds.y == 0);
  CHECK(step_result.bounds.width == 44 && step_result.bounds.height == 65);
  CHECK(filter_result_pixel(step_result, 32, 32)[3] == 116);
  CHECK(filter_result_pixel(step_result, 43, 32)[3] == 1);

  auto white_point = solid_rgba(9, 9, 0, 0, 0, 0);
  auto* center = white_point.pixel(4, 4);
  center[0] = 255;
  center[1] = 255;
  center[2] = 255;
  center[3] = 255;
  const auto premultiplied = patchy::render_smart_filter_stack(
      white_point, patchy::Rect{20, 30, 9, 9}, test_gaussian_smart_filter_stack(1.0));
  CHECK(premultiplied.bounds.x == 21 && premultiplied.bounds.y == 31);
  CHECK(premultiplied.bounds.width == 7 && premultiplied.bounds.height == 7);
  bool saw_transparent_corner = false;
  for (std::int32_t y = 0; y < premultiplied.pixels.height(); ++y) {
    for (std::int32_t x = 0; x < premultiplied.pixels.width(); ++x) {
      const auto* pixel = premultiplied.pixels.pixel(x, y);
      if (pixel[3] == 0U) {
        CHECK(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
        saw_transparent_corner = true;
      } else {
        // Straight-channel blur would turn the white halo gray. Photoshop and
        // Patchy blur premultiplied color, so every visible halo pixel stays white.
        CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
      }
    }
  }
  CHECK(saw_transparent_corner);
}

void smart_filter_high_pass_matches_photoshop_formula_and_preserves_hidden_rgb() {
  patchy::PixelBuffer alpha_ramp(6, 1, patchy::PixelFormat::rgba8());
  constexpr std::array<std::uint8_t, 6> alphas{0, 1, 64, 128, 254, 255};
  for (std::int32_t x = 0; x < alpha_ramp.width(); ++x) {
    auto* pixel = alpha_ramp.pixel(x, 0);
    pixel[0] = 23;
    pixel[1] = 141;
    pixel[2] = 219;
    pixel[3] = alphas[static_cast<std::size_t>(x)];
  }
  const patchy::Rect ramp_bounds{17, 29, alpha_ramp.width(), 1};
  const auto ramp_result = patchy::render_photoshop_high_pass(
      alpha_ramp, ramp_bounds, 4.25);
  CHECK(ramp_result.bounds.x == ramp_bounds.x &&
        ramp_result.bounds.y == ramp_bounds.y &&
        ramp_result.bounds.width == ramp_bounds.width &&
        ramp_result.bounds.height == ramp_bounds.height);
  for (std::int32_t x = 0; x < alpha_ramp.width(); ++x) {
    const auto* pixel = ramp_result.pixels.pixel(x, 0);
    CHECK(pixel[0] == 128U && pixel[1] == 128U && pixel[2] == 128U);
    CHECK(pixel[3] == alphas[static_cast<std::size_t>(x)]);
  }

  // Photoshop 27.8 destructive High Pass captures pin straight-RGB edge
  // repeat and the practical-range radius-10 kernel independently of
  // Patchy's Gaussian implementation.
  patchy::PixelBuffer edge_impulse(4, 1, patchy::PixelFormat::rgba8());
  edge_impulse.clear(0);
  edge_impulse.pixel(0, 0)[0] = 255U;
  edge_impulse.pixel(0, 0)[1] = 255U;
  edge_impulse.pixel(0, 0)[2] = 255U;
  for (std::int32_t x = 0; x < edge_impulse.width(); ++x) {
    edge_impulse.pixel(x, 0)[3] = 255U;
  }
  const auto edge_result = patchy::render_photoshop_high_pass(
      edge_impulse, patchy::Rect::from_size(4, 1), 1.0);
  constexpr std::array<std::uint8_t, 4> kPhotoshopRadius1Edge{
      208, 48, 108, 126};
  for (std::int32_t x = 0; x < edge_impulse.width(); ++x) {
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      CHECK(edge_result.pixels.pixel(x, 0)[channel] ==
            kPhotoshopRadius1Edge[static_cast<std::size_t>(x)]);
    }
    CHECK(edge_result.pixels.pixel(x, 0)[3] == 255U);
  }

  patchy::PixelBuffer radius10_impulse(61, 1,
                                       patchy::PixelFormat::rgba8());
  radius10_impulse.clear(0);
  for (std::int32_t x = 0; x < radius10_impulse.width(); ++x) {
    radius10_impulse.pixel(x, 0)[3] = 255U;
  }
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    radius10_impulse.pixel(30, 0)[channel] = 255U;
  }
  const auto radius10_result = patchy::render_photoshop_high_pass(
      radius10_impulse, patchy::Rect::from_size(61, 1), 10.0);
  constexpr std::array<std::uint8_t, 47> kPhotoshopRadius10HighPass{
      127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 123,
      122, 122, 121, 121, 120, 120, 119, 119, 118, 118, 118, 255,
      118, 118, 118, 119, 119, 120, 120, 121, 121, 122, 122, 123,
      123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};
  for (std::int32_t x = 0; x < radius10_impulse.width(); ++x) {
    const auto local = x - 7;
    const auto expected =
        local >= 0 && local < static_cast<std::int32_t>(
                                   kPhotoshopRadius10HighPass.size())
            ? kPhotoshopRadius10HighPass[static_cast<std::size_t>(local)]
            : 128U;
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      CHECK(radius10_result.pixels.pixel(x, 0)[channel] == expected);
    }
    CHECK(radius10_result.pixels.pixel(x, 0)[3] == 255U);
  }

  const auto check_photoshop_impulse_kernel =
      [](double radius, const auto& weights) {
        const auto support =
            static_cast<std::int32_t>(weights.size() / 2U);
        const auto width = support * 2 + 15;
        const auto center = width / 2;
        patchy::PixelBuffer impulse(width, 1,
                                    patchy::PixelFormat::rgba8());
        impulse.clear(0);
        for (std::int32_t x = 0; x < width; ++x) {
          impulse.pixel(x, 0)[3] = 255U;
        }
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          impulse.pixel(center, 0)[channel] = 255U;
        }
        const auto rendered = patchy::render_photoshop_high_pass(
            impulse, patchy::Rect::from_size(width, 1), radius);
        const auto sum = std::accumulate(weights.begin(), weights.end(), 0.0);
        for (std::int32_t x = 0; x < width; ++x) {
          const auto offset = x - center;
          const auto in_support = offset >= -support && offset <= support;
          const auto blurred = in_support
                                   ? static_cast<int>(std::floor(
                                         255.0 * weights[static_cast<std::size_t>(
                                                     offset + support)] /
                                             sum +
                                         0.5))
                                   : 0;
          const auto source = offset == 0 ? 255 : 0;
          const auto expected = std::clamp(source - blurred + 128, 0, 255);
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            CHECK(rendered.pixels.pixel(x, 0)[channel] == expected);
          }
          CHECK(rendered.pixels.pixel(x, 0)[3] == 255U);
        }
      };
  constexpr std::array<double, 51> kPhotoshopRadius11Kernel{
      1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7,
      7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 7, 7,
      7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2, 1, 1, 1, 1, 1};
  constexpr std::array<double, 57> kPhotoshopRadius12Kernel{
      1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6,
      6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 6,
      6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1};
  check_photoshop_impulse_kernel(11.0, kPhotoshopRadius11Kernel);
  check_photoshop_impulse_kernel(12.0, kPhotoshopRadius12Kernel);

  patchy::PixelBuffer placed_constant(5, 3,
                                      patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < placed_constant.height(); ++y) {
    for (std::int32_t x = 0; x < placed_constant.width(); ++x) {
      auto* pixel = placed_constant.pixel(x, y);
      pixel[0] = 23U;
      pixel[1] = 141U;
      pixel[2] = 219U;
      pixel[3] = 255U;
    }
  }
  const patchy::Rect placed_constant_bounds{4, 5, 5, 3};
  const patchy::Rect larger_filter_canvas{0, 0, 16, 12};
  const auto native_constant = patchy::render_smart_filter_stack(
      placed_constant, placed_constant_bounds, larger_filter_canvas,
      test_high_pass_smart_filter_stack(4.25));
  CHECK(native_constant.bounds.x == placed_constant_bounds.x &&
        native_constant.bounds.y == placed_constant_bounds.y &&
        native_constant.bounds.width == placed_constant_bounds.width &&
        native_constant.bounds.height == placed_constant_bounds.height);
  for (std::int32_t y = 0; y < native_constant.pixels.height(); ++y) {
    for (std::int32_t x = 0; x < native_constant.pixels.width(); ++x) {
      const auto* pixel = native_constant.pixels.pixel(x, y);
      CHECK(pixel[0] == 128U && pixel[1] == 128U && pixel[2] == 128U &&
            pixel[3] == 255U);
    }
  }

  patchy::PixelBuffer placed_impulse(3, 3,
                                     patchy::PixelFormat::rgba8());
  placed_impulse.clear(0);
  auto* placed_center = placed_impulse.pixel(1, 1);
  placed_center[0] = 240U;
  placed_center[1] = 80U;
  placed_center[2] = 20U;
  placed_center[3] = 255U;
  const patchy::Rect placed_impulse_bounds{6, 4, 3, 3};
  const auto gaussian_only = patchy::render_smart_filter_stack(
      placed_impulse, placed_impulse_bounds, larger_filter_canvas,
      test_gaussian_smart_filter_stack(1.0));
  auto gaussian_then_high_pass = test_gaussian_smart_filter_stack(1.0);
  gaussian_then_high_pass.entries.push_back(
      test_high_pass_smart_filter_stack(1.0).entries.front());
  const auto gaussian_then_high_pass_result =
      patchy::render_smart_filter_stack(
          placed_impulse, placed_impulse_bounds, larger_filter_canvas,
          gaussian_then_high_pass);
  CHECK(gaussian_then_high_pass_result.bounds.x == gaussian_only.bounds.x &&
        gaussian_then_high_pass_result.bounds.y == gaussian_only.bounds.y &&
        gaussian_then_high_pass_result.bounds.width ==
            gaussian_only.bounds.width &&
        gaussian_then_high_pass_result.bounds.height ==
            gaussian_only.bounds.height);
  auto high_pass_then_gaussian = test_high_pass_smart_filter_stack(1.0);
  high_pass_then_gaussian.entries.push_back(
      test_gaussian_smart_filter_stack(1.0).entries.front());
  const auto high_pass_then_gaussian_result =
      patchy::render_smart_filter_stack(
          placed_impulse, placed_impulse_bounds, larger_filter_canvas,
          high_pass_then_gaussian);
  CHECK(high_pass_then_gaussian_result.bounds.x < placed_impulse_bounds.x &&
        high_pass_then_gaussian_result.bounds.y < placed_impulse_bounds.y &&
        high_pass_then_gaussian_result.bounds.width >
            placed_impulse_bounds.width &&
        high_pass_then_gaussian_result.bounds.height >
            placed_impulse_bounds.height);

  patchy::PixelBuffer source(7, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 37 + y * 13 + 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 53 + 29) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 71 + y * 7 + 3) % 256);
      pixel[3] = 255U;
    }
  }
  const patchy::Rect bounds{41, 67, source.width(), source.height()};
  const auto blurred = patchy::render_photoshop_gaussian_blur(
      source, bounds, 1.0);
  const auto high_pass = patchy::render_photoshop_high_pass(
      source, bounds, 1.0);
  CHECK(high_pass.bounds.x == bounds.x && high_pass.bounds.y == bounds.y &&
        high_pass.bounds.width == bounds.width &&
        high_pass.bounds.height == bounds.height);
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* original = source.pixel(x, y);
      const auto* low_frequency = blurred.pixels.pixel(x, y);
      const auto* actual = high_pass.pixels.pixel(x, y);
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto expected = std::clamp(
            static_cast<int>(original[channel]) -
                static_cast<int>(low_frequency[channel]) + 128,
            0, 255);
        CHECK(actual[channel] == expected);
      }
      CHECK(actual[3] == original[3]);
    }
  }

  const auto native = patchy::render_smart_filter_stack(
      source, bounds, bounds, test_high_pass_smart_filter_stack(1.0));
  check_filter_result_equal(high_pass, native);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto invocation = registry.default_invocation("patchy.filters.high_pass");
  invocation.parameters["radius"] = 1.0;
  auto destructive = source;
  registry.apply(invocation, destructive);
  CHECK(std::equal(destructive.data().begin(), destructive.data().end(),
                   high_pass.pixels.data().begin()));

  auto compatibility = source;
  auto named_default = source;
  registry.apply("patchy.filters.high_pass", compatibility);
  registry.apply(registry.default_invocation("patchy.filters.high_pass"),
                 named_default);
  CHECK(std::equal(compatibility.data().begin(), compatibility.data().end(),
                   named_default.data().begin()));
}

patchy::PixelBuffer reference_photoshop_square_median(
    const patchy::PixelBuffer& source, int radius) {
  CHECK(source.format() == patchy::PixelFormat::rgba8());
  CHECK(radius >= 1);
  auto rgb_source = source;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      if (source.pixel(x, y)[3] != 0U) {
        continue;
      }
      std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
      std::int32_t best_x = -1;
      std::int32_t best_y = -1;
      for (std::int32_t candidate_y = 0; candidate_y < source.height();
           ++candidate_y) {
        for (std::int32_t candidate_x = 0; candidate_x < source.width();
             ++candidate_x) {
          if (source.pixel(candidate_x, candidate_y)[3] == 0U) {
            continue;
          }
          const auto dx = static_cast<std::int64_t>(candidate_x) - x;
          const auto dy = static_cast<std::int64_t>(candidate_y) - y;
          const auto distance = dx * dx + dy * dy;
          if (distance < best_distance ||
              (distance == best_distance &&
               (candidate_y > best_y ||
                (candidate_y == best_y && candidate_x > best_x)))) {
            best_distance = distance;
            best_x = candidate_x;
            best_y = candidate_y;
          }
        }
      }
      if (best_x >= 0) {
        auto* target = rgb_source.pixel(x, y);
        const auto* nearest = source.pixel(best_x, best_y);
        std::copy_n(nearest, 3U, target);
      }
    }
  }
  patchy::PixelBuffer result(source.width(), source.height(), source.format());
  const auto side = radius * 2 + 1;
  std::vector<std::uint8_t> samples;
  samples.reserve(static_cast<std::size_t>(side) *
                  static_cast<std::size_t>(side));
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* output = result.pixel(x, y);
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        samples.clear();
        for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
          const auto sample_y =
              std::clamp(y + offset_y, 0, source.height() - 1);
          for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
            const auto sample_x =
                std::clamp(x + offset_x, 0, source.width() - 1);
            const auto& channel_source = channel == 3U ? source : rgb_source;
            samples.push_back(
                channel_source.pixel(sample_x, sample_y)[channel]);
          }
        }
        const auto middle = samples.begin() +
                            static_cast<std::ptrdiff_t>(samples.size() / 2U);
        std::nth_element(samples.begin(), middle, samples.end());
        output[channel] = *middle;
      }
    }
  }
  return result;
}

void smart_filter_median_matches_square_clamped_channels_and_native_paths() {
  patchy::PixelBuffer source(6, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 71 + y * 19 + 7) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 13 + y * 83 + 29) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 47 + y * 31 + 113) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 97 + y * 43 + 17) % 256);
    }
  }
  const patchy::Rect bounds{37, 61, source.width(), source.height()};
  const auto radius_one = patchy::render_photoshop_median(
      source, bounds, 1.99);
  CHECK(radius_one.bounds.x == bounds.x && radius_one.bounds.y == bounds.y &&
        radius_one.bounds.width == bounds.width &&
        radius_one.bounds.height == bounds.height);
  const auto expected_one = reference_photoshop_square_median(source, 1);
  CHECK(radius_one.pixels.data().size() == expected_one.data().size());
  CHECK(std::equal(radius_one.pixels.data().begin(),
                   radius_one.pixels.data().end(),
                   expected_one.data().begin()));

  const auto radius_two = patchy::render_photoshop_median(
      source, bounds, 2.99);
  const auto expected_two = reference_photoshop_square_median(source, 2);
  CHECK(radius_two.pixels.data().size() == expected_two.data().size());
  CHECK(std::equal(radius_two.pixels.data().begin(),
                   radius_two.pixels.data().end(),
                   expected_two.data().begin()));

  // Photoshop filters every channel independently, including alpha, and does
  // not clear straight RGB merely because the corresponding alpha median is
  // zero. This is the red-block alpha probe used by the PS 27.8 calibration.
  auto red_block = solid_rgba(9, 9, 3, 197, 211, 0);
  for (std::int32_t y = 3; y <= 5; ++y) {
    for (std::int32_t x = 3; x <= 5; ++x) {
      auto* pixel = red_block.pixel(x, y);
      pixel[0] = 230U;
      pixel[1] = 30U;
      pixel[2] = 10U;
      pixel[3] = 255U;
    }
  }
  const auto red_result = patchy::render_photoshop_median(
      red_block, patchy::Rect{12, 18, 9, 9}, 2.0);
  const auto expected_red = reference_photoshop_square_median(red_block, 2);
  CHECK(red_result.pixels.data().size() == expected_red.data().size());
  CHECK(std::equal(red_result.pixels.data().begin(),
                   red_result.pixels.data().end(),
                   expected_red.data().begin()));
  bool saw_hidden_red = false;
  for (std::int32_t y = 0; y < red_result.pixels.height(); ++y) {
    for (std::int32_t x = 0; x < red_result.pixels.width(); ++x) {
      const auto* pixel = red_result.pixels.pixel(x, y);
      CHECK(pixel[0] == 230U && pixel[1] == 30U && pixel[2] == 10U);
      saw_hidden_red = saw_hidden_red || pixel[3] == 0U;
    }
  }
  CHECK(saw_hidden_red);

  // Two equidistant visible colors make the nearest-RGB tie policy observable:
  // Photoshop prefers the candidate farther down, then farther right. With
  // those ties resolved toward green, green is the 3x3 median at the center.
  auto tie_probe = solid_rgba(3, 3, 9, 19, 29, 0);
  auto* upper_left = tie_probe.pixel(0, 0);
  upper_left[0] = 240U;
  upper_left[1] = 20U;
  upper_left[2] = 30U;
  upper_left[3] = 255U;
  auto* lower_right = tie_probe.pixel(2, 2);
  lower_right[0] = 20U;
  lower_right[1] = 230U;
  lower_right[2] = 70U;
  lower_right[3] = 255U;
  const auto tie_result = patchy::render_photoshop_median(
      tie_probe, patchy::Rect::from_size(3, 3), 1.0);
  const auto expected_tie = reference_photoshop_square_median(tie_probe, 1);
  CHECK(std::equal(tie_result.pixels.data().begin(),
                   tie_result.pixels.data().end(),
                   expected_tie.data().begin()));
  const auto* tied_center = tie_result.pixels.pixel(1, 1);
  CHECK(tied_center[0] == 20U && tied_center[1] == 230U &&
        tied_center[2] == 70U && tied_center[3] == 0U);

  auto partial = solid_rgba(1, 1, 117, 43, 209, 128);
  const auto partial_result = patchy::render_photoshop_median(
      partial, patchy::Rect{3, 4, 1, 1}, 1.0);
  const auto* partial_pixel = partial_result.pixels.pixel(0, 0);
  CHECK(partial_pixel[0] == 117U && partial_pixel[1] == 43U &&
        partial_pixel[2] == 209U && partial_pixel[3] == 128U);

  patchy::PixelBuffer all_transparent(3, 2,
                                      patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < all_transparent.height(); ++y) {
    for (std::int32_t x = 0; x < all_transparent.width(); ++x) {
      auto* pixel = all_transparent.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(17 + x * 31 + y * 7);
      pixel[1] = static_cast<std::uint8_t>(43 + x * 5 + y * 29);
      pixel[2] = static_cast<std::uint8_t>(89 + x * 11 + y * 13);
      pixel[3] = 0U;
    }
  }
  const auto all_transparent_result = patchy::render_photoshop_median(
      all_transparent, patchy::Rect{6, 8, 3, 2}, 2.0);
  CHECK(all_transparent_result.pixels.data().size() ==
        all_transparent.data().size());
  CHECK(std::equal(all_transparent_result.pixels.data().begin(),
                   all_transparent_result.pixels.data().end(),
                   all_transparent.data().begin()));

  const auto native = patchy::render_smart_filter_stack(
      source, bounds, patchy::Rect{0, 0, 96, 80},
      test_median_smart_filter_stack(2.75));
  check_filter_result_equal(radius_two, native);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto invocation = registry.default_invocation("patchy.filters.median");
  invocation.parameters["radius"] = 2.75;
  const auto destructive = registry.render(invocation, source, bounds, false);
  check_filter_result_equal(radius_two, destructive);

  const auto single = solid_rgba(1, 1, 21, 87, 193, 44);
  const auto huge = patchy::render_photoshop_median(
      single, patchy::Rect{8, 9, 1, 1}, 500.0);
  CHECK(huge.bounds.x == 8 && huge.bounds.y == 9 &&
        huge.bounds.width == 1 && huge.bounds.height == 1);
  CHECK(huge.pixels.data().size() == single.data().size());
  CHECK(std::equal(huge.pixels.data().begin(), huge.pixels.data().end(),
                   single.data().begin()));

  bool saw_progress = false;
  patchy::FilterProgress cancel{[&](int completed, int total,
                                     patchy::FilterProgressStage stage) {
    saw_progress = true;
    CHECK(completed >= 0 && completed <= total);
    CHECK(stage == patchy::FilterProgressStage::Filtering);
    return false;
  }};
  bool cancelled = false;
  try {
    (void)patchy::render_photoshop_median(source, bounds, 500.0, &cancel);
  } catch (const patchy::FilterCancelled&) {
    cancelled = true;
  }
  CHECK(saw_progress);
  CHECK(cancelled);
}

patchy::PixelBuffer reference_photoshop_dust_and_scratches(
    const patchy::PixelBuffer& source, int radius, int threshold) {
  CHECK(source.format() == patchy::PixelFormat::rgba8());
  CHECK(radius >= 1);
  CHECK(threshold >= 0 && threshold <= 255);

  auto extended_source = source;
  bool has_visible_source = false;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      has_visible_source = has_visible_source || source.pixel(x, y)[3] != 0U;
    }
  }
  if (!has_visible_source) {
    return source;
  }
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      if (source.pixel(x, y)[3] != 0U) {
        continue;
      }
      std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
      std::int32_t best_x = -1;
      std::int32_t best_y = -1;
      for (std::int32_t candidate_y = 0; candidate_y < source.height();
           ++candidate_y) {
        for (std::int32_t candidate_x = 0; candidate_x < source.width();
             ++candidate_x) {
          if (source.pixel(candidate_x, candidate_y)[3] == 0U) {
            continue;
          }
          const auto dx = static_cast<std::int64_t>(candidate_x) - x;
          const auto dy = static_cast<std::int64_t>(candidate_y) - y;
          const auto distance = dx * dx + dy * dy;
          if (distance < best_distance ||
              (distance == best_distance &&
               (candidate_y > best_y ||
                (candidate_y == best_y && candidate_x > best_x)))) {
            best_distance = distance;
            best_x = candidate_x;
            best_y = candidate_y;
          }
        }
      }
      CHECK(best_x >= 0 && best_y >= 0);
      std::copy_n(source.pixel(best_x, best_y), 3U,
                  extended_source.pixel(x, y));
    }
  }

  const auto median = reference_photoshop_square_median(source, radius);
  auto result = source;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      const auto* straight_source = extended_source.pixel(x, y);
      const auto* candidate = median.pixel(x, y);
      auto* output = result.pixel(x, y);
      int difference = 0;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        difference = std::max(
            difference,
            std::abs(static_cast<int>(straight_source[channel]) -
                     static_cast<int>(candidate[channel])));
      }
      const auto replace = difference > threshold;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        output[channel] = replace ? candidate[channel]
                                  : straight_source[channel];
      }
      output[3] = source.pixel(x, y)[3];
    }
  }
  return result;
}

void smart_filter_dust_and_scratches_matches_photoshop_threshold_and_native_paths() {
  patchy::PixelBuffer source(6, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 71 + y * 19 + 7) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 13 + y * 83 + 29) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 47 + y * 31 + 113) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 97 + y * 43 + 17) % 256);
    }
  }
  source.pixel(1, 1)[3] = 0U;
  source.pixel(4, 3)[3] = 0U;
  source.pixel(2, 2)[3] = 128U;
  const patchy::Rect bounds{37, 61, source.width(), source.height()};
  const auto rendered = patchy::render_photoshop_dust_and_scratches(
      source, bounds, 2, 17);
  CHECK(rendered.bounds.x == bounds.x && rendered.bounds.y == bounds.y &&
        rendered.bounds.width == bounds.width &&
        rendered.bounds.height == bounds.height);
  const auto expected =
      reference_photoshop_dust_and_scratches(source, 2, 17);
  CHECK(rendered.pixels.data().size() == expected.data().size());
  CHECK(std::equal(rendered.pixels.data().begin(),
                   rendered.pixels.data().end(), expected.data().begin()));
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      CHECK(rendered.pixels.pixel(x, y)[3] == source.pixel(x, y)[3]);
    }
  }

  // Photoshop computes one max-channel distance D and replaces the complete
  // RGB triplet only when D is strictly greater than Threshold.
  auto threshold_probe = solid_rgba(3, 3, 10, 100, 200, 255);
  auto* threshold_center = threshold_probe.pixel(1, 1);
  threshold_center[0] = 20U;
  threshold_center[1] = 80U;
  threshold_center[2] = 201U;
  threshold_center[3] = 128U;
  const auto equal_threshold = patchy::render_photoshop_dust_and_scratches(
      threshold_probe, patchy::Rect::from_size(3, 3), 1, 20);
  const auto* equal_center = equal_threshold.pixels.pixel(1, 1);
  CHECK(equal_center[0] == 20U && equal_center[1] == 80U &&
        equal_center[2] == 201U && equal_center[3] == 128U);
  const auto below_threshold = patchy::render_photoshop_dust_and_scratches(
      threshold_probe, patchy::Rect::from_size(3, 3), 1, 19);
  const auto* replaced_center = below_threshold.pixels.pixel(1, 1);
  CHECK(replaced_center[0] == 10U && replaced_center[1] == 100U &&
        replaced_center[2] == 200U && replaced_center[3] == 128U);

  // Transparent straight RGB borrows the nearest visible color. Equal
  // distances prefer the lower row and then the right column, while a
  // partially transparent pixel retains its own RGB.
  auto tie_probe = solid_rgba(3, 3, 9, 19, 29, 0);
  auto* upper_left = tie_probe.pixel(0, 0);
  upper_left[0] = 240U;
  upper_left[1] = 20U;
  upper_left[2] = 30U;
  upper_left[3] = 255U;
  auto* lower_right = tie_probe.pixel(2, 2);
  lower_right[0] = 20U;
  lower_right[1] = 230U;
  lower_right[2] = 70U;
  lower_right[3] = 255U;
  const auto tie_result = patchy::render_photoshop_dust_and_scratches(
      tie_probe, patchy::Rect::from_size(3, 3), 1, 255);
  const auto* tied_center = tie_result.pixels.pixel(1, 1);
  CHECK(tied_center[0] == 20U && tied_center[1] == 230U &&
        tied_center[2] == 70U && tied_center[3] == 0U);
  const auto partial = solid_rgba(1, 1, 117, 43, 209, 128);
  const auto partial_result = patchy::render_photoshop_dust_and_scratches(
      partial, patchy::Rect{3, 4, 1, 1}, 1, 255);
  CHECK(std::equal(partial_result.pixels.data().begin(),
                   partial_result.pixels.data().end(),
                   partial.data().begin()));

  patchy::PixelBuffer all_transparent(3, 2,
                                      patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < all_transparent.height(); ++y) {
    for (std::int32_t x = 0; x < all_transparent.width(); ++x) {
      auto* pixel = all_transparent.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(17 + x * 31 + y * 7);
      pixel[1] = static_cast<std::uint8_t>(43 + x * 5 + y * 29);
      pixel[2] = static_cast<std::uint8_t>(89 + x * 11 + y * 13);
      pixel[3] = 0U;
    }
  }
  const auto all_transparent_result =
      patchy::render_photoshop_dust_and_scratches(
          all_transparent, patchy::Rect{6, 8, 3, 2}, 2, 0);
  CHECK(std::equal(all_transparent_result.pixels.data().begin(),
                   all_transparent_result.pixels.data().end(),
                   all_transparent.data().begin()));

  auto native_source = source;
  for (std::int32_t y = 0; y < native_source.height(); ++y) {
    for (std::int32_t x = 0; x < native_source.width(); ++x) {
      native_source.pixel(x, y)[3] = 255U;
    }
  }
  const auto native_direct = patchy::render_photoshop_dust_and_scratches(
      native_source, bounds, 2, 17);
  const auto native = patchy::render_smart_filter_stack(
      native_source, bounds, patchy::Rect{0, 0, 96, 80},
      test_dust_and_scratches_smart_filter_stack(2, 17));
  check_filter_result_equal(native_direct, native);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto invocation =
      registry.default_invocation("patchy.filters.dust_and_scratches");
  invocation.parameters["radius"] = std::int64_t{2};
  invocation.parameters["threshold"] = std::int64_t{17};
  const auto destructive = registry.render(invocation, source, bounds, false);
  check_filter_result_equal(rendered, destructive);

  auto compatibility = source;
  auto named_default = source;
  registry.apply("patchy.filters.dust_and_scratches", compatibility);
  registry.apply(
      registry.default_invocation("patchy.filters.dust_and_scratches"),
      named_default);
  CHECK(std::equal(compatibility.data().begin(), compatibility.data().end(),
                   named_default.data().begin()));

  const auto single = solid_rgba(1, 1, 21, 87, 193, 44);
  const auto huge = patchy::render_photoshop_dust_and_scratches(
      single, patchy::Rect{8, 9, 1, 1}, 100, 0);
  CHECK(huge.bounds.x == 8 && huge.bounds.y == 9 &&
        huge.bounds.width == 1 && huge.bounds.height == 1);
  CHECK(std::equal(huge.pixels.data().begin(), huge.pixels.data().end(),
                   single.data().begin()));

  bool saw_progress = false;
  patchy::FilterProgress cancel{[&](int completed, int total,
                                     patchy::FilterProgressStage stage) {
    saw_progress = true;
    CHECK(completed >= 0 && completed <= total);
    CHECK(stage == patchy::FilterProgressStage::Filtering);
    return false;
  }};
  bool cancelled = false;
  try {
    (void)patchy::render_photoshop_dust_and_scratches(source, bounds, 100,
                                                       17, &cancel);
  } catch (const patchy::FilterCancelled&) {
    cancelled = true;
  }
  CHECK(saw_progress);
  CHECK(cancelled);
}

patchy::PixelBuffer reference_photoshop_surface_blur(
    const patchy::PixelBuffer& source, double radius, int threshold) {
  CHECK(source.format() == patchy::PixelFormat::rgba8());
  CHECK(std::isfinite(radius) && radius >= 1.0 && radius <= 100.0);
  CHECK(threshold >= 2 && threshold <= 255);

  auto extended_source = source;
  bool has_visible_source = false;
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      has_visible_source =
          has_visible_source || source.pixel(x, y)[3] != 0U;
    }
  }
  if (!has_visible_source) {
    return source;
  }

  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      if (source.pixel(x, y)[3] != 0U) {
        continue;
      }
      std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
      std::int32_t best_x = -1;
      std::int32_t best_y = -1;
      for (std::int32_t candidate_y = 0; candidate_y < source.height();
           ++candidate_y) {
        for (std::int32_t candidate_x = 0; candidate_x < source.width();
             ++candidate_x) {
          if (source.pixel(candidate_x, candidate_y)[3] == 0U) {
            continue;
          }
          const auto dx = static_cast<std::int64_t>(candidate_x) - x;
          const auto dy = static_cast<std::int64_t>(candidate_y) - y;
          const auto distance = dx * dx + dy * dy;
          if (distance < best_distance ||
              (distance == best_distance &&
               (candidate_y > best_y ||
                (candidate_y == best_y && candidate_x > best_x)))) {
            best_distance = distance;
            best_x = candidate_x;
            best_y = candidate_y;
          }
        }
      }
      CHECK(best_x >= 0 && best_y >= 0);
      std::copy_n(source.pixel(best_x, best_y), 3U,
                  extended_source.pixel(x, y));
    }
  }

  const auto effective_radius =
      std::max(1, static_cast<int>(std::floor(radius + 0.5)));
  patchy::PixelBuffer result(source.width(), source.height(), source.format());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* output = result.pixel(x, y);
      for (std::size_t channel = 0; channel < 4U; ++channel) {
        const auto& channel_source =
            channel == 3U ? source : extended_source;
        const auto center = channel_source.pixel(x, y)[channel];
        std::uint64_t weighted_sum = 0U;
        std::uint64_t weight_sum = 0U;
        for (int offset_y = -effective_radius;
             offset_y <= effective_radius; ++offset_y) {
          const auto sample_y =
              std::clamp(y + offset_y, 0, source.height() - 1);
          for (int offset_x = -effective_radius;
               offset_x <= effective_radius; ++offset_x) {
            const auto sample_x =
                std::clamp(x + offset_x, 0, source.width() - 1);
            const auto value = channel_source.pixel(sample_x, sample_y)[channel];
            const auto weight = std::max(
                0, 5 * threshold -
                       2 * std::abs(static_cast<int>(value) -
                                    static_cast<int>(center)));
            weighted_sum += static_cast<std::uint64_t>(weight) * value;
            weight_sum += static_cast<std::uint64_t>(weight);
          }
        }
        CHECK(weight_sum != 0U);
        auto quotient = weighted_sum / weight_sum;
        const auto remainder = weighted_sum % weight_sum;
        const auto complement = weight_sum - remainder;
        if (remainder > complement ||
            (remainder == complement && (quotient & 1U) != 0U)) {
          ++quotient;
        }
        output[channel] = static_cast<std::uint8_t>(quotient);
      }
    }
  }
  return result;
}

// Pins the histogram-free Surface Blur implementations against the brute
// force reference on both sides of kSurfaceBlurDirectMaximumRadius: the
// direct-accumulation path and the per-intensity-level box-sum path used for
// large radii must produce identical bytes (patent design-around, see
// docs/smart-objects.md "Patents and trademarks").
void surface_blur_direct_and_level_paths_match_reference() {
  patchy::PixelBuffer source(23, 17, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 53 + y * 29 + 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71 + 5) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 41 + y * 23 + 201) % 256);
      pixel[3] = static_cast<std::uint8_t>(190 + (x * 7 + y * 3) % 66);
    }
  }
  source.pixel(3, 2)[3] = 0U;
  source.pixel(15, 11)[3] = 0U;
  const auto bounds = patchy::Rect::from_size(source.width(), source.height());

  for (const auto radius : {8.0, 9.0, 14.0}) {
    for (const auto threshold : {2, 15, 255}) {
      const auto rendered = patchy::render_photoshop_surface_blur(
          source, bounds, radius, threshold);
      const auto expected =
          reference_photoshop_surface_blur(source, radius, threshold);
      CHECK(rendered.pixels.data().size() == expected.data().size());
      CHECK(std::equal(rendered.pixels.data().begin(),
                       rendered.pixels.data().end(),
                       expected.data().begin()));
    }
  }
}

void smart_filter_surface_blur_matches_photoshop_weighting_bounds_and_native_paths() {
  patchy::PixelBuffer source(6, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < source.height(); ++y) {
    for (std::int32_t x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 71 + y * 19 + 7) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 13 + y * 83 + 29) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 47 + y * 31 + 113) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 97 + y * 43 + 17) % 256);
    }
  }
  source.pixel(1, 1)[3] = 0U;
  source.pixel(4, 3)[3] = 0U;
  source.pixel(2, 2)[3] = 128U;
  const patchy::Rect bounds{37, 61, source.width(), source.height()};

  const auto rendered =
      patchy::render_photoshop_surface_blur(source, bounds, 2.49, 64);
  CHECK(rendered.bounds.x == bounds.x && rendered.bounds.y == bounds.y &&
        rendered.bounds.width == bounds.width &&
        rendered.bounds.height == bounds.height);
  const auto expected = reference_photoshop_surface_blur(source, 2.49, 64);
  CHECK(rendered.pixels.data().size() == expected.data().size());
  CHECK(std::equal(rendered.pixels.data().begin(),
                   rendered.pixels.data().end(), expected.data().begin()));

  const auto radius_one =
      patchy::render_photoshop_surface_blur(source, bounds, 1.49, 64);
  const auto expected_one = reference_photoshop_surface_blur(source, 1.0, 64);
  CHECK(std::equal(radius_one.pixels.data().begin(),
                   radius_one.pixels.data().end(), expected_one.data().begin()));
  const auto radius_two =
      patchy::render_photoshop_surface_blur(source, bounds, 1.5, 64);
  const auto expected_two = reference_photoshop_surface_blur(source, 2.0, 64);
  CHECK(std::equal(radius_two.pixels.data().begin(),
                   radius_two.pixels.data().end(), expected_two.data().begin()));
  CHECK(!std::equal(radius_one.pixels.data().begin(),
                    radius_one.pixels.data().end(),
                    radius_two.pixels.data().begin()));

  // Photoshop rounds the weighted quotient to nearest with exact ties to
  // even. These two channels create 190.5 and 5.5 at the center.
  auto tie_probe = solid_rgba(3, 3, 191, 6, 50, 255);
  constexpr std::array<std::pair<int, int>, 4> kLowSamples{{
      {0, 0}, {1, 0}, {2, 0}, {1, 1},
  }};
  for (const auto [x, y] : kLowSamples) {
    auto* pixel = tie_probe.pixel(x, y);
    pixel[0] = 190U;
    pixel[1] = 5U;
  }
  const auto tie_result = patchy::render_photoshop_surface_blur(
      tie_probe, patchy::Rect::from_size(3, 3), 1.0, 2);
  const auto* tied_center = tie_result.pixels.pixel(1, 1);
  CHECK(tied_center[0] == 190U);
  CHECK(tied_center[1] == 6U);
  CHECK(tied_center[2] == 50U && tied_center[3] == 255U);

  patchy::PixelBuffer all_transparent(3, 2,
                                      patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < all_transparent.height(); ++y) {
    for (std::int32_t x = 0; x < all_transparent.width(); ++x) {
      auto* pixel = all_transparent.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(17 + x * 31 + y * 7);
      pixel[1] = static_cast<std::uint8_t>(43 + x * 5 + y * 29);
      pixel[2] = static_cast<std::uint8_t>(89 + x * 11 + y * 13);
      pixel[3] = 0U;
    }
  }
  const auto all_transparent_result = patchy::render_photoshop_surface_blur(
      all_transparent, patchy::Rect{6, 8, 3, 2}, 2.0, 255);
  CHECK(std::equal(all_transparent_result.pixels.data().begin(),
                   all_transparent_result.pixels.data().end(),
                   all_transparent.data().begin()));

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto invocation = registry.default_invocation("patchy.filters.surface_blur");
  invocation.parameters["radius"] = 2.0;
  invocation.parameters["threshold"] = std::int64_t{255};
  CHECK(registry.output_margin(invocation, 9, 9) == 2);
  CHECK(!registry.translation_invariant_support(invocation).has_value());

  const auto opaque = solid_rgba(9, 9, 230, 30, 10, 255);
  const patchy::Rect opaque_bounds{18, 18, 9, 9};
  const auto destructive = registry.render(invocation, opaque, opaque_bounds);
  CHECK(destructive.bounds.x == 16 && destructive.bounds.y == 16 &&
        destructive.bounds.width == 13 && destructive.bounds.height == 13);
  CHECK(destructive.pixels.pixel(0, 0)[3] > 0U);
  const auto native = patchy::render_smart_filter_stack(
      opaque, opaque_bounds, patchy::Rect{0, 0, 64, 64},
      test_surface_blur_smart_filter_stack(2.0, 255));
  check_filter_result_equal(destructive, native);

  const auto single = solid_rgba(1, 1, 21, 87, 193, 44);
  const auto huge = patchy::render_photoshop_surface_blur(
      single, patchy::Rect{8, 9, 1, 1}, 100.0, 255);
  CHECK(huge.bounds.x == 8 && huge.bounds.y == 9 &&
        huge.bounds.width == 1 && huge.bounds.height == 1);
  CHECK(std::equal(huge.pixels.data().begin(), huge.pixels.data().end(),
                   single.data().begin()));

  bool saw_progress = false;
  patchy::FilterProgress cancel{[&](int completed, int total,
                                     patchy::FilterProgressStage stage) {
    saw_progress = true;
    CHECK(completed >= 0 && completed <= total);
    CHECK(stage == patchy::FilterProgressStage::Filtering);
    return false;
  }};
  bool cancelled = false;
  try {
    (void)patchy::render_photoshop_surface_blur(source, bounds, 100.0, 64,
                                                 &cancel);
  } catch (const patchy::FilterCancelled&) {
    cancelled = true;
  }
  CHECK(saw_progress);
  CHECK(cancelled);
}

void tilt_shift_blur_is_deterministic_respects_focus_geometry_and_bounds() {
  patchy::PixelBuffer source(17, 13, patchy::PixelFormat::rgba8());
  source.clear(0);
  auto* impulse = source.pixel(8, 6);
  impulse[0] = 240U;
  impulse[1] = 80U;
  impulse[2] = 20U;
  impulse[3] = 255U;

  auto identity = source;
  patchy::apply_tilt_shift_blur_filter(identity, 0.0, 50.0, 50.0, 0,
                                       10.0, 20.0);
  CHECK(std::equal(identity.data().begin(), identity.data().end(),
                   source.data().begin()));

  auto horizontal = source;
  auto repeated = source;
  patchy::apply_tilt_shift_blur_filter(horizontal, 4.0, 50.0, 50.0, 0,
                                       10.0, 20.0);
  patchy::apply_tilt_shift_blur_filter(repeated, 4.0, 50.0, 50.0, 0,
                                       10.0, 20.0);
  CHECK(std::equal(horizontal.data().begin(), horizontal.data().end(),
                   repeated.data().begin()));
  // The horizontal focus band retains the exact center row, while the fully
  // blurred outer band receives part of the impulse's premultiplied alpha.
  for (std::int32_t x = 0; x < source.width(); ++x) {
    CHECK(std::equal(horizontal.pixel(x, 6), horizontal.pixel(x, 6) + 4,
                     source.pixel(x, 6)));
  }
  CHECK(horizontal.pixel(8, 2)[3] > 0U);
  CHECK(horizontal.pixel(8, 2)[3] < 255U);

  auto vertical = source;
  patchy::apply_tilt_shift_blur_filter(vertical, 4.0, 50.0, 50.0, 90,
                                       10.0, 20.0);
  for (std::int32_t y = 0; y < source.height(); ++y) {
    CHECK(std::equal(vertical.pixel(8, y), vertical.pixel(8, y) + 4,
                     source.pixel(8, y)));
  }
  CHECK(vertical.pixel(4, 6)[3] > 0U);
  CHECK(vertical.pixel(4, 6)[3] < 255U);
  CHECK(!std::equal(horizontal.data().begin(), horizontal.data().end(),
                    vertical.data().begin()));

  // Positive filter angles rotate counterclockwise in image coordinates.
  // Therefore the +45-degree focus line runs from lower-left to upper-right:
  // the impulse at (6,2) stays exact for +45, but is blurred for -45.
  patchy::PixelBuffer angle_source(9, 9, patchy::PixelFormat::rgba8());
  angle_source.clear(0);
  auto* angle_impulse = angle_source.pixel(6, 2);
  angle_impulse[0] = 40U;
  angle_impulse[1] = 180U;
  angle_impulse[2] = 230U;
  angle_impulse[3] = 255U;
  auto positive_angle = angle_source;
  auto negative_angle = angle_source;
  patchy::apply_tilt_shift_blur_filter(positive_angle, 2.0, 50.0, 50.0,
                                       45, 0.0, 0.0);
  patchy::apply_tilt_shift_blur_filter(negative_angle, 2.0, 50.0, 50.0,
                                       -45, 0.0, 0.0);
  CHECK(std::equal(positive_angle.pixel(6, 2),
                   positive_angle.pixel(6, 2) + 4,
                   angle_source.pixel(6, 2)));
  CHECK(negative_angle.pixel(6, 2)[3] > 0U);
  CHECK(negative_angle.pixel(6, 2)[3] < 255U);

  // The transition band selects a smaller interpolated blur radius than the
  // fully blurred outer region. At the same off-focus impulse, it therefore
  // retains more of the source alpha without becoming an unchanged hard edge.
  patchy::PixelBuffer transition_source(17, 13,
                                         patchy::PixelFormat::rgba8());
  transition_source.clear(0);
  auto* transition_impulse = transition_source.pixel(8, 10);
  transition_impulse[0] = 210U;
  transition_impulse[1] = 90U;
  transition_impulse[2] = 35U;
  transition_impulse[3] = 255U;
  auto smooth_transition = transition_source;
  auto full_blur = transition_source;
  patchy::apply_tilt_shift_blur_filter(smooth_transition, 4.0, 50.0, 50.0,
                                       0, 0.0, 100.0);
  patchy::apply_tilt_shift_blur_filter(full_blur, 4.0, 50.0, 50.0, 0, 0.0,
                                       0.0);
  CHECK(smooth_transition.pixel(8, 10)[3] > full_blur.pixel(8, 10)[3]);
  CHECK(smooth_transition.pixel(8, 10)[3] < 255U);

  // Blur is performed in premultiplied RGBA. Hidden red under transparent
  // pixels must never fringe the one visible blue-green sample.
  patchy::PixelBuffer fringe(9, 9, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < fringe.height(); ++y) {
    for (std::int32_t x = 0; x < fringe.width(); ++x) {
      auto* pixel = fringe.pixel(x, y);
      pixel[0] = 255U;
      pixel[1] = 0U;
      pixel[2] = 0U;
      pixel[3] = 0U;
    }
  }
  auto* visible = fringe.pixel(4, 4);
  visible[0] = 20U;
  visible[1] = 100U;
  visible[2] = 220U;
  visible[3] = 255U;
  patchy::apply_tilt_shift_blur_filter(fringe, 3.0, 50.0, 0.0, 0, 0.0,
                                       0.0);
  bool saw_translucent = false;
  for (std::int32_t y = 0; y < fringe.height(); ++y) {
    for (std::int32_t x = 0; x < fringe.width(); ++x) {
      const auto* pixel = fringe.pixel(x, y);
      if (pixel[3] == 0U) {
        CHECK(pixel[0] == 0U && pixel[1] == 0U && pixel[2] == 0U);
        continue;
      }
      saw_translucent = saw_translucent || pixel[3] < 255U;
      CHECK(pixel[0] == 20U && pixel[1] == 100U && pixel[2] == 220U);
    }
  }
  CHECK(saw_translucent);

  bool saw_progress = false;
  patchy::FilterProgress cancel{
      [&](int completed, int total, patchy::FilterProgressStage stage) {
        saw_progress = true;
        CHECK(completed >= 0 && completed <= total);
        CHECK(stage == patchy::FilterProgressStage::Blurring);
        return false;
      }};
  bool cancelled = false;
  try {
    auto cancelled_pixels = source;
    patchy::apply_tilt_shift_blur_filter(cancelled_pixels, 4.0, 50.0, 50.0,
                                         0, 10.0, 20.0, &cancel);
  } catch (const patchy::FilterCancelled&) {
    cancelled = true;
  }
  CHECK(saw_progress);
  CHECK(cancelled);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto invocation =
      registry.default_invocation("patchy.filters.tilt_shift_blur");
  invocation.parameters["blur"] = 2.5;
  invocation.parameters["center_y"] = 0.0;
  invocation.parameters["focus_half_width"] = 0.0;
  invocation.parameters["transition_width"] = 0.0;
  CHECK(registry.output_margin(invocation, 5, 5) == 3);
  CHECK(!registry.translation_invariant_support(invocation).has_value());

  patchy::PixelBuffer point(5, 5, patchy::PixelFormat::rgba8());
  point.clear(0);
  auto* point_pixel = point.pixel(2, 4);
  point_pixel[0] = 30U;
  point_pixel[1] = 170U;
  point_pixel[2] = 240U;
  point_pixel[3] = 255U;
  const patchy::Rect bounds{10, 20, 5, 5};
  const auto grown = registry.render(invocation, point, bounds);
  CHECK(grown.bounds.x >= bounds.x - 3);
  CHECK(grown.bounds.y >= bounds.y - 3);
  CHECK(grown.bounds.x + grown.bounds.width <=
        bounds.x + bounds.width + 3);
  CHECK(grown.bounds.y + grown.bounds.height <=
        bounds.y + bounds.height + 3);
  CHECK(grown.bounds.x < bounds.x || grown.bounds.y < bounds.y ||
        grown.bounds.x + grown.bounds.width > bounds.x + bounds.width ||
        grown.bounds.y + grown.bounds.height > bounds.y + bounds.height);

  const auto confined = registry.render(invocation, point, bounds, false);
  CHECK(confined.bounds.x == bounds.x && confined.bounds.y == bounds.y);
  CHECK(confined.bounds.width == bounds.width &&
        confined.bounds.height == bounds.height);

  // Padding remaps centers and the two percentage widths into the larger
  // working buffer. The original-bounds region must remain byte-identical to
  // a direct apply using the unpadded geometry.
  patchy::PixelBuffer geometry_source(11, 9,
                                       patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < geometry_source.height(); ++y) {
    for (std::int32_t x = 0; x < geometry_source.width(); ++x) {
      auto* pixel = geometry_source.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 37 + y * 11 + 5) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 13 + y * 53 + 17) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 71 + y * 19 + 31) % 256);
      pixel[3] = static_cast<std::uint8_t>(64 + (x * 17 + y * 29) % 192);
    }
  }
  auto geometry =
      registry.default_invocation("patchy.filters.tilt_shift_blur");
  geometry.parameters["blur"] = 3.5;
  geometry.parameters["center_x"] = 22.5;
  geometry.parameters["center_y"] = 71.5;
  geometry.parameters["angle"] = std::int64_t{37};
  geometry.parameters["focus_half_width"] = 12.5;
  geometry.parameters["transition_width"] = 31.25;
  auto direct_geometry = geometry_source;
  registry.apply(geometry, direct_geometry);
  const patchy::Rect geometry_bounds{31, 47, geometry_source.width(),
                                     geometry_source.height()};
  const auto expanded_geometry =
      registry.render(geometry, geometry_source, geometry_bounds, true);
  for (std::int32_t y = 0; y < geometry_source.height(); ++y) {
    for (std::int32_t x = 0; x < geometry_source.width(); ++x) {
      const auto result_x = geometry_bounds.x + x - expanded_geometry.bounds.x;
      const auto result_y = geometry_bounds.y + y - expanded_geometry.bounds.y;
      CHECK(result_x >= 0 && result_x < expanded_geometry.pixels.width());
      CHECK(result_y >= 0 && result_y < expanded_geometry.pixels.height());
      CHECK(std::equal(direct_geometry.pixel(x, y),
                       direct_geometry.pixel(x, y) + 4,
                       expanded_geometry.pixels.pixel(result_x, result_y)));
    }
  }
}

void smart_filter_shared_mask_and_disable_states_are_applied_once() {
  auto source = solid_rgba(5, 5, 0, 0, 0, 255);
  for (std::int32_t y = 0; y < source.height(); ++y) {
    auto* pixel = source.pixel(2, y);
    pixel[0] = 255;
    pixel[1] = 255;
    pixel[2] = 255;
  }
  const patchy::Rect bounds{10, 20, 5, 5};
  const auto stack = test_gaussian_smart_filter_stack(1.0);
  const auto unmasked = patchy::render_smart_filter_stack(source, bounds, stack);
  CHECK(filter_result_pixel(unmasked, 12, 22)[0] == 96);

  auto all_white = stack;
  all_white.mask.bounds = bounds;
  all_white.mask.pixels = patchy::PixelBuffer(5, 5, patchy::PixelFormat::gray8());
  all_white.mask.pixels.clear(255);
  all_white.mask.default_color = 255;
  all_white.mask.extend_with_white = true;
  check_filter_result_equal(
      unmasked, patchy::render_smart_filter_stack(source, bounds, all_white));

  auto black = stack;
  black.mask.bounds = bounds;
  black.mask.pixels = patchy::PixelBuffer(5, 5, patchy::PixelFormat::gray8());
  black.mask.pixels.clear(0);
  black.mask.default_color = 0;
  black.mask.extend_with_white = false;
  const auto black_result = patchy::render_smart_filter_stack(source, bounds, black);
  CHECK(black_result.bounds.x == bounds.x && black_result.bounds.y == bounds.y);
  CHECK(black_result.bounds.width == bounds.width && black_result.bounds.height == bounds.height);
  CHECK(std::equal(source.data().begin(), source.data().end(),
                   black_result.pixels.data().begin()));

  auto gray = black;
  gray.mask.pixels.pixel(2, 2)[0] = 128;
  const auto gray_result = patchy::render_smart_filter_stack(source, bounds, gray);
  CHECK(filter_result_pixel(gray_result, 12, 22)[0] == 175);
  CHECK(filter_result_pixel(gray_result, 12, 22)[3] == 255);
  CHECK(filter_result_pixel(gray_result, 12, 21)[0] == 255);

  auto disabled_mask = black;
  disabled_mask.mask.enabled = false;
  check_filter_result_equal(
      unmasked, patchy::render_smart_filter_stack(source, bounds, disabled_mask));

  auto disabled_entry = stack;
  disabled_entry.entries.front().enabled = false;
  const auto entry_result = patchy::render_smart_filter_stack(source, bounds, disabled_entry);
  CHECK(entry_result.bounds.x == bounds.x && entry_result.bounds.y == bounds.y);
  CHECK(entry_result.bounds.width == bounds.width && entry_result.bounds.height == bounds.height);
  CHECK(std::equal(source.data().begin(), source.data().end(),
                   entry_result.pixels.data().begin()));

  auto disabled_stack = stack;
  disabled_stack.enabled = false;
  const auto stack_result = patchy::render_smart_filter_stack(source, bounds, disabled_stack);
  CHECK(stack_result.bounds.x == bounds.x && stack_result.bounds.y == bounds.y);
  CHECK(stack_result.bounds.width == bounds.width && stack_result.bounds.height == bounds.height);
  CHECK(std::equal(source.data().begin(), source.data().end(),
                   stack_result.pixels.data().begin()));

  // Disabled identity paths leave pixels unchanged but use Photoshop's normal
  // alpha-trimmed baked-layer bounds.
  auto transparent_source = solid_rgba(5, 5, 0, 0, 0, 0);
  auto* visible = transparent_source.pixel(2, 2);
  visible[0] = 10;
  visible[1] = 20;
  visible[2] = 30;
  visible[3] = 255;
  const auto transparent_entry = patchy::render_smart_filter_stack(
      transparent_source, bounds, disabled_entry);
  CHECK(transparent_entry.bounds.x == 12 && transparent_entry.bounds.y == 22);
  CHECK(transparent_entry.bounds.width == 1 && transparent_entry.bounds.height == 1);
  CHECK(std::equal(visible, visible + 4, transparent_entry.pixels.data().begin()));
  const auto transparent_stack = patchy::render_smart_filter_stack(
      transparent_source, bounds, disabled_stack);
  CHECK(transparent_stack.bounds.x == 12 && transparent_stack.bounds.y == 22);
  CHECK(transparent_stack.bounds.width == 1 && transparent_stack.bounds.height == 1);
  CHECK(std::equal(visible, visible + 4, transparent_stack.pixels.data().begin()));
}

void smart_filter_entry_blending_matches_photoshop_baked_pixels() {
  const auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-model.psd"));
  const auto& source_layer =
      require_layer_named(document, "_Shared source without filters");
  const auto& expected_layer =
      require_layer_named(document, "Gaussian Multiply 37 percent");
  const auto& multiply_stack =
      require_smart_filter_stack(document, expected_layer.name());
  CHECK(multiply_stack.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(multiply_stack.entries.size() == 1U);
  CHECK(multiply_stack.entries.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(multiply_stack.entries.front().opacity - 0.37) < 1e-9);

  // Every layer in this Photoshop fixture shares a 24x22 placed source canvas.
  // The unfiltered layer record is alpha-trimmed, so restore that crop into its
  // transparent placed-canvas rectangle before executing the native stack.
  const patchy::Rect placed_bounds{8, 9, 24, 22};
  patchy::PixelBuffer placed_pixels(placed_bounds.width, placed_bounds.height,
                                    patchy::PixelFormat::rgba8());
  placed_pixels.clear(0);
  const auto source_bounds = source_layer.bounds();
  CHECK(source_bounds.x >= placed_bounds.x && source_bounds.y >= placed_bounds.y);
  CHECK(source_bounds.x + source_bounds.width <=
        placed_bounds.x + placed_bounds.width);
  CHECK(source_bounds.y + source_bounds.height <=
        placed_bounds.y + placed_bounds.height);
  for (std::int32_t y = 0; y < source_bounds.height; ++y) {
    for (std::int32_t x = 0; x < source_bounds.width; ++x) {
      const auto destination_x = source_bounds.x - placed_bounds.x + x;
      const auto destination_y = source_bounds.y - placed_bounds.y + y;
      std::copy_n(source_layer.pixels().pixel(x, y), 4,
                  placed_pixels.pixel(destination_x, destination_y));
    }
  }

  const auto actual = patchy::render_smart_filter_stack(
      placed_pixels, placed_bounds, multiply_stack);
  const auto expected_bounds = expected_layer.bounds();
  CHECK(actual.bounds.x == expected_bounds.x &&
        actual.bounds.y == expected_bounds.y);
  CHECK(actual.bounds.width == expected_bounds.width &&
        actual.bounds.height == expected_bounds.height);
  int maximum_visible_rgb_delta = 0;
  int maximum_alpha_delta = 0;
  std::size_t compared_visible_pixels = 0;
  for (std::int32_t y = 0; y < expected_bounds.height; ++y) {
    for (std::int32_t x = 0; x < expected_bounds.width; ++x) {
      const auto* expected = expected_layer.pixels().pixel(x, y);
      const auto* rendered = actual.pixels.pixel(x, y);
      maximum_alpha_delta = std::max(
          maximum_alpha_delta,
          std::abs(static_cast<int>(rendered[3]) - static_cast<int>(expected[3])));
      if (rendered[3] == 0U && expected[3] == 0U) {
        continue;
      }
      ++compared_visible_pixels;
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        maximum_visible_rgb_delta = std::max(
            maximum_visible_rgb_delta,
            std::abs(static_cast<int>(rendered[channel]) -
                     static_cast<int>(expected[channel])));
      }
    }
  }
  CHECK(compared_visible_pixels > 0U);
  CHECK(maximum_visible_rgb_delta <= 1);
  CHECK(maximum_alpha_delta <= 1);

  // Normal/100% is the filter replacement operation, not a source-over blend.
  // Keep its calibrated radius-1.5 impulse byte-exact while non-default entry
  // blending follows Photoshop's source-over rule above.
  auto impulse = solid_rgba(9, 1, 0, 0, 0, 0);
  auto* center = impulse.pixel(4, 0);
  center[0] = 255;
  center[1] = 255;
  center[2] = 255;
  center[3] = 255;
  const auto normal_stack = test_gaussian_smart_filter_stack(1.5);
  CHECK(normal_stack.entries.front().blend_mode == patchy::BlendMode::Normal);
  CHECK(normal_stack.entries.front().opacity == 1.0);
  const auto normal = patchy::render_smart_filter_stack(
      impulse, patchy::Rect{0, 0, 9, 1}, normal_stack);
  CHECK(normal.bounds.x == 0 && normal.bounds.y == 0);
  CHECK(normal.bounds.width == 9 && normal.bounds.height == 1);
  constexpr std::array<std::uint8_t, 9> kExpectedAlpha{
      2, 10, 28, 52, 72, 52, 28, 10, 2};
  for (std::int32_t x = 0; x < normal.bounds.width; ++x) {
    const auto* pixel = normal.pixels.pixel(x, 0);
    CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
    CHECK(pixel[3] == kExpectedAlpha[static_cast<std::size_t>(x)]);
  }
}

void smart_filter_unsharp_mask_matches_photoshop_scaled_threshold() {
  auto impulse = solid_rgba(65, 1, 128, 128, 128, 255);
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    impulse.pixel(32, 0)[channel] = 255U;
  }
  const auto rendered = patchy::render_photoshop_unsharp_mask(
      impulse, patchy::Rect::from_size(65, 1), 100.0, 2.5, 0);
  constexpr std::array<std::uint8_t, 13> kPhotoshopImpulse{
      127, 126, 122, 118, 113, 109, 255, 109, 113, 118, 122, 126, 127};
  for (std::int32_t x = 0; x < 65; ++x) {
    const auto local = x - 26;
    const auto expected =
        local >= 0 &&
                local < static_cast<std::int32_t>(kPhotoshopImpulse.size())
            ? kPhotoshopImpulse[static_cast<std::size_t>(local)]
            : 128U;
    const auto *pixel = rendered.pixels.pixel(x, 0);
    CHECK(pixel[0] == expected && pixel[1] == expected &&
          pixel[2] == expected && pixel[3] == 255U);
  }

  auto threshold_source = solid_rgba(65, 1, 128, 128, 128, 255);
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    threshold_source.pixel(31, 0)[channel] = 134U;
    threshold_source.pixel(32, 0)[channel] = 142U;
    threshold_source.pixel(33, 0)[channel] = 134U;
  }
  const auto thresholded = patchy::render_smart_filter_stack(
      threshold_source, patchy::Rect::from_size(65, 1),
      test_unsharp_mask_smart_filter_stack(175.0, 2.5, 7));
  CHECK(thresholded.bounds.x == 0 && thresholded.bounds.y == 0 &&
        thresholded.bounds.width == 65 && thresholded.bounds.height == 1);
  for (std::int32_t x = 0; x < 65; ++x) {
    const auto expected = x == 31 || x == 33 ? 134U : (x == 32 ? 152U : 128U);
    const auto *pixel = thresholded.pixels.pixel(x, 0);
    CHECK(pixel[0] == expected && pixel[1] == expected &&
          pixel[2] == expected && pixel[3] == 255U);
  }
}

void smart_filter_motion_blur_matches_photoshop_axis_kernel_and_growth() {
  auto impulse = solid_rgba(65, 1, 0, 0, 0, 255);
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    impulse.pixel(32, 0)[channel] = 255U;
  }
  const auto rendered = patchy::render_photoshop_motion_blur(
      impulse, patchy::Rect::from_size(65, 1), 0, 12);
  for (std::int32_t x = 0; x < 65; ++x) {
    const auto expected = x >= 26 && x <= 38 ? 20U : 0U;
    const auto *pixel = rendered.pixels.pixel(x, 0);
    CHECK(pixel[0] == expected && pixel[1] == expected &&
          pixel[2] == expected && pixel[3] == 255U);
  }

  const auto short_blur = patchy::render_photoshop_motion_blur(
      impulse, patchy::Rect::from_size(65, 1), 0, 1);
  for (std::int32_t x = 0; x < 65; ++x) {
    const auto expected = x == 31 || x == 32 ? 128U : 0U;
    CHECK(short_blur.pixels.pixel(x, 0)[0] == expected);
  }

  auto transparent_point = solid_rgba(1, 1, 255, 255, 255, 255);
  const auto grown = patchy::render_smart_filter_stack(
      transparent_point, patchy::Rect{32, 0, 1, 1},
      patchy::Rect::from_size(65, 1),
      test_motion_blur_smart_filter_stack(0, 12));
  CHECK(grown.bounds.x == 26 && grown.bounds.y == 0 &&
        grown.bounds.width == 13 && grown.bounds.height == 1);
  for (std::int32_t x = 0; x < grown.bounds.width; ++x) {
    const auto *pixel = grown.pixels.pixel(x, 0);
    CHECK(pixel[0] == 255U && pixel[1] == 255U && pixel[2] == 255U &&
          pixel[3] == 20U);
  }
}

void smart_filter_layer_model_revisions_are_explicit() {
  patchy::Layer layer(1, "Filtered", patchy::PixelBuffer(2, 2, patchy::PixelFormat::rgba8()));
  patchy::SmartFilterStack stack;
  stack.enabled = true;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  patchy::SmartFilterEntry entry;
  entry.kind = patchy::SmartFilterKind::GaussianBlur;
  entry.native_class_id = "GsnB";
  entry.native_filter_id = 0x47736e42U;
  entry.parameters = patchy::GaussianBlurSmartFilter{2.5};
  stack.entries.push_back(std::move(entry));

  const auto initial_render = layer.render_revision();
  const auto initial_content = layer.content_revision();
  const auto initial_pixels = layer.pixel_revision();
  layer.set_smart_filter_stack(stack);
  CHECK(layer.render_revision() > initial_render);
  CHECK(layer.content_revision() > initial_content);
  CHECK(layer.pixel_revision() == initial_pixels);
  const auto render_after_set = layer.render_revision();
  const auto content_after_set = layer.content_revision();
  const auto* stored = std::as_const(layer).smart_filter_stack();
  CHECK(stored != nullptr);
  CHECK(stored->entries.size() == 1U);
  CHECK(layer.render_revision() == render_after_set);
  CHECK(layer.content_revision() == content_after_set);
  CHECK(layer.pixel_revision() == initial_pixels);

  layer.set_pixels(patchy::PixelBuffer(2, 2, patchy::PixelFormat::rgba8()));
  CHECK(layer.smart_filter_stack() != nullptr);
  const auto pixel_after_replace = layer.pixel_revision();
  layer.clear_smart_filter_stack();
  CHECK(layer.smart_filter_stack() == nullptr);
  CHECK(layer.pixel_revision() == pixel_after_replace);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_pixels_tests() {
  return {
      {"smart_filter_layer_model_revisions_are_explicit",
       smart_filter_layer_model_revisions_are_explicit},
      {"smart_filter_gaussian_matches_photoshop_calibrated_kernels",
       smart_filter_gaussian_matches_photoshop_calibrated_kernels},
      {"smart_filter_gaussian_repeats_canvas_edges_and_blurs_premultiplied",
       smart_filter_gaussian_repeats_canvas_edges_and_blurs_premultiplied},
      {"smart_filter_high_pass_matches_photoshop_formula_and_preserves_hidden_rgb",
       smart_filter_high_pass_matches_photoshop_formula_and_preserves_hidden_rgb},
      {"smart_filter_median_matches_square_clamped_channels_and_native_paths",
       smart_filter_median_matches_square_clamped_channels_and_native_paths},
      {"smart_filter_dust_and_scratches_matches_photoshop_threshold_and_native_paths",
       smart_filter_dust_and_scratches_matches_photoshop_threshold_and_native_paths},
      {"smart_filter_surface_blur_matches_photoshop_weighting_bounds_and_native_paths",
       smart_filter_surface_blur_matches_photoshop_weighting_bounds_and_native_paths},
      {"surface_blur_direct_and_level_paths_match_reference",
       surface_blur_direct_and_level_paths_match_reference},
      {"tilt_shift_blur_is_deterministic_respects_focus_geometry_and_bounds",
       tilt_shift_blur_is_deterministic_respects_focus_geometry_and_bounds},
      {"smart_filter_shared_mask_and_disable_states_are_applied_once",
       smart_filter_shared_mask_and_disable_states_are_applied_once},
      {"smart_filter_entry_blending_matches_photoshop_baked_pixels",
       smart_filter_entry_blending_matches_photoshop_baked_pixels},
      {"smart_filter_unsharp_mask_matches_photoshop_scaled_threshold",
       smart_filter_unsharp_mask_matches_photoshop_scaled_threshold},
      {"smart_filter_motion_blur_matches_photoshop_axis_kernel_and_growth",
       smart_filter_motion_blur_matches_photoshop_axis_kernel_and_growth},
  };
}
