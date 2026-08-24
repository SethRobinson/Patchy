#include "core/image_trace.hpp"
#include "core/mask_outline.hpp"
#include "core/path_fit.hpp"
#include "core/vector_shape.hpp"

#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

namespace {

using patchy::ImageTraceOptions;
using patchy::ImageTraceResult;
using patchy::PathCombineOp;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::RgbColor;

PixelBuffer solid_image(std::int32_t width, std::int32_t height, RgbColor color, std::uint8_t alpha = 255) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = alpha;
    }
  }
  return pixels;
}

void fill_rect(PixelBuffer& pixels, std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height,
               RgbColor color, std::uint8_t alpha = 255) {
  for (std::int32_t y = top; y < top + height; ++y) {
    for (std::int32_t x = left; x < left + width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = color.red;
      px[1] = color.green;
      px[2] = color.blue;
      px[3] = alpha;
    }
  }
}

void fill_disc(PixelBuffer& pixels, double cx, double cy, double radius, RgbColor color, std::uint8_t alpha = 255) {
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const double dx = x + 0.5 - cx;
      const double dy = y + 0.5 - cy;
      if (dx * dx + dy * dy <= radius * radius) {
        auto* px = pixels.pixel(x, y);
        px[0] = color.red;
        px[1] = color.green;
        px[2] = color.blue;
        px[3] = alpha;
      }
    }
  }
}

constexpr RgbColor kBlack{0, 0, 0};
constexpr RgbColor kWhite{255, 255, 255};
constexpr RgbColor kRed{220, 30, 30};
constexpr RgbColor kBlue{30, 40, 220};

ImageTraceOptions color_options(ImageTraceOptions::Method method = ImageTraceOptions::Method::Abutting) {
  ImageTraceOptions options;
  options.mode = ImageTraceOptions::Mode::Color;
  options.colors = 8;
  options.noise = 1;
  options.method = method;
  return options;
}

const patchy::ImageTraceLayer* layer_with_color(const ImageTraceResult& result, RgbColor color) {
  for (const auto& layer : result.layers) {
    if (layer.color == color) {
      return &layer;
    }
  }
  return nullptr;
}

std::size_t count_ops(const patchy::VectorPath& path, PathCombineOp op) {
  std::size_t count = 0;
  for (const auto& subpath : path.subpaths) {
    if (subpath.op == op) {
      ++count;
    }
  }
  return count;
}

bool anchors_are_straight(const patchy::PathSubpath& subpath) {
  for (const auto& anchor : subpath.anchors) {
    if (anchor.in_x != anchor.anchor_x || anchor.in_y != anchor.anchor_y || anchor.out_x != anchor.anchor_x ||
        anchor.out_y != anchor.anchor_y) {
      return false;
    }
  }
  return true;
}

// --- mask_outline -------------------------------------------------------------

void mask_outline_traces_square_and_hole() {
  // A 6x6 block with a 2x2 hole: one clockwise outer loop of four corners,
  // one counterclockwise hole loop of four corners.
  constexpr int kWidth = 8;
  constexpr int kHeight = 8;
  const std::size_t stride = kWidth + 2;
  std::vector<std::uint8_t> mask(stride * (kHeight + 2), 0);
  for (int y = 1; y < 7; ++y) {
    for (int x = 1; x < 7; ++x) {
      const bool hole = x >= 3 && x < 5 && y >= 3 && y < 5;
      mask[(y + 1) * stride + (x + 1)] = hole ? 0 : 1;
    }
  }
  const auto loops = patchy::trace_mask_outlines(mask.data(), kWidth, kHeight, stride);
  CHECK(loops.size() == 2);
  CHECK(loops[0].points.size() == 4);
  CHECK(patchy::loop_signed_area(loops[0].points) > 0.0);
  CHECK((loops[0].points[0] == patchy::FitPoint{1.0, 1.0}));
  CHECK(loops[0].bounds.x == 1 && loops[0].bounds.y == 1 && loops[0].bounds.width == 6 &&
        loops[0].bounds.height == 6);
  CHECK(loops[1].points.size() == 4);
  CHECK(patchy::loop_signed_area(loops[1].points) < 0.0);
  CHECK((loops[1].points[0] == patchy::FitPoint{3.0, 3.0}));
  CHECK(loops[1].bounds.width == 2 && loops[1].bounds.height == 2);
}

// --- path_fit options -----------------------------------------------------------

