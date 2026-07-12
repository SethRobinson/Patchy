#pragma once

#include <QPixmap>
#include <QWidget>

#include <functional>

namespace patchy::ui {

// Compact angle control used beside the catalog-generated numeric editor. The
// host owns the authoritative value: interactions report requested values and
// the host pushes accepted values back through set_angle(). The hand wraps
// visually, while the value retains multiple turns for filters such as Twirl.
class FilterAngleDial final : public QWidget {
public:
  explicit FilterAngleDial(QWidget* parent = nullptr);

  void set_range(int minimum, int maximum);
  [[nodiscard]] int minimum() const noexcept;
  [[nodiscard]] int maximum() const noexcept;

  void set_angle(int degrees);
  [[nodiscard]] int angle() const noexcept;
  void set_default_angle(int degrees);

  // gesture_finished is false during a pointer drag and true on release or
  // for a keyboard edit.
  void set_angle_changed_callback(
      std::function<void(int degrees, bool gesture_finished)> callback);

  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  [[nodiscard]] QRectF dial_rect() const;
  [[nodiscard]] QPointF dial_center() const;
  [[nodiscard]] double pointer_angle(QPointF position) const;
  [[nodiscard]] int requested_angle_from_pointer(QPointF position);
  void request_angle(int degrees, bool gesture_finished);
  void publish_state();
  void invalidate_cache();
  void rebuild_cache();

  std::function<void(int, bool)> angle_changed_;
  QPixmap face_cache_;
  qreal cache_device_pixel_ratio_{0.0};
  int minimum_{-180};
  int maximum_{180};
  int angle_{0};
  int default_angle_{0};
  int last_requested_angle_{0};
  double drag_angle_{0.0};
  double last_pointer_angle_{0.0};
  bool dragging_{false};
  bool drag_changed_{false};
};

struct FilterWaveformValues {
  int amplitude{12};
  int wavelength{48};
  int phase{0};

  friend bool operator==(const FilterWaveformValues&,
                         const FilterWaveformValues&) = default;
};

// Visual companion for Wave's numeric controls. Vertical dragging adjusts
// amplitude, horizontal dragging adjusts phase, and the wheel adjusts
// wavelength. The cached graph is a bounded visualization only; no image
// filtering is reachable from paintEvent.
class FilterWaveformControl final : public QWidget {
public:
  explicit FilterWaveformControl(QWidget* parent = nullptr);

  void set_ranges(FilterWaveformValues minimum, FilterWaveformValues maximum);
  void set_values(FilterWaveformValues values);
  [[nodiscard]] FilterWaveformValues values() const noexcept;
  void set_default_values(FilterWaveformValues values);

  // gesture_finished is false during a pointer drag and true on release,
  // wheel, or keyboard edits.
  void set_values_changed_callback(
      std::function<void(FilterWaveformValues values, bool gesture_finished)>
          callback);

  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  [[nodiscard]] FilterWaveformValues normalized_values(
      FilterWaveformValues values) const;
  [[nodiscard]] FilterWaveformValues values_from_drag(QPointF position) const;
  void request_values(FilterWaveformValues values, bool gesture_finished);
  void publish_state();
  void invalidate_cache();
  void rebuild_cache();

  std::function<void(FilterWaveformValues, bool)> values_changed_;
  QPixmap graph_cache_;
  qreal cache_device_pixel_ratio_{0.0};
  FilterWaveformValues minimum_{0, 4, 0};
  FilterWaveformValues maximum_{64, 256, 360};
  FilterWaveformValues values_{};
  FilterWaveformValues default_values_{};
  FilterWaveformValues drag_start_values_{};
  FilterWaveformValues last_requested_values_{};
  QPointF drag_start_position_{};
  bool dragging_{false};
  bool drag_changed_{false};
};

}  // namespace patchy::ui
