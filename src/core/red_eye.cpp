#include "core/red_eye.hpp"

#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace patchy {

namespace {

// Redness rule: r^2 / (g^2 + b^2 + 20^2) must reach a threshold that falls from 6.0 at
// Pupil Size 1 to 1.8 at 100 (both x100 here). The +20^2 keeps near-black noise from
// counting; the square keeps brown irises (about 1.5) and skin (about 1.0) out at every
// setting while a flash-red pupil sits near 10.
constexpr std::int64_t kDarkFloor = 400;
constexpr int kMinRed = 24;

std::int64_t redness_threshold_x100(int pupil_size) noexcept {
  const auto size = std::clamp(pupil_size, 1, 100);
  return 180 + (static_cast<std::int64_t>(100 - size) * 420) / 99;
}

// 0 when not red, else 192..255 rising with how far past the threshold the pixel is (full
// strength a quarter past it), so a pixel that counts as pupil turns neutral.
std::uint8_t red_weight(int r, int g, int b, std::int64_t threshold) noexcept {
  if (r < kMinRed || r <= g || r <= b) {
    return 0;
  }
  const auto numerator = static_cast<std::int64_t>(r) * r * 100;
  const auto denominator = threshold * (static_cast<std::int64_t>(g) * g + static_cast<std::int64_t>(b) * b + kDarkFloor);
  if (numerator < denominator) {
    return 0;
  }
  const auto rise = (252 * (numerator - denominator)) / denominator;
  return static_cast<std::uint8_t>(std::min<std::int64_t>(255, 192 + rise));
}

// One pixel as display-encoded 8-bit RGB plus whether it is at least half opaque.
struct EncodedSample {
  int r{0};
  int g{0};
  int b{0};
  bool opaque{false};
};

int round_encoded(float value) noexcept {
  return static_cast<int>(std::lround(std::clamp(value, 0.0F, 255.0F)));
}

EncodedSample encoded_sample(const PixelBuffer& pixels, std::int32_t x, std::int32_t y) {
  const auto format = pixels.format();
  const auto* px = pixels.pixel(x, y);
  if (format.bit_depth == BitDepth::UInt8) {
    return {px[0], px[1], px[2], format.channels < 4 || px[3] >= 128};
  }
  const auto values = load_pixel(format, px);
  EncodedSample sample;
  sample.opaque = values[3] >= 127.5F;
  if (format.bit_depth == BitDepth::Float32) {
    const auto encode = [](float linear) {
      return round_encoded(static_cast<float>(srgb_encode(std::clamp(linear / kDeepScale, 0.0F, 1.0F))) * kDeepScale);
    };
    sample.r = encode(values[0]);
    sample.g = encode(values[1]);
    sample.b = encode(values[2]);
  } else {
    sample.r = round_encoded(values[0]);
    sample.g = round_encoded(values[1]);
    sample.b = round_encoded(values[2]);
  }
  return sample;
}

}  // namespace

std::uint8_t RedEyeMask::at(std::int32_t x, std::int32_t y) const noexcept {
  if (!bounds.contains(x, y)) {
    return 0;
  }
  return weight[static_cast<std::size_t>(y - bounds.y) * static_cast<std::size_t>(bounds.width) +
                static_cast<std::size_t>(x - bounds.x)];
}

std::int32_t red_eye_click_box_size(std::int32_t document_width, std::int32_t document_height) noexcept {
  // About a sixth of the shorter side: an eye with its lids in a portrait, a whole face in a
  // group shot (the connected-area rule keeps a click to one pupil either way).
  return std::clamp(std::min(document_width, document_height) / 6, 32, 600);
}

