#pragma once

// Pixel preparation shared by every engine (docs/ocr.md): any ImageView becomes opaque
// BGRA over white, optionally scaled down by averaging whole source pixels. Integer math
// only, so a tile is the same bytes on every toolchain.

#include "ocr/ocr_engine.hpp"

#include <cstdint>
#include <vector>

namespace patchy::ocr {

// Opaque BGRA8, rows packed (stride = width * 4).
struct BgraImage {
  std::vector<std::uint8_t> pixels;
  std::int32_t width{0};
  std::int32_t height{0};

  [[nodiscard]] ImageView view() const noexcept;
};

// Rows [top, top + rows) of `source` scaled to scaled_width x scaled_height (never larger
// than the source on either axis), flattened over white. Scaled pixel (x, y) is the rounded
// mean of the source pixels in columns [x*W/sw, (x+1)*W/sw) and rows [y*H/sh, (y+1)*H/sh),
// at least one each. No full scaled copy of the page is ever made: a band is produced
// straight from its source rows.
[[nodiscard]] BgraImage scaled_rows_bgra(const ImageView& source, std::int32_t scaled_width,
                                         std::int32_t scaled_height, std::int32_t top, std::int32_t rows);

}  // namespace patchy::ocr
