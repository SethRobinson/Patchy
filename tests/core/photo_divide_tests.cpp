#include "core/photo_divide.hpp"
#include "core/warp_mesh.hpp"

#include "test_harness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using patchy::PhotoDetectOptions;
using patchy::PhotoDetectResult;
using patchy::PhotoExtractMode;
using patchy::PhotoRegion;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;

constexpr std::uint8_t kBackgroundGray = 245;

PixelBuffer background_image(std::int32_t width, std::int32_t height,
                             std::uint8_t gray = kBackgroundGray) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    auto row = pixels.row(y);
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = row.data() + static_cast<std::size_t>(x) * 4;
      px[0] = gray;
      px[1] = gray;
      px[2] = gray;
      px[3] = 255;
    }
  }
  return pixels;
}

void fill_axis_rect(PixelBuffer& pixels, Rect rect, std::uint8_t red, std::uint8_t green,
                    std::uint8_t blue) {
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = red;
      px[1] = green;
      px[2] = blue;
      px[3] = 255;
    }
  }
}

std::array<double, 8> rotated_rect_quad(double center_x, double center_y, double width,
                                        double height, double angle_degrees) {
  const double radians = angle_degrees * 3.14159265358979323846 / 180.0;
  const double ux = std::cos(radians);
  const double uy = std::sin(radians);
  const double vx = -uy;
  const double vy = ux;
  const double hw = width / 2.0;
  const double hh = height / 2.0;
  return {center_x - hw * ux - hh * vx, center_y - hw * uy - hh * vy,
          center_x + hw * ux - hh * vx, center_y + hw * uy - hh * vy,
          center_x + hw * ux + hh * vx, center_y + hw * uy + hh * vy,
          center_x - hw * ux + hh * vx, center_y - hw * uy + hh * vy};
}

bool point_in_quad(const std::array<double, 8>& quad, double x, double y) {
  double reference = 0.0;
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    const double edge_x = quad[static_cast<std::size_t>(j * 2)] - quad[static_cast<std::size_t>(i * 2)];
    const double edge_y =
        quad[static_cast<std::size_t>(j * 2 + 1)] - quad[static_cast<std::size_t>(i * 2 + 1)];
    const double to_x = x - quad[static_cast<std::size_t>(i * 2)];
    const double to_y = y - quad[static_cast<std::size_t>(i * 2 + 1)];
    const double cross = edge_x * to_y - edge_y * to_x;
    if (cross == 0.0) {
      continue;
    }
    if (reference == 0.0) {
      reference = cross;
    } else if ((cross > 0.0) != (reference > 0.0)) {
      return false;
    }
  }
  return true;
}

void fill_quad_solid(PixelBuffer& pixels, const std::array<double, 8>& quad, std::uint8_t red,
                     std::uint8_t green, std::uint8_t blue) {
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (point_in_quad(quad, x + 0.5, y + 0.5)) {
        auto* px = pixels.pixel(x, y);
        px[0] = red;
        px[1] = green;
        px[2] = blue;
        px[3] = 255;
      }
    }
  }
}

std::uint64_t splitmix64_next(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t mixed = state;
  mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
  mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
  return mixed ^ (mixed >> 31);
}

bool regions_equal(const PhotoRegion& a, const PhotoRegion& b) {
  return a.quad == b.quad && a.perspective_corners == b.perspective_corners &&
         a.angle_degrees == b.angle_degrees && a.bounding_box.x == b.bounding_box.x &&
         a.bounding_box.y == b.bounding_box.y && a.bounding_box.width == b.bounding_box.width &&
         a.bounding_box.height == b.bounding_box.height &&
         a.perspective_quad == b.perspective_quad && a.user_added == b.user_added;
}

bool rect_equals(Rect rect, std::int32_t x, std::int32_t y, std::int32_t width,
                 std::int32_t height) {
  return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

// --- detection ----------------------------------------------------------------

void photo_divide_detects_photos_on_plain_background() {
  PixelBuffer image = background_image(900, 700);
  fill_axis_rect(image, Rect{60, 80, 200, 150}, 60, 70, 80);
  fill_axis_rect(image, Rect{400, 70, 180, 140}, 90, 40, 40);
  fill_axis_rect(image, Rect{100, 400, 250, 180}, 30, 90, 60);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 3);
  CHECK(rect_equals(result.regions[0].bounding_box, 60, 80, 200, 150));
  CHECK(rect_equals(result.regions[1].bounding_box, 400, 70, 180, 140));
  CHECK(rect_equals(result.regions[2].bounding_box, 100, 400, 250, 180));
  for (const auto& region : result.regions) {
    CHECK(region.angle_degrees == 0.0);
    CHECK(!region.user_added);
  }
}