void path_fit_options_corner_angle_and_snap() {
  // A 30-degree kink at (40, 0): a corner under a 20-degree threshold, a
  // smooth bend under a 40-degree one.
  const std::vector<patchy::FitPoint> kinked{{0.0, 0.0}, {40.0, 0.0}, {80.0, 23.0}, {80.0, 60.0}, {0.0, 60.0}};
  const auto corner_anchors_at = [](const patchy::PathSubpath& subpath, double x, double y) {
    std::size_t count = 0;
    for (const auto& anchor : subpath.anchors) {
      if (anchor.anchor_x == x && anchor.anchor_y == y && !anchor.smooth) {
        ++count;
      }
    }
    return count;
  };
  patchy::PathFitOptions sharp;
  sharp.tolerance = 0.5;
  sharp.corner_angle_degrees = 20.0;
  const auto with_corners = patchy::fit_closed_loop(kinked, sharp);
  CHECK(with_corners.anchors.size() == 5);
  CHECK(corner_anchors_at(with_corners, 40.0, 0.0) == 1);
  patchy::PathFitOptions rounded = sharp;
  rounded.corner_angle_degrees = 40.0;
  const auto smoothed = patchy::fit_closed_loop(kinked, rounded);
  CHECK(corner_anchors_at(smoothed, 40.0, 0.0) == 0);
  CHECK(corner_anchors_at(smoothed, 80.0, 23.0) == 1);

  // A rectangle whose top edge carries a sub-tolerance bump: the bump is not
  // a significant vertex, but the raw run still fits to a faintly curved
  // cubic. Snap Curves To Lines collapses it back to a straight segment.
  const std::vector<patchy::FitPoint> bumped{{0.0, 0.0}, {50.0, 0.4}, {100.0, 0.0}, {100.0, 40.0}, {0.0, 40.0}};
  patchy::PathFitOptions curved;
  curved.tolerance = 1.0;
  const auto with_curve = patchy::fit_closed_loop(bumped, curved);
  CHECK(with_curve.anchors.size() == 4);
  CHECK(!anchors_are_straight(with_curve));
  patchy::PathFitOptions snapped = curved;
  snapped.snap_curves_to_lines = true;
  const auto straight = patchy::fit_closed_loop(bumped, snapped);
  CHECK(straight.anchors.size() == 4);
  CHECK(anchors_are_straight(straight));
  // The two-argument overload still reproduces the default-option result.
  CHECK(patchy::fit_closed_loop(kinked, 2.0) == patchy::fit_closed_loop(kinked, patchy::PathFitOptions{}));
}

// --- trace_image --------------------------------------------------------------

void image_trace_two_color_square_yields_one_layer_per_color() {
  auto pixels = solid_image(64, 64, kWhite);
  fill_rect(pixels, 16, 16, 32, 32, kRed);
  const auto result = patchy::trace_image(pixels, color_options());
  CHECK(result.palette_size == 2);
  CHECK(result.layers.size() == 2);
  // Largest region first (the white background), then the red square.
  CHECK(result.layers[0].color == kWhite);
  CHECK(result.layers[1].color == kRed);
  const auto* red = layer_with_color(result, kRed);
  CHECK(red != nullptr);
  CHECK(red->area == 32 * 32);
  CHECK(red->path.subpaths.size() == 1);
  CHECK(red->path.subpaths[0].op == PathCombineOp::Add);
  CHECK(red->path.subpaths[0].anchors.size() == 4);
  CHECK(anchors_are_straight(red->path.subpaths[0]));
  const auto* white = layer_with_color(result, kWhite);
  CHECK(white != nullptr);
  // The background is a square with a square hole: Add outer, Subtract hole.
  CHECK(white->path.subpaths.size() == 2);
  CHECK(white->path.subpaths[0].op == PathCombineOp::Add);
  CHECK(white->path.subpaths[1].op == PathCombineOp::Subtract);
  CHECK(white->path.subpaths[0].shape_group != white->path.subpaths[1].shape_group);
  CHECK(result.anchor_count == 12);
}

