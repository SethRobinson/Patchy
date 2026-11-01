#pragma once

// A layer prepared for a GIMP 3 plug-in run (main_window_gimp_plugin_runs.cpp): its
// pixels in the format the plug-in gets (a layer without alpha that may hold
// transparency gains an opaque alpha channel), the selection's coverage, and the
// original pixels to put back after a preview.

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"

#include <QRect>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace patchy::ui {

struct GimpLayerInput {
  std::int64_t session_id{0};
  LayerId layer_id{};
  int width{0};
  int height{0};
  QRect layer_rect;  // in document pixels
  PixelFormat format{};
  bool expand_alpha{false};   // handed over with an opaque alpha channel
  bool protect_alpha{false};  // a locked-transparency layer with alpha
  std::size_t channel_bytes{1};
  std::size_t layer_pixel_bytes{4};  // per pixel in the layer
  std::size_t pixel_bytes{4};        // per pixel as the plug-in gets it
  std::size_t row_bytes{0};
  std::vector<std::uint8_t> opaque;    // one alpha value meaning opaque
  std::vector<std::uint8_t> input;     // width * height * pixel_bytes
  std::vector<std::uint8_t> coverage;  // width * height selection coverage, empty without one
  QRect mask_bounds;                   // the selection's bounds in layer pixels
  PixelBuffer original;                // the layer's pixels before the run
  QString layer_name;
};

}  // namespace patchy::ui
