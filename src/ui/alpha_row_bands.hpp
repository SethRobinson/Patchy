#pragma once

#include "core/pixel_buffer.hpp"

#include <vector>

namespace patchy::ui {

// A run of raster rows that carry visible alpha: [top, bottom) in buffer rows.
struct AlphaRowBand {
  int top{0};
  int bottom{0};
};

// The visible-alpha row bands of a raster (a text layer's lines, typically).
// A row is active when at least max(2, width / 180) of its pixels have alpha
// 12 or more; active rows at most two rows apart join one band, and a band
// must be at least two rows tall. Empty for buffers without an alpha channel.
// MainWindow scores PSD text re-renders against the original raster with it,
// and the UI tests measure the same bands, so this is the one definition.
[[nodiscard]] std::vector<AlphaRowBand> alpha_row_bands(const PixelBuffer& pixels);

// Rows from the first band's top to the last band's bottom; 0 without bands.
[[nodiscard]] int alpha_row_band_span(const std::vector<AlphaRowBand>& bands);

}  // namespace patchy::ui
