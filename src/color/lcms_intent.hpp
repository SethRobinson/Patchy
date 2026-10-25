#pragma once

#include "color/color_management.hpp"

#include "lcms2.h"

// Private to patchy_color: lcms2.h never leaks into a public header.

namespace patchy {

// The Little-CMS intent code for a RenderingIntent.
[[nodiscard]] inline cmsUInt32Number lcms_intent(RenderingIntent intent) noexcept {
  switch (intent) {
    case RenderingIntent::Perceptual:
      return INTENT_PERCEPTUAL;
    case RenderingIntent::RelativeColorimetric:
      return INTENT_RELATIVE_COLORIMETRIC;
    case RenderingIntent::Saturation:
      return INTENT_SATURATION;
    case RenderingIntent::AbsoluteColorimetric:
      return INTENT_ABSOLUTE_COLORIMETRIC;
  }
  return INTENT_RELATIVE_COLORIMETRIC;
}

}  // namespace patchy
