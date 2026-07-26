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
};

}  // namespace patchy
