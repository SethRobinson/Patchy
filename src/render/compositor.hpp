#pragma once

#include "core/document.hpp"

namespace photoslop {

class Compositor {
public:
  [[nodiscard]] PixelBuffer flatten_rgb8(const Document& document) const;

private:
  void composite_layer(PixelBuffer& destination, const Layer& layer, Rect clip) const;
  void composite_pixels(PixelBuffer& destination, const Layer& layer, Rect clip) const;
};

}  // namespace photoslop
