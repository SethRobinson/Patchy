#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <functional>
#include <optional>

class QMouseEvent;
class QPainter;
class QKeyEvent;
class QResizeEvent;
class QWheelEvent;

namespace patchy::ui {

// Coordinates are relative to the displayed image. A radius of 1.0 is half
// the image's shorter edge, matching the percent-radius convention used by
// Patchy's radial filters. A missing radius leaves only the center handle.
struct NormalizedCenterRadiusOverlay {
  QPointF center{0.5, 0.5};
  std::optional<double> radius{};

  friend bool operator==(const NormalizedCenterRadiusOverlay&,
                         const NormalizedCenterRadiusOverlay&) = default;
};

// Coordinates are relative to the displayed image. Both widths are fractions
// of the image's shorter edge. The transition width is measured outwards from
// each focus boundary, matching the catalog's two independent percentages.
struct NormalizedTiltShiftOverlay {
  QPointF center{0.5, 0.5};
  double angle_degrees{0.0};
  double focus_half_width{0.1};
  double transition_width{0.2};

  friend bool operator==(const NormalizedTiltShiftOverlay&,
                         const NormalizedTiltShiftOverlay&) = default;
};

// A bounded-image viewer used by dialogs that need Photoshop-style fit, 100%,
// wheel zoom, and drag-to-pan controls. Image processing is deliberately kept
// outside paintEvent; this widget only displays the last completed QImage.
class ZoomableImagePreview final : public QWidget {
public:
  explicit ZoomableImagePreview(QWidget* parent = nullptr);

  void set_image(QImage image);
  [[nodiscard]] const QImage& image() const noexcept;
  [[nodiscard]] double zoom() const;
  [[nodiscard]] bool fit_mode() const noexcept;

  void zoom_to_fit();
  void zoom_to(double factor,
               std::optional<QPointF> anchor = std::nullopt);
  void zoom_step(int direction,
                 std::optional<QPointF> anchor = std::nullopt);
  void set_zoom_changed_callback(std::function<void()> callback);

  // A missing outer optional hides the overlay. Like Patchy's other reusable
  // editors, interaction is callback-driven: the host pushes accepted values
  // back through set_center_radius_overlay().
  void set_center_radius_overlay(
      std::optional<NormalizedCenterRadiusOverlay> overlay);
  [[nodiscard]] const std::optional<NormalizedCenterRadiusOverlay>&
  center_radius_overlay() const noexcept;
  void set_center_radius_changed_callback(
      std::function<void(NormalizedCenterRadiusOverlay overlay,
                         bool gesture_finished)> callback);
  void set_tilt_shift_overlay(
      std::optional<NormalizedTiltShiftOverlay> overlay);
  [[nodiscard]] const std::optional<NormalizedTiltShiftOverlay>&
  tilt_shift_overlay() const noexcept;
  void set_tilt_shift_changed_callback(
      std::function<void(NormalizedTiltShiftOverlay overlay,
                         bool gesture_finished)> callback);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class OverlayHandle {
    None,
    Center,
    Radius,
    TiltCenter,
    TiltAngle,
    TiltFocus,
    TiltTransition,
  };

  [[nodiscard]] double fit_zoom() const;
  [[nodiscard]] QRectF displayed_rect() const;
  [[nodiscard]] QPointF overlay_center_point(
      const NormalizedCenterRadiusOverlay& overlay) const;
  [[nodiscard]] double overlay_radius_pixels(
      const NormalizedCenterRadiusOverlay& overlay) const;
  [[nodiscard]] QPointF overlay_radius_handle_point(
      const NormalizedCenterRadiusOverlay& overlay) const;
  [[nodiscard]] OverlayHandle overlay_handle_at(QPointF position) const;
  [[nodiscard]] NormalizedCenterRadiusOverlay requested_overlay_at(
      QPointF position) const;
  [[nodiscard]] QPointF tilt_center_point(
      const NormalizedTiltShiftOverlay& overlay) const;
  [[nodiscard]] QPointF tilt_angle_handle_point(
      const NormalizedTiltShiftOverlay& overlay) const;
  [[nodiscard]] QPointF tilt_focus_handle_point(
      const NormalizedTiltShiftOverlay& overlay, double side) const;
  [[nodiscard]] QPointF tilt_transition_handle_point(
      const NormalizedTiltShiftOverlay& overlay, double side) const;
  [[nodiscard]] OverlayHandle tilt_overlay_handle_at(QPointF position) const;
  [[nodiscard]] NormalizedTiltShiftOverlay requested_tilt_overlay_at(
      QPointF position) const;
  void draw_center_radius_overlay(QPainter& painter) const;
  void draw_tilt_shift_overlay(QPainter& painter) const;
  void request_center_radius_overlay(NormalizedCenterRadiusOverlay overlay,
                                     bool gesture_finished);
  void request_tilt_shift_overlay(NormalizedTiltShiftOverlay overlay,
                                  bool gesture_finished);
  void update_interaction_cursor(QPointF position);
  void clamp_pan();
  void publish_state();
  void publish_overlay_state();

  QImage image_;
  QPointF pan_offset_;
  QPointF pan_press_position_;
  QPointF pan_press_offset_;
  std::function<void()> zoom_changed_;
  std::optional<NormalizedCenterRadiusOverlay> center_radius_overlay_;
  NormalizedCenterRadiusOverlay last_requested_overlay_{};
  std::function<void(NormalizedCenterRadiusOverlay, bool)>
      center_radius_changed_;
  std::optional<NormalizedCenterRadiusOverlay>
      center_radius_gesture_start_;
  std::optional<NormalizedTiltShiftOverlay> tilt_shift_overlay_;
  NormalizedTiltShiftOverlay last_requested_tilt_shift_overlay_{};
  std::function<void(NormalizedTiltShiftOverlay, bool)>
      tilt_shift_changed_;
  std::optional<NormalizedTiltShiftOverlay> tilt_shift_gesture_start_;
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
  OverlayHandle active_overlay_handle_{OverlayHandle::None};
  bool overlay_drag_changed_{false};
};

}  // namespace patchy::ui
