#include "ui/filter_gallery_controls.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace patchy::ui {

namespace {

constexpr QColor kAccentColor{76, 154, 255};
constexpr int kDialMinimumSize = 84;
constexpr int kDialMaximumSize = 128;
constexpr double kDialCenterDeadRadius = 6.0;
constexpr int kWaveMinimumWidth = 160;
constexpr int kWaveMaximumWidth = 640;
constexpr int kWaveHeight = 104;

double wrapped_angle_delta(double delta) {
  while (delta > 180.0) {
    delta -= 360.0;
  }
  while (delta <= -180.0) {
    delta += 360.0;
  }
  return delta;
}

double closest_equivalent_angle(double pointer, double reference, int minimum,
                                int maximum) {
  const auto first_turn =
      std::ceil((static_cast<double>(minimum) - pointer) / 360.0);
  const auto last_turn =
      std::floor((static_cast<double>(maximum) - pointer) / 360.0);
  if (first_turn > last_turn) {
    return std::clamp(pointer, static_cast<double>(minimum),
                      static_cast<double>(maximum));
  }
  const auto nearest_turn =
      std::round((reference - pointer) / 360.0);
  const auto turn = std::clamp(nearest_turn, first_turn, last_turn);
  return pointer + turn * 360.0;
}

QPixmap empty_device_pixmap(const QWidget& widget) {
  const auto ratio = widget.devicePixelRatioF();
  const QSize pixel_size(
      std::max(1, static_cast<int>(std::ceil(widget.width() * ratio))),
      std::max(1, static_cast<int>(std::ceil(widget.height() * ratio))));
  QPixmap result(pixel_size);
  result.setDevicePixelRatio(ratio);
  result.fill(Qt::transparent);
  return result;
}

}  // namespace

FilterAngleDial::FilterAngleDial(QWidget* parent) : QWidget(parent) {
  setMinimumSize(kDialMinimumSize, kDialMinimumSize);
  setMaximumSize(kDialMaximumSize, kDialMaximumSize);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  const auto name =
      QCoreApplication::translate("FilterGalleryControls", "Angle");
  const auto description = QCoreApplication::translate(
      "FilterGalleryControls",
      "Drag around the dial to set the angle. Use the arrow keys for precise "
      "changes; hold Shift for larger steps.");
  setAccessibleName(name);
  setAccessibleDescription(description);
  setToolTip(description);
  publish_state();
}

void FilterAngleDial::set_range(int minimum, int maximum) {
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  if (minimum_ == minimum && maximum_ == maximum) {
    return;
  }
  minimum_ = minimum;
  maximum_ = maximum;
  default_angle_ = std::clamp(default_angle_, minimum_, maximum_);
  set_angle(angle_);
  publish_state();
}

int FilterAngleDial::minimum() const noexcept { return minimum_; }

int FilterAngleDial::maximum() const noexcept { return maximum_; }

void FilterAngleDial::set_angle(int degrees) {
  degrees = std::clamp(degrees, minimum_, maximum_);
  if (angle_ == degrees) {
    return;
  }
  angle_ = degrees;
  if (!dragging_) {
    last_requested_angle_ = degrees;
    drag_angle_ = degrees;
  }
  publish_state();
  update();
}

int FilterAngleDial::angle() const noexcept { return angle_; }

void FilterAngleDial::set_default_angle(int degrees) {
  default_angle_ = std::clamp(degrees, minimum_, maximum_);
  setProperty("filterAngleDefaultDegrees", default_angle_);
}

void FilterAngleDial::set_angle_changed_callback(
    std::function<void(int, bool)> callback) {
  angle_changed_ = std::move(callback);
}

QSize FilterAngleDial::sizeHint() const { return QSize(104, 104); }

