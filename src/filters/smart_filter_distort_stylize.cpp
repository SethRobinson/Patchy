#include "filters/smart_filter_distort_stylize.hpp"

#include "core/rect_utils.hpp"
#include "core/smart_filter_schema.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace patchy {
namespace {

void report(const FilterProgress* progress, int completed, int total, FilterProgressStage stage) {
  if (progress != nullptr && progress->update && !progress->update(completed, total, stage)) {
    throw FilterCancelled();
  }
}

// Photoshop's Solarize: values above 127 invert, the rest pass through.
// Alpha and bounds are unchanged.
FilterRenderResult render_solarize(const FilterRenderResult& input, const FilterProgress* progress) {
  FilterRenderResult result = input;
  auto& pixels = result.pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    if ((y & 63) == 0) {
      report(progress, y, pixels.height(), FilterProgressStage::Filtering);
    }
    auto* row = pixels.pixel(0, y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = row + static_cast<std::size_t>(x) * 4U;
      for (int channel = 0; channel < 3; ++channel) {
        if (px[channel] > 127) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
  }
  report(progress, pixels.height(), pixels.height(), FilterProgressStage::Filtering);
  return result;
}

// round(sqrt(1 + (i / 256)^2) * 16384) for i in 0..256, in integers so every
// toolchain builds the same table: round(sqrt(4096 * (65536 + i^2))).
const std::array<std::int64_t, 257>& find_edges_hypot_table() {
  static const auto table = [] {
    std::array<std::int64_t, 257> values{};
    for (std::int64_t index = 0; index <= 256; ++index) {
      const auto n = 4096 * (65536 + index * index);
      auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(n)));
      while (root * root > n) {
        --root;
      }
      while ((root + 1) * (root + 1) <= n) {
        ++root;
      }
      values[static_cast<std::size_t>(index)] = n - root * root > root ? root + 1 : root;
    }
    return values;
  }();
  return table;
}

// Photoshop's Find Edges (calibration in docs/smart-filters-distort-stylize.md):
// each pixel is matted over white, a 3x3 Sobel pair per channel (edge-clamped
// at the filter canvas, white beyond the content) gives an edge strength
// max(|gx|,|gy|) * T[256 * min / max] >> 14 (the table above), and
// 255 - strength is un-matted from white again. Alpha and bounds are unchanged.
FilterRenderResult render_find_edges(const FilterRenderResult& input, Rect canvas,
                                     const FilterProgress* progress) {
  const auto bounds = input.bounds;
  FilterRenderResult result = input;
  if (bounds.width <= 0 || bounds.height <= 0) {
    return result;
  }
  const auto& table = find_edges_hypot_table();
  // Matted samples for the content grown by one pixel; positions outside the
  // canvas repeat its edge, positions outside the content are transparent.
  const auto padded_width = bounds.width + 2;
  const auto padded_height = bounds.height + 2;
  std::vector<std::int32_t> matte(static_cast<std::size_t>(padded_width) *
                                  static_cast<std::size_t>(padded_height) * 3U);
  const auto canvas_right = canvas.x + canvas.width - 1;
  const auto canvas_bottom = canvas.y + canvas.height - 1;
  for (std::int32_t py = 0; py < padded_height; ++py) {
    const auto doc_y = std::clamp(bounds.y + py - 1, canvas.y, std::max(canvas.y, canvas_bottom));
    for (std::int32_t px = 0; px < padded_width; ++px) {
      const auto doc_x =
          std::clamp(bounds.x + px - 1, canvas.x, std::max(canvas.x, canvas_right));
      auto* out = matte.data() +
                  (static_cast<std::size_t>(py) * static_cast<std::size_t>(padded_width) +
                   static_cast<std::size_t>(px)) *
                      3U;
      const auto local_x = doc_x - bounds.x;
      const auto local_y = doc_y - bounds.y;
      if (local_x < 0 || local_y < 0 || local_x >= bounds.width || local_y >= bounds.height) {
        out[0] = out[1] = out[2] = 255;
        continue;
      }
      const auto* src = input.pixels.pixel(local_x, local_y);
      const std::int32_t alpha = src[3];
      for (int channel = 0; channel < 3; ++channel) {
        out[channel] = (src[channel] * alpha + 255 * (255 - alpha) + 127) / 255;
      }
    }
  }
  const auto sample = [&](std::int32_t px, std::int32_t py, int channel) {
    return matte[(static_cast<std::size_t>(py) * static_cast<std::size_t>(padded_width) +
                  static_cast<std::size_t>(px)) *
                     3U +
                 static_cast<std::size_t>(channel)];
  };
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    if ((y & 31) == 0) {
      report(progress, y, bounds.height, FilterProgressStage::DetectingEdges);
    }
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      auto* px = result.pixels.pixel(x, y);
      const std::int64_t alpha = px[3];
      if (alpha == 0) {
        px[0] = px[1] = px[2] = 255;
        continue;
      }
      const auto cx = x + 1;
      const auto cy = y + 1;
      for (int channel = 0; channel < 3; ++channel) {
        const std::int64_t gx =
            (sample(cx + 1, cy - 1, channel) + 2 * sample(cx + 1, cy, channel) +
             sample(cx + 1, cy + 1, channel)) -
            (sample(cx - 1, cy - 1, channel) + 2 * sample(cx - 1, cy, channel) +
             sample(cx - 1, cy + 1, channel));
        const std::int64_t gy =
            (sample(cx - 1, cy + 1, channel) + 2 * sample(cx, cy + 1, channel) +
             sample(cx + 1, cy + 1, channel)) -
            (sample(cx - 1, cy - 1, channel) + 2 * sample(cx, cy - 1, channel) +
             sample(cx + 1, cy - 1, channel));
        const auto high = std::max(std::abs(gx), std::abs(gy));
        const auto low = std::min(std::abs(gx), std::abs(gy));
        const auto strength =
            high == 0 ? std::int64_t{0}
                      : (high * table[static_cast<std::size_t>(low * 256 / high)]) >> 14;
        const auto ink = std::min<std::int64_t>(255, strength);
        const auto straight_ink = std::min<std::int64_t>(255, (ink * 255 + alpha / 2) / alpha);
        px[channel] = static_cast<std::uint8_t>(255 - straight_ink);
      }
    }
  }
  report(progress, bounds.height, bounds.height, FilterProgressStage::DetectingEdges);
  return result;
}

}  // namespace

FilterRenderResult render_schema_smart_filter(const FilterRenderResult& current,
                                              Rect filter_canvas_bounds, SmartFilterKind kind,
                                              const SchemaSmartFilter& settings,
                                              const FilterProgress* progress) {
  const auto* schema = find_smart_filter_schema(kind);
  if (schema == nullptr || !schema_settings_valid(*schema, settings) ||
      current.pixels.format() != PixelFormat::rgba8() ||
      current.pixels.width() != current.bounds.width ||
      current.pixels.height() != current.bounds.height) {
    throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
  }
  switch (kind) {
    case SmartFilterKind::Solarize:
      return render_solarize(current, progress);
    case SmartFilterKind::FindEdges:
      return render_find_edges(current, filter_canvas_bounds, progress);
    default:
      break;
  }
  throw std::invalid_argument(PATCHY_TRANSLATE_NOOP("QObject", "Unsupported Smart Filter entry"));
}

}  // namespace patchy
