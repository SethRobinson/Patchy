#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <functional>
#include <optional>

class QMouseEvent;
class QPainter;
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

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class OverlayHandle {
    None,
    Center,
    Radius,
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
  void draw_center_radius_overlay(QPainter& painter) const;
  void request_center_radius_overlay(NormalizedCenterRadiusOverlay overlay,
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
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
  OverlayHandle active_overlay_handle_{OverlayHandle::None};
  bool overlay_drag_changed_{false};
};

}  // namespace patchy::ui