void photo_divide_detects_rotated_photo_angle() {
  PixelBuffer image = background_image(700, 700);
  fill_quad_solid(image, rotated_rect_quad(350.0, 350.0, 320.0, 220.0, 7.0), 50, 60, 70);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  CHECK(std::abs(result.regions[0].angle_degrees - 7.0) < 0.5);
}

void photo_divide_snaps_small_angles_to_zero() {
  PixelBuffer image = background_image(700, 700);
  fill_quad_solid(image, rotated_rect_quad(350.0, 350.0, 240.0, 160.0, 0.2), 50, 60, 70);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  const PhotoRegion& region = result.regions[0];
  CHECK(region.angle_degrees == 0.0);
  const PixelBuffer cut = patchy::extract_photo_region(image, region, PhotoExtractMode::Cut);
  const PixelBuffer straightened =
      patchy::extract_photo_region(image, region, PhotoExtractMode::Straighten);
  CHECK(cut.width() == straightened.width());
  CHECK(cut.height() == straightened.height());
  CHECK(cut.byte_size() == straightened.byte_size());
  CHECK(std::memcmp(cut.data().data(), straightened.data().data(), cut.byte_size()) == 0);
}

void photo_divide_ignores_dust_below_minimum_size() {
  PixelBuffer image = background_image(800, 600);
  fill_axis_rect(image, Rect{100, 100, 200, 150}, 60, 70, 80);
  fill_axis_rect(image, Rect{600, 50, 5, 5}, 10, 10, 10);
  fill_axis_rect(image, Rect{700, 500, 8, 8}, 10, 10, 10);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  CHECK(rect_equals(result.regions[0].bounding_box, 100, 100, 200, 150));

  // With a known PPI the minimum photo edge is physical (half an inch).
  PixelBuffer with_ppi = background_image(800, 600);
  fill_axis_rect(with_ppi, Rect{100, 100, 200, 150}, 60, 70, 80);
  fill_axis_rect(with_ppi, Rect{500, 300, 40, 40}, 10, 10, 10);  // 0.4 in at 100 ppi
  PhotoDetectOptions options;
  options.source_ppi = 100.0;
  const auto physical = patchy::detect_photo_regions(with_ppi, options);
  CHECK(physical.regions.size() == 1);
  CHECK(rect_equals(physical.regions[0].bounding_box, 100, 100, 200, 150));
}

void photo_divide_sensitivity_expands_background_tolerance() {
  PixelBuffer image = background_image(600, 500, 240);
  fill_axis_rect(image, Rect{150, 120, 250, 200}, 225, 225, 225);  // 15 gray levels off
  PhotoDetectOptions low;
  low.sensitivity = 0;
  CHECK(patchy::detect_photo_regions(image, low).regions.empty());
  PhotoDetectOptions high;
  high.sensitivity = 100;
  const auto found = patchy::detect_photo_regions(image, high);
  CHECK(found.regions.size() == 1);
  CHECK(rect_equals(found.regions[0].bounding_box, 150, 120, 250, 200));
}

void photo_divide_merges_split_detections() {
  PixelBuffer image = background_image(500, 400);
  fill_axis_rect(image, Rect{100, 100, 300, 200}, 60, 70, 80);
  // A two-pixel background-colored stripe fragments the photo; morphological
  // closing bridges it back into one region.
  fill_axis_rect(image, Rect{100, 195, 300, 2}, kBackgroundGray, kBackgroundGray, kBackgroundGray);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  CHECK(rect_equals(result.regions[0].bounding_box, 100, 100, 300, 200));
}

