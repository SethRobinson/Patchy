#include "ui/blend_if_range_editor.hpp"

#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace patchy::ui {

namespace {

constexpr int kHorizontalGutter = 12;
constexpr int kRampTop = 8;
constexpr int kRampHeight = 20;
constexpr int kHandleHalfWidth = 8;
constexpr int kHandleHeight = 17;
constexpr int kMinimumWidth = 260;
constexpr int kWidgetHeight = 56;

constexpr QColor kSelectedColor{76, 154, 255};
constexpr QColor kDarkOutline{32, 35, 40};
constexpr QColor kLightOutline{232, 235, 240};

BlendIfThresholds normalized_thresholds(BlendIfThresholds value) {
  int black_low = value.black_low;
  int black_high = std::max(black_low, static_cast<int>(value.black_high));
  int white_low = std::max(black_high, static_cast<int>(value.white_low));
  int white_high = std::max(white_low, static_cast<int>(value.white_high));

  black_low = std::clamp(black_low, 0, 255);
  black_high = std::clamp(black_high, black_low, 255);
  white_low = std::clamp(white_low, black_high, 255);
  white_high = std::clamp(white_high, white_low, 255);

  value.black_low = static_cast<std::uint8_t>(black_low);
  value.black_high = static_cast<std::uint8_t>(black_high);
  value.white_low = static_cast<std::uint8_t>(white_low);
  value.white_high = static_cast<std::uint8_t>(white_high);
  return value;
}

QColor ramp_end_color(BlendIfRampChannel channel) {
  switch (channel) {
    case BlendIfRampChannel::Gray:
      return QColor(255, 255, 255);
    case BlendIfRampChannel::Red:
      return QColor(255, 0, 0);
    case BlendIfRampChannel::Green:
      return QColor(0, 255, 0);
    case BlendIfRampChannel::Blue:
      return QColor(0, 0, 255);
  }
  return QColor(255, 255, 255);
}

}  // namespace

BlendIfRangeEditorWidget::BlendIfRangeEditorWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(kMinimumWidth, kWidgetHeight);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  last_requested_thresholds_ = thresholds_;
}

void BlendIfRangeEditorWidget::set_thresholds(BlendIfThresholds thresholds) {
  thresholds = normalized_thresholds(thresholds);
  if (thresholds_ == thresholds) {
    return;
  }
  thresholds_ = thresholds;
  last_requested_thresholds_ = thresholds;
  update_cursor(mapFromGlobal(QCursor::pos()));
  update();
}

BlendIfThresholds BlendIfRangeEditorWidget::value() const noexcept {
  return thresholds_;
}

void BlendIfRangeEditorWidget::set_ramp_channel(BlendIfRampChannel channel) {
  if (ramp_channel_ == channel) {
    return;
  }
  ramp_channel_ = channel;
  update();
}

BlendIfRampChannel BlendIfRangeEditorWidget::ramp_channel() const noexcept {
  return ramp_channel_;
}

void BlendIfRangeEditorWidget::set_accessibility_text(const QString& name, const QString& description) {
  setAccessibleName(name);
  setAccessibleDescription(description);
  setToolTip(description);
}

QSize BlendIfRangeEditorWidget::sizeHint() const {
  return QSize(360, kWidgetHeight);
}

void BlendIfRangeEditorWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  const auto ramp = ramp_rect();

  QLinearGradient gradient(ramp.topLeft(), ramp.topRight());
  gradient.setColorAt(0.0, QColor(0, 0, 0));
  gradient.setColorAt(1.0, ramp_end_color(ramp_channel_));
  painter.fillRect(ramp, gradient);
  painter.setPen(QPen(QColor(112, 119, 130), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(ramp.adjusted(0, 0, -1, -1));

  painter.setRenderHint(QPainter::Antialiasing, true);
  constexpr std::array<Handle, 4> handles{
      Handle::BlackLow,
      Handle::BlackHigh,
      Handle::WhiteLow,
      Handle::WhiteHigh,
  };

  // Paint selected halves last so coincident or tightly split handles remain
  // legible and have deterministic visual priority.
  for (const bool selected_pass : {false, true}) {
    for (const auto handle : handles) {
      if (handle_is_selected_for_paint(handle) != selected_pass) {
        continue;
      }
      painter.setBrush(handle_is_black(handle) ? QColor(20, 22, 26) : QColor(246, 247, 249));
      painter.setPen(QPen(selected_pass ? kSelectedColor
                                       : (handle_is_black(handle) ? kLightOutline : kDarkOutline),
                          selected_pass ? 2.0 : 1.25));
      painter.drawPath(handle_path(handle));
    }
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(QPen(hasFocus() ? kSelectedColor : QColor(78, 84, 94), hasFocus() ? 2 : 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect().adjusted(1, 1, -2, -2));
}

void BlendIfRangeEditorWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  const auto position = event->position().toPoint();
  auto handle = hit_handle(position);
  if (handle == Handle::None) {
    QWidget::mousePressEvent(event);
    return;
  }

  setFocus(Qt::MouseFocusReason);
  const bool joined = pair_is_joined(handle);
  const bool split_modifier = (event->modifiers() & Qt::AltModifier) != 0;
  if (joined && split_modifier) {
    // Photoshop splits toward the usable interior: the right half of the
    // black handle and the left half of the white handle.
    handle = handle_is_black(handle) ? Handle::BlackHigh : Handle::WhiteLow;
  }

  selected_handle_ = handle;
  active_handle_ = handle;
  drag_press_x_ = position.x();
  drag_start_value_ = handle_value(handle);
  drag_joined_pair_ = joined && !split_modifier;
  drag_changed_ = false;
  last_requested_thresholds_ = thresholds_;
  update_cursor(position);
  update();
  event->accept();
}

void BlendIfRangeEditorWidget::mouseMoveEvent(QMouseEvent* event) {
  const auto position = event->position().toPoint();
  if ((event->buttons() & Qt::LeftButton) != 0 && active_handle_ != Handle::None) {
    const auto requested = drag_joined_pair_
                               ? thresholds_with_joined_pair_value(active_handle_, dragged_value_from_x(position.x()))
                               : thresholds_with_handle_value(active_handle_, dragged_value_from_x(position.x()));
    if (requested != last_requested_thresholds_) {
      drag_changed_ = true;
      last_requested_thresholds_ = requested;
      request_thresholds(requested, false);
    }
    event->accept();
    return;
  }

  update_cursor(position);
  QWidget::mouseMoveEvent(event);
}

void BlendIfRangeEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && active_handle_ != Handle::None) {
    const auto release_x = event->position().toPoint().x();
    const bool position_changed = drag_changed_ || release_x != drag_press_x_;
    const auto final_requested =
        drag_joined_pair_ ? thresholds_with_joined_pair_value(active_handle_, dragged_value_from_x(release_x))
                          : thresholds_with_handle_value(active_handle_, dragged_value_from_x(release_x));
    last_requested_thresholds_ = final_requested;
    active_handle_ = Handle::None;
    drag_joined_pair_ = false;
    if (position_changed && (drag_changed_ || final_requested != thresholds_)) {
      request_thresholds(last_requested_thresholds_, true);
    }
    drag_changed_ = false;
    update_cursor(event->position().toPoint());
    update();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void BlendIfRangeEditorWidget::leaveEvent(QEvent* event) {
  if (active_handle_ == Handle::None) {
    unsetCursor();
  }
  QWidget::leaveEvent(event);
}

void BlendIfRangeEditorWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  update();
}

void BlendIfRangeEditorWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  update();
}

void BlendIfRangeEditorWidget::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
    select_relative_handle(event->key() == Qt::Key_PageUp ? -1 : 1);
    event->accept();
    return;
  }

  int requested_value = handle_value(selected_handle_);
  const int step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10 : 1;
  switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
      requested_value -= step;
      break;
    case Qt::Key_Right:
    case Qt::Key_Up:
      requested_value += step;
      break;
    case Qt::Key_Home:
      requested_value = minimum_for_handle(selected_handle_);
      break;
    case Qt::Key_End:
      requested_value = maximum_for_handle(selected_handle_);
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }

  const auto requested = thresholds_with_handle_value(selected_handle_, requested_value);
  if (requested != thresholds_) {
    request_thresholds(requested, true);
  }
  event->accept();
}

