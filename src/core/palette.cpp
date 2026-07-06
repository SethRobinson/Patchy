#include "core/palette.hpp"

#include "core/document.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace patchy {
namespace {

inline constexpr std::size_t kMaxPaletteColors = 256;

[[nodiscard]] std::size_t lut_bucket(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  return (static_cast<std::size_t>(r >> 3U) << 10U) | (static_cast<std::size_t>(g >> 3U) << 5U) |
         static_cast<std::size_t>(b >> 3U);
}

[[nodiscard]] std::uint8_t clamp_channel(std::int32_t value) noexcept {
  return static_cast<std::uint8_t>(std::clamp<std::int32_t>(value, 0, 255));
}

struct ColorBox {
  std::vector<PaletteColorCount> colors;
  std::uint64_t population{0};
  std::uint8_t min_red{0};
  std::uint8_t max_red{0};
  std::uint8_t min_green{0};
  std::uint8_t max_green{0};
  std::uint8_t min_blue{0};
  std::uint8_t max_blue{0};
};

[[nodiscard]] ColorBox make_box(std::vector<PaletteColorCount> colors) {
  if (colors.empty()) {
    throw std::runtime_error("Cannot quantize an empty color box");
  }
  ColorBox box;
  box.colors = std::move(colors);
  box.min_red = box.max_red = box.colors.front().color.red;
  box.min_green = box.max_green = box.colors.front().color.green;
  box.min_blue = box.max_blue = box.colors.front().color.blue;
  for (const auto& entry : box.colors) {
    box.population += entry.count;
    box.min_red = std::min(box.min_red, entry.color.red);
    box.max_red = std::max(box.max_red, entry.color.red);
    box.min_green = std::min(box.min_green, entry.color.green);
    box.max_green = std::max(box.max_green, entry.color.green);
    box.min_blue = std::min(box.min_blue, entry.color.blue);
    box.max_blue = std::max(box.max_blue, entry.color.blue);
  }
  return box;
}

[[nodiscard]] int widest_channel(const ColorBox& box) noexcept {
  const auto red_range = static_cast<int>(box.max_red) - static_cast<int>(box.min_red);
  const auto green_range = static_cast<int>(box.max_green) - static_cast<int>(box.min_green);
  const auto blue_range = static_cast<int>(box.max_blue) - static_cast<int>(box.min_blue);
  if (red_range >= green_range && red_range >= blue_range) {
    return 0;
  }
  if (green_range >= blue_range) {
    return 1;
  }
  return 2;
}

[[nodiscard]] int channel_value(RgbColor color, int channel) noexcept {
  switch (channel) {
    case 0:
      return color.red;
    case 1:
      return color.green;
    default:
      return color.blue;
  }
}

// Ordered-dither threshold matrices (values 0..n*n-1).
inline constexpr std::array<std::array<std::uint8_t, 4>, 4> kBayer4 = {{
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
}};

inline constexpr std::array<std::array<std::uint8_t, 8>, 8> kBayer8 = {{
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
}};

struct DirtyBounds {
  std::int32_t min_x{std::numeric_limits<std::int32_t>::max()};
  std::int32_t min_y{std::numeric_limits<std::int32_t>::max()};
  std::int32_t max_x{std::numeric_limits<std::int32_t>::min()};
  std::int32_t max_y{std::numeric_limits<std::int32_t>::min()};

  void include(std::int32_t x, std::int32_t y) noexcept {
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
  }

  [[nodiscard]] Rect to_rect() const noexcept {
    if (max_x < min_x || max_y < min_y) {
      return Rect{};
    }
    return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  }
};

// Thresholds a 4-channel pixel's alpha in place; returns true when the pixel is
// visible afterwards. 3-channel pixels are always visible.
[[nodiscard]] bool threshold_pixel_alpha(std::uint8_t* px, std::uint16_t channels, std::uint8_t alpha_threshold,
                                         bool& changed) noexcept {
  if (channels < 4) {
    return true;
  }
  const auto hardened = px[3] >= alpha_threshold ? std::uint8_t{255} : std::uint8_t{0};
  if (px[3] != hardened) {
    px[3] = hardened;
    changed = true;
  }
  return hardened == 255;
}

}  // namespace

