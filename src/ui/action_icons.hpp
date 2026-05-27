#pragma once

#include "core/pixel_tools.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

namespace patchy::ui {

QIcon simple_icon(QString text, QColor accent = QColor(220, 226, 235));
QIcon patchy_app_icon();
QIcon window_chrome_icon(QString role);
QIcon canvas_anchor_icon(CanvasAnchor anchor);

}  // namespace patchy::ui