QRect BlendIfRangeEditorWidget::ramp_rect() const {
  return QRect(kHorizontalGutter, kRampTop, std::max(1, width() - kHorizontalGutter * 2), kRampHeight)
      .adjusted(0, 0, -1, -1);
}

int BlendIfRangeEditorWidget::x_from_value(int value) const {
  const auto ramp = ramp_rect();
  return ramp.left() + static_cast<int>(std::lround(static_cast<double>(std::clamp(value, 0, 255)) / 255.0 *
                                                    static_cast<double>(std::max(1, ramp.width() - 1))));
}

int BlendIfRangeEditorWidget::dragged_value_from_x(int x) const {
  const auto ramp = ramp_rect();
  const auto delta = static_cast<int>(std::lround(static_cast<double>(x - drag_press_x_) * 255.0 /
                                                  static_cast<double>(std::max(1, ramp.width() - 1))));
  return std::clamp(drag_start_value_ + delta, 0, 255);
}

int BlendIfRangeEditorWidget::handle_value(Handle handle) const {
  switch (handle) {
    case Handle::BlackLow:
      return thresholds_.black_low;
    case Handle::BlackHigh:
      return thresholds_.black_high;
    case Handle::WhiteLow:
      return thresholds_.white_low;
    case Handle::WhiteHigh:
      return thresholds_.white_high;
    case Handle::None:
      break;
  }
  return 0;
}

QPainterPath BlendIfRangeEditorWidget::handle_path(Handle handle) const {
  if (handle == Handle::None) {
    return {};
  }
  const auto ramp = ramp_rect();
  const double x = static_cast<double>(x_from_value(handle_value(handle)));
  const double top = static_cast<double>(ramp.bottom() + 2);
  const double bottom = std::min(static_cast<double>(height() - 5), top + kHandleHeight);

  QPainterPath path;
  path.moveTo(x, top);
  if (handle_is_left_half(handle)) {
    path.lineTo(x, bottom);
    path.lineTo(x - kHandleHalfWidth, bottom);
  } else {
    path.lineTo(x + kHandleHalfWidth, bottom);
    path.lineTo(x, bottom);
  }
  path.closeSubpath();
  return path;
}

BlendIfRangeEditorWidget::Handle BlendIfRangeEditorWidget::hit_handle(QPoint position) const {
  constexpr std::array<Handle, 4> priority{
      Handle::BlackHigh,
      Handle::BlackLow,
      Handle::WhiteLow,
      Handle::WhiteHigh,
  };

  const auto point = QPointF(position);
  const auto direct_contains = [&](Handle handle) { return handle_path(handle).contains(point); };
  const auto forgiving_contains = [&](Handle handle) {
    return handle_path(handle).controlPointRect().adjusted(-4.0, -4.0, 4.0, 4.0).contains(point);
  };

  // Prefer the actual half-triangle under the pointer before considering the
  // forgiving hit padding. Otherwise a selected half can steal clicks just
  // across the seam from its joined partner.
  const auto find_hit = [&](const auto& contains) {
    if (selected_handle_ != Handle::None && contains(selected_handle_)) {
      return selected_handle_;
    }

    Handle best = Handle::None;
    int best_distance = 0;
    for (const auto handle : priority) {
      if (!contains(handle)) {
        continue;
      }
      const auto distance = std::abs(position.x() - x_from_value(handle_value(handle)));
      if (best == Handle::None || distance < best_distance) {
        best = handle;
        best_distance = distance;
      }
    }
    return best;
  };

  if (const auto direct = find_hit(direct_contains); direct != Handle::None) {
    return direct;
  }
  return find_hit(forgiving_contains);
}

bool BlendIfRangeEditorWidget::pair_is_joined(Handle handle) const noexcept {
  return handle_is_black(handle) ? thresholds_.black_low == thresholds_.black_high
                                 : thresholds_.white_low == thresholds_.white_high;
}

