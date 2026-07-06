#include "ui/main_window_shared.hpp"

#include "ui/canvas_widget.hpp"

#include <QObject>
#include <QPoint>
#include <QRegion>

#include <algorithm>
#include <cstdint>

namespace patchy::ui {

QString localized_adjustment_display_name(AdjustmentKind kind) {
  switch (kind) {
    case AdjustmentKind::Levels:
      return QObject::tr("Levels");
    case AdjustmentKind::Curves:
      return QObject::tr("Curves");
    case AdjustmentKind::HueSaturation:
      return QObject::tr("Hue/Saturation");
    case AdjustmentKind::ColorBalance:
      return QObject::tr("Color Balance");
  }
  return QObject::tr("Adjustment");
}

PixelBuffer selection_mask_pixels(const CanvasWidget& canvas, QRect selection_rect) {
  PixelBuffer mask_pixels(selection_rect.width(), selection_rect.height(), PixelFormat::gray8());
  mask_pixels.clear(0);
  if (selection_rect.isEmpty()) {
    return mask_pixels;
  }

  if (!canvas.selection_has_partial_alpha()) {
    const auto selected = canvas.selected_document_region().intersected(QRegion(selection_rect));
    const QRect local_bounds(0, 0, selection_rect.width(), selection_rect.height());
    for (const auto& rect : selected) {
      const auto local =
          QRect(rect.x() - selection_rect.x(), rect.y() - selection_rect.y(), rect.width(), rect.height())
              .intersected(local_bounds);
      if (local.isEmpty()) {
        continue;
      }
      for (int y = local.top(); y <= local.bottom(); ++y) {
        auto row = mask_pixels.row(y);
        std::fill(row.begin() + local.left(), row.begin() + local.left() + local.width(),
                  static_cast<std::uint8_t>(255));
      }
    }
    return mask_pixels;
  }

  for (int y = 0; y < selection_rect.height(); ++y) {
    for (int x = 0; x < selection_rect.width(); ++x) {
      const QPoint document_point(selection_rect.x() + x, selection_rect.y() + y);
      *mask_pixels.pixel(x, y) = canvas.selection_alpha_at(document_point);
    }
  }
  return mask_pixels;
}

}  // namespace patchy::ui
