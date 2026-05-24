#pragma once

#include "core/document.hpp"

#include <string>
#include <vector>

namespace photoslop {

struct ColorTransformSpec {
  std::string source_profile_name{"document"};
  std::string destination_profile_name{"display"};
  std::string rendering_intent{"relative-colorimetric"};
  bool black_point_compensation{true};
};

class ColorManager {
public:
  void assign_icc_profile(Document& document, std::vector<std::uint8_t> icc_profile) const;
  [[nodiscard]] PixelBuffer preview_rgb8(const Document& document, const PixelBuffer& source,
                                         const ColorTransformSpec& spec = {}) const;
};

}  // namespace photoslop
