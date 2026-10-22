#include "ui/eyedropper_sample_overlay.hpp"

#include <algorithm>

namespace patchy::ui {

bool eyedropper_sample_outline_visible(int sample_size, double zoom) noexcept {
  return sample_size > 1 && static_cast<double>(sample_size) * zoom >= kEyedropperSampleOutlineMinScreenPixels;
}

QRect eyedropper_sample_rect(QPoint point, int sample_size, QSize document_size) noexcept {
  // The same square pick_color averages: odd sides centered on the pixel.
  const int half = std::max(1, sample_size) / 2;
  return QRect(point.x() - half, point.y() - half, 2 * half + 1, 2 * half + 1)
      .intersected(QRect(QPoint(0, 0), document_size));
}

}  // namespace patchy::ui
