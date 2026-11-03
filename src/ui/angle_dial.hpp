#pragma once

#include <QWidget>

#include <functional>

class QAbstractSpinBox;
class QDoubleSpinBox;
class QSpinBox;

namespace patchy::ui {

// A small round angle control that sits beside an angle number field (GitHub
// issue 618): a circle with a line from the center. Angles follow the fields'
// convention: 0 points right and positive turns counterclockwise. A click or
// drag points the line at the pointer. The AngleAltitude mode (Bevel & Emboss
// shading) also sets an altitude from the pointer's distance to the center:
// 90 at the center, 0 at the rim, shown as a marker at that point.
//
// The number fields own the values: the dial reports requests through the
// changed callback and the host pushes accepted values back with set_angle /
// set_altitude (bind_angle_dial does both). Keyboard: arrows (Shift for 15),
// Page Up / Page Down; the wheel turns it only while it has keyboard focus, so
// scrolling a page never changes an angle by accident.
class AngleDial final : public QWidget {
  Q_OBJECT

public:
  enum class Mode { Angle, AngleAltitude };

  explicit AngleDial(Mode mode = Mode::Angle, QWidget* parent = nullptr);

  [[nodiscard]] Mode mode() const noexcept { return mode_; }

  // The angle range of the field the dial drives. A range spanning a full
  // turn wraps (keyboard past 180 lands on -179), anything shorter clamps.
  void set_angle_range(double minimum, double maximum);
  void set_angle(double degrees);
  [[nodiscard]] double angle() const noexcept { return angle_; }
  void set_altitude(double degrees);
  [[nodiscard]] double altitude() const noexcept { return altitude_; }

  // Called with the requested whole-degree angle and altitude (the altitude
  // is the current one in Angle mode).
  void set_changed_callback(std::function<void(int angle, int altitude)> callback);

  // The angle and altitude a pointer at `offset` from the center would pick
  // on a dial of `radius` (y grows downward, as in widget coordinates).
  [[nodiscard]] static int angle_for_offset(QPointF offset);
  [[nodiscard]] static int altitude_for_distance(double distance, double radius);
  // The widget position the current values draw their end point at.
  [[nodiscard]] QPointF marker_position() const;
  [[nodiscard]] QPointF center() const;
  [[nodiscard]] double radius() const;

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

private:
  void request_from_pointer(QPointF position);
  void request(double angle, double altitude);
  [[nodiscard]] double fit_angle(double degrees, bool wrap) const;
  void publish_state();

  Mode mode_;
  double minimum_{-180.0};
  double maximum_{180.0};
  double angle_{0.0};
  double altitude_{30.0};
  bool dragging_{false};
  std::function<void(int, int)> changed_;
};

// Keeps `dial` and the number fields in step both ways: the dial sets the
// fields, and typed or stepped values move the dial. `altitude` is for the
// AngleAltitude mode.
void bind_angle_dial(AngleDial& dial, QSpinBox& angle, QSpinBox* altitude = nullptr);
void bind_angle_dial(AngleDial& dial, QDoubleSpinBox& angle);

// Moves the dial bound to `field` to its value: for hosts that load fields
// under a QSignalBlocker, which hides the change from the binding.
void sync_angle_dial(QAbstractSpinBox& field);

// `field` (a QSpinBox or QDoubleSpinBox angle field) followed by a bound
// AngleDial in one row widget, for forms and grids without - / + steppers.
[[nodiscard]] QWidget* wrap_angle_field_with_dial(QAbstractSpinBox* field, QWidget* parent);

// The dial's object name for an angle field named "...Spin": "...Dial".
[[nodiscard]] QString angle_dial_object_name(QString spin_object_name);

}  // namespace patchy::ui
