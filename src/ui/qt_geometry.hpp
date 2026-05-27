#pragma once

#include "core/layer.hpp"

#include <QRect>
#include <QRegion>

namespace patchy::ui {

[[nodiscard]] QRect to_qrect(Rect rect);
[[nodiscard]] Rect to_core_rect(QRect rect);
[[nodiscard]] QRegion expanded_region(const QRegion& region, int pixels, QRect bounds);
[[nodiscard]] QRegion selection_outline_region(const QRegion& selection, int thickness, QRect bounds);

}  // namespace patchy::ui
