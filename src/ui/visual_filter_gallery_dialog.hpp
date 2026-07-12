#pragma once

#include "filters/filter_registry.hpp"

#include <QRegion>

#include <functional>
#include <optional>

class QWidget;

namespace patchy::ui {

enum class VisualFilterGalleryOutcome {
  Cancelled,
  Original,
  Filter,
};

struct VisualFilterGalleryResult {
  VisualFilterGalleryOutcome outcome{VisualFilterGalleryOutcome::Cancelled};
  std::optional<FilterInvocation> invocation;
};

struct VisualFilterGalleryPreview {
  bool canvas_enabled{true};
  std::optional<FilterInvocation> invocation;
};

using VisualFilterGalleryPreviewCallback =
    std::function<void(const VisualFilterGalleryPreview&)>;

// Presents every catalogued Filter-menu effect over an immutable, bounded
// proxy of the supplied layer pixels. A null preview invocation means Original;
// the result outcome distinguishes accepting Original from cancelling.
[[nodiscard]] VisualFilterGalleryResult request_visual_filter_gallery(
    QWidget* parent, const PixelBuffer& immutable_original, Rect bounds,
    const QRegion& selection, const FilterRegistry& registry,
    RgbColor foreground, RgbColor background,
    VisualFilterGalleryPreviewCallback preview_changed = {});

}  // namespace patchy::ui
