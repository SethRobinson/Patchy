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
  // Smoothing and an anchor budget stay deterministic and cancellable too.
  options.smoothing = 2;
  options.max_anchors = 400;
  const auto smoothed_a = patchy::trace_image(pixels, options);
  const auto smoothed_b = patchy::trace_image(pixels, options);
  CHECK(smoothed_a.layers.size() == smoothed_b.layers.size());
  for (std::size_t i = 0; i < smoothed_a.layers.size(); ++i) {
    CHECK(patchy::serialize_vector_path(smoothed_a.layers[i].path) ==
          patchy::serialize_vector_path(smoothed_b.layers[i].path));
  }
  CHECK(patchy::trace_image(pixels, options, [] { return true; }).layers.empty());
  options.smoothing = 0;
  options.max_anchors = 0;
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

// A selection-masked copy (alpha 0 outside the "selection") traced with the
// whole image as palette_source picks the whole image's colors, where the old
// selection-scoped palette gave near-identical grain shades their own entries
// and shattered flat areas into speckle.
void image_trace_palette_source_uses_whole_image_colors() {
  const RgbColor shade{224, 30, 30};  // 4 off kRed: quantization grain
  auto full = solid_image(96, 64, kRed);
  fill_rect(full, 48, 0, 24, 64, kBlue);
  fill_rect(full, 72, 0, 24, 64, kWhite);
  // A 1 px checkerboard of kRed/shade inside the red field: the "selection".
  for (std::int32_t y = 8; y < 40; ++y) {
    for (std::int32_t x = 8; x < 40; ++x) {
      if ((x + y) % 2 == 0) {
        auto* px = full.pixel(x, y);
        px[0] = shade.red;
        px[1] = shade.green;
        px[2] = shade.blue;
      }
    }
  }
  auto masked = full;
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t x = 0; x < 96; ++x) {
      if (x < 8 || x >= 40 || y < 8 || y >= 40) {
        masked.pixel(x, y)[3] = 0;
      }
    }
  }
  auto options = color_options();
  options.colors = 3;

  const auto whole = patchy::trace_image(full, options);
  CHECK(whole.palette_size == 3);
  CHECK(whole.layers.size() == 3);  // 4 unique colors cluster; the grain merges into red
  const auto in_whole = [&](RgbColor color) { return layer_with_color(whole, color) != nullptr; };

  const auto with_source = patchy::trace_image(masked, options, {}, 0, &full);
  CHECK(with_source.layers.size() == 1);  // the checkerboard collapses into one flat region
  for (const auto& layer : with_source.layers) {
    CHECK(in_whole(layer.color));
  }

  const auto without_source = patchy::trace_image(masked, options);
  bool has_foreign_color = false;
  for (const auto& layer : without_source.layers) {
    has_foreign_color = has_foreign_color || !in_whole(layer.color);
  }
  CHECK(has_foreign_color);  // the old behavior promoted the grain shade to a palette entry
  CHECK(without_source.anchor_count > with_source.anchor_count * 20);
}

// nullptr, the traced buffer itself, and an identical copy as palette_source
// are all byte-for-byte no-ops, with and without smoothing.
void image_trace_palette_source_null_and_self_are_no_ops() {
  auto pixels = solid_image(48, 32, kRed);
  fill_rect(pixels, 24, 0, 24, 32, kBlue);
  fill_disc(pixels, 24.0, 16.0, 8.0, kWhite);
  const auto same_result = [](const ImageTraceResult& a, const ImageTraceResult& b) {
    CHECK(a.palette_size == b.palette_size);
    CHECK(a.anchor_count == b.anchor_count);
    CHECK(a.layers.size() == b.layers.size());
    for (std::size_t i = 0; i < a.layers.size() && i < b.layers.size(); ++i) {
      CHECK(a.layers[i].color == b.layers[i].color);
      CHECK(a.layers[i].area == b.layers[i].area);
      CHECK(a.layers[i].depth == b.layers[i].depth);
      CHECK(patchy::serialize_vector_path(a.layers[i].path) == patchy::serialize_vector_path(b.layers[i].path));
    }
  };
  const auto copy = pixels;
  for (const auto mode : {ImageTraceOptions::Mode::Color, ImageTraceOptions::Mode::Grayscale}) {
    for (const int smoothing : {0, 2}) {
      auto options = color_options();
      options.mode = mode;
      options.colors = 4;
      options.smoothing = smoothing;
      const auto baseline = patchy::trace_image(pixels, options);
      same_result(baseline, patchy::trace_image(pixels, options, {}, 0, &pixels));
      same_result(baseline, patchy::trace_image(pixels, options, {}, 0, &copy));
    }
  }
}