bool red_eye_is_red(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept {
  return red_weight(r, g, b, redness_threshold_x100(pupil_size)) != 0;
}

RedEyeMask find_red_eye(const PixelBuffer& pixels, Rect region, std::int32_t seed_x, std::int32_t seed_y,
                        std::int32_t search_radius, const RedEyeOptions& options) {
  RedEyeMask mask;
  if (pixels.empty() || pixels.format().channels < 3) {
    return mask;
  }
  const Rect buffer_rect{0, 0, pixels.width(), pixels.height()};
  region = intersect_rect(region, buffer_rect);
  if (region.empty()) {
    return mask;
  }
  const auto threshold = redness_threshold_x100(options.pupil_size);
  const auto width = region.width;
  const auto height = region.height;
  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto index_of = [width](std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  };

  // Red weight of every region pixel (0 = not red).
  std::vector<std::uint8_t> red(cells, 0U);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto sample = encoded_sample(pixels, region.x + x, region.y + y);
      if (sample.opaque) {
        red[index_of(x, y)] = red_weight(sample.r, sample.g, sample.b, threshold);
      }
    }
  }

  // The seed: the user's point when it is red, else the nearest red pixel within reach
  // (row-major scan with a strict comparison, so ties go to the first one found).
  std::int32_t start_x = seed_x - region.x;
  std::int32_t start_y = seed_y - region.y;
  const bool seed_inside = start_x >= 0 && start_y >= 0 && start_x < width && start_y < height;
  if (!seed_inside || red[index_of(start_x, start_y)] == 0U) {
    const auto reach = static_cast<std::int64_t>(std::max(0, search_radius));
    auto best = std::numeric_limits<std::int64_t>::max();
    std::int32_t best_x = -1;
    std::int32_t best_y = -1;
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        if (red[index_of(x, y)] == 0U) {
          continue;
        }
        const auto dx = static_cast<std::int64_t>(x - start_x);
        const auto dy = static_cast<std::int64_t>(y - start_y);
        const auto distance = dx * dx + dy * dy;
        if (distance <= reach * reach && distance < best) {
          best = distance;
          best_x = x;
          best_y = y;
        }
      }
    }
    if (best_x < 0) {
      return mask;
    }
    start_x = best_x;
    start_y = best_y;
  }

  // Grow the connected red area (8-connected) from the seed.
  std::vector<std::uint8_t> area(cells, 0U);
  std::vector<std::int32_t> queue;
  queue.reserve(256);
  area[index_of(start_x, start_y)] = red[index_of(start_x, start_y)];
  queue.push_back(static_cast<std::int32_t>(index_of(start_x, start_y)));
  std::int32_t min_x = start_x;
  std::int32_t max_x = start_x;
  std::int32_t min_y = start_y;
  std::int32_t max_y = start_y;
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto cell = queue[head];
    const auto cx = cell % width;
    const auto cy = cell / width;
    min_x = std::min(min_x, cx);
    max_x = std::max(max_x, cx);
    min_y = std::min(min_y, cy);
    max_y = std::max(max_y, cy);
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
      for (std::int32_t dx = -1; dx <= 1; ++dx) {
        const auto nx = cx + dx;
        const auto ny = cy + dy;
        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
          continue;
        }
        const auto neighbor = index_of(nx, ny);
        if (area[neighbor] != 0U || red[neighbor] == 0U) {
          continue;
        }
        area[neighbor] = red[neighbor];
        queue.push_back(static_cast<std::int32_t>(neighbor));
      }
    }
  }
  mask.pupil_pixels = static_cast<std::int64_t>(queue.size());

  // Weights over the area's bounds plus a one-pixel ring: area pixels keep their red
  // weight, ring pixels get the 3x3 mean of the area around them (the feathered edge).
  mask.bounds = intersect_rect(Rect{region.x + min_x - 1, region.y + min_y - 1, max_x - min_x + 3, max_y - min_y + 3},
                               buffer_rect);
  mask.weight.assign(static_cast<std::size_t>(mask.bounds.width) * static_cast<std::size_t>(mask.bounds.height), 0U);
  const auto area_at = [&](std::int32_t x, std::int32_t y) -> int {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return 0;
    }
    return area[index_of(x, y)];
  };
  for (std::int32_t y = 0; y < mask.bounds.height; ++y) {
    for (std::int32_t x = 0; x < mask.bounds.width; ++x) {
      const auto rx = mask.bounds.x + x - region.x;
      const auto ry = mask.bounds.y + y - region.y;
      auto value = area_at(rx, ry);
      if (value == 0) {
        int sum = 0;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
          for (std::int32_t dx = -1; dx <= 1; ++dx) {
            sum += area_at(rx + dx, ry + dy);
          }
        }
        value = sum / 9;
      }
      mask.weight[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.bounds.width) +
                  static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(value);
    }
  }
  return mask;
}

