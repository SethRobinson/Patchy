#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace patchy {

// The sRGB electro-optical transfer function, shared by every reader that decodes
// linear-light float samples into Patchy's 8-bit sRGB pixels: PSD/PSB 32-bit channels
// (psd_channel_data.cpp), Affinity RGBAFloat rasters (af_document_io.cpp), and the JPEG XR
// HDR tone map (jxr_document_io.cpp). Keep them on one formula so the same linear value
// always bakes to the same byte regardless of which container carried it.
[[nodiscard]] inline std::uint8_t linear_to_srgb8(float value) {
  value = std::clamp(value, 0.0F, 1.0F);
  const float srgb = value <= 0.0031308F ? value * 12.92F
                                         : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

}  // namespace patchy
