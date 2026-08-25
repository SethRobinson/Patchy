#pragma once

#include "core/image_trace.hpp"
#include "core/pixel_buffer.hpp"

#include <QByteArray>
#include <QString>

#include <cstddef>
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
  // "Pick colors from the whole layer" (selection traces only). Deliberately
  // outside ImageTraceOptions: it is a scope switch, not a preset option.
  bool palette_from_layer{true};
};

// Modal "Trace Image to Shapes" dialog: preset, mode, color count or
// threshold, path fidelity, corner sharpness, noise, smoothing, anchor
// budget, method, and the toggles, beside a zoomable preview of the
// traced result. Every control
// change re-traces on a background worker (latest-wins, debounced); OK waits
// for the current settings' trace and returns it. nullopt on Cancel.
//
// Legal boundary (docs/legal-constraints.md, "Vector tracing"): the preview
// shows only the traced result, never the source. Do not add overlay/outline
// renditions of the source, per-region parameters, or a link that re-traces
// after the dialog closes. The "Show anchors" toggle stays inside that
// boundary: it marks the traced paths' own anchor points on the traced
// result and displays nothing derived from the source.
// `inside_selection` shows the "Tracing inside the selection" note: the
// caller already masked `pixels` (pixels_limited_to_selection). With a
// selection the caller also passes the unmasked layer in
// `whole_layer_pixels`; the "Pick colors from the whole layer" checkbox
// (seeded from `initial_palette_from_layer`) then hands it to trace_image as
// the palette source, so the traced colors match a whole-layer trace.
[[nodiscard]] std::optional<ImageTraceDialogResult> request_image_trace(
    QWidget* parent, std::shared_ptr<const PixelBuffer> pixels, const ImageTraceOptions& initial,
    bool inside_selection = false, std::shared_ptr<const PixelBuffer> whole_layer_pixels = nullptr,
    bool initial_palette_from_layer = true);

// The dialog warns when a result is this large: editing slows down and the
// exported SVG grows to many megabytes.
[[nodiscard]] bool image_trace_result_is_large(std::size_t layers, std::size_t anchors) noexcept;

// User presets (Save... / Delete beside the Preset combo), stored under the
// QSettings key imageTrace/userPresets as a compact JSON array whose keys
// match the imageTrace/* option spellings. Names are the identity; malformed
// elements are skipped one by one.
struct ImageTraceUserPreset {
  QString name;
  ImageTraceOptions options;
};
[[nodiscard]] QByteArray serialize_image_trace_user_presets(const std::vector<ImageTraceUserPreset>& presets);
[[nodiscard]] std::vector<ImageTraceUserPreset> deserialize_image_trace_user_presets(const QByteArray& json);
[[nodiscard]] std::vector<ImageTraceUserPreset> load_image_trace_user_presets();
void save_image_trace_user_presets(const std::vector<ImageTraceUserPreset>& presets);

// Named option sets offered by the Preset combo (the dialog adds a leading
// "Custom" row for hand-edited settings).
struct ImageTracePreset {
  const char* english_name;
  ImageTraceOptions options;
};
[[nodiscard]] const std::vector<ImageTracePreset>& image_trace_presets();

}  // namespace patchy::ui
