#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

#include <functional>
#include <optional>

class QMouseEvent;
class QResizeEvent;
class QWheelEvent;

namespace patchy::ui {

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

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  [[nodiscard]] double fit_zoom() const;
  [[nodiscard]] QRectF displayed_rect() const;
  void clamp_pan();
  void publish_state();

  QImage image_;
  QPointF pan_offset_;
  QPointF pan_press_position_;
  QPointF pan_press_offset_;
  std::function<void()> zoom_changed_;
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
};

}  // namespace patchy::ui
