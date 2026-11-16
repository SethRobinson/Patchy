#pragma once

// Images larger than an engine's size limit (docs/ocr.md, "Oversized images"). The rule is
// fixed geometry, the same for every engine and every image:
//   1. An image wider than the limit M is scaled down so its width is M (height in
//      proportion, rounded to nearest).
//   2. If the scaled height still exceeds M it is read as full-width horizontal bands of
//      M rows that overlap by V = max(16, M / 8) rows; the last band ends at the bottom.
//   3. A line read in two neighbouring bands is kept once: by the band whose side of the
//      overlap's midpoint holds the line's vertical centre.
// Lines come back in the coordinates of the original image. Nothing here compares text or
// looks at pixels to decide anything.

#include "ocr/ocr_engine.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy::ocr {

// Rows of the scaled image, end exclusive.
struct Band {
  std::int32_t start{0};
  std::int32_t end{0};
};

struct BandPlan {
  std::int32_t scaled_width{0};
  std::int32_t scaled_height{0};
  std::int32_t overlap{0};  // 0 with one band
  std::vector<Band> bands;
};

// max_dimension <= 0 means the engine has no limit: one band, no scaling.
[[nodiscard]] BandPlan plan_bands(std::int32_t width, std::int32_t height, std::int32_t max_dimension);

// What an engine is handed for one band: opaque BGRA pixels, and where they came from.
struct Tile {
  ImageView image;
  std::int32_t band_index{0};
  std::int32_t band_start{0};  // first row of this tile in the scaled image
  std::int32_t scaled_width{0};
  std::int32_t scaled_height{0};
  std::int32_t source_width{0};
  std::int32_t source_height{0};
};

// Returns lines in the tile's own pixels.
using TileRecognizer = std::function<Result(const Tile&)>;

// Reads `image` through `recognize_tile` by the rule above. Throws RecognitionCancelled when
// `cancel` is requested before a band starts; whatever recognize_tile throws passes through.
[[nodiscard]] Result recognize_in_bands(const ImageView& image, std::int32_t max_dimension,
                                        const TileRecognizer& recognize_tile, const Cancel* cancel = nullptr);

}  // namespace patchy::ocr