std::uint32_t palette_color_key(RgbColor color) noexcept {
  return (static_cast<std::uint32_t>(color.red) << 16U) | (static_cast<std::uint32_t>(color.green) << 8U) |
         static_cast<std::uint32_t>(color.blue);
}

RgbColor palette_color_from_key(std::uint32_t key) noexcept {
  return RgbColor{static_cast<std::uint8_t>((key >> 16U) & 0xffU), static_cast<std::uint8_t>((key >> 8U) & 0xffU),
                  static_cast<std::uint8_t>(key & 0xffU)};
}

std::uint16_t nearest_palette_index(RgbColor color, std::span<const RgbColor> palette) noexcept {
  std::uint32_t best_distance = std::numeric_limits<std::uint32_t>::max();
  std::uint16_t best_index = 0;
  const auto count = std::min<std::size_t>(palette.size(), kMaxPaletteColors);
  for (std::uint16_t index = 0; index < count; ++index) {
    const auto& candidate = palette[index];
    const auto dr = static_cast<int>(color.red) - static_cast<int>(candidate.red);
    const auto dg = static_cast<int>(color.green) - static_cast<int>(candidate.green);
    const auto db = static_cast<int>(color.blue) - static_cast<int>(candidate.blue);
    const auto distance = static_cast<std::uint32_t>(dr * dr + dg * dg + db * db);
    if (distance < best_distance) {
      best_distance = distance;
      best_index = index;
    }
  }
  return best_index;
}

void PaletteLut::build(std::span<const RgbColor> colors) {
  colors_.assign(colors.begin(), colors.begin() + std::min<std::size_t>(colors.size(), kMaxPaletteColors));
  exact_.clear();
  table_.clear();
  if (colors_.empty()) {
    return;
  }

  exact_.reserve(colors_.size());
  for (const auto& color : colors_) {
    exact_.insert(palette_color_key(color));
  }

  table_.resize(32768);
  for (std::size_t bucket = 0; bucket < table_.size(); ++bucket) {
    const auto center = RgbColor{static_cast<std::uint8_t>(((bucket >> 10U) & 0x1fU) << 3U | 4U),
                                 static_cast<std::uint8_t>(((bucket >> 5U) & 0x1fU) << 3U | 4U),
                                 static_cast<std::uint8_t>((bucket & 0x1fU) << 3U | 4U)};
    table_[bucket] = static_cast<std::uint8_t>(nearest_palette_index(center, colors_));
  }
}

bool PaletteLut::contains(RgbColor color) const noexcept {
  return exact_.find(palette_color_key(color)) != exact_.end();
}

std::uint16_t PaletteLut::index_for(std::uint8_t r, std::uint8_t g, std::uint8_t b) const noexcept {
  if (table_.empty()) {
    return 0;
  }
  return table_[lut_bucket(r, g, b)];
}

RgbColor PaletteLut::snap(std::uint8_t r, std::uint8_t g, std::uint8_t b) const noexcept {
  if (colors_.empty()) {
    return RgbColor{r, g, b};
  }
  const auto color = RgbColor{r, g, b};
  if (contains(color)) {
    return color;
  }
  return colors_[table_[lut_bucket(r, g, b)]];
}

void snap_pixel_to_palette(std::uint8_t* px, std::uint16_t channels, const PaletteSnapContext& context) noexcept {
  if (context.lut == nullptr || context.lut->empty() || channels < 3) {
    return;
  }
  const auto snapped = context.lut->snap(px[0], px[1], px[2]);
  px[0] = snapped.red;
  px[1] = snapped.green;
  px[2] = snapped.blue;
  if (channels >= 4) {
    px[3] = px[3] >= context.alpha_threshold ? 255 : 0;
  }
}

Rect remap_exact_color(PixelBuffer& pixels, RgbColor from, RgbColor to) {
  const auto channels = pixels.format().channels;
  if (channels < 3 || palette_color_key(from) == palette_color_key(to)) {
    return Rect{};
  }

  DirtyBounds bounds;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      if (px[0] == from.red && px[1] == from.green && px[2] == from.blue) {
        px[0] = to.red;
        px[1] = to.green;
        px[2] = to.blue;
        bounds.include(x, y);
      }
    }
  }
  return bounds.to_rect();
}

