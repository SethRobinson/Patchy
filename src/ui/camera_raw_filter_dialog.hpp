#pragma once

#include "core/photo_develop.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/filter_selection.hpp"

#include <memory>
#include <optional>

class QWidget;

namespace patchy::ui {

struct CameraRawFilterRequest {
  std::shared_ptr<const PixelBuffer> source;  // the layer's pixels, any depth
  Rect bounds{};                              // document position of `source`
  FilterSelection selection;                  // empty: the whole layer
  PhotoDevelopParams initial;
};

struct CameraRawFilterOutcome {
  PhotoDevelopParams params;
  // The filtered layer with the selection applied; null when the settings are neutral.
  std::shared_ptr<const PixelBuffer> pixels;
};

// Filter > Camera Raw Filter (GitHub issue 392; docs/camera-raw.md "Camera Raw Filter"):
// Develop Raw's white balance, tone and color controls on an ordinary layer, with the
// same histogram, slider clicks, zoom bar and draft-then-refine preview. Renders run on
// worker threads; OK reuses the refined result when it matches. Returns nullopt on Cancel.
[[nodiscard]] std::optional<CameraRawFilterOutcome> run_camera_raw_filter_dialog(
    QWidget* parent, const CameraRawFilterRequest& request);

}  // namespace patchy::ui
