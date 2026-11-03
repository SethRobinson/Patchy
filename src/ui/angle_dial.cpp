#include "ui/angle_dial.hpp"

#include "ui/theme_palette.hpp"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSpinBox>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

constexpr int kAngleDialSide = 36;
constexpr int kAngleAltitudeDialSide = 64;
// A press this close to the center keeps the angle (it has no direction).
constexpr double kCenterDeadRadius = 2.0;
constexpr double kMaximumAltitude = 90.0;

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

QPointF offset_for(double angle_degrees, double distance) {
  return {std::cos(radians(angle_degrees)) * distance, -std::sin(radians(angle_degrees)) * distance};
}

}  // namespace

AngleDial::AngleDial(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode) {
  const auto side = mode_ == Mode::AngleAltitude ? kAngleAltitudeDialSide : kAngleDialSide;
  setFixedSize(side, side);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  if (mode_ == Mode::AngleAltitude) {
    setAccessibleName(QCoreApplication::translate("AngleDial", "Angle and altitude"));
    setToolTip(QCoreApplication::translate(
        "AngleDial",
        "Click or drag to set the angle. The distance from the center sets the altitude: "
        "90° at the center, 0° at the edge. Arrow keys: Left and Right turn, Up and Down "
        "change the altitude."));
  } else {
    setAccessibleName(QCoreApplication::translate("AngleDial", "Angle"));
    setToolTip(QCoreApplication::translate(
        "AngleDial", "Click or drag to set the angle. Arrow keys turn it; hold Shift for 15° steps."));
  }
  setAccessibleDescription(toolTip());
  publish_state();
}

void AngleDial::set_angle_range(double minimum, double maximum) {
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  minimum_ = minimum;
  maximum_ = maximum;
  angle_ = std::clamp(angle_, minimum_, maximum_);
  publish_state();
  update();
}

void AngleDial::set_angle(double degrees) {
  degrees = std::clamp(degrees, minimum_, maximum_);
  if (degrees == angle_) {
    return;
  }
  angle_ = degrees;
  publish_state();
  update();
}

void AngleDial::set_altitude(double degrees) {
  degrees = std::clamp(degrees, 0.0, kMaximumAltitude);
  if (degrees == altitude_) {
    return;
  }
  altitude_ = degrees;
  publish_state();
  update();
}

void AngleDial::set_changed_callback(std::function<void(int, int)> callback) { changed_ = std::move(callback); }

int AngleDial::angle_for_offset(QPointF offset) {
  const auto degrees = static_cast<int>(std::lround(std::atan2(-offset.y(), offset.x()) * 180.0 / std::numbers::pi));
  // Straight left reads 180, never -180 (atan2 of -0.0 gives the latter).
  return degrees == -180 ? 180 : degrees;
}

int AngleDial::altitude_for_distance(double distance, double radius) {
  if (radius <= 0.0) {
    return 0;
  }
  const auto fraction = std::clamp(distance / radius, 0.0, 1.0);
  return static_cast<int>(std::lround(kMaximumAltitude * (1.0 - fraction)));
}

QPointF AngleDial::center() const { return {width() / 2.0, height() / 2.0}; }

double AngleDial::radius() const { return std::max(1.0, std::min(width(), height()) / 2.0 - 2.0); }

QPointF AngleDial::marker_position() const {
  const auto distance = mode_ == Mode::AngleAltitude ? radius() * (1.0 - altitude_ / kMaximumAltitude)
                                                     : radius() - 3.0;
  return center() + offset_for(angle_, distance);
}

QSize AngleDial::sizeHint() const {
  const auto side = mode_ == Mode::AngleAltitude ? kAngleAltitudeDialSide : kAngleDialSide;
  return {side, side};
}

QSize AngleDial::minimumSizeHint() const { return sizeHint(); }

