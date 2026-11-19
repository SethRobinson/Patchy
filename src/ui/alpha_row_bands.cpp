#include "ui/alpha_row_bands.hpp"

#include "core/pixel_depth.hpp"

#include <algorithm>
#include <cstddef>

namespace patchy::ui {

std::vector<AlphaRowBand> alpha_row_bands(const PixelBuffer& pixels) {
  std::vector<AlphaRowBand> bands;
  if (pixels.empty() || pixels.width() <= 0 || pixels.height() <= 0) {
    return bands;
  }
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return bands;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto active_threshold = std::max(2, pixels.width() / 180);
  const auto count_ink = [active_threshold](const auto& row, std::size_t first, std::size_t step) {
    constexpr int kAlphaThreshold = 12;
    int count = 0;
    for (auto i = first; i < row.size() && count < active_threshold; i += step) {
      count += row[i] >= kAlphaThreshold;
    }
    return count;
  };
  std::vector<float> deep_row;
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    deep_row.resize(static_cast<std::size_t>(pixels.width()) * 4U);
  }
  constexpr int kMergeGap = 2;

  bool in_band = false;
  int band_top = 0;
  int last_active = -1;
  int last_ink = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    int ink = 0;
    if (deep_row.empty()) {
      ink = count_ink(pixels.row(y), alpha_channel, channels);
    } else {
      load_rgba_row(pixels, y, 0, pixels.width(), deep_domain_for(pixels.format().bit_depth), deep_row);
      ink = count_ink(deep_row, 3U, 4U);
    }
    // A thin stroke can connect two dense rows. Only blank rows separate bands;
    // the density threshold still decides where the measurable ink starts and ends.
    if (ink > 0) {
      last_ink = y;
    }
    if (ink >= active_threshold) {
      if (!in_band) {
        in_band = true;
        band_top = y;
      }
      last_active = y;
    } else if (in_band && y - last_ink > kMergeGap) {
      if (last_active + 1 - band_top >= 2) {
        bands.push_back(AlphaRowBand{band_top, last_active + 1});
      }
      in_band = false;
      last_active = -1;
    }
  }
  if (in_band && last_active + 1 - band_top >= 2) {
    bands.push_back(AlphaRowBand{band_top, last_active + 1});
  }
  return bands;
}

int alpha_row_band_span(const std::vector<AlphaRowBand>& bands) {
  if (bands.empty()) {
    return 0;
  }
  return std::max(0, bands.back().bottom - bands.front().top);
}

}  // namespace patchy::ui
