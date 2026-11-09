#include "core/book_scan.hpp"

#include "core/photo_divide.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace patchy {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::int32_t kMaxPageEdge = 30000;
// Gutter search: the profile's sampling grid and the central band searched.
constexpr std::int32_t kGutterColumns = 512;
constexpr std::int32_t kGutterRows = 192;
constexpr std::size_t kGutterPercentile = 80;
constexpr double kGutterBandStart = 0.35;
constexpr double kGutterBandEnd = 0.65;
constexpr std::int32_t kGutterSmoothRadius = 4;
// A fold must be at least this much darker (luminance units, x256) than the band's
// median, else the spread is split at its middle.
constexpr std::int64_t kGutterMinimumContrast = 6 * 256;
// A detected spread must cover at least this share of the fallback outline (or of the
// image when there is none) to be trusted over it.
constexpr double kPlausibleShareOfFallback = 0.5;
constexpr double kPlausibleShareOfImage = 0.1;
constexpr int kColorChroma = 40;
constexpr double kGrayscaleSuggestionFraction = 0.002;

struct Axes {
  double ux;
  double uy;
  double vx;
  double vy;
};

Axes box_axes(double angle_degrees) {
  const double radians = angle_degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  return {c, s, -s, c};
}

double distance(double ax, double ay, double bx, double by) {
  const double dx = bx - ax;
  const double dy = by - ay;
  return std::sqrt(dx * dx + dy * dy);
}

bool supported_page_format(const PixelFormat& format) {
  if (format.bit_depth != BitDepth::UInt8) {
    return false;
  }
  if (format.color_mode == ColorMode::Grayscale) {
    return format.channels == 1 || format.channels == 2;
  }
  return format.color_mode == ColorMode::RGB && (format.channels == 3 || format.channels == 4);
}

bool is_gray(const PixelFormat& format) {
  return format.color_mode == ColorMode::Grayscale;
}

int luma(int red, int green, int blue) {
  return (299 * red + 587 * green + 114 * blue + 500) / 1000;
}

// The pixel over white, as RGB (gray sources repeat the value).
void fetch_rgb(const PixelBuffer& source, std::int32_t x, std::int32_t y, int out[3]) {
  const auto* px = source.pixel(x, y);
  const int channels = source.format().channels;
  const bool gray = is_gray(source.format());
  int red = px[0];
  int green = gray ? px[0] : px[1];
  int blue = gray ? px[0] : px[2];
  const bool has_alpha = gray ? channels == 2 : channels == 4;
  if (has_alpha) {
    const int alpha = px[channels - 1];
    red = (red * alpha + 255 * (255 - alpha) + 127) / 255;
    green = (green * alpha + 255 * (255 - alpha) + 127) / 255;
    blue = (blue * alpha + 255 * (255 - alpha) + 127) / 255;
  }
  out[0] = red;
  out[1] = green;
  out[2] = blue;
}

int fetch_luma(const PixelBuffer& source, std::int32_t x, std::int32_t y) {
  int rgb[3];
  fetch_rgb(source, x, y, rgb);
  return is_gray(source.format()) ? rgb[0] : luma(rgb[0], rgb[1], rgb[2]);
}

double quad_area(const std::array<double, 8>& quad) {
  double twice = 0.0;
  for (int i = 0; i < 4; ++i) {
    const int j = (i + 1) % 4;
    twice += quad[static_cast<std::size_t>(i * 2)] * quad[static_cast<std::size_t>(j * 2 + 1)] -
             quad[static_cast<std::size_t>(j * 2)] * quad[static_cast<std::size_t>(i * 2 + 1)];
  }
  return std::abs(twice) / 2.0;
}

OrientedBox whole_image_box(const PixelBuffer& source) {
  return {source.width() / 2.0, source.height() / 2.0, static_cast<double>(source.width()),
          static_cast<double>(source.height()), 0.0};
}

// The box with its local x range [from, to] (fractions of its width from the left edge).
OrientedBox box_slice(const OrientedBox& box, double from, double to) {
  const Axes axes = box_axes(box.angle_degrees);
  const double local_center = ((from + to) / 2.0 - 0.5) * box.width;
  OrientedBox slice = box;
  slice.center_x = box.center_x + local_center * axes.ux;
  slice.center_y = box.center_y + local_center * axes.uy;
  slice.width = std::max(0.0, (to - from) * box.width);
  return slice;
}

