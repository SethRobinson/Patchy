#pragma once

#include <QCursor>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QMouseEvent;
class QPainter;
class QKeyEvent;
class QFocusEvent;
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
// Dialogs that paint their own pixels (the Image Trace result, the palette
// conversion windows) derive from it, give it a logical size instead of an
// image and draw inside the protected paint hooks, so there is one zoom, pan
// and mouse implementation. Zoom is clamped to [min(fit, 1/16), 16]; fit mode
// shows the whole image however large it is.
class ZoomableImagePreview : public QWidget {
public:
  explicit ZoomableImagePreview(QWidget* parent = nullptr);

  void set_image(QImage image, QSize logical_size = {});
  [[nodiscard]] const QImage& image() const noexcept;
  [[nodiscard]] double zoom() const;
  [[nodiscard]] bool fit_mode() const noexcept;

  void zoom_to_fit();
  void zoom_to(double factor,
               std::optional<QPointF> anchor = std::nullopt);
  void zoom_step(int direction,
                 std::optional<QPointF> anchor = std::nullopt);
  void set_zoom_changed_callback(std::function<void()> callback);
  // Scrubby Zoom, the Zoom tool's gesture (docs/view-navigation.md): a left
  // drag zooms about the press point, right = in, vertical travel ignored; a
  // click without travel steps in, Alt-click out. Space held or the middle
  // button still pans. Off by default (a left drag pans).
  void set_scrubby_zoom(bool enabled);
  [[nodiscard]] bool scrubby_zoom() const noexcept;

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
  // Subclass surface. paintEvent calls paint_backdrop, then, once a logical
  // size is set, paint_content with the image's rect at the current zoom and
  // pan (it may extend past the widget), then the overlays on top.
  virtual void paint_backdrop(QPainter& painter);
  // Default: the transparency checkerboard, the image (a smooth-scaled cache
  // below 100%) and the frame.
  virtual void paint_content(QPainter& painter, const QRectF& image_rect);
  // The cursor while idle or panning when no overlay handle is under the
  // pointer; a subclass can show a busy cursor instead.
  [[nodiscard]] virtual QCursor interaction_cursor(bool panning) const;
  // The displayed area for a subclass that paints its own pixels (no QImage).
  // Like set_image, a size change returns to fit.
  void set_logical_size(QSize logical_size);
  [[nodiscard]] QSize logical_size() const noexcept;
  [[nodiscard]] bool panning() const noexcept;
  // Where the logical image sits in widget coordinates at the current zoom and
  // pan; empty without a logical size.
  [[nodiscard]] QRectF displayed_rect() const;
  // Refreshes the zoom/fit/pan properties and tells the zoom-changed callback;
  // a subclass calls it when its own content changes what the label shows.
  void publish_state();

  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
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
  [[nodiscard]] double minimum_zoom() const;
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
  void refresh_interaction_tooltip();
  void clamp_pan();
  void publish_overlay_state();
  void request_display_cache();
  void start_display_cache();

  struct ScaleState;
  std::shared_ptr<ScaleState> scale_state_;
  QImage scaled_image_;
  QSize logical_size_;
  QSize scaled_size_;
  qint64 scaled_source_key_{0};

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
  Qt::MouseButton pan_button_{Qt::NoButton};
  bool scrubby_zoom_{false};
  bool space_held_{false};
  bool scrub_armed_{false};
  bool scrub_started_{false};
  QPointF scrub_anchor_;
  QPointF scrub_press_position_;
  QPointF scrub_last_position_;
  OverlayHandle active_overlay_handle_{OverlayHandle::None};
  bool overlay_drag_changed_{false};
};

}  // namespace patchy::ui
