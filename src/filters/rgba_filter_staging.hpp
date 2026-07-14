#pragma once

#include <cstdint>

#include "core/pixel_buffer.hpp"

namespace patchy {

// Shared RGB->RGBA staging scaffold for the render_photoshop_* wrappers in
// builtin_filters.cpp and filter_engine.cpp. Internal to src/filters; do not
// include it elsewhere.
//
// Copies the caller's RGB pixels (plus alpha, or opaque 255) into a fresh
// RGBA8 buffer, invokes `render` on it, and copies the rendered RGB (plus
// alpha when the caller has one) back into `pixels`. `render` receives the
// staged RGBA buffer and must return a FilterRenderResult whose .pixels
// covers the same extent as `pixels` (the callers pass
// Rect::from_size(rgba.width(), rgba.height()) as the render bounds).
//
// The copy loops are byte-for-byte the historical per-site blocks and every
// caller's output is pinned bit-identically by the calibrated filter tests
// and catalog canaries: do not change the loop semantics.
template <typename RenderFn>
void stage_rgba_and_render(PixelBuffer &pixels, RenderFn &&render) {
  PixelBuffer rgba(pixels.width(), pixels.height(), PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto *source = pixels.pixel(x, y);
      auto *destination = rgba.pixel(x, y);
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
      destination[3] = pixels.format().channels >= 4 ? source[3] : 255U;
    }
  }
  const auto result = render(rgba);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto *destination = pixels.pixel(x, y);
      const auto *source = result.pixels.pixel(x, y);
      destination[0] = source[0];
      destination[1] = source[1];
      destination[2] = source[2];
      if (pixels.format().channels >= 4) {
        destination[3] = source[3];
      }
    }
  }
}

} // namespace patchy