void image_trace_palette_source_grayscale_histogram_from_source() {
  // Bands of gray 0, 100, 250; two levels place at 50 and 250. A selection
  // over the right two bands re-solved alone would place a level at 100; the
  // whole-image histogram keeps the whole trace's levels.
  auto full = solid_image(60, 30, kBlack);
  fill_rect(full, 20, 0, 20, 30, RgbColor{100, 100, 100});
  fill_rect(full, 40, 0, 20, 30, RgbColor{250, 250, 250});
  auto masked = full;
  fill_rect(masked, 0, 0, 20, 30, kBlack, 0);
  ImageTraceOptions options;
  options.mode = ImageTraceOptions::Mode::Grayscale;
  options.colors = 2;
  options.noise = 1;
  const auto whole = patchy::trace_image(full, options);
  const auto in_whole = [&](RgbColor color) { return layer_with_color(whole, color) != nullptr; };
  const auto with_source = patchy::trace_image(masked, options, {}, 0, &full);
  CHECK(!with_source.layers.empty());
  for (const auto& layer : with_source.layers) {
    CHECK(in_whole(layer.color));
  }
  const auto without_source = patchy::trace_image(masked, options);
  bool has_foreign_gray = false;
  for (const auto& layer : without_source.layers) {
    has_foreign_gray = has_foreign_gray || !in_whole(layer.color);
  }
  CHECK(has_foreign_gray);
}

void image_trace_palette_source_converts_deep_buffers() {
  // A 16-bit palette source normalizes through the same value/257 conversion,
  // so it traces identically to its pre-converted 8-bit equivalent.
  PixelBuffer full8(16, 16, PixelFormat::rgba8());
  PixelBuffer deep16(16, 16, PixelFormat::rgb16());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 16; ++x) {
      auto* px = full8.pixel(x, y);
      px[0] = x < 8 ? 255 : 0;
      px[1] = 0;
      px[2] = x < 8 ? 0 : 255;
      px[3] = 255;
      const std::uint16_t values[3] = {static_cast<std::uint16_t>(x < 8 ? 65535 : 0), 0,
                                       static_cast<std::uint16_t>(x < 8 ? 0 : 65535)};
      std::memcpy(deep16.pixel(x, y), values, sizeof(values));
    }
  }
  auto masked = full8;
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 8; x < 16; ++x) {
      masked.pixel(x, y)[3] = 0;
    }
  }
  auto options = color_options();
  options.colors = 2;
  const auto via_deep = patchy::trace_image(masked, options, {}, 0, &deep16);
  const auto via_eight = patchy::trace_image(masked, options, {}, 0, &full8);
  CHECK(via_deep.layers.size() == via_eight.layers.size());
  for (std::size_t i = 0; i < via_deep.layers.size() && i < via_eight.layers.size(); ++i) {
    CHECK(via_deep.layers[i].color == via_eight.layers[i].color);
    CHECK(patchy::serialize_vector_path(via_deep.layers[i].path) ==
          patchy::serialize_vector_path(via_eight.layers[i].path));
  }
}