// The box of width x height whose center sits `local_x` along the source box's x axis.
OrientedBox box_at(const OrientedBox& box, double local_x, double width, double height) {
  const Axes axes = box_axes(box.angle_degrees);
  OrientedBox placed = box;
  placed.center_x = box.center_x + local_x * axes.ux;
  placed.center_y = box.center_y + local_x * axes.uy;
  placed.width = width;
  placed.height = height;
  return placed;
}

double median_of(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return values[middle];
  }
  return (values[middle - 1] + values[middle]) / 2.0;
}

}  // namespace

bool operator==(const OrientedBox& lhs, const OrientedBox& rhs) {
  return lhs.center_x == rhs.center_x && lhs.center_y == rhs.center_y && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.angle_degrees == rhs.angle_degrees;
}

std::array<double, 8> oriented_box_corners(const OrientedBox& box) {
  const Axes axes = box_axes(box.angle_degrees);
  const double hw = box.width / 2.0;
  const double hh = box.height / 2.0;
  const double cx = box.center_x;
  const double cy = box.center_y;
  return {cx - hw * axes.ux - hh * axes.vx, cy - hw * axes.uy - hh * axes.vy,
          cx + hw * axes.ux - hh * axes.vx, cy + hw * axes.uy - hh * axes.vy,
          cx + hw * axes.ux + hh * axes.vx, cy + hw * axes.uy + hh * axes.vy,
          cx - hw * axes.ux + hh * axes.vx, cy - hw * axes.uy + hh * axes.vy};
}

OrientedBox oriented_box_from_quad(const std::array<double, 8>& quad) {
  OrientedBox box;
  box.center_x = (quad[0] + quad[2] + quad[4] + quad[6]) / 4.0;
  box.center_y = (quad[1] + quad[3] + quad[5] + quad[7]) / 4.0;
  box.width = (distance(quad[0], quad[1], quad[2], quad[3]) + distance(quad[6], quad[7], quad[4], quad[5])) / 2.0;
  box.height = (distance(quad[0], quad[1], quad[6], quad[7]) + distance(quad[2], quad[3], quad[4], quad[5])) / 2.0;
  const double dx = (quad[2] - quad[0]) + (quad[4] - quad[6]);
  const double dy = (quad[3] - quad[1]) + (quad[5] - quad[7]);
  box.angle_degrees = (dx == 0.0 && dy == 0.0) ? 0.0 : std::atan2(dy, dx) * 180.0 / kPi;
  return box;
}

Rect oriented_box_bounds(const OrientedBox& box) {
  const auto corners = oriented_box_corners(box);
  double left = corners[0];
  double right = corners[0];
  double top = corners[1];
  double bottom = corners[1];
  for (int i = 1; i < 4; ++i) {
    left = std::min(left, corners[static_cast<std::size_t>(i * 2)]);
    right = std::max(right, corners[static_cast<std::size_t>(i * 2)]);
    top = std::min(top, corners[static_cast<std::size_t>(i * 2 + 1)]);
    bottom = std::max(bottom, corners[static_cast<std::size_t>(i * 2 + 1)]);
  }
  const auto x = static_cast<std::int32_t>(std::floor(left));
  const auto y = static_cast<std::int32_t>(std::floor(top));
  return {x, y, static_cast<std::int32_t>(std::ceil(right)) - x, static_cast<std::int32_t>(std::ceil(bottom)) - y};
}

bool oriented_box_touches_edge(const OrientedBox& box, std::int32_t image_width, std::int32_t image_height,
                               double margin) {
  const auto corners = oriented_box_corners(box);
  for (int i = 0; i < 4; ++i) {
    const double x = corners[static_cast<std::size_t>(i * 2)];
    const double y = corners[static_cast<std::size_t>(i * 2 + 1)];
    if (x <= margin || y <= margin || x >= image_width - margin || y >= image_height - margin) {
      return true;
    }
  }
  return false;
}