Rect apply_red_eye(PixelBuffer& pixels, const RedEyeMask& mask, const RedEyeOptions& options,
                   const std::function<float(std::int32_t, std::int32_t)>& coverage,
                   const PaletteSnapContext* palette_snap) {
  Rect dirty{};
  const auto format = pixels.format();
  if (mask.empty() || format.channels < 3) {
    return dirty;
  }
  const auto bounds = intersect_rect(mask.bounds, Rect{0, 0, pixels.width(), pixels.height()});
  if (bounds.empty()) {
    return dirty;
  }
  const auto darken = std::clamp(options.darken_amount, 1, 100);
  // One detach on this thread before any write (copy-on-write storage, AGENTS.md).
  const auto bytes = pixels.data();
  const auto stride = pixels.stride_bytes();
  const auto pixel_bytes = bytes_per_pixel(format);
  std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_x = -1;
  std::int32_t max_y = -1;
  for (std::int32_t y = bounds.y; y < bounds.y + bounds.height; ++y) {
    for (std::int32_t x = bounds.x; x < bounds.x + bounds.width; ++x) {
      int weight = mask.at(x, y);
      if (weight == 0) {
        continue;
      }
      if (coverage) {
        const auto amount = std::clamp(coverage(x, y), 0.0F, 1.0F);
        weight = static_cast<int>(static_cast<float>(weight) * amount + 0.5F);
        if (weight == 0) {
          continue;
        }
      }
      if (palette_snap != nullptr) {
        if (static_cast<float>(weight) < palette_snap->coverage_threshold * 255.0F) {
          continue;
        }
        weight = 255;
      }
      auto* px = bytes.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * pixel_bytes;
      if (format.bit_depth == BitDepth::UInt8) {
        if (format.channels >= 4 && px[3] == 0) {
          continue;  // fully transparent: nothing to see, leave the hidden color alone
        }
        int r = px[0];
        const int g = px[1];
        const int b = px[2];
        const int neutral = (g + b + 1) / 2;
        if (r > neutral) {
          r -= ((r - neutral) * weight + 127) / 255;
        }
        const int keep = 25500 - darken * weight;
        px[0] = static_cast<std::uint8_t>((r * keep + 12750) / 25500);
        px[1] = static_cast<std::uint8_t>((g * keep + 12750) / 25500);
        px[2] = static_cast<std::uint8_t>((b * keep + 12750) / 25500);
        if (palette_snap != nullptr) {
          snap_pixel_to_palette(px, format.channels, *palette_snap);
        }
      } else {
        // 16-bit values are display-encoded and 32-bit values linear light (both on the
        // deep scale). The fix runs on encoded values, as at 8 bits: a 32-bit pixel within
        // 0..1 goes through the sRGB curve and back. A brighter 32-bit pixel (the curve
        // ends at 1.0) is fixed in linear light, the darkening raised to the 2.2 gamma.
        auto values = load_pixel(format, px);
        if (values[3] <= 0.0F) {
          continue;
        }
        const bool linear = format.bit_depth == BitDepth::Float32;
        const bool encode = linear && std::max({values[0], values[1], values[2]}) <= kDeepScale;
        if (encode) {
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            values[channel] =
                static_cast<float>(srgb_encode(std::max(0.0F, values[channel]) / kDeepScale)) * kDeepScale;
          }
        }
        const auto amount = static_cast<float>(weight) / 255.0F;
        const auto neutral = (values[1] + values[2]) * 0.5F;
        if (values[0] > neutral) {
          values[0] -= (values[0] - neutral) * amount;
        }
        auto keep = std::max(0.0F, 1.0F - static_cast<float>(darken) / 100.0F * amount);
        if (linear && !encode) {
          keep = std::pow(keep, 2.2F);
        }
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          values[channel] *= keep;
          if (encode) {
            values[channel] = static_cast<float>(srgb_decode(values[channel] / kDeepScale)) * kDeepScale;
          }
        }
        store_pixel(format, px, values);
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x >= 0) {
    dirty = Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  }
  return dirty;
}

}  // namespace patchy
