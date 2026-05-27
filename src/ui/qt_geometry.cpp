#include "ui/qt_geometry.hpp"

#include <algorithm>

namespace patchy::ui {

QRect to_qrect(Rect rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

Rect to_core_rect(QRect rect) {
  rect = rect.normalized();
  return Rect{rect.x(), rect.y(), rect.width(), rect.height()};
}

QRegion expanded_region(const QRegion& region, int pixels, QRect bounds) {
  if (region.isEmpty() || pixels <= 0) {
    return region.intersected(bounds);
  }

  pixels = std::clamp(pixels, 0, 250);
  QRegion expanded;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    for (int dx = -pixels; dx <= pixels; ++dx) {
      expanded = expanded.united(region.translated(dx, dy));
    }
  }
  return expanded.intersected(bounds);
}

QRegion selection_outline_region(const QRegion& selection, int thickness, QRect bounds) {
  if (selection.isEmpty()) {
    return {};
  }

  QRegion outline;
  outline = outline.united(selection.subtracted(selection.translated(1, 0)));
  outline = outline.united(selection.subtracted(selection.translated(-1, 0)));
  outline = outline.united(selection.subtracted(selection.translated(0, 1)));
  outline = outline.united(selection.subtracted(selection.translated(0, -1)));
  return expanded_region(outline, std::max(0, thickness / 2), bounds);
}

}  // namespace patchy::ui
