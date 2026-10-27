#pragma once

// The saturation/value field and the vertical hue strip, shared by the color picker
// window (color_panel.cpp, its Square tab) and the docked Color panel
// (color_dock_panel.cpp). Both draw and edit an HsvFieldSource, which owns the HSV
// state and decides what a pick changes.

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

class QMouseEvent;
class QPainter;
class QPaintEvent;
class QPointF;

namespace patchy::ui {

// The color a field shows and edits. Hue is 0..359; saturation and value 0..255.
class HsvFieldSource {
public:
  virtual ~HsvFieldSource() = default;

  [[nodiscard]] virtual int hue() const = 0;
  [[nodiscard]] virtual int saturation() const = 0;
  [[nodiscard]] virtual int value() const = 0;

  // A left press starts a pick with the press event's own modifiers (never
  // QApplication::keyboardModifiers()); the moves that follow belong to it until
  // end_field_pick, so a modifier read here holds for the whole drag.
  virtual void begin_field_pick(Qt::KeyboardModifiers modifiers) { Q_UNUSED(modifiers); }
  virtual void end_field_pick() {}
  virtual void set_saturation_value_from_point(QPoint point, QSize size) = 0;
  virtual void set_hue_from_point(QPoint point, QSize size) = 0;
};

// position / maximum_position scaled onto 0..maximum_value, rounded and clamped.
[[nodiscard]] int rounded_scaled_channel(int position, int maximum_position, int maximum_value);
// The saturation (x) / value (y) gradient for one hue.
[[nodiscard]] QImage render_saturation_value_image(int hue, QSize size);
// A two-tone crosshair (dark halo, light core) that stays visible over any color.
void draw_crosshair_marker(QPainter& painter, QPointF center);

// Saturation (x) by value (y) for the source's hue, with a crosshair at its color.
class ColorPlaneWidget final : public QWidget {
public:
  static constexpr int kDefaultSize = 188;

  ColorPlaneWidget(HsvFieldSource& source, QWidget* parent);

  [[nodiscard]] QSize sizeHint() const override { return {kDefaultSize, kDefaultSize}; }
  // How many times the gradient was rendered (the drag-cost tests read it).
  [[nodiscard]] int plane_render_count() const noexcept { return plane_render_count_; }

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  HsvFieldSource& source_;
  // The gradient is rendered only when the hue or the size changes, not per repaint.
  QImage plane_;
  int plane_hue_{-1};
  int plane_render_count_{0};
};

// Hue top (0) to bottom (359), with a line at the source's hue.
class HueSliderWidget final : public QWidget {
public:
  static constexpr int kDefaultWidth = 16;

  HueSliderWidget(HsvFieldSource& source, QWidget* parent);

  [[nodiscard]] QSize sizeHint() const override { return {kDefaultWidth, ColorPlaneWidget::kDefaultSize}; }

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  HsvFieldSource& source_;
};

}  // namespace patchy::ui