void image_trace_ring_produces_subtract_hole_group() {
  // A black ring on transparency: Abutting keeps the hole as a Subtract
  // group, and Overlapping keeps it too because nothing sits inside it.
  auto pixels = solid_image(80, 80, kWhite, 0);
  fill_disc(pixels, 40.0, 40.0, 30.0, kBlack);
  fill_disc(pixels, 40.0, 40.0, 12.0, kWhite, 0);
  for (const auto method : {ImageTraceOptions::Method::Abutting, ImageTraceOptions::Method::Overlapping}) {
    const auto result = patchy::trace_image(pixels, color_options(method));
    CHECK(result.layers.size() == 1);
    const auto& ring = result.layers[0];
    CHECK(ring.color == kBlack);
    CHECK(ring.depth == 0);
    CHECK(count_ops(ring.path, PathCombineOp::Add) == 1);
    CHECK(count_ops(ring.path, PathCombineOp::Subtract) == 1);
    // Circles fit to smooth anchors, far fewer than the traced staircase.
    for (const auto& subpath : ring.path.subpaths) {
      CHECK(subpath.anchors.size() >= 2);
      CHECK(subpath.anchors.size() <= 16);
    }
  }
}

void image_trace_overlapping_nests_by_depth() {
  // Blue disc inside a black ring on white. Abutting: three cutouts with the
  // ring and the background carrying holes. Overlapping: the background and
  // the ring lose their holes and the enclosed regions stack above them.
  auto pixels = solid_image(96, 96, kWhite);
  fill_disc(pixels, 48.0, 48.0, 36.0, kBlack);
  fill_disc(pixels, 48.0, 48.0, 16.0, kBlue);
  const auto abutting = patchy::trace_image(pixels, color_options(ImageTraceOptions::Method::Abutting));
  CHECK(abutting.layers.size() == 3);
  for (const auto& layer : abutting.layers) {
    CHECK(layer.depth == 0);
  }
  CHECK(count_ops(layer_with_color(abutting, kBlack)->path, PathCombineOp::Subtract) == 1);
  CHECK(count_ops(layer_with_color(abutting, kWhite)->path, PathCombineOp::Subtract) == 1);
  CHECK(count_ops(layer_with_color(abutting, kBlue)->path, PathCombineOp::Subtract) == 0);

  const auto overlapping = patchy::trace_image(pixels, color_options(ImageTraceOptions::Method::Overlapping));
  CHECK(overlapping.layers.size() == 3);
  CHECK(overlapping.layers[0].color == kWhite);
  CHECK(overlapping.layers[0].depth == 0);
  CHECK(overlapping.layers[1].color == kBlack);
  CHECK(overlapping.layers[1].depth == 1);
  CHECK(overlapping.layers[2].color == kBlue);
  CHECK(overlapping.layers[2].depth == 2);
  for (const auto& layer : overlapping.layers) {
    CHECK(count_ops(layer.path, PathCombineOp::Subtract) == 0);
    CHECK(count_ops(layer.path, PathCombineOp::Add) == 1);
  }
  // Rendering the stack reproduces the source: the holes are painted over
  // by the layers above them.
  const auto rendered = patchy::render_image_trace(overlapping, pixels.width(), pixels.height());
  std::int64_t mismatches = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* expected = pixels.pixel(x, y);
      const auto* actual = rendered.pixel(x, y);
      if (std::abs(expected[0] - actual[0]) > 8 || std::abs(expected[1] - actual[1]) > 8 ||
          std::abs(expected[2] - actual[2]) > 8 || actual[3] < 250) {
        ++mismatches;
      }
    }
  }
  // Only the rims of the two circles may differ: the 1 px fit tolerance at
  // Paths 50 allows about one pixel along the ~330 px of combined perimeter
  // (measured 307 on MSVC), so budget two.
  CHECK(mismatches < 660);
}

void image_trace_noise_filter_removes_speckles() {
  auto pixels = solid_image(48, 48, kWhite);
  fill_rect(pixels, 8, 8, 24, 24, kRed);
  fill_rect(pixels, 40, 40, 2, 2, kBlue);   // a 4-pixel speck on white
  fill_rect(pixels, 12, 12, 1, 1, kWhite);  // a 1-pixel pinhole in the red square
  auto options = color_options();
  options.noise = 1;
  const auto kept = patchy::trace_image(pixels, options);
  CHECK(kept.layers.size() == 3);
  CHECK(layer_with_color(kept, kBlue) != nullptr);
  CHECK(count_ops(layer_with_color(kept, kRed)->path, PathCombineOp::Subtract) == 1);

  options.noise = 5;
  const auto cleaned = patchy::trace_image(pixels, options);
  CHECK(cleaned.layers.size() == 2);
  CHECK(layer_with_color(cleaned, kBlue) == nullptr);
  // The pinhole merged into the red square, which is a plain rectangle again.
  const auto* red = layer_with_color(cleaned, kRed);
  CHECK(red != nullptr);
  CHECK(red->path.subpaths.size() == 1);
  CHECK(red->area == 24 * 24);
}

