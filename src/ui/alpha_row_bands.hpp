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
// A band's endpoints each have at least max(2, width / 180) pixels with alpha
// at or above 12/255. Sparser ink between them belongs to the band. Three
// consecutive blank rows separate bands. A band must be at least two rows tall.
// A single-channel buffer supplies coverage. Empty for RGB without alpha.
// MainWindow scores PSD text re-renders against the original raster with it,
// and the UI tests measure the same bands, so this is the one definition.
[[nodiscard]] std::vector<AlphaRowBand> alpha_row_bands(const PixelBuffer& pixels);

// Rows from the first band's top to the last band's bottom; 0 without bands.
[[nodiscard]] int alpha_row_band_span(const std::vector<AlphaRowBand>& bands);

}  // namespace patchy::ui