void FilterAngleDial::paintEvent(QPaintEvent*) {
  if (face_cache_.isNull() ||
      !qFuzzyCompare(cache_device_pixel_ratio_, devicePixelRatioF())) {
    rebuild_cache();
  }

  QPainter painter(this);
  painter.drawPixmap(0, 0, face_cache_);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const auto face = dial_rect();
  const auto center = face.center();
  const auto radius = std::max(1.0, face.width() / 2.0 - 12.0);
  const auto radians = static_cast<double>(angle_) * std::numbers::pi / 180.0;
  const QPointF end(center.x() + std::cos(radians) * radius,
                    center.y() - std::sin(radians) * radius);

  painter.setPen(QPen(QColor(20, 22, 26, isEnabled() ? 230 : 120), 5.0,
                      Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center, end);
  painter.setPen(QPen(kAccentColor, 2.5, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center, end);
  painter.setPen(QPen(QColor(20, 22, 26), 1.0));
  painter.setBrush(kAccentColor);
  painter.drawEllipse(center, 4.5, 4.5);

  if (hasFocus()) {
    painter.setPen(QPen(kAccentColor, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(face.adjusted(1.0, 1.0, -1.0, -1.0));
  }
}

void FilterAngleDial::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  invalidate_cache();
}

void FilterAngleDial::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::StyleChange ||
      event->type() == QEvent::EnabledChange) {
    invalidate_cache();
  }
  QWidget::changeEvent(event);
}

void FilterAngleDial::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton ||
      !dial_rect().adjusted(-4.0, -4.0, 4.0, 4.0).contains(
          event->position())) {
    QWidget::mousePressEvent(event);
    return;
  }

  setFocus(Qt::MouseFocusReason);
  const auto offset = event->position() - dial_center();
  if (std::hypot(offset.x(), offset.y()) < kDialCenterDeadRadius) {
    event->accept();
    return;
  }
  const auto pointer = pointer_angle(event->position());
  drag_angle_ = closest_equivalent_angle(pointer, angle_, minimum_, maximum_);
  last_requested_angle_ = angle_;
  last_pointer_angle_ = pointer;
  const auto requested = std::clamp(
      static_cast<int>(std::lround(drag_angle_)), minimum_, maximum_);
  dragging_ = true;
  drag_changed_ = requested != angle_;
  last_requested_angle_ = requested;
  setCursor(Qt::ClosedHandCursor);
  publish_state();
  if (drag_changed_) {
    request_angle(requested, false);
  }
  update();
  event->accept();
}

void FilterAngleDial::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton) != 0) {
    const auto requested = requested_angle_from_pointer(event->position());
    if (requested != last_requested_angle_) {
      last_requested_angle_ = requested;
      drag_changed_ = true;
      request_angle(requested, false);
    }
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void FilterAngleDial::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && dragging_) {
    const auto requested = requested_angle_from_pointer(event->position());
    if (requested != last_requested_angle_) {
      last_requested_angle_ = requested;
      drag_changed_ = true;
      request_angle(requested, false);
    }
    dragging_ = false;
    unsetCursor();
    publish_state();
    if (drag_changed_) {
      request_angle(last_requested_angle_, true);
    }
    drag_changed_ = false;
    update();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void FilterAngleDial::keyPressEvent(QKeyEvent* event) {
  const int step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 15 : 1;
  int requested = angle_;
  switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
      requested -= step;
      break;
    case Qt::Key_Right:
    case Qt::Key_Up:
      requested += step;
      break;
    case Qt::Key_Home:
      requested = default_angle_;
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  requested = std::clamp(requested, minimum_, maximum_);
  if (requested != angle_) {
    request_angle(requested, true);
  }
  event->accept();
}

void FilterAngleDial::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  update();
}

void FilterAngleDial::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  update();
}

