#pragma once

#include <QRect>
#include <QRegion>

namespace patchy::ui {

// Accumulates document-space damage for the GPU frame cache.
//
// The incremental Dawn path must rebuild the tiles that the *document* edit
// touched, not the tiles the *widget* happened to expose. Deriving dirty tiles
// from the paint event's exposed region and then recording the whole document
// revision as cached leaves offscreen tiles stale when a zoomed-in view edits
// something outside the viewport; panning later reuses the matching cache key
// and shows the old pixels. CanvasWidget records every document change here
// (document_changed_impl and the full-invalidation overloads) and the frame
// path consumes the pending region independently of the exposed widget area.
class GpuFrameInvalidation {
public:
  // Marks the whole document dirty. The next frame is a full composition.
  void mark_full() noexcept {
    full_ = true;
    region_ = QRegion();
  }

  // Records a document-space change. An empty region means "unknown extent"
  // and is treated as a full invalidation, matching the CPU render cache.
  void mark(const QRegion& document_region, const QRect& document_bounds) {
    if (full_) {
      return;
    }
    if (document_region.isEmpty() || document_bounds.isEmpty()) {
      mark_full();
      return;
    }
    const auto clipped = document_region.intersected(document_bounds);
    if (!clipped.isEmpty()) {
      region_ += clipped;
    }
  }

  void clear() noexcept {
    full_ = false;
    region_ = QRegion();
  }

  [[nodiscard]] bool full() const noexcept { return full_; }
  [[nodiscard]] bool empty() const noexcept { return !full_ && region_.isEmpty(); }
  [[nodiscard]] const QRegion& region() const noexcept { return region_; }

private:
  bool full_{false};
  QRegion region_;
};

}  // namespace patchy::ui
