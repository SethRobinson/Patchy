#pragma once

#include "core/brush_dynamics.hpp"

#include <QImage>
#include <QWidget>

#include <memory>

class QTimer;

namespace patchy {
struct BrushTip;
}

namespace patchy::ui {

// Paints an S-curve stroke with the real stamping engine into a scratch document (never the
// user's), so the preview shows what the canvas will produce: spacing, antialiasing and the
// tip's dynamics under a simulated pen-pressure ramp that rises and falls along the stroke,
// with a fixed seed so jitter stays put between redraws. Setters coalesce into one rebuild a
// few milliseconds later; the image is cached until a setting or the size changes.
// Used by the Brush Tips manager and the Dynamics popup's preview strip.
class BrushStrokePreview : public QWidget {
  Q_OBJECT

public:
  explicit BrushStrokePreview(QWidget* parent = nullptr);

  void set_tip(std::shared_ptr<const patchy::BrushTip> tip);
  void set_spacing(double spacing);
  void set_dynamics(const patchy::BrushDynamics& dynamics, double base_angle_degrees,
                    double base_roundness);
  [[nodiscard]] bool has_tip() const noexcept { return tip_ != nullptr; }

  // Runs a pending rebuild now (tests; the throttle otherwise does it).
  void rebuild_now();
  [[nodiscard]] const QImage& stroke_image() const noexcept { return stroke_; }
  [[nodiscard]] int rebuild_count() const noexcept { return rebuild_count_; }

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

private:
  void schedule_rebuild();
  void rebuild();

  std::shared_ptr<const patchy::BrushTip> tip_;
  double spacing_{0.25};
  patchy::BrushDynamics dynamics_{};
  double base_angle_degrees_{0.0};
  double base_roundness_{100.0};
  QImage stroke_;
  QTimer* rebuild_timer_{nullptr};
  int rebuild_count_{0};
};

// The hard disc (Round) or solid square (Square) the preview stamps for the procedural
// brushes, which have no bitmap tip of their own. Built once.
[[nodiscard]] std::shared_ptr<const patchy::BrushTip> procedural_brush_preview_tip(bool square);

}  // namespace patchy::ui
