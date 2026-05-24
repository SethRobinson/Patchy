#include "color/color_management.hpp"

#include <stdexcept>
#include <utility>

namespace photoslop {

void ColorManager::assign_icc_profile(Document& document, std::vector<std::uint8_t> icc_profile) const {
  document.color_state().embedded_icc_profile = std::move(icc_profile);
}

PixelBuffer ColorManager::preview_rgb8(const Document& /*document*/, const PixelBuffer& source,
                                       const ColorTransformSpec& /*spec*/) const {
  if (source.format() != PixelFormat::rgb8()) {
    throw std::invalid_argument("Color preview placeholder currently accepts RGB8 buffers only");
  }
  return source;
}

}  // namespace photoslop
