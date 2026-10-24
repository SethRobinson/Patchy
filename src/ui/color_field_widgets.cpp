#include "ui/color_field_widgets.hpp"

#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

int rounded_scaled_channel(int position, int maximum_position, int maximum_value) {
  if (maximum_position <= 0) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(position) * maximum_value / maximum_position)), 0,
                    maximum_value);
}

QImage render_saturation_value_image(int hue, QSize size) {
  if (size.isEmpty()) {
    return {};
  }
  QImage image(size, QImage::Format_RGB32);
  const auto max_x = std::max(1, size.width() - 1);
  const auto max_y = std::max(1, size.height() - 1);
  for (int y = 0; y < size.height(); ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    const int value = 255 - rounded_scaled_channel(y, max_y, 255);
    for (int x = 0; x < size.width(); ++x) {
      const int saturation = rounded_scaled_channel(x, max_x, 255);
      scanline[x] = QColor::fromHsv(hue, saturation, value).rgb();
    }
  }
  return image;
}

void draw_crosshair_marker(QPainter& painter, QPointF center) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(10, 10, 10), 3.0));
  painter.drawLine(center + QPointF(-8.0, 0.0), center + QPointF(8.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -8.0), center + QPointF(0.0, 8.0));
  painter.setPen(QPen(QColor(245, 245, 245), 1.0));
  painter.drawLine(center + QPointF(-8.0, 0.0), center + QPointF(8.0, 0.0));
  painter.drawLine(center + QPointF(0.0, -8.0), center + QPointF(0.0, 8.0));
}

ColorPlaneWidget::ColorPlaneWidget(HsvFieldSource& source, QWidget* parent) : QWidget(parent), source_(source) {
  setObjectName(QStringLiteral("patchyColorPlane"));
  setCursor(Qt::CrossCursor);
  setMinimumSize(kDefaultSize, kDefaultSize);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFocusPolicy(Qt::ClickFocus);  // Edit > Copy/Paste route to the picker
}

void ColorPlaneWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  const auto paint_size = size();
  if (paint_size.isEmpty()) {
    return;
  }

  const auto max_x = std::max(1, paint_size.width() - 1);
  const auto max_y = std::max(1, paint_size.height() - 1);

  if (plane_hue_ != source_.hue() || plane_.size() != paint_size) {
    plane_ = render_saturation_value_image(source_.hue(), paint_size);
    plane_hue_ = source_.hue();
  }
  QPainter painter(this);
  painter.drawImage(QPoint(0, 0), plane_);
  painter.setPen(QPen(QColor(26, 26, 26), 1));
  painter.drawRect(rect().adjusted(0, 0, -1, -1));

  const double cursor_x = static_cast<double>(source_.saturation()) / 255.0 * max_x;
  const double cursor_y = static_cast<double>(255 - source_.value()) / 255.0 * max_y;
  draw_crosshair_marker(painter, QPointF(cursor_x, cursor_y));
}

void ColorPlaneWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    source_.begin_field_pick(event->modifiers());
    source_.set_saturation_value_from_point(event->position().toPoint(), size());
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ColorPlaneWidget::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) != 0) {
    source_.set_saturation_value_from_point(event->position().toPoint(), size());
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ColorPlaneWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    source_.end_field_pick();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

HueSliderWidget::HueSliderWidget(HsvFieldSource& source, QWidget* parent) : QWidget(parent), source_(source) {
  setObjectName(QStringLiteral("patchyHueSlider"));
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(kDefaultWidth, ColorPlaneWidget::kDefaultSize);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFocusPolicy(Qt::ClickFocus);
}

void HueSliderWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  const auto max_y = std::max(1, height() - 1);
  for (int y = 0; y < height(); ++y) {
    const int hue = rounded_scaled_channel(y, max_y, 359);
    painter.setPen(QColor::fromHsv(hue, 255, 255));
    painter.drawLine(0, y, width() - 1, y);
  }

  painter.setPen(QPen(QColor(26, 26, 26), 1));
  painter.drawRect(rect().adjusted(0, 0, -1, -1));

  const int marker_y = rounded_scaled_channel(source_.hue(), 359, max_y);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(0, 0, 0), 3.0));
  painter.drawLine(QPointF(0.0, marker_y), QPointF(width() - 1.0, marker_y));
  painter.setPen(QPen(QColor(245, 245, 245), 1.0));
  painter.drawLine(QPointF(0.0, marker_y), QPointF(width() - 1.0, marker_y));
}

void HueSliderWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    source_.begin_field_pick(event->modifiers());
    source_.set_hue_from_point(event->position().toPoint(), size());
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void HueSliderWidget::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) != 0) {
    source_.set_hue_from_point(event->position().toPoint(), size());
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void HueSliderWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    source_.end_field_pick();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

}  // namespace patchy::ui