bool BlendIfRangeEditorWidget::handle_is_black(Handle handle) const noexcept {
  return handle == Handle::BlackLow || handle == Handle::BlackHigh;
}

bool BlendIfRangeEditorWidget::handle_is_left_half(Handle handle) const noexcept {
  return handle == Handle::BlackLow || handle == Handle::WhiteLow;
}

bool BlendIfRangeEditorWidget::handle_is_selected_for_paint(Handle handle) const noexcept {
  return handle == selected_handle_;
}

BlendIfThresholds BlendIfRangeEditorWidget::thresholds_with_handle_value(Handle handle, int value) const {
  auto result = thresholds_;
  value = std::clamp(value, minimum_for_handle(handle), maximum_for_handle(handle));
  switch (handle) {
    case Handle::BlackLow:
      result.black_low = static_cast<std::uint8_t>(value);
      break;
    case Handle::BlackHigh:
      result.black_high = static_cast<std::uint8_t>(value);
      break;
    case Handle::WhiteLow:
      result.white_low = static_cast<std::uint8_t>(value);
      break;
    case Handle::WhiteHigh:
      result.white_high = static_cast<std::uint8_t>(value);
      break;
    case Handle::None:
      break;
  }
  return normalized_thresholds(result);
}

BlendIfThresholds BlendIfRangeEditorWidget::thresholds_with_joined_pair_value(Handle handle, int value) const {
  auto result = thresholds_;
  if (handle_is_black(handle)) {
    value = std::clamp(value, 0, static_cast<int>(thresholds_.white_low));
    result.black_low = static_cast<std::uint8_t>(value);
    result.black_high = static_cast<std::uint8_t>(value);
  } else {
    value = std::clamp(value, static_cast<int>(thresholds_.black_high), 255);
    result.white_low = static_cast<std::uint8_t>(value);
    result.white_high = static_cast<std::uint8_t>(value);
  }
  return result;
}

int BlendIfRangeEditorWidget::minimum_for_handle(Handle handle) const noexcept {
  switch (handle) {
    case Handle::BlackLow:
      return 0;
    case Handle::BlackHigh:
      return thresholds_.black_low;
    case Handle::WhiteLow:
      return thresholds_.black_high;
    case Handle::WhiteHigh:
      return thresholds_.white_low;
    case Handle::None:
      break;
  }
  return 0;
}

int BlendIfRangeEditorWidget::maximum_for_handle(Handle handle) const noexcept {
  switch (handle) {
    case Handle::BlackLow:
      return thresholds_.black_high;
    case Handle::BlackHigh:
      return thresholds_.white_low;
    case Handle::WhiteLow:
      return thresholds_.white_high;
    case Handle::WhiteHigh:
      return 255;
    case Handle::None:
      break;
  }
  return 255;
}

void BlendIfRangeEditorWidget::request_thresholds(BlendIfThresholds thresholds, bool immediate) {
  thresholds = normalized_thresholds(thresholds);
  if (changed) {
    changed(thresholds, immediate);
  }
}

void BlendIfRangeEditorWidget::select_relative_handle(int delta) {
  constexpr std::array<Handle, 4> handles{
      Handle::BlackLow,
      Handle::BlackHigh,
      Handle::WhiteLow,
      Handle::WhiteHigh,
  };
  const auto found = std::find(handles.begin(), handles.end(), selected_handle_);
  const int current = found == handles.end() ? 0 : static_cast<int>(std::distance(handles.begin(), found));
  const int count = static_cast<int>(handles.size());
  selected_handle_ = handles[static_cast<std::size_t>((current + delta + count) % count)];
  update();
}

void BlendIfRangeEditorWidget::update_cursor(QPoint position) {
  if (!rect().contains(position)) {
    unsetCursor();
    return;
  }
  if (active_handle_ != Handle::None || hit_handle(position) != Handle::None) {
    setCursor(Qt::SizeHorCursor);
    return;
  }
  unsetCursor();
}

}  // namespace patchy::ui
