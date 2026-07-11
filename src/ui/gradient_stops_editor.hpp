#pragma once

#include "core/layer.hpp"
#include "core/pixel_tools.hpp"

#include <QColor>
#include <QPainterPath>
#include <QWidget>

#include <functional>
#include <vector>

namespace patchy::ui {

// Interactive gradient bar with draggable stop tags. Default mode shows one
// track of RGBA color-stop tags below the bar (the gradient tool's Edit
// Gradient Stops dialog). Enabling the opacity track adds a Photoshop-style
// second track of opacity-stop tags above the bar; the bar then composites the
// color ramp with the opacity ramp over the checkerboard, and color-stop
// alphas are ignored (transparency belongs to the opacity track).
//
// The widget never mutates its own stop vectors: every interaction is reported
// through a callback and the host pushes the new state back in via the
// setters. At most one stop is selected across both tracks.
class GradientStopsEditorWidget final : public QWidget {
  Q_OBJECT

public:
  explicit GradientStopsEditorWidget(QWidget* parent = nullptr);

  // Enable before the widget is shown; the fixed height grows from 66 to 96.
  void set_opacity_track_enabled(bool enabled);

  void set_stops(std::vector<GradientStop> stops);
  // Midpoints are aligned with the color-stop rows. A value belongs to the
  // segment from the previous stop into that destination stop; the first stop's
  // value is preserved but has no visible handle.
  void set_color_midpoints(std::vector<float> midpoints);
  void set_current_row(int row);
  void set_opacity_stops(std::vector<GradientAlphaStop> stops);
  void set_current_opacity_row(int row);

  [[nodiscard]] QSize sizeHint() const override;

  // Color-track callbacks.
  std::function<void(int)> stop_selected;
  std::function<void(int)> choose_stop_color_requested;
  std::function<void(int, int)> stop_location_changed;         // row, percent 0..100
  std::function<void(int, int)> color_midpoint_changed;        // destination row, percent 5..95
  std::function<void(int, QColor)> stop_color_picked;          // bar click samples under cursor
  std::function<int(GradientStop)> stop_add_requested;         // returns new row, or -1 to refuse
  std::function<void(int)> stop_delete_requested;

  // Opacity-track callbacks (fire only while the opacity track is enabled).
  std::function<void(int)> opacity_stop_selected;
  std::function<void(int, int)> opacity_stop_location_changed;        // row, percent 0..100
  std::function<void(int, int)> opacity_midpoint_changed;             // destination row, percent 5..95
  std::function<int(GradientAlphaStop)> opacity_stop_add_requested;   // returns new row, or -1
  std::function<void(int)> opacity_stop_delete_requested;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class Track { Color, Opacity };

  [[nodiscard]] QRect bar_rect() const;
  [[nodiscard]] QRect handle_area_rect() const;          // color tags, below the bar
  [[nodiscard]] QRect opacity_handle_area_rect() const;  // opacity tags, above the bar
  [[nodiscard]] bool is_delete_zone(Track track, QPoint pos) const;
  [[nodiscard]] double position_from_x(int x) const;
  [[nodiscard]] int x_from_position(float location) const;
  [[nodiscard]] QPainterPath handle_path(int row) const;
  [[nodiscard]] QPainterPath opacity_handle_path(int row) const;
  [[nodiscard]] QPainterPath color_midpoint_path(int row) const;
  [[nodiscard]] QPainterPath opacity_midpoint_path(int row) const;
  [[nodiscard]] int hit_stop(QPoint pos) const;
  [[nodiscard]] int hit_opacity_stop(QPoint pos) const;
  [[nodiscard]] int hit_color_midpoint(QPoint pos) const;
  [[nodiscard]] int hit_opacity_midpoint(QPoint pos) const;
  [[nodiscard]] int previous_color_stop_row(int row) const;
  [[nodiscard]] int previous_opacity_stop_row(int row) const;
  [[nodiscard]] float opacity_at(double position) const;
  [[nodiscard]] RgbColor style_color_at(double position) const;
  void update_cursor(QPoint pos);

  bool opacity_track_enabled_{false};
  std::vector<GradientStop> stops_;
  std::vector<float> color_midpoints_;
  std::vector<GradientAlphaStop> opacity_stops_;
  int current_row_{-1};
  int current_opacity_row_{-1};
  Track active_track_{Track::Color};
  int active_row_{-1};
  bool active_midpoint_{false};
  QPoint press_position_;
  bool dragging_{false};
  bool open_color_on_release_{false};
  bool pending_delete_{false};
};

}  // namespace patchy::ui
