#pragma once

#include "core/image_trace.hpp"
#include "core/pixel_buffer.hpp"

#include <memory>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

struct ImageTraceDialogResult {
  ImageTraceOptions options;
  // The trace of `options`, computed by the preview worker and handed over
  // so the caller never traces twice.
  std::shared_ptr<const ImageTraceResult> result;
};

// Modal "Trace Image to Shapes" dialog: preset, mode, color count or
// threshold, path fidelity, corner sharpness, noise, method, and the two
// toggles, beside a zoomable preview of the traced result. Every control
// change re-traces on a background worker (latest-wins, debounced); OK waits
// for the current settings' trace and returns it. nullopt on Cancel.
//
// Legal boundary (docs/legal-constraints.md, "Vector tracing"): the preview
// shows only the traced result. Do not add overlay/outline renditions of the
// source, per-region parameters, or a link that re-traces after the dialog
// closes.
[[nodiscard]] std::optional<ImageTraceDialogResult> request_image_trace(
    QWidget* parent, std::shared_ptr<const PixelBuffer> pixels, const ImageTraceOptions& initial);

// Named option sets offered by the Preset combo (the dialog adds a leading
// "Custom" row for hand-edited settings).
struct ImageTracePreset {
  const char* english_name;
  ImageTraceOptions options;
};
[[nodiscard]] const std::vector<ImageTracePreset>& image_trace_presets();

}  // namespace patchy::ui
