#include "ui/alpha_row_bands.hpp"

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
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  const auto active_threshold = std::max(2, pixels.width() / 180);
  constexpr int kAlphaThreshold = 12;
  constexpr int kMergeGap = 2;

  bool in_band = false;
  int band_top = 0;
  int last_active = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    int active_pixels = 0;
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset < bytes.size() && bytes[offset] >= kAlphaThreshold) {
        ++active_pixels;
      }
    }
    const bool active = active_pixels >= active_threshold;
    if (active) {
      if (!in_band) {
        in_band = true;
        band_top = y;
      }
      last_active = y;
    } else if (in_band && last_active >= 0 && y - last_active > kMergeGap) {
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
