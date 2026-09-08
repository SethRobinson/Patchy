#pragma once

#include "core/layer.hpp"

#include <unordered_map>

namespace patchy::render_detail {

// Temporary viewport rasters contain a clipped portion of each layer. Paint
// anchors still refer to the whole layer, and their style masks must not enter
// the document renderer's persistent caches. This context belongs to one
// synchronous compositor call on a preview worker, never to a document.
struct RasterViewAppearance {
  Rect bounds;
  Rect visible_bounds;
  Rect fill_visible_bounds;
};

struct RasterViewContext {
  std::unordered_map<LayerId, RasterViewAppearance> appearance;
};

inline thread_local const RasterViewContext* raster_view_context = nullptr;

class ScopedRasterViewContext {
public:
  explicit ScopedRasterViewContext(const RasterViewContext& context) noexcept
      : previous_(raster_view_context) { raster_view_context = &context; }
  ~ScopedRasterViewContext() { raster_view_context = previous_; }
  ScopedRasterViewContext(const ScopedRasterViewContext&) = delete;
  ScopedRasterViewContext& operator=(const ScopedRasterViewContext&) = delete;
private:
  const RasterViewContext* previous_;
};

inline const RasterViewAppearance* raster_view_appearance(LayerId id) {
  if (raster_view_context == nullptr) { return nullptr; }
  const auto found = raster_view_context->appearance.find(id);
  return found == raster_view_context->appearance.end() ? nullptr : &found->second;
}

}  // namespace patchy::render_detail