void photo_divide_detection_is_deterministic() {
  PixelBuffer image = background_image(800, 640);
  std::uint64_t state = 0x1234'5678'9ABC'DEF0ULL;
  for (std::int32_t y = 0; y < image.height(); ++y) {
    for (std::int32_t x = 0; x < image.width(); ++x) {
      auto* px = image.pixel(x, y);
      const auto offset = static_cast<std::int32_t>(splitmix64_next(state) % 13ULL) - 6;
      const auto value = static_cast<std::uint8_t>(kBackgroundGray + offset);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  fill_axis_rect(image, Rect{80, 60, 220, 160}, 60, 70, 80);
  fill_axis_rect(image, Rect{420, 300, 260, 200}, 40, 45, 55);
  const auto first = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  const auto second = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(first.regions.size() == 2);
  CHECK(second.regions.size() == first.regions.size());
  for (std::size_t i = 0; i < first.regions.size(); ++i) {
    CHECK(regions_equal(first.regions[i], second.regions[i]));
  }
  // Cross-toolchain canary: the noisy scene still resolves the drawn integer
  // boxes exactly (the pipeline decisions are integer math).
  CHECK(rect_equals(first.regions[0].bounding_box, 80, 60, 220, 160));
  CHECK(rect_equals(first.regions[1].bounding_box, 420, 300, 260, 200));
}

void photo_divide_perspective_quad_fits_keystone() {
  PixelBuffer image = background_image(640, 560);
  const std::array<double, 8> drawn = {150.0, 100.0, 450.0, 130.0, 420.0, 420.0, 180.0, 400.0};
  fill_quad_solid(image, drawn, 55, 60, 75);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  const PhotoRegion& region = result.regions[0];
  CHECK(region.perspective_quad);
  for (int corner = 0; corner < 8; ++corner) {
    CHECK(std::abs(region.perspective_corners[static_cast<std::size_t>(corner)] -
                   drawn[static_cast<std::size_t>(corner)]) < 2.5);
  }
}

void photo_divide_quad_quality_falls_back_to_rect() {
  PixelBuffer image = background_image(600, 600);
  // A right triangle has no fourth side to fit; the quad candidate misses the
  // hull area band and the region keeps its min-area rect.
  const std::array<double, 8> triangle = {100.0, 100.0, 400.0, 100.0, 400.0, 100.0, 100.0, 400.0};
  fill_quad_solid(image, triangle, 55, 60, 75);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  CHECK(!result.regions[0].perspective_quad);
  CHECK(result.regions[0].perspective_corners == result.regions[0].quad);
}

void photo_divide_reading_order_row_major() {
  PixelBuffer image = background_image(600, 500);
  fill_axis_rect(image, Rect{50, 50, 150, 100}, 60, 70, 80);
  fill_axis_rect(image, Rect{350, 60, 150, 100}, 60, 70, 80);
  fill_axis_rect(image, Rect{60, 300, 150, 100}, 60, 70, 80);
  fill_axis_rect(image, Rect{360, 290, 150, 100}, 60, 70, 80);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 4);
  CHECK(rect_equals(result.regions[0].bounding_box, 50, 50, 150, 100));
  CHECK(rect_equals(result.regions[1].bounding_box, 350, 60, 150, 100));
  CHECK(rect_equals(result.regions[2].bounding_box, 60, 300, 150, 100));
  CHECK(rect_equals(result.regions[3].bounding_box, 360, 290, 150, 100));
}

void photo_divide_rejects_unsupported_and_empty_sources() {
  CHECK(patchy::detect_photo_regions(PixelBuffer{}, PhotoDetectOptions{}).regions.empty());
  PixelBuffer tiny(2, 2, PixelFormat::rgba8());
  CHECK(patchy::detect_photo_regions(tiny, PhotoDetectOptions{}).regions.empty());
  bool cancelled_polled = false;
  PixelBuffer image = background_image(300, 300);
  fill_axis_rect(image, Rect{100, 100, 100, 100}, 60, 70, 80);
  const auto cancelled = patchy::detect_photo_regions(image, PhotoDetectOptions{}, [&] {
    cancelled_polled = true;
    return true;
  });
  CHECK(cancelled_polled);
  CHECK(cancelled.regions.empty());
}

// --- extraction ---------------------------------------------------------------

void photo_divide_extract_cut_matches_source_bytes() {
  PixelBuffer image = background_image(500, 400);
  fill_axis_rect(image, Rect{120, 90, 180, 140}, 60, 70, 80);
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  const PixelBuffer cut =
      patchy::extract_photo_region(image, result.regions[0], PhotoExtractMode::Cut);
  CHECK(cut.width() == 180);
  CHECK(cut.height() == 140);
  for (std::int32_t y = 0; y < cut.height(); ++y) {
    CHECK(std::memcmp(cut.row(y).data(), image.pixel(120, 90 + y),
                      static_cast<std::size_t>(cut.width()) * 4) == 0);
  }
}

void photo_divide_extract_straighten_rms() {
  PixelBuffer image = background_image(600, 600);
  const double angle_degrees = 10.0;
  const double width = 220.0;
  const double height = 150.0;
  const double radians = angle_degrees * 3.14159265358979323846 / 180.0;
  const double ux = std::cos(radians);
  const double uy = std::sin(radians);
  for (std::int32_t y = 0; y < image.height(); ++y) {
    for (std::int32_t x = 0; x < image.width(); ++x) {
      const double dx = x + 0.5 - 300.0;
      const double dy = y + 0.5 - 300.0;
      const double u = dx * ux + dy * uy;
      const double v = -dx * uy + dy * ux;
      if (std::abs(u) <= width / 2.0 && std::abs(v) <= height / 2.0) {
        auto* px = image.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(
            std::floor((u + width / 2.0) / width * 255.0 + 0.5));
        px[1] = static_cast<std::uint8_t>(
            std::floor((v + height / 2.0) / height * 255.0 + 0.5));
        px[2] = 128;
      }
    }
  }
  const auto result = patchy::detect_photo_regions(image, PhotoDetectOptions{});
  CHECK(result.regions.size() == 1);
  const PixelBuffer out =
      patchy::extract_photo_region(image, result.regions[0], PhotoExtractMode::Straighten);
  CHECK(std::abs(out.width() - 220) <= 2);
  CHECK(std::abs(out.height() - 150) <= 2);
  double squared_error = 0.0;
  std::int64_t samples = 0;
  for (std::int32_t y = 4; y < out.height() - 4; ++y) {
    for (std::int32_t x = 4; x < out.width() - 4; ++x) {
      const auto* px = out.pixel(x, y);
      const double expected_red = (x + 0.5) / out.width() * 255.0;
      const double expected_green = (y + 0.5) / out.height() * 255.0;
      squared_error += (px[0] - expected_red) * (px[0] - expected_red);
      squared_error += (px[1] - expected_green) * (px[1] - expected_green);
      samples += 2;
    }
  }
  CHECK(samples > 0);
  CHECK(std::sqrt(squared_error / static_cast<double>(samples)) < 5.0);
  CHECK(std::abs(result.regions[0].angle_degrees - angle_degrees) < 0.5);
}

void photo_divide_extract_perspective_rms() {
  PixelBuffer image = background_image(640, 560);
  const std::array<double, 8> quad = {140.0, 110.0, 420.0, 90.0, 460.0, 380.0, 120.0, 350.0};
  const double rect_width = 280.0;
  const double rect_height = 260.0;
  const auto forward =
      patchy::homography_from_rect_to_quad(0.0, 0.0, rect_width, rect_height, quad);
  CHECK(forward.has_value());
  const auto inverse = patchy::invert_homography(*forward);
  CHECK(inverse.has_value());
  for (std::int32_t y = 0; y < image.height(); ++y) {
    for (std::int32_t x = 0; x < image.width(); ++x) {
      const auto rect_point = patchy::apply_homography(*inverse, x + 0.5, y + 0.5);
      if (rect_point[0] >= 0.0 && rect_point[0] <= rect_width && rect_point[1] >= 0.0 &&
          rect_point[1] <= rect_height) {
        auto* px = image.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(
            std::floor(rect_point[0] / rect_width * 255.0 + 0.5));
        px[1] = static_cast<std::uint8_t>(
            std::floor(rect_point[1] / rect_height * 255.0 + 0.5));
        px[2] = 128;
      }
    }
  }
  PhotoRegion region;
  region.quad = quad;
  region.perspective_corners = quad;
  region.perspective_quad = true;
  region.bounding_box = Rect{120, 90, 341, 291};
  const PixelBuffer out =
      patchy::extract_photo_region(image, region, PhotoExtractMode::Perspective);
  CHECK(out.width() > 0);
  CHECK(out.height() > 0);
  double squared_error = 0.0;
  std::int64_t samples = 0;
  for (std::int32_t y = 5; y < out.height() - 5; ++y) {
    for (std::int32_t x = 5; x < out.width() - 5; ++x) {
      const auto* px = out.pixel(x, y);
      const double expected_red = (x + 0.5) / out.width() * 255.0;
      const double expected_green = (y + 0.5) / out.height() * 255.0;
      squared_error += (px[0] - expected_red) * (px[0] - expected_red);
      squared_error += (px[1] - expected_green) * (px[1] - expected_green);
      samples += 2;
    }
  }
  CHECK(samples > 0);
  CHECK(std::sqrt(squared_error / static_cast<double>(samples)) < 5.0);
}

// --- aspect recovery ----------------------------------------------------------

void photo_divide_rectified_aspect_recovers_known_ratio() {
  // Project a 40 x 30 rectangle (aspect 4:3) through a real pinhole camera:
  // focal 800, principal point at the center of an 800 x 600 image.
  const double focal = 800.0;
  const double center_x = 400.0;
  const double center_y = 300.0;
  const double tilt = 25.0 * 3.14159265358979323846 / 180.0;
  const double yaw = 15.0 * 3.14159265358979323846 / 180.0;
  std::array<double, 8> quad{};
  const double corners[4][2] = {{-20.0, -15.0}, {20.0, -15.0}, {20.0, 15.0}, {-20.0, 15.0}};
  for (int i = 0; i < 4; ++i) {
    const double x = corners[i][0];
    const double y = corners[i][1];
    // Rx(tilt) then Ry(yaw), then translate along z.
    const double y1 = y * std::cos(tilt);
    const double z1 = y * std::sin(tilt);
    const double x2 = x * std::cos(yaw) + z1 * std::sin(yaw);
    const double z2 = -x * std::sin(yaw) + z1 * std::cos(yaw) + 100.0;
    quad[static_cast<std::size_t>(i * 2)] = focal * x2 / z2 + center_x;
    quad[static_cast<std::size_t>(i * 2 + 1)] = focal * y1 / z2 + center_y;
  }
  const auto aspect = patchy::rectified_aspect_ratio(quad, 800.0, 600.0);
  CHECK(aspect.has_value());
  CHECK(std::abs(*aspect - 4.0 / 3.0) < 0.013);

  // A parallelogram (pure affine view) falls back to the affine closed form.
  const auto affine_quad = rotated_rect_quad(400.0, 300.0, 200.0, 150.0, 20.0);
  const auto affine_aspect = patchy::rectified_aspect_ratio(affine_quad, 800.0, 600.0);
  CHECK(affine_aspect.has_value());
  CHECK(std::abs(*affine_aspect - 4.0 / 3.0) < 0.001);
}

void photo_divide_aspect_snaps_to_print_ratios() {
  CHECK(std::abs(patchy::snap_aspect_to_print_ratios(1.34) - 4.0 / 3.0) < 1e-12);
  CHECK(std::abs(patchy::snap_aspect_to_print_ratios(1.31) - 4.0 / 3.0) < 1e-12);
  CHECK(patchy::snap_aspect_to_print_ratios(1.62) == 1.62);  // between 3:2 and 16:9
  CHECK(std::abs(patchy::snap_aspect_to_print_ratios(0.748) - 0.75) < 1e-12);  // portrait 3:4
  CHECK(std::abs(patchy::snap_aspect_to_print_ratios(1.5) - 1.5) < 1e-12);
}

}  // namespace