void image_trace_merge_colors_merges_near_duplicates() {
  // Two reds 4 apart (weighted distance 2 * 4^2 = 32) beside a far blue.
  // Merge colors 2 (threshold 9 * 2^2 = 36) collapses the reds into their
  // population-weighted mean; 1 (threshold 9) keeps them; 0 changes nothing.
  const RgbColor red_a{100, 0, 0};
  const RgbColor red_b{104, 0, 0};
  auto pixels = solid_image(60, 40, red_a);
  fill_rect(pixels, 30, 0, 10, 40, red_b);
  fill_rect(pixels, 40, 0, 20, 40, kBlue);
  auto options = color_options();
  options.colors = 3;
  for (const int keep_merge : {0, 1}) {
    options.merge_colors = keep_merge;
    const auto kept = patchy::trace_image(pixels, options);
    CHECK(kept.layers.size() == 3);
    CHECK(layer_with_color(kept, red_a) != nullptr);
    CHECK(layer_with_color(kept, red_b) != nullptr);
  }
  options.merge_colors = 2;
  const auto merged = patchy::trace_image(pixels, options);
  CHECK(merged.layers.size() == 2);
  // (1200 * 100 + 400 * 104) / 1600 = 101.
  const auto* red = layer_with_color(merged, RgbColor{101, 0, 0});
  CHECK(red != nullptr);
  CHECK(red->area == 40 * 40);
  CHECK(layer_with_color(merged, kBlue) != nullptr);

  // Grayscale merges levels the same way (means stay gray).
  auto grays = solid_image(60, 20, RgbColor{100, 100, 100});
  fill_rect(grays, 30, 0, 10, 20, RgbColor{104, 104, 104});
  fill_rect(grays, 40, 0, 20, 20, RgbColor{200, 200, 200});
  ImageTraceOptions gray_options;
  gray_options.mode = ImageTraceOptions::Mode::Grayscale;
  gray_options.colors = 3;
  gray_options.noise = 1;
  gray_options.merge_colors = 4;
  const auto merged_grays = patchy::trace_image(grays, gray_options);
  CHECK(merged_grays.layers.size() == 2);
  CHECK(layer_with_color(merged_grays, RgbColor{101, 101, 101}) != nullptr);
  CHECK(layer_with_color(merged_grays, RgbColor{200, 200, 200}) != nullptr);

  // The documented mapping endpoints.
  CHECK(patchy::image_trace_merge_distance(0) == 0);
  CHECK(patchy::image_trace_merge_distance(10) == 900);
  CHECK(patchy::image_trace_merge_distance(100) == 90000);
}

void image_trace_exact_assignment_beats_lut_on_close_palette() {
  // Two reds four levels apart share one 5-5-5 lookup bucket; the exact
  // per-unique-color assignment keeps them separate where the old bucketed
  // lookup collapsed one into the other.
  const RgbColor red_a{100, 0, 0};
  const RgbColor red_b{103, 0, 0};
  auto pixels = solid_image(40, 40, red_a);
  fill_rect(pixels, 20, 0, 20, 40, red_b);
  auto options = color_options();
  options.colors = 2;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.palette_size == 2);
  CHECK(result.layers.size() == 2);
  const auto* a = layer_with_color(result, red_a);
  const auto* b = layer_with_color(result, red_b);
  CHECK(a != nullptr);
  CHECK(b != nullptr);
  CHECK(a->area == 20 * 40);
  CHECK(b->area == 20 * 40);
}