QRectF FilterAngleDial::dial_rect() const {
  const auto side = std::max(1.0, std::min(width(), height()) - 16.0);
  return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QPointF FilterAngleDial::dial_center() const { return dial_rect().center(); }

double FilterAngleDial::pointer_angle(QPointF position) const {
  const auto center = dial_center();
  return std::atan2(center.y() - position.y(), position.x() - center.x()) *
         180.0 / std::numbers::pi;
}

int FilterAngleDial::requested_angle_from_pointer(QPointF position) {
  const auto offset = position - dial_center();
  if (std::hypot(offset.x(), offset.y()) < kDialCenterDeadRadius) {
    return last_requested_angle_;
  }
  const auto pointer = pointer_angle(position);
  const auto delta = wrapped_angle_delta(pointer - last_pointer_angle_);
  drag_angle_ = std::clamp(drag_angle_ + delta,
                           static_cast<double>(minimum_),
                           static_cast<double>(maximum_));
  last_pointer_angle_ = pointer;
  return std::clamp(static_cast<int>(std::lround(drag_angle_)), minimum_,
                    maximum_);
}

void FilterAngleDial::request_angle(int degrees, bool gesture_finished) {
  degrees = std::clamp(degrees, minimum_, maximum_);
  if (angle_changed_) {
    angle_changed_(degrees, gesture_finished);
  }
}

void FilterAngleDial::publish_state() {
  setProperty("filterAngleDegrees", angle_);
  setProperty("filterAngleMinimum", minimum_);
  setProperty("filterAngleMaximum", maximum_);
  setProperty("filterAngleDefaultDegrees", default_angle_);
  setProperty("filterAngleDragging", dragging_);
}

void FilterAngleDial::invalidate_cache() {
  face_cache_ = {};
  cache_device_pixel_ratio_ = 0.0;
  update();
}

void FilterAngleDial::rebuild_cache() {
  face_cache_ = empty_device_pixmap(*this);
  cache_device_pixel_ratio_ = devicePixelRatioF();

  QPainter painter(&face_cache_);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto face = dial_rect();
  const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
  painter.setPen(QPen(palette().color(group, QPalette::Mid), 1.25));
  painter.setBrush(palette().color(group, QPalette::Base));
  painter.drawEllipse(face.adjusted(3.0, 3.0, -3.0, -3.0));

  const auto center = face.center();
  const auto radius = face.width() / 2.0;
  painter.setPen(QPen(palette().color(group, QPalette::Text), 1.0,
                      Qt::SolidLine, Qt::RoundCap));
  for (int tick = 0; tick < 12; ++tick) {
    const auto radians = static_cast<double>(tick) * std::numbers::pi / 6.0;
    const QPointF outer(center.x() + std::cos(radians) * (radius - 8.0),
                        center.y() - std::sin(radians) * (radius - 8.0));
    const auto length = tick % 3 == 0 ? 8.0 : 5.0;
    const QPointF inner(center.x() +
                            std::cos(radians) * (radius - 8.0 - length),
                        center.y() -
                            std::sin(radians) * (radius - 8.0 - length));
    painter.drawLine(inner, outer);
  }
}

FilterWaveformControl::FilterWaveformControl(QWidget* parent)
    : QWidget(parent) {
  setMinimumSize(kWaveMinimumWidth, kWaveHeight);
  setMaximumSize(kWaveMaximumWidth, kWaveHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::CrossCursor);

  const auto name =
      QCoreApplication::translate("FilterGalleryControls", "Waveform");
  const auto description = QCoreApplication::translate(
      "FilterGalleryControls",
      "Drag horizontally to change phase and vertically to change amplitude. "
      "Use the mouse wheel to change wavelength.");
  setAccessibleName(name);
  setAccessibleDescription(description);
  setToolTip(description);
  publish_state();
}

void FilterWaveformControl::set_ranges(FilterWaveformValues minimum,
                                       FilterWaveformValues maximum) {
  if (minimum.amplitude > maximum.amplitude) {
    std::swap(minimum.amplitude, maximum.amplitude);
  }
  if (minimum.wavelength > maximum.wavelength) {
    std::swap(minimum.wavelength, maximum.wavelength);
  }
  if (minimum.phase > maximum.phase) {
    std::swap(minimum.phase, maximum.phase);
  }
  if (minimum_ == minimum && maximum_ == maximum) {
    return;
  }
  minimum_ = minimum;
  maximum_ = maximum;
  default_values_ = normalized_values(default_values_);
  values_ = normalized_values(values_);
  last_requested_values_ = values_;
  publish_state();
  invalidate_cache();
}

void FilterWaveformControl::set_values(FilterWaveformValues values) {
  values = normalized_values(values);
  if (values_ == values) {
    return;
  }
  values_ = values;
  if (!dragging_) {
    last_requested_values_ = values;
  }
  publish_state();
  invalidate_cache();
}

FilterWaveformValues FilterWaveformControl::values() const noexcept {
  return values_;
}

void FilterWaveformControl::set_default_values(FilterWaveformValues values) {
  default_values_ = normalized_values(values);
  setProperty("filterWaveDefaultAmplitude", default_values_.amplitude);
  setProperty("filterWaveDefaultWavelength", default_values_.wavelength);
  setProperty("filterWaveDefaultPhase", default_values_.phase);
}

void FilterWaveformControl::set_values_changed_callback(
    std::function<void(FilterWaveformValues, bool)> callback) {
  values_changed_ = std::move(callback);
}

QSize FilterWaveformControl::sizeHint() const {
  return QSize(260, kWaveHeight);
}

void FilterWaveformControl::paintEvent(QPaintEvent*) {
  if (graph_cache_.isNull() ||
      !qFuzzyCompare(cache_device_pixel_ratio_, devicePixelRatioF())) {
    rebuild_cache();
  }

  QPainter painter(this);
  painter.drawPixmap(0, 0, graph_cache_);
  if (hasFocus()) {
    painter.setPen(QPen(kAccentColor, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
  }
}

void FilterWaveformControl::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  invalidate_cache();
}

void FilterWaveformControl::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::StyleChange ||
      event->type() == QEvent::EnabledChange) {
    invalidate_cache();
  }
  QWidget::changeEvent(event);
}

