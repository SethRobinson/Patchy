#include "core/pixel_depth.hpp"
#include "core/red_eye.hpp"
#include "test_harness.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using patchy::BitDepth;
using patchy::PixelBuffer;
using patchy::PixelFormat;
using patchy::Rect;
using patchy::RedEyeOptions;

constexpr int kWidth = 120;
constexpr int kHeight = 80;
constexpr int kEyeX = 40;
constexpr int kEyeY = 40;
constexpr std::array<std::uint8_t, 3> kSkin{215, 165, 135};
constexpr std::array<std::uint8_t, 3> kIris{90, 60, 40};
constexpr std::array<std::uint8_t, 3> kPupilRed{200, 40, 40};
constexpr std::array<std::uint8_t, 3> kRim{170, 80, 80};

void put(PixelBuffer& pixels, int x, int y, const std::array<std::uint8_t, 3>& color) {
  auto* px = pixels.pixel(x, y);
  px[0] = color[0];
  px[1] = color[1];
  px[2] = color[2];
}

std::array<int, 3> get(const PixelBuffer& pixels, int x, int y) {
  const auto* px = pixels.pixel(x, y);
  return {px[0], px[1], px[2]};
}

// Skin with one eye: a brown iris (radius 14) around a flash-red pupil (radius 7) with a
// pinkish rim (radius 7..9) and a 2x2 white glint. A red square far right sits outside a
// click's box, and a red dot up and right of the eye sits inside the box but apart from
// the pupil.
PixelBuffer make_face() {
  PixelBuffer pixels(kWidth, kHeight, PixelFormat::rgb8());
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto dx = x - kEyeX;
      const auto dy = y - kEyeY;
      const auto d2 = dx * dx + dy * dy;
      if (d2 <= 7 * 7) {
        put(pixels, x, y, kPupilRed);
      } else if (d2 <= 9 * 9) {
        put(pixels, x, y, kRim);
      } else if (d2 <= 14 * 14) {
        put(pixels, x, y, kIris);
      } else {
        put(pixels, x, y, kSkin);
      }
    }
  }
  for (int y = 37; y < 39; ++y) {
    for (int x = 37; x < 39; ++x) {
      put(pixels, x, y, {255, 255, 255});
    }
  }
  for (int y = 35; y < 46; ++y) {
    for (int x = 100; x < 111; ++x) {
      put(pixels, x, y, kPupilRed);
    }
  }
  for (int y = 26; y < 29; ++y) {
    for (int x = 52; x < 55; ++x) {
      put(pixels, x, y, kPupilRed);
    }
  }
  return pixels;
}

Rect click_region(int x, int y) {
  const auto side = patchy::red_eye_click_box_size(kWidth, kHeight);
  return Rect{x - side / 2, y - side / 2, side, side};
}

Rect click_and_apply(PixelBuffer& pixels, int x, int y, const RedEyeOptions& options,
                     const std::function<float(std::int32_t, std::int32_t)>& coverage = {}) {
  const auto region = click_region(x, y);
  const auto mask = patchy::find_red_eye(pixels, region, x, y, region.width / 4, options);
  return patchy::apply_red_eye(pixels, mask, options, coverage);
}

bool red_dominant(const std::array<int, 3>& c) {
  return c[0] > std::max(c[1], c[2]) + 8;
}