namespace {

void remap_layers_exact_color(std::vector<Layer>& layers, RgbColor from, RgbColor to) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      remap_layers_exact_color(layer.children(), from, to);
      continue;
    }
    if (layer.kind() == LayerKind::Adjustment) {
      continue;
    }
    auto& pixels = layer.pixels();
    if (pixels.width() <= 0 || pixels.height() <= 0 || pixels.format().channels < 3) {
      continue;
    }
    (void)remap_exact_color(pixels, from, to);
  }
}

}  // namespace

void remap_document_exact_color(Document& document, RgbColor from, RgbColor to) {
  remap_layers_exact_color(document.layers(), from, to);
}

std::vector<PaletteColorCount> collect_color_counts(const PixelBuffer& pixels, std::uint8_t alpha_threshold) {
  const auto channels = pixels.format().channels;
  std::unordered_map<std::uint32_t, std::uint64_t> counts;
  counts.reserve(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()));
  if (channels >= 3) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* pixel = pixels.pixel(x, y);
        if (channels >= 4 && alpha_threshold > 0 && pixel[3] < alpha_threshold) {
          continue;
        }
        ++counts[palette_color_key(RgbColor{pixel[0], pixel[1], pixel[2]})];
      }
    }
  }

  std::vector<PaletteColorCount> colors;
  colors.reserve(counts.size());
  for (const auto& [key, count] : counts) {
    colors.push_back(PaletteColorCount{palette_color_from_key(key), count});
  }
  std::sort(colors.begin(), colors.end(), [](const PaletteColorCount& lhs, const PaletteColorCount& rhs) {
    return palette_color_key(lhs.color) < palette_color_key(rhs.color);
  });
  return colors;
}

std::vector<RgbColor> median_cut_palette(const std::vector<PaletteColorCount>& colors, std::size_t target_size) {
  if (colors.empty() || target_size == 0) {
    return {};
  }
  std::vector<ColorBox> boxes;
  boxes.push_back(make_box(colors));

  while (boxes.size() < target_size) {
    auto best = boxes.end();
    for (auto it = boxes.begin(); it != boxes.end(); ++it) {
      if (it->colors.size() < 2) {
        continue;
      }
      if (best == boxes.end()) {
        best = it;
        continue;
      }
      const auto range = std::max({static_cast<int>(it->max_red) - static_cast<int>(it->min_red),
                                   static_cast<int>(it->max_green) - static_cast<int>(it->min_green),
                                   static_cast<int>(it->max_blue) - static_cast<int>(it->min_blue)});
      const auto best_range = std::max({static_cast<int>(best->max_red) - static_cast<int>(best->min_red),
                                        static_cast<int>(best->max_green) - static_cast<int>(best->min_green),
                                        static_cast<int>(best->max_blue) - static_cast<int>(best->min_blue)});
      if (range > best_range || (range == best_range && it->population > best->population)) {
        best = it;
      }
    }
    if (best == boxes.end()) {
      break;
    }

    auto sorted = std::move(best->colors);
    const auto channel = widest_channel(*best);
    std::sort(sorted.begin(), sorted.end(), [channel](const PaletteColorCount& lhs, const PaletteColorCount& rhs) {
      const auto lhs_channel = channel_value(lhs.color, channel);
      const auto rhs_channel = channel_value(rhs.color, channel);
      if (lhs_channel != rhs_channel) {
        return lhs_channel < rhs_channel;
      }
      return palette_color_key(lhs.color) < palette_color_key(rhs.color);
    });

    const auto half_population = std::max<std::uint64_t>(1, best->population / 2U);
    std::uint64_t running = 0;
    std::size_t split = 0;
    for (; split + 1U < sorted.size(); ++split) {
      running += sorted[split].count;
      if (running >= half_population) {
        ++split;
        break;
      }
    }
    split = std::clamp<std::size_t>(split, 1, sorted.size() - 1U);

    std::vector<PaletteColorCount> left(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(split));
    std::vector<PaletteColorCount> right(sorted.begin() + static_cast<std::ptrdiff_t>(split), sorted.end());
    *best = make_box(std::move(left));
    boxes.push_back(make_box(std::move(right)));
  }

  std::sort(boxes.begin(), boxes.end(), [](const ColorBox& lhs, const ColorBox& rhs) {
    return palette_color_key(lhs.colors.front().color) < palette_color_key(rhs.colors.front().color);
  });

  std::vector<RgbColor> palette;
  palette.reserve(boxes.size());
  for (const auto& box : boxes) {
    std::uint64_t red = 0;
    std::uint64_t green = 0;
    std::uint64_t blue = 0;
    for (const auto& entry : box.colors) {
      red += static_cast<std::uint64_t>(entry.color.red) * entry.count;
      green += static_cast<std::uint64_t>(entry.color.green) * entry.count;
      blue += static_cast<std::uint64_t>(entry.color.blue) * entry.count;
    }
    const auto divisor = std::max<std::uint64_t>(1, box.population);
    palette.push_back(RgbColor{static_cast<std::uint8_t>((red + divisor / 2U) / divisor),
                               static_cast<std::uint8_t>((green + divisor / 2U) / divisor),
                               static_cast<std::uint8_t>((blue + divisor / 2U) / divisor)});
  }
  return palette;
}

