#include "core/red_eye.hpp"

#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <vector>

namespace patchy {

namespace {

// Weights run 0..kOne (fixed point); every rule is integer math on display-encoded 8-bit
// values, so the mask is the same on every toolchain and at every depth.
constexpr int kOne = 256;
// Redness a pixel needs to seed the area.
constexpr int kSeedRedness = 160;
// Soft ring outside the filled area, one value per ring.
constexpr std::array<std::uint8_t, 3> kRing{192, 112, 40};

int ramp(int value, int low, int high) noexcept {
  if (value <= low) {
    return 0;
  }
  if (value >= high) {
    return kOne;
  }
  return (value - low) * kOne / (high - low);
}

// Pupil Size 1..100 onto the internal scale, 50 at the user's 1 and 90 at 100: the default
// (70 inside) catches orange and deep red red-eye, the top end pink and purple red-eye.
int effective_pupil(int pupil_size) noexcept {
  return 70 + (std::clamp(pupil_size, 1, 100) - 50) * 2 / 5;
}

struct HueSat {
  int hue_abs;     // distance from pure red in 1/10 degree
  bool magenta;    // toward magenta (blue above green) rather than orange
  int saturation;  // 1/1000
};

// Only for pixels whose red channel is the largest.
HueSat red_hue_sat(int r, int g, int b) noexcept {
  const auto chroma = r - std::min(g, b);
  return {600 * std::abs(g - b) / chroma, b > g, 1000 * chroma / r};
}

// Flash red: a hue near red (to 8 degrees toward orange, where brown irises, skin and lashes
// sit, and to 30 toward magenta), saturated, and red well above green and blue. Drives the
// area and the darkening.
int redness(int r, int g, int b, int pupil) noexcept {
  if (r <= g || r <= b || r < 16) {
    return 0;
  }
  const auto [hue, magenta, saturation] = red_hue_sat(r, g, b);
  const auto shift = pupil - 50;
  const auto w_hue = magenta ? kOne - ramp(hue, 300, 450) : kOne - ramp(hue, 80, 180);
  const auto w_sat = ramp(saturation, 350 - shift * 2, 550 - shift * 2);
  const auto w_excess = ramp(r - std::max(g, b), 24 - shift / 5, 64 - shift / 3);
  return w_hue * w_sat / kOne * w_excess / kOne;
}

// Any reddish or pinkish cast, however faint. Drives the desaturation inside the area.
int tint(int r, int g, int b) noexcept {
  if (r <= g || r <= b) {
    return 0;
  }
  const auto [hue, magenta, saturation] = red_hue_sat(r, g, b);
  const auto w_hue = magenta ? kOne - ramp(hue, 400, 600) : kOne - ramp(hue, 150, 300);
  return w_hue * ramp(saturation, 60, 220) / kOne;
}

// Any color cast at all (1/1000 saturation onto a weight), for inside the filled area.
int cast(int r, int g, int b) noexcept {
  const auto high = std::max({r, g, b});
  if (high == 0) {
    return 0;
  }
  return ramp(1000 * (high - std::min({r, g, b})) / high, 60, 220);
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

float encode_linear(float linear) noexcept {
  return static_cast<float>(srgb_encode(std::clamp(linear / kDeepScale, 0.0F, 1.0F))) * kDeepScale;
}

EncodedSample encoded_sample(PixelFormat format, const std::uint8_t* px) {
  if (format.bit_depth == BitDepth::UInt8) {
    return {px[0], px[1], px[2], format.channels < 4 || px[3] >= 128};
  }
  const auto values = load_pixel(format, px);
  EncodedSample sample;
  sample.opaque = values[3] >= 127.5F;
  const bool linear = format.bit_depth == BitDepth::Float32;
  sample.r = round_encoded(linear ? encode_linear(values[0]) : values[0]);
  sample.g = round_encoded(linear ? encode_linear(values[1]) : values[1]);
  sample.b = round_encoded(linear ? encode_linear(values[2]) : values[2]);
  return sample;
}

// 8-connected growth from (x, y) over cells where `accept` holds; marks `area`, returns the
// count and the bounds.
template <typename Accept>
std::int64_t grow(std::int32_t width, std::int32_t height, std::int32_t x, std::int32_t y,
                  std::vector<std::uint8_t>& area, std::vector<std::int32_t>& queue, Accept accept,
                  std::array<std::int32_t, 4>& box) {
  std::fill(area.begin(), area.end(), std::uint8_t{0});
  queue.clear();
  const auto start = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  area[start] = 1;
  queue.push_back(static_cast<std::int32_t>(start));
  box = {x, y, x, y};
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto cx = queue[head] % width;
    const auto cy = queue[head] / width;
    box[0] = std::min(box[0], cx);
    box[1] = std::min(box[1], cy);
    box[2] = std::max(box[2], cx);
    box[3] = std::max(box[3], cy);
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
      for (std::int32_t dx = -1; dx <= 1; ++dx) {
        const auto nx = cx + dx;
        const auto ny = cy + dy;
        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
          continue;
        }
        const auto cell = static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
        if (area[cell] != 0U || !accept(nx, ny, cell)) {
          continue;
        }
        area[cell] = 1;
        queue.push_back(static_cast<std::int32_t>(cell));
      }
    }
  }
  return static_cast<std::int64_t>(queue.size());
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
  // A third of the longer side: big enough for a close-up pupil, and the growth rules (not
  // the box) keep a click to one pupil in a group shot.
  return std::clamp(std::max(document_width, document_height) / 3, 64, 1000);
}

