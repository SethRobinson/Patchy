#include "ocr/ocr_bands.hpp"

#include "ocr/ocr_image.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace patchy::ocr {

BandPlan plan_bands(std::int32_t width, std::int32_t height, std::int32_t max_dimension) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("text recognition was given an empty image");
  }
  BandPlan plan;
  plan.scaled_width = width;
  plan.scaled_height = height;
  if (max_dimension > 0 && width > max_dimension) {
    plan.scaled_width = max_dimension;
    const std::int64_t rounded =
        (2 * static_cast<std::int64_t>(height) * max_dimension + width) / (2 * static_cast<std::int64_t>(width));
    plan.scaled_height = static_cast<std::int32_t>(std::max<std::int64_t>(1, rounded));
  }
  if (max_dimension <= 0 || plan.scaled_height <= max_dimension) {
    plan.bands.push_back(Band{0, plan.scaled_height});
    return plan;
  }
  // The overlap is at most half a band, so a limit too small for the usual 16 rows still
  // makes progress.
  plan.overlap = std::max(1, std::min(std::max(16, max_dimension / 8), max_dimension / 2));
  const std::int32_t stride = std::max(1, max_dimension - plan.overlap);
  const std::int32_t count =
      (plan.scaled_height - plan.overlap + stride - 1) / stride;  // ceil((Hs - V) / (M - V))
  for (std::int32_t index = 0; index < count; ++index) {
    const std::int32_t start =
        index + 1 < count ? index * stride : std::max(0, plan.scaled_height - max_dimension);
    plan.bands.push_back(Band{start, std::min(plan.scaled_height, start + max_dimension)});
  }
  return plan;
}

Result recognize_in_bands(const ImageView& image, std::int32_t max_dimension, const TileRecognizer& recognize_tile,
                          const Cancel* cancel) {
  if (image.data == nullptr) {
    throw std::invalid_argument("text recognition was given an empty image");
  }
  const BandPlan plan = plan_bands(image.width, image.height, max_dimension);
  const double scale_x = static_cast<double>(image.width) / plan.scaled_width;
  const double scale_y = static_cast<double>(image.height) / plan.scaled_height;
  Result merged;
  for (std::size_t index = 0; index < plan.bands.size(); ++index) {
    if (cancel != nullptr && cancel->requested()) {
      throw RecognitionCancelled();
    }
    const Band band = plan.bands[index];
    const BgraImage pixels =
        scaled_rows_bgra(image, plan.scaled_width, plan.scaled_height, band.start, band.end - band.start);
    Tile tile;
    tile.image = pixels.view();
    tile.band_index = static_cast<std::int32_t>(index);
    tile.band_start = band.start;
    tile.scaled_width = plan.scaled_width;
    tile.scaled_height = plan.scaled_height;
    tile.source_width = image.width;
    tile.source_height = image.height;
    Result found = recognize_tile(tile);
    if (!merged.text_angle_degrees.has_value()) {
      merged.text_angle_degrees = found.text_angle_degrees;
    }

    // Ownership cuts, in doubled scaled rows: the midpoint of each overlap.
    const bool has_lower = index > 0;
    const bool has_upper = index + 1 < plan.bands.size();
    const std::int64_t lower = has_lower ? static_cast<std::int64_t>(band.start) + plan.bands[index - 1].end : 0;
    const std::int64_t upper = has_upper ? static_cast<std::int64_t>(plan.bands[index + 1].start) + band.end : 0;
    const auto to_source = [&](Point point) {
      return Point{point.x * scale_x, (point.y + band.start) * scale_y};
    };
    for (auto& line : found.lines) {
      const double doubled_center = 2.0 * (band.start + line.box.y + line.box.height / 2.0);
      if ((has_lower && doubled_center < static_cast<double>(lower)) ||
          (has_upper && doubled_center >= static_cast<double>(upper))) {
        continue;
      }
      for (auto& point : line.quad) {
        point = to_source(point);
      }
      line.box = box_from_quad(line.quad);
      for (auto& word : line.words) {
        for (auto& point : word.quad) {
          point = to_source(point);
        }
        word.box = box_from_quad(word.quad);
      }
      merged.lines.push_back(std::move(line));
    }
  }
  return merged;
}

}  // namespace patchy::ocr