void AngleDial::paintEvent(QPaintEvent*) {
  const auto& colors = theme();
  const bool enabled = isEnabled();
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto middle = center();
  const auto r = radius();

  painter.setPen(QPen(enabled ? colors.field_inset_border : colors.field_border_disabled, 1.0));
  painter.setBrush(enabled ? colors.field_bg : colors.field_bg_disabled);
  painter.drawEllipse(middle, r, r);

  const auto tick_color = enabled ? colors.text_secondary : colors.text_disabled;
  if (mode_ == Mode::AngleAltitude) {
    // Altitude guides at 30° and 60°.
    auto guide = tick_color;
    guide.setAlpha(std::min(guide.alpha(), 90));
    painter.setPen(QPen(guide, 1.0, Qt::DotLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(middle, r * 2.0 / 3.0, r * 2.0 / 3.0);
    painter.drawEllipse(middle, r / 3.0, r / 3.0);
  }
  painter.setPen(QPen(tick_color, 1.0, Qt::SolidLine, Qt::FlatCap));
  const auto tick = mode_ == Mode::AngleAltitude ? 5.0 : 3.0;
  for (int quarter = 0; quarter < 4; ++quarter) {
    const auto degrees = quarter * 90.0;
    painter.drawLine(middle + offset_for(degrees, r - tick), middle + offset_for(degrees, r));
  }

  const auto hand = enabled ? colors.accent_control : colors.text_disabled;
  const auto end = marker_position();
  painter.setPen(QPen(hand, 2.0, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(middle, end);
  painter.setPen(Qt::NoPen);
  painter.setBrush(hand);
  painter.drawEllipse(middle, 2.0, 2.0);
  if (mode_ == Mode::AngleAltitude) {
    painter.setPen(QPen(hand, 2.0));
    painter.setBrush(enabled ? colors.field_bg : colors.field_bg_disabled);
    painter.drawEllipse(end, 4.0, 4.0);
  } else {
    painter.drawEllipse(end, 2.5, 2.5);
  }

  if (hasFocus()) {
    painter.setPen(QPen(colors.accent, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(middle, r + 0.5, r + 0.5);
  }
}

void AngleDial::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  dragging_ = true;
  publish_state();
  request_from_pointer(event->position());
  event->accept();
}

void AngleDial::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton) != 0) {
    request_from_pointer(event->position());
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void AngleDial::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && dragging_) {
    request_from_pointer(event->position());
    dragging_ = false;
    publish_state();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void AngleDial::keyPressEvent(QKeyEvent* event) {
  const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
  const double turn = shift ? 15.0 : 1.0;
  const double lift = shift ? 10.0 : 1.0;
  const bool altitude_keys = mode_ == Mode::AngleAltitude;
  auto angle = angle_;
  auto altitude = altitude_;
  switch (event->key()) {
    case Qt::Key_Right:
      angle += turn;
      break;
    case Qt::Key_Left:
      angle -= turn;
      break;
    case Qt::Key_Up:
      if (altitude_keys) {
        altitude += lift;
      } else {
        angle += turn;
      }
      break;
    case Qt::Key_Down:
      if (altitude_keys) {
        altitude -= lift;
      } else {
        angle -= turn;
      }
      break;
    case Qt::Key_PageUp:
      angle += 15.0;
      break;
    case Qt::Key_PageDown:
      angle -= 15.0;
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  request(fit_angle(angle, true), altitude);
  event->accept();
}

void AngleDial::wheelEvent(QWheelEvent* event) {
  // Like the page's other fields (install_wheel_scroll_guard): the wheel
  // scrolls the page unless the dial has keyboard focus.
  if (!hasFocus()) {
    event->ignore();
    return;
  }
  const auto delta = event->angleDelta();
  const auto notches = (delta.y() != 0 ? delta.y() : delta.x()) / 120;
  if (notches == 0) {
    event->accept();
    return;
  }
  const double step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 15.0 : 1.0;
  request(fit_angle(angle_ + notches * step, true), altitude_);
  event->accept();
}

void AngleDial::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  update();
}

void AngleDial::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  update();
}

void AngleDial::request_from_pointer(QPointF position) {
  const auto offset = position - center();
  const auto distance = std::hypot(offset.x(), offset.y());
  auto angle = angle_;
  if (distance >= kCenterDeadRadius) {
    angle = fit_angle(angle_for_offset(offset), true);
  }
  auto altitude = altitude_;
  if (mode_ == Mode::AngleAltitude) {
    altitude = altitude_for_distance(distance, radius());
  }
  request(angle, altitude);
}

void AngleDial::request(double angle, double altitude) {
  const auto requested_angle = static_cast<int>(std::lround(std::clamp(angle, minimum_, maximum_)));
  const auto requested_altitude = static_cast<int>(std::lround(std::clamp(altitude, 0.0, kMaximumAltitude)));
  if (changed_) {
    changed_(requested_angle, requested_altitude);
  } else {
    set_angle(requested_angle);
    set_altitude(requested_altitude);
  }
}

double AngleDial::fit_angle(double degrees, bool wrap) const {
  if (wrap && maximum_ - minimum_ >= 360.0) {
    while (degrees > maximum_) {
      degrees -= 360.0;
    }
    while (degrees < minimum_) {
      degrees += 360.0;
    }
  }
  return std::clamp(degrees, minimum_, maximum_);
}

void AngleDial::publish_state() {
  setProperty("angleDialAngle", angle_);
  setProperty("angleDialAltitude", altitude_);
  setProperty("angleDialDragging", dragging_);
}

namespace {
constexpr char kBoundDialProperty[] = "patchy.angleDial";
constexpr char kBoundAltitudeProperty[] = "patchy.angleDialAltitude";

template <typename Spin, typename Apply>
void follow_field(AngleDial& dial, Spin& field, Apply apply, bool altitude) {
  QObject::connect(&field, qOverload<decltype(field.value())>(&Spin::valueChanged), &dial,
                   [dial = &dial, apply](auto value) { apply(*dial, static_cast<double>(value)); });
  // sync_angle_dial finds the dial again for loads under a QSignalBlocker.
  field.setProperty(kBoundDialProperty, QVariant::fromValue(static_cast<QObject*>(&dial)));
  field.setProperty(kBoundAltitudeProperty, altitude);
}
}  // namespace

void sync_angle_dial(QAbstractSpinBox& field) {
  auto* dial = qobject_cast<AngleDial*>(field.property(kBoundDialProperty).value<QObject*>());
  if (dial == nullptr) {
    return;
  }
  double value = 0.0;
  if (auto* integer = qobject_cast<QSpinBox*>(&field); integer != nullptr) {
    value = integer->value();
  } else if (auto* real = qobject_cast<QDoubleSpinBox*>(&field); real != nullptr) {
    value = real->value();
  } else {
    return;
  }
  if (field.property(kBoundAltitudeProperty).toBool()) {
    dial->set_altitude(value);
  } else {
    dial->set_angle(value);
  }
}

void bind_angle_dial(AngleDial& dial, QSpinBox& angle, QSpinBox* altitude) {
  dial.set_angle_range(angle.minimum(), angle.maximum());
  dial.set_angle(angle.value());
  if (altitude != nullptr) {
    dial.set_altitude(altitude->value());
  }
  QPointer<QSpinBox> angle_field(&angle);
  QPointer<QSpinBox> altitude_field(altitude);
  dial.set_changed_callback([angle_field, altitude_field](int degrees, int lift) {
    if (angle_field != nullptr) {
      angle_field->setValue(degrees);
    }
    if (altitude_field != nullptr) {
      altitude_field->setValue(lift);
    }
  });
  follow_field(dial, angle, [](AngleDial& target, double degrees) { target.set_angle(degrees); }, false);
  if (altitude != nullptr) {
    follow_field(dial, *altitude, [](AngleDial& target, double degrees) { target.set_altitude(degrees); }, true);
  }
}

void bind_angle_dial(AngleDial& dial, QDoubleSpinBox& angle) {
  dial.set_angle_range(angle.minimum(), angle.maximum());
  dial.set_angle(angle.value());
  QPointer<QDoubleSpinBox> angle_field(&angle);
  dial.set_changed_callback([angle_field](int degrees, int) {
    if (angle_field != nullptr) {
      angle_field->setValue(degrees);
    }
  });
  follow_field(dial, angle, [](AngleDial& target, double degrees) { target.set_angle(degrees); }, false);
}

QWidget* wrap_angle_field_with_dial(QAbstractSpinBox* field, QWidget* parent) {
  auto* row = new QWidget(parent);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addWidget(field, 0, Qt::AlignTop);
  auto* dial = new AngleDial(AngleDial::Mode::Angle, row);
  dial->setObjectName(angle_dial_object_name(field->objectName()));
  layout->addWidget(dial);
  layout->addStretch(1);
  if (auto* integer = qobject_cast<QSpinBox*>(field); integer != nullptr) {
    bind_angle_dial(*dial, *integer);
  } else if (auto* real = qobject_cast<QDoubleSpinBox*>(field); real != nullptr) {
    bind_angle_dial(*dial, *real);
  }
  return row;
}

QString angle_dial_object_name(QString spin_object_name) {
  if (spin_object_name.endsWith(QStringLiteral("Spin"))) {
    spin_object_name.chop(4);
  }
  return spin_object_name + QStringLiteral("Dial");
}

}  // namespace patchy::ui
