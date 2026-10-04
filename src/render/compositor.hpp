#pragma once

#include "core/document.hpp"

#include <cstdint>
#include <vector>

namespace patchy {

class Compositor {
public:
  // merged_alpha (optional) receives the flatten's accumulated per-pixel coverage,
  // row-major, 0..255. Requesting it does not change the RGB output: the compositor's
  // colors are straight (unmatted), with uncovered pixels left at the cleared black.
  [[nodiscard]] PixelBuffer flatten_rgb8(const Document& document,
                                         std::vector<std::uint8_t>* merged_alpha = nullptr) const;

  // Composes only the document-space intersection of `region`. The returned
  // buffer is local to that intersection and uses the same RGB8 kernel as the
  // full flatten. This is a reference operation for bounded GPU/tile passes;
  // it is not itself a GPU implementation.
  [[nodiscard]] PixelBuffer flatten_rgb8_region(const Document& document, Rect region) const;
};

}  // namespace patchy