void image_trace_grayscale_places_optimal_levels() {
  // Two tight gray pairs and one lone value: the exact 1D quantizer puts each
  // level on its cluster's population mean.
  auto pixels = solid_image(80, 20, RgbColor{10, 10, 10});
  fill_rect(pixels, 20, 0, 20, 20, RgbColor{12, 12, 12});
  fill_rect(pixels, 40, 0, 20, 20, RgbColor{100, 100, 100});
  fill_rect(pixels, 60, 0, 20, 20, RgbColor{240, 240, 240});
  ImageTraceOptions options;
  options.mode = ImageTraceOptions::Mode::Grayscale;
  options.colors = 3;
  options.noise = 1;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.palette_size == 3);
  CHECK(layer_with_color(result, RgbColor{11, 11, 11}) != nullptr);
  CHECK(layer_with_color(result, RgbColor{100, 100, 100}) != nullptr);
  CHECK(layer_with_color(result, RgbColor{240, 240, 240}) != nullptr);
}

void image_trace_supports_256_colors() {
  // 300 unique colors; a 256-color trace must use more than the old 64 cap
  // and stay deterministic. An absurd request clamps instead of failing.
  PixelBuffer pixels(300, 8, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 8; ++y) {
    for (std::int32_t x = 0; x < 300; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x % 256);
      px[1] = static_cast<std::uint8_t>(x < 256 ? 60 : 180);
      px[2] = 90;
      px[3] = 255;
    }
  }
  auto options = color_options();
  options.colors = 256;
  options.noise = 1;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.palette_size > 64);
  CHECK(result.palette_size <= 256);
  options.colors = 9999;
  const auto clamped = patchy::trace_image(pixels, options);
  CHECK(clamped.palette_size <= 256);
  const auto again = patchy::trace_image(pixels, options);
  CHECK(clamped.layers.size() == again.layers.size());
  for (std::size_t i = 0; i < clamped.layers.size(); ++i) {
    CHECK(patchy::serialize_vector_path(clamped.layers[i].path) ==
          patchy::serialize_vector_path(again.layers[i].path));
  }
}

void image_trace_smoothing_cleans_noise_and_preserves_alpha() {
  // A noisy interface between two fields, plus a transparent hole. Smoothing
  // must simplify the traced interface while never changing which pixels are
  // traced (the alpha cut stays crisp).
  auto pixels = solid_image(64, 64, RgbColor{200, 30, 30});
  fill_rect(pixels, 32, 0, 32, 64, kBlue);
  for (std::int32_t y = 0; y < 64; ++y) {
    for (std::int32_t offset = -2; offset <= 2; ++offset) {
      if ((y * 7 + offset) % 3 == 0) {
        const auto x = 32 + offset;
        auto* px = pixels.pixel(x, y);
        const bool flip = ((y + offset) % 2) == 0;
        const auto color = flip ? kBlue : RgbColor{200, 30, 30};
        px[0] = color.red;
        px[1] = color.green;
        px[2] = color.blue;
      }
    }
  }
  fill_rect(pixels, 8, 8, 10, 10, kBlack, 0);  // transparent hole
  auto options = color_options();
  options.colors = 2;
  options.noise = 1;
  const auto raw = patchy::trace_image(pixels, options);
  options.smoothing = 3;
  const auto smoothed = patchy::trace_image(pixels, options);
  CHECK(smoothed.palette_size == 2);
  CHECK(smoothed.anchor_count < raw.anchor_count);
  // The transparent hole stays untraced with identical bounds.
  const auto rendered = patchy::render_image_trace(smoothed, pixels.width(), pixels.height());
  CHECK(rendered.pixel(12, 12)[3] == 0);
  CHECK(rendered.pixel(7, 12)[3] == 255);
  CHECK(rendered.pixel(12, 7)[3] == 255);
  CHECK(rendered.pixel(18, 12)[3] == 255);
}