void FilterWaveformControl::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  dragging_ = true;
  drag_changed_ = false;
  drag_start_position_ = event->position();
  drag_start_values_ = values_;
  last_requested_values_ = values_;
  publish_state();
  event->accept();
}

void FilterWaveformControl::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton) != 0) {
    const auto requested = values_from_drag(event->position());
    if (requested != last_requested_values_) {
      last_requested_values_ = requested;
      drag_changed_ = true;
      request_values(requested, false);
    }
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void FilterWaveformControl::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && dragging_) {
    const auto requested = values_from_drag(event->position());
    if (requested != last_requested_values_) {
      last_requested_values_ = requested;
      drag_changed_ = true;
      request_values(requested, false);
    }
    dragging_ = false;
    publish_state();
    if (drag_changed_) {
      request_values(last_requested_values_, true);
    }
    drag_changed_ = false;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void FilterWaveformControl::wheelEvent(QWheelEvent* event) {
  const auto delta = event->angleDelta().y();
  if (delta == 0) {
    event->ignore();
    return;
  }
  auto requested = values_;
  const int step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 8 : 1;
  requested.wavelength += delta > 0 ? step : -step;
  requested = normalized_values(requested);
  if (requested != values_) {
    request_values(requested, true);
  }
  event->accept();
}

void FilterWaveformControl::keyPressEvent(QKeyEvent* event) {
  auto requested = values_;
  const bool large_step =
      (event->modifiers() & Qt::ShiftModifier) != 0;
  switch (event->key()) {
    case Qt::Key_Left:
      requested.phase -= large_step ? 15 : 1;
      break;
    case Qt::Key_Right:
      requested.phase += large_step ? 15 : 1;
      break;
    case Qt::Key_Down:
      requested.amplitude -= large_step ? 8 : 1;
      break;
    case Qt::Key_Up:
      requested.amplitude += large_step ? 8 : 1;
      break;
    case Qt::Key_PageDown:
      requested.wavelength -= large_step ? 16 : 4;
      break;
    case Qt::Key_PageUp:
      requested.wavelength += large_step ? 16 : 4;
      break;
    case Qt::Key_Home:
      requested = default_values_;
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  requested = normalized_values(requested);
  if (requested != values_) {
    request_values(requested, true);
  }
  event->accept();
}

void FilterWaveformControl::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  update();
}

void FilterWaveformControl::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  update();
}

FilterWaveformValues FilterWaveformControl::normalized_values(
    FilterWaveformValues values) const {
  values.amplitude =
      std::clamp(values.amplitude, minimum_.amplitude, maximum_.amplitude);
  values.wavelength =
      std::clamp(values.wavelength, minimum_.wavelength, maximum_.wavelength);
  values.phase = std::clamp(values.phase, minimum_.phase, maximum_.phase);
  return values;
}

FilterWaveformValues FilterWaveformControl::values_from_drag(
    QPointF position) const {
  const auto usable_width = std::max(1, width() - 20);
  const auto usable_height = std::max(1, height() - 20);
  const auto delta = position - drag_start_position_;

  auto requested = drag_start_values_;
  const auto amplitude_span = maximum_.amplitude - minimum_.amplitude;
  const auto phase_span = maximum_.phase - minimum_.phase;
  requested.amplitude += static_cast<int>(std::lround(
      -delta.y() * static_cast<double>(amplitude_span) / usable_height));
  requested.phase += static_cast<int>(std::lround(
      delta.x() * static_cast<double>(phase_span) / usable_width));
  return normalized_values(requested);
}