void image_trace_ignore_white_drops_background() {
  auto pixels = solid_image(40, 40, kWhite);
  fill_rect(pixels, 10, 10, 20, 20, kBlack);
  auto options = color_options();
  options.ignore_white = true;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.layers.size() == 1);
  CHECK(result.layers[0].color == kBlack);
  // With the white treated as transparent the Overlapping method still
  // leaves a ring's hole open.
  auto ring = solid_image(80, 80, kWhite);
  fill_disc(ring, 40.0, 40.0, 30.0, kBlack);
  fill_disc(ring, 40.0, 40.0, 12.0, kWhite);
  options.method = ImageTraceOptions::Method::Overlapping;
  const auto traced = patchy::trace_image(ring, options);
  CHECK(traced.layers.size() == 1);
  CHECK(count_ops(traced.layers[0].path, PathCombineOp::Subtract) == 1);
}

void image_trace_black_and_white_threshold() {
  // Three gray bands; the threshold decides where the middle one lands.
  auto pixels = solid_image(60, 30, kWhite);
  fill_rect(pixels, 0, 0, 20, 30, kBlack);
  fill_rect(pixels, 20, 0, 20, 30, RgbColor{100, 100, 100});
  ImageTraceOptions options;
  options.mode = ImageTraceOptions::Mode::BlackAndWhite;
  options.noise = 1;
  options.threshold = 128;
  const auto dark = patchy::trace_image(pixels, options);
  CHECK(dark.palette_size == 2);
  CHECK(dark.layers.size() == 2);
  CHECK(layer_with_color(dark, kBlack)->area == 40 * 30);
  CHECK(layer_with_color(dark, kWhite)->area == 20 * 30);
  options.threshold = 90;
  const auto light = patchy::trace_image(pixels, options);
  CHECK(layer_with_color(light, kBlack)->area == 20 * 30);
  CHECK(layer_with_color(light, kWhite)->area == 40 * 30);
  // Grayscale mode with two grays splits the same way through median cut.
  options.mode = ImageTraceOptions::Mode::Grayscale;
  options.colors = 3;
  const auto grays = patchy::trace_image(pixels, options);
  CHECK(grays.layers.size() == 3);
}

void image_trace_rasterized_result_matches_source_coverage() {
  // A synthetic logo: two discs, a bar, and a hole. Rasterizing the traced
  // layers must reproduce the quantized source almost exactly (IoU per color).
  auto pixels = solid_image(160, 120, kWhite);
  fill_disc(pixels, 50.0, 60.0, 38.0, kRed);
  fill_disc(pixels, 110.0, 60.0, 38.0, kBlue);
  fill_rect(pixels, 40, 100, 80, 12, kBlack);
  fill_disc(pixels, 50.0, 60.0, 10.0, kWhite);
  auto options = color_options();
  options.paths = 80;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.layers.size() == 4);
  const auto rendered = patchy::render_image_trace(result, pixels.width(), pixels.height());
  for (const auto color : {kWhite, kRed, kBlue, kBlack}) {
    std::int64_t intersection = 0;
    std::int64_t union_count = 0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* expected = pixels.pixel(x, y);
        const auto* actual = rendered.pixel(x, y);
        const bool in_source = expected[0] == color.red && expected[1] == color.green && expected[2] == color.blue;
        const bool in_result = actual[3] >= 128 && std::abs(actual[0] - color.red) <= 32 &&
                               std::abs(actual[1] - color.green) <= 32 && std::abs(actual[2] - color.blue) <= 32;
        intersection += (in_source && in_result) ? 1 : 0;
        union_count += (in_source || in_result) ? 1 : 0;
      }
    }
    CHECK(union_count > 0);
    CHECK(static_cast<double>(intersection) / static_cast<double>(union_count) >= 0.95);
  }
}