void image_trace_speckle_merge_prefers_similar_color() {
  // A dark-red speck inside the blue field but touching the red field: its
  // blue border is longer, but the merge now follows color similarity, so the
  // speck joins the reds. The old longest-border rule handed it to blue.
  const RgbColor red{220, 30, 30};
  const RgbColor speck{180, 40, 40};
  auto pixels = solid_image(40, 40, kBlue);
  fill_rect(pixels, 20, 0, 20, 40, red);
  fill_rect(pixels, 17, 10, 3, 3, speck);
  auto options = color_options();
  options.colors = 3;
  options.noise = 10;
  const auto result = patchy::trace_image(pixels, options);
  CHECK(result.layers.size() == 2);
  const auto* reds = layer_with_color(result, red);
  const auto* blues = layer_with_color(result, kBlue);
  CHECK(reds != nullptr);
  CHECK(blues != nullptr);
  CHECK(reds->area == 20 * 40 + 9);
  CHECK(blues->area == 20 * 40 - 9);
  // Dust surrounded by transparency still vanishes entirely.
  auto dusty = solid_image(30, 30, kBlack, 0);
  fill_rect(dusty, 14, 14, 2, 2, kRed);
  const auto vanished = patchy::trace_image(dusty, options);
  CHECK(vanished.layers.empty());
}

void image_trace_max_anchors_budget_escalates_globally() {
  auto pixels = solid_image(160, 120, kWhite);
  fill_disc(pixels, 50.0, 60.0, 38.0, kRed);
  fill_disc(pixels, 110.0, 60.0, 38.0, kBlue);
  fill_rect(pixels, 40, 100, 80, 12, kBlack);
  fill_disc(pixels, 50.0, 60.0, 10.0, kWhite);
  auto options = color_options();
  const auto unbudgeted = patchy::trace_image(pixels, options);
  CHECK(unbudgeted.anchor_count > 0);
  // A budget at or above the unbudgeted total is a no-op: identical paths.
  options.max_anchors = static_cast<int>(unbudgeted.anchor_count);
  const auto satisfied = patchy::trace_image(pixels, options);
  CHECK(satisfied.layers.size() == unbudgeted.layers.size());
  for (std::size_t i = 0; i < satisfied.layers.size(); ++i) {
    CHECK(patchy::serialize_vector_path(satisfied.layers[i].path) ==
          patchy::serialize_vector_path(unbudgeted.layers[i].path));
  }
  // A tight budget escalates the global tolerance and sheds anchors while
  // keeping every color layer.
  options.max_anchors = 20;
  const auto tight = patchy::trace_image(pixels, options);
  CHECK(tight.anchor_count < unbudgeted.anchor_count);
  CHECK(tight.layers.size() == unbudgeted.layers.size());
  const auto tight_again = patchy::trace_image(pixels, options);
  CHECK(tight.anchor_count == tight_again.anchor_count);
}

void image_trace_parallel_output_matches_serial() {
  auto pixels = solid_image(160, 120, kWhite);
  fill_disc(pixels, 50.0, 60.0, 38.0, kRed);
  fill_disc(pixels, 110.0, 60.0, 38.0, kBlue);
  fill_rect(pixels, 40, 100, 80, 12, kBlack);
  auto options = color_options(ImageTraceOptions::Method::Overlapping);
  options.smoothing = 2;
  const auto serial = patchy::trace_image(pixels, options, {}, 1);
  const auto parallel = patchy::trace_image(pixels, options, {}, 8);
  CHECK(serial.layers.size() == parallel.layers.size());
  CHECK(serial.anchor_count == parallel.anchor_count);
  for (std::size_t i = 0; i < serial.layers.size(); ++i) {
    CHECK(serial.layers[i].color == parallel.layers[i].color);
    CHECK(patchy::serialize_vector_path(serial.layers[i].path) ==
          patchy::serialize_vector_path(parallel.layers[i].path));
  }
  const auto cancelled = patchy::trace_image(pixels, options, [] { return true; }, 8);
  CHECK(cancelled.layers.empty());
}

