#pragma once

#include "core/layer.hpp"

#include <QPainterPath>
#include <QString>
#include <QWidget>

#include <functional>

namespace patchy::ui {

using BlendIfRampChannel = BlendIfChannel;

// One Photoshop-style Blend If range row. The four scalar thresholds are drawn
// as two split-capable compound handles (black and white). Like the gradient
// stop editor, this widget is callback-driven: it reports requested values and
// the host pushes accepted state back through set_thresholds().
class BlendIfRangeEditorWidget final : public QWidget {
public:
  explicit BlendIfRangeEditorWidget(QWidget* parent = nullptr);

  void set_thresholds(BlendIfThresholds thresholds);
  [[nodiscard]] BlendIfThresholds value() const noexcept;

  void set_ramp_channel(BlendIfRampChannel channel);
  [[nodiscard]] BlendIfRampChannel ramp_channel() const noexcept;

  // The caller supplies localized strings so this reusable widget does not
  // introduce its own translation context.
  void set_accessibility_text(const QString& name, const QString& description);

  // immediate=false while dragging, true on release and for keyboard edits.
  std::function<void(BlendIfThresholds, bool immediate)> changed;

  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  enum class Handle {
    None = -1,
    BlackLow = 0,
    BlackHigh,
    WhiteLow,
    WhiteHigh,
  };

  [[nodiscard]] QRect ramp_rect() const;
  [[nodiscard]] int x_from_value(int value) const;
  [[nodiscard]] int dragged_value_from_x(int x) const;
  [[nodiscard]] int handle_value(Handle handle) const;
  [[nodiscard]] QPainterPath handle_path(Handle handle) const;
  [[nodiscard]] Handle hit_handle(QPoint position) const;
  [[nodiscard]] bool pair_is_joined(Handle handle) const noexcept;
  [[nodiscard]] bool handle_is_black(Handle handle) const noexcept;
  [[nodiscard]] bool handle_is_left_half(Handle handle) const noexcept;
  [[nodiscard]] bool handle_is_selected_for_paint(Handle handle) const noexcept;
  [[nodiscard]] BlendIfThresholds thresholds_with_handle_value(Handle handle, int value) const;
  [[nodiscard]] BlendIfThresholds thresholds_with_joined_pair_value(Handle handle, int value) const;
  [[nodiscard]] int minimum_for_handle(Handle handle) const noexcept;
  [[nodiscard]] int maximum_for_handle(Handle handle) const noexcept;
  void request_thresholds(BlendIfThresholds thresholds, bool immediate);
  void select_relative_handle(int delta);
  void update_cursor(QPoint position);

  BlendIfThresholds thresholds_{};
  BlendIfThresholds last_requested_thresholds_{};
  BlendIfRampChannel ramp_channel_{BlendIfRampChannel::Gray};
  Handle selected_handle_{Handle::BlackHigh};
  Handle active_handle_{Handle::None};
  int drag_press_x_{0};
  int drag_start_value_{0};
  bool drag_joined_pair_{false};
  bool drag_changed_{false};
};

}  // namespace patchy::ui