std::optional<OrientedBox> detect_book_outline(const PixelBuffer& source, const BookDetectOptions& options,
                                               const std::function<bool()>& cancelled) {
  PhotoDetectOptions detect;
  detect.sensitivity = options.sensitivity;
  detect.source_ppi = options.source_ppi;
  const PhotoDetectResult result = detect_photo_regions(source, detect, cancelled);
  const PhotoRegion* largest = nullptr;
  double largest_area = 0.0;
  for (const auto& region : result.regions) {
    const double area = quad_area(region.quad);
    if (area > largest_area) {
      largest_area = area;
      largest = &region;
    }
  }
  if (largest == nullptr) {
    return std::nullopt;
  }
  return oriented_box_from_quad(largest->quad);
}

CoverLayout default_cover_layout(const OrientedBox& outline) {
  CoverLayout layout;
  layout.outline = outline;
  return layout;
}

OrientedBox cover_half_box(const CoverLayout& cover, BookPageSide side) {
  const double spine_left = std::clamp(cover.spine_left, 0.0, 1.0);
  const double spine_right = std::clamp(std::max(cover.spine_right, spine_left), 0.0, 1.0);
  return side == BookPageSide::Left ? box_slice(cover.outline, 0.0, spine_left)
                                    : box_slice(cover.outline, spine_right, 1.0);
}

double detect_gutter_fraction(const PixelBuffer& source, const OrientedBox& spread) {
  if (source.empty() || !supported_page_format(source.format()) || spread.width < 8.0 || spread.height < 8.0) {
    return 0.5;
  }
  const Axes axes = box_axes(spread.angle_degrees);
  // Each column's brightness is a high percentile of its samples, not their mean: a
  // column of text is dark lines on white paper and keeps a white percentile, while
  // the fold's shadow darkens the paper on every row.
  std::vector<std::int64_t> column(static_cast<std::size_t>(kGutterColumns), -1);
  std::vector<int> samples;
  samples.reserve(static_cast<std::size_t>(kGutterRows));
  for (std::int32_t i = 0; i < kGutterColumns; ++i) {
    const double local_x = ((i + 0.5) / kGutterColumns - 0.5) * spread.width;
    samples.clear();
    for (std::int32_t j = 0; j < kGutterRows; ++j) {
      const double local_y = (0.1 + 0.8 * (j + 0.5) / kGutterRows - 0.5) * spread.height;
      const double world_x = spread.center_x + local_x * axes.ux + local_y * axes.vx;
      const double world_y = spread.center_y + local_x * axes.uy + local_y * axes.vy;
      const auto x = static_cast<std::int32_t>(std::floor(world_x));
      const auto y = static_cast<std::int32_t>(std::floor(world_y));
      if (x < 0 || y < 0 || x >= source.width() || y >= source.height()) {
        continue;
      }
      samples.push_back(fetch_luma(source, x, y));
    }
    if (!samples.empty()) {
      const auto rank = static_cast<std::ptrdiff_t>((samples.size() * kGutterPercentile) / 100);
      std::nth_element(samples.begin(), samples.begin() + rank, samples.end());
      column[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(samples[static_cast<std::size_t>(rank)]) * 256;
    }
  }
  const auto band_start = static_cast<std::int32_t>(kGutterBandStart * kGutterColumns);
  const auto band_end = static_cast<std::int32_t>(kGutterBandEnd * kGutterColumns);
  std::vector<std::int64_t> smoothed;
  smoothed.reserve(static_cast<std::size_t>(band_end - band_start));
  for (std::int32_t i = band_start; i < band_end; ++i) {
    std::int64_t sum = 0;
    std::int64_t count = 0;
    for (std::int32_t k = i - kGutterSmoothRadius; k <= i + kGutterSmoothRadius; ++k) {
      if (k < 0 || k >= kGutterColumns || column[static_cast<std::size_t>(k)] < 0) {
        continue;
      }
      sum += column[static_cast<std::size_t>(k)];
      ++count;
    }
    smoothed.push_back(count > 0 ? sum / count : -1);
  }
  std::vector<std::int64_t> valid;
  for (const auto value : smoothed) {
    if (value >= 0) {
      valid.push_back(value);
    }
  }
  if (valid.size() < 3) {
    return 0.5;
  }
  std::sort(valid.begin(), valid.end());
  const std::int64_t median = valid[valid.size() / 2];
  std::int32_t best = -1;
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(smoothed.size()); ++i) {
    const auto value = smoothed[static_cast<std::size_t>(i)];
    if (value >= 0 && (best < 0 || value < smoothed[static_cast<std::size_t>(best)])) {
      best = i;
    }
  }
  const std::int64_t minimum = smoothed[static_cast<std::size_t>(best)];
  if (median - minimum < kGutterMinimumContrast) {
    return 0.5;
  }
  // The fold's middle: the run of columns within a quarter of the contrast of the darkest.
  const std::int64_t limit = minimum + (median - minimum) / 4;
  std::int32_t run_start = best;
  std::int32_t run_end = best;
  while (run_start > 0 && smoothed[static_cast<std::size_t>(run_start - 1)] >= 0 &&
         smoothed[static_cast<std::size_t>(run_start - 1)] <= limit) {
    --run_start;
  }
  while (run_end + 1 < static_cast<std::int32_t>(smoothed.size()) &&
         smoothed[static_cast<std::size_t>(run_end + 1)] >= 0 &&
         smoothed[static_cast<std::size_t>(run_end + 1)] <= limit) {
    ++run_end;
  }
  const double column_center = band_start + (run_start + run_end) / 2.0 + 0.5;
  return column_center / kGutterColumns;
}