void FilterWaveformControl::request_values(FilterWaveformValues values,
                                           bool gesture_finished) {
  values = normalized_values(values);
  if (values_changed_) {
    values_changed_(values, gesture_finished);
  }
}

void FilterWaveformControl::publish_state() {
  setProperty("filterWaveAmplitude", values_.amplitude);
  setProperty("filterWaveWavelength", values_.wavelength);
  setProperty("filterWavePhase", values_.phase);
  setProperty("filterWaveMinimumAmplitude", minimum_.amplitude);
  setProperty("filterWaveMaximumAmplitude", maximum_.amplitude);
  setProperty("filterWaveMinimumWavelength", minimum_.wavelength);
  setProperty("filterWaveMaximumWavelength", maximum_.wavelength);
  setProperty("filterWaveMinimumPhase", minimum_.phase);
  setProperty("filterWaveMaximumPhase", maximum_.phase);
  setProperty("filterWaveDefaultAmplitude", default_values_.amplitude);
  setProperty("filterWaveDefaultWavelength", default_values_.wavelength);
  setProperty("filterWaveDefaultPhase", default_values_.phase);
  setProperty("filterWaveDragging", dragging_);
}

void FilterWaveformControl::invalidate_cache() {
  graph_cache_ = {};
  cache_device_pixel_ratio_ = 0.0;
  update();
}

void FilterWaveformControl::rebuild_cache() {
  graph_cache_ = empty_device_pixmap(*this);
  cache_device_pixel_ratio_ = devicePixelRatioF();

  QPainter painter(&graph_cache_);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
  const QRectF graph = QRectF(rect()).adjusted(9.5, 9.5, -9.5, -9.5);

  painter.fillRect(graph, palette().color(group, QPalette::Base));
  painter.setPen(QPen(palette().color(group, QPalette::Mid), 1.0));
  painter.drawRect(graph);
  painter.setPen(QPen(palette().color(group, QPalette::Midlight), 1.0));
  painter.drawLine(QPointF(graph.left(), graph.center().y()),
                   QPointF(graph.right(), graph.center().y()));
  for (int division = 1; division < 4; ++division) {
    const auto x = graph.left() + graph.width() * division / 4.0;
    painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
  }

  const auto amplitude_span =
      std::max(1, maximum_.amplitude - minimum_.amplitude);
  const auto wavelength_span =
      std::max(1, maximum_.wavelength - minimum_.wavelength);
  const auto amplitude_fraction =
      static_cast<double>(values_.amplitude - minimum_.amplitude) /
      amplitude_span;
  const auto wavelength_fraction =
      static_cast<double>(values_.wavelength - minimum_.wavelength) /
      wavelength_span;
  const auto visual_amplitude =
      amplitude_fraction * std::max(1.0, graph.height() / 2.0 - 5.0);
  const auto visual_period =
      graph.width() * (0.12 + wavelength_fraction * 0.88);
  const auto phase = static_cast<double>(values_.phase) *
                     std::numbers::pi / 180.0;
  const auto sample_count =
      std::clamp(static_cast<int>(std::ceil(graph.width())), 2, 512);

  QPainterPath waveform;
  for (int sample = 0; sample < sample_count; ++sample) {
    const auto fraction = static_cast<double>(sample) / (sample_count - 1);
    const auto x = graph.left() + fraction * graph.width();
    const auto y = graph.center().y() -
                   std::sin((x - graph.left()) * 2.0 * std::numbers::pi /
                                std::max(1.0, visual_period) +
                            phase) *
                       visual_amplitude;
    if (sample == 0) {
      waveform.moveTo(x, y);
    } else {
      waveform.lineTo(x, y);
    }
  }

  painter.setClipRect(graph.adjusted(1.0, 1.0, -1.0, -1.0));
  painter.setPen(QPen(QColor(20, 22, 26, isEnabled() ? 220 : 110), 4.0,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(waveform);
  painter.setPen(QPen(kAccentColor, 2.0, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
  painter.drawPath(waveform);
}

}  // namespace patchy::ui