std::optional<Palette> exact_palette_from_pixels(const PixelBuffer& pixels, std::size_t cap,
                                                 std::uint8_t alpha_threshold) {
  const auto counts = collect_color_counts(pixels, alpha_threshold);
  if (counts.empty() || counts.size() > cap) {
    return std::nullopt;
  }
  Palette palette;
  palette.colors.reserve(counts.size());
  for (const auto& entry : counts) {
    palette.colors.push_back(entry.color);
  }
  return palette;
}

Palette quantize_to_palette(const PixelBuffer& pixels, std::size_t max_colors, std::uint8_t alpha_threshold) {
  max_colors = std::clamp<std::size_t>(max_colors, 1, kMaxPaletteColors);
  if (auto exact = exact_palette_from_pixels(pixels, max_colors, alpha_threshold)) {
    return std::move(*exact);
  }
  Palette palette;
  palette.colors = median_cut_palette(collect_color_counts(pixels, alpha_threshold), max_colors);
  return palette;
}

Rect apply_palette_to_pixels(PixelBuffer& pixels, const PaletteLut& lut, PaletteDither dither,
                             std::uint8_t alpha_threshold) {
  if (lut.empty() || pixels.width() <= 0 || pixels.height() <= 0 || pixels.format().channels < 3) {
    return Rect{};
  }

  const auto channels = pixels.format().channels;
  const auto width = pixels.width();
  const auto height = pixels.height();
  DirtyBounds bounds;

  const auto write_snapped = [&](std::int32_t x, std::int32_t y, std::uint8_t* px, RgbColor snapped) {
    if (px[0] != snapped.red || px[1] != snapped.green || px[2] != snapped.blue) {
      px[0] = snapped.red;
      px[1] = snapped.green;
      px[2] = snapped.blue;
      bounds.include(x, y);
    }
  };

  if (dither == PaletteDither::FloydSteinberg) {
    // Serpentine scan; error carried in 1/16 units as plain ints so results are
    // toolchain-deterministic.
    std::vector<std::array<std::int32_t, 3>> current(static_cast<std::size_t>(width), {0, 0, 0});
    std::vector<std::array<std::int32_t, 3>> next(static_cast<std::size_t>(width), {0, 0, 0});
    for (std::int32_t y = 0; y < height; ++y) {
      std::fill(next.begin(), next.end(), std::array<std::int32_t, 3>{0, 0, 0});
      const bool left_to_right = (y % 2) == 0;
      for (std::int32_t step = 0; step < width; ++step) {
        const auto x = left_to_right ? step : (width - 1 - step);
        auto* px = pixels.pixel(x, y);
        bool alpha_changed = false;
        const auto visible = threshold_pixel_alpha(px, channels, alpha_threshold, alpha_changed);
        if (alpha_changed) {
          bounds.include(x, y);
        }
        if (!visible) {
          continue;  // transparent pixels neither receive nor diffuse error
        }
        const auto& carried = current[static_cast<std::size_t>(x)];
        std::array<std::uint8_t, 3> adjusted{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
          adjusted[channel] = clamp_channel(static_cast<std::int32_t>(px[channel]) + carried[channel] / 16);
        }
        const auto snapped = lut.snap(adjusted[0], adjusted[1], adjusted[2]);
        std::array<std::int32_t, 3> error{};
        error[0] = static_cast<std::int32_t>(adjusted[0]) - static_cast<std::int32_t>(snapped.red);
        error[1] = static_cast<std::int32_t>(adjusted[1]) - static_cast<std::int32_t>(snapped.green);
        error[2] = static_cast<std::int32_t>(adjusted[2]) - static_cast<std::int32_t>(snapped.blue);
        write_snapped(x, y, px, snapped);

        const auto forward = left_to_right ? 1 : -1;
        for (std::size_t channel = 0; channel < 3; ++channel) {
          const auto err = error[channel];
          if (err == 0) {
            continue;
          }
          const auto ahead = x + forward;
          const auto behind = x - forward;
          if (ahead >= 0 && ahead < width) {
            current[static_cast<std::size_t>(ahead)][channel] += err * 7;
            next[static_cast<std::size_t>(ahead)][channel] += err;
          }
          if (behind >= 0 && behind < width) {
            next[static_cast<std::size_t>(behind)][channel] += err * 3;
          }
          next[static_cast<std::size_t>(x)][channel] += err * 5;
        }
      }
      std::swap(current, next);
    }
    return bounds.to_rect();
  }

  const auto bayer_offset = [dither](std::int32_t x, std::int32_t y) -> std::int32_t {
    // Signed offset in roughly [-24, 24): ((m + 0.5) / n^2 - 0.5) * 48 in int math.
    if (dither == PaletteDither::OrderedBayer4x4) {
      const auto m = static_cast<std::int32_t>(kBayer4[static_cast<std::size_t>(y % 4)][static_cast<std::size_t>(x % 4)]);
      return ((2 * m + 1) * 48 - 16 * 48) / 32;
    }
    if (dither == PaletteDither::OrderedBayer8x8) {
      const auto m = static_cast<std::int32_t>(kBayer8[static_cast<std::size_t>(y % 8)][static_cast<std::size_t>(x % 8)]);
      return ((2 * m + 1) * 48 - 64 * 48) / 128;
    }
    return 0;
  };

  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = pixels.pixel(x, y);
      bool alpha_changed = false;
      const auto visible = threshold_pixel_alpha(px, channels, alpha_threshold, alpha_changed);
      if (alpha_changed) {
        bounds.include(x, y);
      }
      if (!visible) {
        continue;
      }
      const auto offset = bayer_offset(x, y);
      const auto snapped = lut.snap(clamp_channel(static_cast<std::int32_t>(px[0]) + offset),
                                    clamp_channel(static_cast<std::int32_t>(px[1]) + offset),
                                    clamp_channel(static_cast<std::int32_t>(px[2]) + offset));
      write_snapped(x, y, px, snapped);
    }
  }
  return bounds.to_rect();
}

bool pixels_are_palette_clean(const PixelBuffer& pixels, const PaletteLut& lut) noexcept {
  if (lut.empty()) {
    return false;
  }
  const auto channels = pixels.format().channels;
  if (channels < 3) {
    return true;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      if (channels >= 4) {
        if (px[3] == 0) {
          continue;
        }
        if (px[3] != 255) {
          return false;
        }
      }
      if (!lut.contains(RgbColor{px[0], px[1], px[2]})) {
        return false;
      }
    }
  }
  return true;
}

void sync_document_indexed_palette(Document& document) {
  const auto& editing = document.palette_editing();
  if (!editing.has_value() || editing->palette.colors.empty()) {
    return;
  }
  const auto count = editing->palette.colors.size();
  const std::uint16_t bit_depth = count <= 4 ? 2 : (count <= 16 ? 4 : 8);
  document.indexed_palette() = DocumentIndexedPalette{editing->palette.colors, bit_depth};
}

}  // namespace patchy