int red_eye_redness(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept {
  return redness(r, g, b, effective_pupil(pupil_size));
}

bool red_eye_is_red(std::uint8_t r, std::uint8_t g, std::uint8_t b, int pupil_size) noexcept {
  return red_eye_redness(r, g, b, pupil_size) >= kSeedRedness;
}

RedEyeMask find_red_eye(const PixelBuffer& pixels, Rect region, std::int32_t seed_x, std::int32_t seed_y,
                        std::int32_t search_radius, const RedEyeOptions& options, bool limit_to_disk) {
  RedEyeMask mask;
  if (pixels.empty() || pixels.format().channels < 3 || region.empty()) {
    return mask;
  }
  const Rect buffer_rect{0, 0, pixels.width(), pixels.height()};
  // The working rect holds the region plus room for the soft ring.
  const auto pad = static_cast<std::int32_t>(kRing.size()) + 1;
  const auto work = intersect_rect(Rect{region.x - pad, region.y - pad, region.width + pad * 2, region.height + pad * 2},
                                   buffer_rect);
  const auto grow_rect = intersect_rect(region, buffer_rect);
  if (work.empty() || grow_rect.empty()) {
    return mask;
  }
  const auto pupil = effective_pupil(options.pupil_size);
  const auto width = work.width;
  const auto height = work.height;
  const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto index_of = [width](std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
  };

  // Redness of every pixel the area may grow into (0 elsewhere).
  std::vector<std::uint16_t> red(cells, 0U);
  const auto format = pixels.format();
  for (std::int32_t y = grow_rect.y; y < grow_rect.y + grow_rect.height; ++y) {
    for (std::int32_t x = grow_rect.x; x < grow_rect.x + grow_rect.width; ++x) {
      const auto sample = encoded_sample(format, pixels.pixel(x, y));
      if (sample.opaque) {
        red[index_of(x - work.x, y - work.y)] = static_cast<std::uint16_t>(redness(sample.r, sample.g, sample.b, pupil));
      }
    }
  }

  // A click's disk: inscribed in the region, centered on it.
  const auto disk_x = static_cast<std::int64_t>(region.x - work.x) * 2 + region.width;
  const auto disk_y = static_cast<std::int64_t>(region.y - work.y) * 2 + region.height;
  const auto disk_d = static_cast<std::int64_t>(std::min(region.width, region.height));
  const auto in_disk = [&](std::int32_t x, std::int32_t y) {
    if (!limit_to_disk) {
      return true;
    }
    // In doubled coordinates so odd sizes center exactly.
    const auto dx = static_cast<std::int64_t>(x) * 2 + 1 - disk_x;
    const auto dy = static_cast<std::int64_t>(y) * 2 + 1 - disk_y;
    return dx * dx + dy * dy <= disk_d * disk_d;
  };

  // The seed: the strongly red pixel nearest the user's point within reach (row-major scan
  // with a strict comparison, so ties go to the first one found).
  const auto px_seed = seed_x - work.x;
  const auto py_seed = seed_y - work.y;
  const auto reach = static_cast<std::int64_t>(std::max(0, search_radius));
  auto best = std::numeric_limits<std::int64_t>::max();
  std::int32_t start_x = -1;
  std::int32_t start_y = -1;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      if (red[index_of(x, y)] < kSeedRedness || !in_disk(x, y)) {
        continue;
      }
      const auto dx = static_cast<std::int64_t>(x - px_seed);
      const auto dy = static_cast<std::int64_t>(y - py_seed);
      const auto distance = dx * dx + dy * dy;
      if (distance <= reach * reach && distance < best) {
        best = distance;
        start_x = x;
        start_y = y;
      }
    }
  }
  if (start_x < 0) {
    return mask;
  }

  // Hysteresis growth from the seed; an area that comes out irregular, elongated or (for a
  // click) filling most of the disk has leaked into lashes or skin, so it is regrown with a
  // stricter threshold.
  std::vector<std::uint8_t> area(cells, 0U);
  std::vector<std::int32_t> queue;
  queue.reserve(1024);
  std::array<std::int32_t, 4> box{};
  std::int64_t count = 0;
  const std::array<int, 3> thresholds{std::max(32, 100 - (pupil - 50)), 140, 192};
  const auto disk_area_x100 = 55 * disk_d * disk_d;  // 0.7 * pi * r^2 * 100, r = d / 2
  for (const auto low : thresholds) {
    count = grow(width, height, start_x, start_y, area, queue,
                 [&](std::int32_t x, std::int32_t y, std::size_t cell) { return red[cell] >= low && in_disk(x, y); },
                 box);
    const auto box_w = static_cast<std::int64_t>(box[2] - box[0] + 1);
    const auto box_h = static_cast<std::int64_t>(box[3] - box[1] + 1);
    const bool compact = count * 100 >= 45 * box_w * box_h && std::max(box_w, box_h) * 10 <= 22 * std::min(box_w, box_h);
    const bool small_enough = !limit_to_disk || count * 100 <= disk_area_x100;
    if ((compact && small_enough) || low == thresholds.back()) {
      break;
    }
  }
  mask.pupil_pixels = count;

  // Fill holes (the glint): cells inside the area's bounds that the outside cannot reach.
  const auto bx0 = std::max(0, box[0] - 1);
  const auto by0 = std::max(0, box[1] - 1);
  const auto bx1 = std::min(width - 1, box[2] + 1);
  const auto by1 = std::min(height - 1, box[3] + 1);
  std::vector<std::uint8_t> outside(cells, 0U);
  queue.clear();
  for (std::int32_t y = by0; y <= by1; ++y) {
    for (std::int32_t x = bx0; x <= bx1; ++x) {
      const bool edge = x == bx0 || x == bx1 || y == by0 || y == by1;
      if (edge && area[index_of(x, y)] == 0U) {
        outside[index_of(x, y)] = 1;
        queue.push_back(static_cast<std::int32_t>(index_of(x, y)));
      }
    }
  }
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const auto cx = queue[head] % width;
    const auto cy = queue[head] / width;
    constexpr std::array<std::array<std::int32_t, 2>, 4> kSteps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (const auto& step : kSteps) {
      const auto nx = cx + step[0];
      const auto ny = cy + step[1];
      if (nx < bx0 || ny < by0 || nx > bx1 || ny > by1) {
        continue;
      }
      const auto cell = index_of(nx, ny);
      if (area[cell] == 0U && outside[cell] == 0U) {
        outside[cell] = 1;
        queue.push_back(static_cast<std::int32_t>(cell));
      }
    }
  }
  std::vector<std::uint8_t> soft(cells, 0U);
  for (std::int32_t y = by0; y <= by1; ++y) {
    for (std::int32_t x = bx0; x <= bx1; ++x) {
      if (area[index_of(x, y)] != 0U || outside[index_of(x, y)] == 0U) {
        soft[index_of(x, y)] = 255;
      }
    }
  }

  // Soft ring: three 8-connected dilations outside the filled area.
  std::vector<std::uint8_t> next(cells, 0U);
  for (const auto level : kRing) {
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        if (soft[index_of(x, y)] != 0U) {
          continue;
        }
        bool touches = false;
        for (std::int32_t dy = -1; dy <= 1 && !touches; ++dy) {
          for (std::int32_t dx = -1; dx <= 1; ++dx) {
            const auto nx = x + dx;
            const auto ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < width && ny < height && soft[index_of(nx, ny)] > level) {
              touches = true;
              break;
            }
          }
        }
        next[index_of(x, y)] = touches ? level : std::uint8_t{0};
      }
    }
    for (std::size_t cell = 0; cell < cells; ++cell) {
      soft[cell] = std::max(soft[cell], next[cell]);
    }
  }

  // Crop to the soft area's bounds.
  const auto ring = static_cast<std::int32_t>(kRing.size());
  const auto mx0 = std::max(0, bx0 - ring);
  const auto my0 = std::max(0, by0 - ring);
  const auto mx1 = std::min(width - 1, bx1 + ring);
  const auto my1 = std::min(height - 1, by1 + ring);
  mask.bounds = Rect{work.x + mx0, work.y + my0, mx1 - mx0 + 1, my1 - my0 + 1};
  mask.weight.assign(static_cast<std::size_t>(mask.bounds.width) * static_cast<std::size_t>(mask.bounds.height), 0U);
  for (std::int32_t y = my0; y <= my1; ++y) {
    for (std::int32_t x = mx0; x <= mx1; ++x) {
      mask.weight[static_cast<std::size_t>(y - my0) * static_cast<std::size_t>(mask.bounds.width) +
                  static_cast<std::size_t>(x - mx0)] = soft[index_of(x, y)];
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
  const auto pupil = effective_pupil(options.pupil_size);
  // 192/256 of the darkening at Darken 100, so a quarter of the brightness stays.
  const auto darken = 192 * std::clamp(options.darken_amount, 1, 100) / 100;
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
      const auto stored = mask.at(x, y);
      if (stored == 0U) {
        continue;
      }
      const bool interior = stored == 255U;
      int area = interior ? kOne : stored;
      if (coverage) {
        const auto amount = std::clamp(coverage(x, y), 0.0F, 1.0F);
        area = static_cast<int>(static_cast<float>(area) * amount + 0.5F);
        if (area == 0) {
          continue;
        }
      }
      if (palette_snap != nullptr) {
        if (static_cast<float>(area) < palette_snap->coverage_threshold * static_cast<float>(kOne)) {
          continue;
        }
        area = kOne;
      }
      auto* px = bytes.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * pixel_bytes;
      const auto sample = encoded_sample(format, px);
      if ((format.channels >= 4 && format.bit_depth == BitDepth::UInt8 && px[3] == 0)) {
        continue;  // fully transparent: nothing to see, leave the hidden color alone
      }
      // How much to desaturate (its tint, any cast inside the area) and darken (its redness).
      const auto red = redness(sample.r, sample.g, sample.b, pupil) * area / kOne;
      auto desaturate = std::max(tint(sample.r, sample.g, sample.b) * area / kOne, red);
      if (interior) {
        desaturate = std::max(desaturate, cast(sample.r, sample.g, sample.b) * area / kOne);
      }
      if (desaturate == 0) {
        continue;
      }
      // Squared, so pixels that are only partly red mostly lose their cast and keep their tone.
      const auto keep = kOne - darken * (red * red / kOne) / kOne;
      if (format.bit_depth == BitDepth::UInt8) {
        const int level = (px[1] + px[2]) / 2;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          const int toward = (px[channel] * (kOne - desaturate) + level * desaturate) / kOne;
          px[channel] = static_cast<std::uint8_t>(toward * keep / kOne);
        }
        if (palette_snap != nullptr) {
          snap_pixel_to_palette(px, format.channels, *palette_snap);
        }
      } else {
        // 16-bit values are display-encoded and 32-bit values linear light (both on the deep
        // scale). The fix runs on encoded values, as at 8 bits: a 32-bit pixel within 0..1
        // goes through the sRGB curve and back. A brighter 32-bit pixel (the curve ends at
        // 1.0) is fixed in linear light, the darkening raised to the 2.2 gamma.
        auto values = load_pixel(format, px);
        if (values[3] <= 0.0F) {
          continue;
        }
        const bool linear = format.bit_depth == BitDepth::Float32;
        const bool encode = linear && std::max({values[0], values[1], values[2]}) <= kDeepScale;
        if (encode) {
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            values[channel] = encode_linear(std::max(0.0F, values[channel]));
          }
        }
        const auto amount = static_cast<float>(desaturate) / static_cast<float>(kOne);
        auto factor = static_cast<float>(keep) / static_cast<float>(kOne);
        if (linear && !encode) {
          factor = std::pow(factor, 2.2F);
        }
        const auto level = (values[1] + values[2]) * 0.5F;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          auto value = (values[channel] + (level - values[channel]) * amount) * factor;
          if (encode) {
            value = static_cast<float>(srgb_decode(value / kDeepScale)) * kDeepScale;
          }
          values[channel] = value;
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
