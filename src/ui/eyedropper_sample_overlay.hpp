#pragma once

// The Pick tool's canvas overlays, kept free of CanvasWidget so their geometry is
// testable on its own (docs/pick-tool.md): the sample square's outline at
// the pointer (GitHub issue 319). It is drawn in cursor ink (a dark-plus-light pair),
// which is exempt from the theme roles like every tool cursor (docs/ui-conventions.md).

#include <QPoint>
#include <QRect>
#include <QSize>

namespace patchy::ui {

// The outline shows for an average (not Point Sample) whose square is at least this
// many screen pixels across.
inline constexpr double kEyedropperSampleOutlineMinScreenPixels = 4.0;
[[nodiscard]] bool eyedropper_sample_outline_visible(int sample_size, double zoom) noexcept;
// The document square a pick of `sample_size` averages around `point`, clipped to the document.
[[nodiscard]] QRect eyedropper_sample_rect(QPoint point, int sample_size, QSize document_size) noexcept;

}  // namespace patchy::ui