void image_trace_overlapping_underlap_keeps_silhouette() {
  // A blue disc on a black disc on transparency. Overlapping tucks the black
  // shape under the blue one to cover hairline seams, but must never grow the
  // silhouette into the transparent surroundings.
  auto pixels = solid_image(80, 80, kWhite, 0);
  fill_disc(pixels, 40.0, 40.0, 30.0, kBlack);
  fill_disc(pixels, 40.0, 40.0, 12.0, kBlue);
  const auto result = patchy::trace_image(pixels, color_options(ImageTraceOptions::Method::Overlapping));
  CHECK(result.layers.size() == 2);
  CHECK(result.layers[0].color == kBlack);
  CHECK(result.layers[1].color == kBlue);
  // The black shape is solid (its hole is stacked over by the blue disc).
  CHECK(count_ops(result.layers[0].path, PathCombineOp::Subtract) == 0);
  const auto rendered = patchy::render_image_trace(result, pixels.width(), pixels.height());
  CHECK(rendered.pixel(40, 40)[3] == 255);
  CHECK(std::abs(rendered.pixel(40, 40)[2] - kBlue.blue) <= 8);
  // Transparent corners and the area just outside the disc stay empty.
  CHECK(rendered.pixel(4, 4)[3] == 0);
  CHECK(rendered.pixel(40, 5)[3] == 0);
  CHECK(rendered.pixel(75, 40)[3] == 0);
}

void path_fit_refine_iterations_shrink_noisy_circles() {
  // A wobbly circle: more reparametrization attempts converge runs that the
  // historical four iterations split, so anchors never increase.
  std::vector<patchy::FitPoint> circle;
  for (int i = 0; i < 96; ++i) {
    const double angle = i * (2.0 * 3.14159265358979323846 / 96.0);
    const double wobble = ((i * 7) % 5) * 0.08;
    circle.push_back({40.0 + (20.0 + wobble) * std::cos(angle), 40.0 + (20.0 + wobble) * std::sin(angle)});
  }
  patchy::PathFitOptions four;
  four.tolerance = 0.5;
  patchy::PathFitOptions eight = four;
  eight.refine_iterations = 8;
  const auto historical = patchy::fit_closed_loop(circle, four);
  const auto refined = patchy::fit_closed_loop(circle, eight);
  CHECK(!historical.anchors.empty());
  CHECK(!refined.anchors.empty());
  CHECK(refined.anchors.size() <= historical.anchors.size());
  // The default option count matches the historical literal.
  patchy::PathFitOptions defaults;
  CHECK(defaults.refine_iterations == 4);
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
      {"image_trace_palette_source_uses_whole_image_colors", image_trace_palette_source_uses_whole_image_colors},
      {"image_trace_palette_source_null_and_self_are_no_ops", image_trace_palette_source_null_and_self_are_no_ops},
      {"image_trace_palette_source_grayscale_histogram_from_source",
       image_trace_palette_source_grayscale_histogram_from_source},
      {"image_trace_palette_source_converts_deep_buffers", image_trace_palette_source_converts_deep_buffers},
      {"image_trace_merge_colors_merges_near_duplicates", image_trace_merge_colors_merges_near_duplicates},
      {"image_trace_exact_assignment_beats_lut_on_close_palette",
       image_trace_exact_assignment_beats_lut_on_close_palette},
      {"image_trace_grayscale_places_optimal_levels", image_trace_grayscale_places_optimal_levels},
      {"image_trace_supports_256_colors", image_trace_supports_256_colors},
      {"image_trace_smoothing_cleans_noise_and_preserves_alpha",
       image_trace_smoothing_cleans_noise_and_preserves_alpha},
      {"image_trace_speckle_merge_prefers_similar_color", image_trace_speckle_merge_prefers_similar_color},
      {"image_trace_max_anchors_budget_escalates_globally", image_trace_max_anchors_budget_escalates_globally},
      {"image_trace_parallel_output_matches_serial", image_trace_parallel_output_matches_serial},
      {"image_trace_overlapping_underlap_keeps_silhouette", image_trace_overlapping_underlap_keeps_silhouette},
      {"path_fit_refine_iterations_shrink_noisy_circles", path_fit_refine_iterations_shrink_noisy_circles},
  };
}