std::vector<patchy::test::TestCase> photo_divide_tests() {
  return {
      {"photo_divide_detects_photos_on_plain_background",
       photo_divide_detects_photos_on_plain_background},
      {"photo_divide_detects_rotated_photo_angle", photo_divide_detects_rotated_photo_angle},
      {"photo_divide_snaps_small_angles_to_zero", photo_divide_snaps_small_angles_to_zero},
      {"photo_divide_ignores_dust_below_minimum_size",
       photo_divide_ignores_dust_below_minimum_size},
      {"photo_divide_sensitivity_expands_background_tolerance",
       photo_divide_sensitivity_expands_background_tolerance},
      {"photo_divide_merges_split_detections", photo_divide_merges_split_detections},
      {"photo_divide_detection_is_deterministic", photo_divide_detection_is_deterministic},
      {"photo_divide_perspective_quad_fits_keystone",
       photo_divide_perspective_quad_fits_keystone},
      {"photo_divide_quad_quality_falls_back_to_rect",
       photo_divide_quad_quality_falls_back_to_rect},
      {"photo_divide_reading_order_row_major", photo_divide_reading_order_row_major},
      {"photo_divide_rejects_unsupported_and_empty_sources",
       photo_divide_rejects_unsupported_and_empty_sources},
      {"photo_divide_extract_cut_matches_source_bytes",
       photo_divide_extract_cut_matches_source_bytes},
      {"photo_divide_extract_straighten_rms", photo_divide_extract_straighten_rms},
      {"photo_divide_extract_perspective_rms", photo_divide_extract_perspective_rms},
      {"photo_divide_rectified_aspect_recovers_known_ratio",
       photo_divide_rectified_aspect_recovers_known_ratio},
      {"photo_divide_aspect_snaps_to_print_ratios", photo_divide_aspect_snaps_to_print_ratios},
  };
}