bool same(const std::array<int, 3>& a, const std::array<std::uint8_t, 3>& b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

void red_eye_click_removes_red_and_darkens_pupil() {
  auto pixels = make_face();
  const auto dirty = click_and_apply(pixels, kEyeX + 3, kEyeY + 2, RedEyeOptions{});
  CHECK(!dirty.empty());
  // The pupil turns neutral and, at Darken 50, half as bright as its green/blue level.
  const auto pupil = get(pixels, kEyeX + 4, kEyeY);
  CHECK(!red_dominant(pupil));
  CHECK(std::abs(pupil[0] - 20) <= 2 && std::abs(pupil[1] - 20) <= 2 && std::abs(pupil[2] - 20) <= 2);
  // Skin, iris, the red square outside the box and the red dot apart from the pupil stay.
  CHECK(same(get(pixels, 5, 5), kSkin));
  CHECK(same(get(pixels, kEyeX + 12, kEyeY), kIris));
  CHECK(same(get(pixels, 105, 40), kPupilRed));
  CHECK(same(get(pixels, 53, 27), kPupilRed));
  CHECK(dirty.x >= kEyeX - 10 && dirty.x + dirty.width <= kEyeX + 11);
}

void red_eye_click_beside_pupil_finds_nearest_red() {
  auto pixels = make_face();
  // A click on the iris, off the pupil: the nearest red area is the pupil.
  const auto dirty = click_and_apply(pixels, kEyeX - 11, kEyeY, RedEyeOptions{});
  CHECK(!dirty.empty());
  CHECK(!red_dominant(get(pixels, kEyeX, kEyeY + 4)));
  CHECK(same(get(pixels, 53, 27), kPupilRed));
  // Far from any red there is nothing to fix.
  auto untouched = make_face();
  const auto region = click_region(8, 8);
  CHECK(patchy::find_red_eye(untouched, region, 8, 8, region.width / 4, RedEyeOptions{}).empty());
}

void red_eye_pupil_size_takes_in_the_rim() {
  // The rim's outer ring (radius about 8.5) is not red enough at the default Pupil Size
  // and is not next to the pupil, so it stays; at 100 the rim counts as pupil.
  const int rim_x = kEyeX + 8;
  const int rim_y = kEyeY + 3;
  auto pixels = make_face();
  click_and_apply(pixels, kEyeX, kEyeY + 3, RedEyeOptions{50, 50});
  CHECK(same(get(pixels, rim_x, rim_y), kRim));
  auto wide = make_face();
  click_and_apply(wide, kEyeX, kEyeY + 3, RedEyeOptions{100, 50});
  CHECK(!red_dominant(get(wide, rim_x, rim_y)));
  CHECK(same(get(wide, kEyeX + 12, kEyeY), kIris));
  CHECK(patchy::red_eye_is_red(kRim[0], kRim[1], kRim[2], 100));
  CHECK(!patchy::red_eye_is_red(kRim[0], kRim[1], kRim[2], 50));
  CHECK(!patchy::red_eye_is_red(kIris[0], kIris[1], kIris[2], 100));
  CHECK(!patchy::red_eye_is_red(kSkin[0], kSkin[1], kSkin[2], 100));
}

void red_eye_darken_amount_scales_brightness() {
  auto light = make_face();
  click_and_apply(light, kEyeX, kEyeY + 3, RedEyeOptions{50, 25});
  auto dark = make_face();
  click_and_apply(dark, kEyeX, kEyeY + 3, RedEyeOptions{50, 100});
  const auto light_pupil = get(light, kEyeX + 4, kEyeY);
  const auto dark_pupil = get(dark, kEyeX + 4, kEyeY);
  CHECK(std::abs(light_pupil[1] - 30) <= 2);  // 40 * 0.75
  CHECK(dark_pupil[0] <= 2 && dark_pupil[1] <= 2 && dark_pupil[2] <= 2);
}

void red_eye_selection_limits_the_change() {
  auto pixels = make_face();
  // A selection covering the left half of the pupil only.
  click_and_apply(pixels, kEyeX, kEyeY + 3, RedEyeOptions{},
                  [](std::int32_t x, std::int32_t) { return x < kEyeX ? 1.0F : 0.0F; });
  CHECK(!red_dominant(get(pixels, kEyeX - 4, kEyeY)));
  CHECK(same(get(pixels, kEyeX + 4, kEyeY), kPupilRed));
}

// The 16 and 32-bit results narrow to within a few levels of the 8-bit result, and every
// pixel the mask does not cover keeps its exact deep value.
void check_deep_variant(BitDepth depth, int tolerance) {
  const auto face = make_face();
  auto expected = face;
  click_and_apply(expected, kEyeX + 3, kEyeY + 2, RedEyeOptions{});
  const auto original = patchy::convert_pixel_buffer_depth(face, depth, patchy::SampleKind::Color);
  auto deep = original;
  const auto region = click_region(kEyeX + 3, kEyeY + 2);
  const auto mask = patchy::find_red_eye(deep, region, kEyeX + 3, kEyeY + 2, region.width / 4, RedEyeOptions{});
  CHECK(!mask.empty());
  const auto dirty = patchy::apply_red_eye(deep, mask, RedEyeOptions{});
  CHECK(!dirty.empty());
  const auto narrowed = patchy::convert_pixel_buffer_depth(deep, BitDepth::UInt8, patchy::SampleKind::Color);
  int worst = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto a = get(narrowed, x, y);
      const auto b = get(expected, x, y);
      for (int c = 0; c < 3; ++c) {
        worst = std::max(worst, std::abs(a[static_cast<std::size_t>(c)] - b[static_cast<std::size_t>(c)]));
      }
      if (mask.at(x, y) == 0) {
        const auto bytes = patchy::bytes_per_pixel(deep.format());
        CHECK(std::memcmp(deep.pixel(x, y), original.pixel(x, y), bytes) == 0);
      }
    }
  }
  if (worst > tolerance) {
    std::printf("red eye depth %d: worst difference %d levels\n", static_cast<int>(depth), worst);
  }
  CHECK(worst <= tolerance);
  CHECK(!red_dominant(get(narrowed, kEyeX + 4, kEyeY)));
  CHECK(same(get(narrowed, 105, 40), kPupilRed));
}

void red_eye_16_bit_matches_8_bit() {
  check_deep_variant(BitDepth::UInt16, 2);
}

void red_eye_32_bit_matches_8_bit() {
  check_deep_variant(BitDepth::Float32, 6);
}

void red_eye_keeps_alpha_and_skips_transparent_red() {
  PixelBuffer pixels(16, 16, PixelFormat::rgba8());
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 200;
      px[1] = 40;
      px[2] = 40;
      px[3] = x < 8 ? 255 : 0;
    }
  }
  const auto mask = patchy::find_red_eye(pixels, Rect{0, 0, 16, 16}, 4, 8, 8, RedEyeOptions{});
  CHECK(mask.pupil_pixels == 8 * 16);
  patchy::apply_red_eye(pixels, mask, RedEyeOptions{});
  CHECK(pixels.pixel(3, 8)[0] < 40 && pixels.pixel(3, 8)[3] == 255);
  CHECK(pixels.pixel(12, 8)[0] == 200 && pixels.pixel(12, 8)[3] == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> red_eye_tests() {
  return {
      {"red_eye_click_removes_red_and_darkens_pupil", red_eye_click_removes_red_and_darkens_pupil},
      {"red_eye_click_beside_pupil_finds_nearest_red", red_eye_click_beside_pupil_finds_nearest_red},
      {"red_eye_pupil_size_takes_in_the_rim", red_eye_pupil_size_takes_in_the_rim},
      {"red_eye_darken_amount_scales_brightness", red_eye_darken_amount_scales_brightness},
      {"red_eye_selection_limits_the_change", red_eye_selection_limits_the_change},
      {"red_eye_16_bit_matches_8_bit", red_eye_16_bit_matches_8_bit},
      {"red_eye_32_bit_matches_8_bit", red_eye_32_bit_matches_8_bit},
      {"red_eye_keeps_alpha_and_skips_transparent_red", red_eye_keeps_alpha_and_skips_transparent_red},
  };
}