void image_trace_is_deterministic_and_cancellable() {
  auto pixels = solid_image(120, 90, RgbColor{240, 240, 240});
  fill_disc(pixels, 40.0, 45.0, 30.0, kRed);
  fill_disc(pixels, 80.0, 45.0, 30.0, kBlue);
  fill_rect(pixels, 10, 70, 100, 10, kBlack);
  auto options = color_options(ImageTraceOptions::Method::Overlapping);
  options.colors = 6;
  options.noise = 4;
  const auto first = patchy::trace_image(pixels, options);
  const auto second = patchy::trace_image(pixels, options);
  CHECK(first.layers.size() == second.layers.size());
  for (std::size_t i = 0; i < first.layers.size(); ++i) {
    CHECK(first.layers[i].color == second.layers[i].color);
    CHECK(patchy::serialize_vector_path(first.layers[i].path) ==
          patchy::serialize_vector_path(second.layers[i].path));
  }
  const auto cancelled = patchy::trace_image(pixels, options, [] { return true; });
  CHECK(cancelled.layers.empty());
  // Parameter mappings keep their documented endpoints.
  CHECK(std::abs(patchy::image_trace_fit_tolerance(0) - 4.0) < 1e-9);
  CHECK(std::abs(patchy::image_trace_fit_tolerance(50) - 1.0) < 1e-9);
  CHECK(std::abs(patchy::image_trace_fit_tolerance(100) - 0.25) < 1e-9);
  CHECK(std::abs(patchy::image_trace_corner_angle(0) - 120.0) < 1e-9);
  CHECK(std::abs(patchy::image_trace_corner_angle(100) - 30.0) < 1e-9);
  // Unsupported channel counts and transparent images trace to nothing.
  CHECK(patchy::trace_image(PixelBuffer(8, 8, PixelFormat{patchy::ColorMode::RGB, patchy::BitDepth::UInt8, 5}),
                            options)
            .layers.empty());
  CHECK(patchy::trace_image(solid_image(8, 8, kRed, 0), options).layers.empty());
}

// 16-bit and float buffers convert to 8-bit (value/257 rounded; floats
// clamped) and trace like their 8-bit equivalents.
void image_trace_converts_16_bit_and_float_buffers() {
  ImageTraceOptions options;
  options.mode = ImageTraceOptions::Mode::Color;
  options.colors = 4;
  options.noise = 1;
  const auto check_halves = [&](const PixelBuffer& deep) {
    const auto traced = patchy::trace_image(deep, options);
    CHECK(traced.layers.size() == 2);
    bool saw_red = false;
    bool saw_blue = false;
    for (const auto& layer : traced.layers) {
      saw_red = saw_red || (layer.color.red == 255 && layer.color.green == 0 && layer.color.blue == 0);
      saw_blue = saw_blue || (layer.color.red == 0 && layer.color.green == 0 && layer.color.blue == 255);
    }
    CHECK(saw_red);
    CHECK(saw_blue);
  };

  PixelBuffer deep16(16, 16, PixelFormat::rgb16());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const std::uint16_t values[3] = {static_cast<std::uint16_t>(x < 8 ? 65535 : 0), 0,
                                       static_cast<std::uint16_t>(x < 8 ? 0 : 65535)};
      std::memcpy(deep16.pixel(x, y), values, sizeof(values));
    }
  }
  check_halves(deep16);

  PixelBuffer deep32(16, 16, PixelFormat::rgbf32());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      const float values[3] = {x < 8 ? 1.5F : -0.5F, 0.0F, x < 8 ? 0.0F : 1.0F};  // clamps
      std::memcpy(deep32.pixel(x, y), values, sizeof(values));
    }
  }
  check_halves(deep32);
}

}  // namespace

std::vector<patchy::test::TestCase> image_trace_tests() {
  return {
      {"mask_outline_traces_square_and_hole", mask_outline_traces_square_and_hole},
      {"path_fit_options_corner_angle_and_snap", path_fit_options_corner_angle_and_snap},
      {"image_trace_two_color_square_yields_one_layer_per_color",
       image_trace_two_color_square_yields_one_layer_per_color},
      {"image_trace_ring_produces_subtract_hole_group", image_trace_ring_produces_subtract_hole_group},
      {"image_trace_overlapping_nests_by_depth", image_trace_overlapping_nests_by_depth},
      {"image_trace_noise_filter_removes_speckles", image_trace_noise_filter_removes_speckles},
      {"image_trace_ignore_white_drops_background", image_trace_ignore_white_drops_background},
      {"image_trace_black_and_white_threshold", image_trace_black_and_white_threshold},
      {"image_trace_rasterized_result_matches_source_coverage",
       image_trace_rasterized_result_matches_source_coverage},
      {"image_trace_is_deterministic_and_cancellable", image_trace_is_deterministic_and_cancellable},
      {"image_trace_converts_16_bit_and_float_buffers", image_trace_converts_16_bit_and_float_buffers},
  };
}