SpreadLayout detect_spread(const PixelBuffer& source, const BookDetectOptions& options,
                           const std::optional<OrientedBox>& fallback, const std::function<bool()>& cancelled) {
  SpreadLayout layout;
  layout.spread = fallback.value_or(whole_image_box(source));
  const auto outline = detect_book_outline(source, options, cancelled);
  if (outline) {
    const double reference = fallback ? fallback->width * fallback->height
                                      : static_cast<double>(source.width()) * source.height();
    const double share = fallback ? kPlausibleShareOfFallback : kPlausibleShareOfImage;
    if (outline->width * outline->height >= reference * share) {
      layout.spread = *outline;
    }
  }
  layout.gutter = detect_gutter_fraction(source, layout.spread);
  return layout;
}

OrientedBox spread_page_box(const SpreadLayout& layout, BookPageSide side) {
  const double gutter = std::clamp(layout.gutter, 0.0, 1.0);
  return side == BookPageSide::Left ? box_slice(layout.spread, 0.0, gutter)
                                    : box_slice(layout.spread, gutter, 1.0);
}

BookPageSize median_page_size(std::vector<BookPageSize> sizes) {
  std::vector<double> widths;
  std::vector<double> heights;
  for (const auto& size : sizes) {
    widths.push_back(size.width);
    heights.push_back(size.height);
  }
  return {median_of(std::move(widths)), median_of(std::move(heights))};
}

BookPageSize largest_page_size(const std::vector<BookPageSize>& sizes) {
  BookPageSize largest;
  for (const auto& size : sizes) {
    largest.width = std::max(largest.width, size.width);
    largest.height = std::max(largest.height, size.height);
  }
  return largest;
}

BookPageSize cover_page_size(const CoverLayout& cover, double ppi) {
  if (ppi <= 0.0) {
    return {};
  }
  const OrientedBox left = cover_half_box(cover, BookPageSide::Left);
  const OrientedBox right = cover_half_box(cover, BookPageSide::Right);
  return {(left.width + right.width) / 2.0 / ppi, cover.outline.height / ppi};
}

OrientedBox uniform_spread_page_box(const SpreadLayout& layout, BookPageSide side, double width_px,
                                    double height_px) {
  const double half = layout.spread.width / 2.0;
  const double local_x = side == BookPageSide::Left ? -half + width_px / 2.0 : half - width_px / 2.0;
  return box_at(layout.spread, local_x, width_px, height_px);
}

OrientedBox centered_page_box(const OrientedBox& measured, double width_px, double height_px) {
  return box_at(measured, 0.0, width_px, height_px);
}

PixelBuffer extract_book_page(const PixelBuffer& source, const OrientedBox& box, std::int32_t out_width,
                              std::int32_t out_height) {
  if (source.empty() || !supported_page_format(source.format()) || out_width <= 0 || out_height <= 0 ||
      out_width > kMaxPageEdge || out_height > kMaxPageEdge || box.width <= 0.0 || box.height <= 0.0) {
    return {};
  }
  const bool gray = is_gray(source.format());
  const int out_channels = gray ? 1 : 3;
  PixelBuffer out(out_width, out_height, gray ? PixelFormat::gray8() : PixelFormat::rgb8());
  const Axes axes = box_axes(box.angle_degrees);
  const double step_x = box.width / out_width;
  const double step_y = box.height / out_height;
  const int samples_x = std::clamp(static_cast<int>(std::ceil(step_x - 0.01)), 1, 8);
  const int samples_y = std::clamp(static_cast<int>(std::ceil(step_y - 0.01)), 1, 8);
  const double sample_count = static_cast<double>(samples_x) * samples_y;
  const std::int32_t source_width = source.width();
  const std::int32_t source_height = source.height();
  // The box's top-left corner in source coordinates.
  const double origin_x = box.center_x - box.width / 2.0 * axes.ux - box.height / 2.0 * axes.vx;
  const double origin_y = box.center_y - box.width / 2.0 * axes.uy - box.height / 2.0 * axes.vy;
  for (std::int32_t oy = 0; oy < out_height; ++oy) {
    auto* out_row = out.row(oy).data();
    for (std::int32_t ox = 0; ox < out_width; ++ox) {
      double accumulated[3] = {0.0, 0.0, 0.0};
      for (int sy = 0; sy < samples_y; ++sy) {
        const double local_y = (oy + (sy + 0.5) / samples_y) * step_y;
        for (int sx = 0; sx < samples_x; ++sx) {
          const double local_x = (ox + (sx + 0.5) / samples_x) * step_x;
          const double world_x = origin_x + local_x * axes.ux + local_y * axes.vx - 0.5;
          const double world_y = origin_y + local_x * axes.uy + local_y * axes.vy - 0.5;
          const double floor_x = std::floor(world_x);
          const double floor_y = std::floor(world_y);
          const double fx = world_x - floor_x;
          const double fy = world_y - floor_y;
          const auto x0 = static_cast<std::int32_t>(floor_x);
          const auto y0 = static_cast<std::int32_t>(floor_y);
          const double weights[4] = {(1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy, fx * fy};
          const std::int32_t xs[4] = {x0, x0 + 1, x0, x0 + 1};
          const std::int32_t ys[4] = {y0, y0, y0 + 1, y0 + 1};
          for (int tap = 0; tap < 4; ++tap) {
            int rgb[3] = {255, 255, 255};
            if (xs[tap] >= 0 && ys[tap] >= 0 && xs[tap] < source_width && ys[tap] < source_height) {
              fetch_rgb(source, xs[tap], ys[tap], rgb);
            }
            for (int channel = 0; channel < out_channels; ++channel) {
              accumulated[channel] += weights[tap] * rgb[channel];
            }
          }
        }
      }
      for (int channel = 0; channel < out_channels; ++channel) {
        const double value = std::floor(accumulated[channel] / sample_count + 0.5);
        out_row[static_cast<std::size_t>(ox) * out_channels + channel] =
            static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
      }
    }
  }
  return out;
}

PixelBuffer apply_book_output(const PixelBuffer& page, const BookOutputOptions& options) {
  if (page.empty() || !supported_page_format(page.format())) {
    return {};
  }
  const int black = std::clamp(options.black_point, 0, 254);
  const int white = std::clamp(options.white_point, black + 1, 255);
  std::array<std::uint8_t, 256> levels{};
  for (int value = 0; value < 256; ++value) {
    const int scaled = ((value - black) * 255 + (white - black) / 2) / (white - black);
    levels[static_cast<std::size_t>(value)] = static_cast<std::uint8_t>(std::clamp(scaled, 0, 255));
  }
  const bool source_gray = is_gray(page.format());
  const bool color_out = options.mode == BookOutputMode::Color && !source_gray;
  PixelBuffer out(page.width(), page.height(), color_out ? PixelFormat::rgb8() : PixelFormat::gray8());
  const int threshold = std::clamp(options.threshold, 0, 256);
  for (std::int32_t y = 0; y < page.height(); ++y) {
    auto* out_row = out.row(y).data();
    for (std::int32_t x = 0; x < page.width(); ++x) {
      int rgb[3];
      fetch_rgb(page, x, y, rgb);
      if (color_out) {
        for (int channel = 0; channel < 3; ++channel) {
          out_row[static_cast<std::size_t>(x) * 3 + channel] = levels[static_cast<std::size_t>(rgb[channel])];
        }
        continue;
      }
      const int gray_value = source_gray ? rgb[0] : luma(rgb[0], rgb[1], rgb[2]);
      const std::uint8_t leveled = levels[static_cast<std::size_t>(gray_value)];
      out_row[x] = options.mode == BookOutputMode::BlackAndWhite ? (leveled >= threshold ? 255 : 0) : leveled;
    }
  }
  return out;
}

double book_page_color_fraction(const PixelBuffer& page) {
  if (page.empty() || !supported_page_format(page.format()) || is_gray(page.format())) {
    return 0.0;
  }
  const std::int64_t pixels = static_cast<std::int64_t>(page.width()) * page.height();
  // About 250,000 samples at most, on a fixed grid.
  const auto step = static_cast<std::int32_t>(
      std::max<std::int64_t>(1, static_cast<std::int64_t>(std::sqrt(static_cast<double>(pixels) / 250000.0))));
  std::int64_t total = 0;
  std::int64_t colored = 0;
  for (std::int32_t y = step / 2; y < page.height(); y += step) {
    for (std::int32_t x = step / 2; x < page.width(); x += step) {
      int rgb[3];
      fetch_rgb(page, x, y, rgb);
      const int chroma = std::max({rgb[0], rgb[1], rgb[2]}) - std::min({rgb[0], rgb[1], rgb[2]});
      ++total;
      if (chroma > kColorChroma) {
        ++colored;
      }
    }
  }
  return total > 0 ? static_cast<double>(colored) / static_cast<double>(total) : 0.0;
}

bool book_page_looks_grayscale(const PixelBuffer& page) {
  return book_page_color_fraction(page) < kGrayscaleSuggestionFraction;
}

bool book_spread_fits_bed(double width, double height, double bed_width, double bed_height, double margin) {
  if (bed_width <= 0.0 || bed_height <= 0.0) {
    return true;  // unknown bed: assume it fits
  }
  const double w = width + 2.0 * margin;
  const double h = height + 2.0 * margin;
  return (w <= bed_width && h <= bed_height) || (h <= bed_width && w <= bed_height);
}

SpreadLayout half_turned_spread(const SpreadLayout& layout, std::int32_t image_width, std::int32_t image_height) {
  SpreadLayout turned = layout;
  turned.spread.center_x = image_width - layout.spread.center_x;
  turned.spread.center_y = image_height - layout.spread.center_y;
  turned.gutter = 1.0 - layout.gutter;
  return turned;
}

CoverLayout half_turned_cover(const CoverLayout& cover, std::int32_t image_width, std::int32_t image_height) {
  CoverLayout turned = cover;
  turned.outline.center_x = image_width - cover.outline.center_x;
  turned.outline.center_y = image_height - cover.outline.center_y;
  turned.spine_left = 1.0 - cover.spine_right;
  turned.spine_right = 1.0 - cover.spine_left;
  return turned;
}

Rect unturned_rect(const Rect& turned, int clockwise_turns, std::int32_t raw_width, std::int32_t raw_height) {
  switch (((clockwise_turns % 4) + 4) % 4) {
    case 1:
      return {turned.y, raw_height - turned.x - turned.width, turned.height, turned.width};
    case 2:
      return {raw_width - turned.x - turned.width, raw_height - turned.y - turned.height, turned.width,
              turned.height};
    case 3:
      return {raw_width - turned.y - turned.height, turned.x, turned.height, turned.width};
    default:
      return turned;
  }
}

}  // namespace patchy
