#include "ui/brush_stroke_preview.hpp"

#include "core/brush_tip.hpp"
#include "core/document.hpp"
#include "core/pixel_tools.hpp"
#include "ui/theme_palette.hpp"

#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Edits arrive per spin step and slider move; one rebuild per burst keeps a drag smooth.
constexpr int kRebuildDelayMs = 30;

// The simulated pen: light at both ends, full pressure mid-stroke, so a Pen Pressure control
// shows its taper (a mouse would paint everything at full value).
[[nodiscard]] double simulated_pressure(double t) {
  return 0.15 + 0.85 * std::sin(std::clamp(t, 0.0, 1.0) * kPi);
}

}  // namespace

BrushStrokePreview::BrushStrokePreview(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("brushTipStrokePreview"));
  setMinimumSize(320, 130);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  rebuild_timer_ = new QTimer(this);
  rebuild_timer_->setSingleShot(true);
  rebuild_timer_->setInterval(kRebuildDelayMs);
  connect(rebuild_timer_, &QTimer::timeout, this, &BrushStrokePreview::rebuild);
}

void BrushStrokePreview::set_tip(std::shared_ptr<const patchy::BrushTip> tip) {
  tip_ = std::move(tip);
  schedule_rebuild();
}

void BrushStrokePreview::set_spacing(double spacing) {
  spacing_ = std::clamp(spacing, 0.01, 10.0);
  schedule_rebuild();
}

void BrushStrokePreview::set_dynamics(const patchy::BrushDynamics& dynamics,
                                      double base_angle_degrees, double base_roundness) {
  dynamics_ = dynamics;
  base_angle_degrees_ = base_angle_degrees;
  base_roundness_ = std::clamp(base_roundness, 1.0, 100.0);
  schedule_rebuild();
}

void BrushStrokePreview::rebuild_now() {
  if (rebuild_timer_->isActive()) {
    rebuild_timer_->stop();
  }
  rebuild();
}

void BrushStrokePreview::schedule_rebuild() {
  rebuild_timer_->start();
}

void BrushStrokePreview::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  // White paper and black paint depict a painted document, like the transparency
  // checkerboard, so they stay the same in both schemes; the outline is chrome.
  painter.fillRect(rect(), Qt::white);
  if (!stroke_.isNull()) {
    painter.drawImage(rect().topLeft(), stroke_);
  }
  painter.setPen(theme().swatch_border);
  painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void BrushStrokePreview::resizeEvent(QResizeEvent* /*event*/) {
  schedule_rebuild();
}

void BrushStrokePreview::showEvent(QShowEvent* /*event*/) {
  // Paint the first frame with the stroke rather than blank paper.
  if (rebuild_timer_->isActive()) {
    rebuild_now();
  }
}

void BrushStrokePreview::rebuild() {
  ++rebuild_count_;
  stroke_ = {};
  if (tip_ == nullptr || tip_->empty() || width() <= 4 || height() <= 4) {
    update();
    return;
  }

  const auto document_width = std::max(64, width());
  const auto document_height = std::max(32, height());
  patchy::Document document(document_width, document_height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(document_width, document_height, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  const auto layer_id = document.add_pixel_layer("Preview", std::move(pixels)).id();

  const auto mips = patchy::build_brush_tip_mips(*tip_);
  const auto brush_size =
      std::clamp(std::max(tip_->width, tip_->height), 8, std::max(8, document_height / 2));
  const auto scaled = patchy::make_scaled_brush_tip(mips, brush_size);
  if (scaled.empty()) {
    update();
    return;
  }

  patchy::EditOptions options;
  options.primary = patchy::EditColor{0, 0, 0, 255};
  options.secondary = patchy::EditColor{255, 255, 255, 255};
  options.brush_size = brush_size;
  options.brush_tip = &scaled;
  options.brush_tip_spacing = spacing_;
  options.brush_angle_degrees = base_angle_degrees_;
  options.brush_roundness = static_cast<int>(std::lround(base_roundness_));
  options.brush_dynamics = dynamics_;
  options.brush_dynamics.seed = 1234;  // fixed seed: a stable preview instead of reshuffling per repaint

  // Sample a gentle S-curve across the preview, chopped into short segments like real input.
  patchy::BrushTipStrokeState state;
  const auto margin = static_cast<double>(brush_size) / 2.0 + 4.0;
  const auto usable_width = static_cast<double>(document_width) - 2.0 * margin;
  const auto center_y = static_cast<double>(document_height) / 2.0;
  const auto wave_height = std::max(2.0, static_cast<double>(document_height) / 2.0 - margin);
  constexpr int kSegments = 48;
  double previous_x = margin;
  double previous_y = center_y;
  for (int step = 1; step <= kSegments; ++step) {
    const auto t = static_cast<double>(step) / kSegments;
    const auto x = margin + usable_width * t;
    const auto y = center_y - std::sin(t * 2.0 * kPi) * wave_height;
    options.brush_dynamics.pen_pressure = simulated_pressure(t);
    (void)patchy::paint_brush_segment(document, layer_id, previous_x, previous_y, x, y, options,
                                      false, state);
    previous_x = x;
    previous_y = y;
  }
  (void)patchy::finish_brush_tip_stroke(document, layer_id, options, false, state);

  const auto* layer = document.find_layer(layer_id);
  if (layer == nullptr) {
    update();
    return;
  }
  const auto& painted = layer->pixels();
  QImage image(painted.width(), painted.height(), QImage::Format_RGBA8888);
  for (std::int32_t y = 0; y < painted.height(); ++y) {
    const auto row = painted.row(y);
    std::copy_n(row.data(), static_cast<std::size_t>(painted.width()) * 4U, image.scanLine(y));
  }
  stroke_ = std::move(image);
  update();
}

std::shared_ptr<const patchy::BrushTip> procedural_brush_preview_tip(bool square) {
  const auto make = [](bool square_tip) {
    constexpr std::int32_t kSize = 64;
    constexpr float kRadius = 31.0F;
    auto tip = std::make_shared<patchy::BrushTip>();
    tip->width = kSize;
    tip->height = kSize;
    tip->default_spacing = 0.25;
    tip->mask.resize(static_cast<std::size_t>(kSize) * kSize);
    for (std::int32_t y = 0; y < kSize; ++y) {
      for (std::int32_t x = 0; x < kSize; ++x) {
        const auto dx = static_cast<float>(x) + 0.5F - 32.0F;
        const auto dy = static_cast<float>(y) + 0.5F - 32.0F;
        const auto coverage =
            square_tip ? 1.0F : std::clamp(kRadius + 0.5F - std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
        tip->mask[static_cast<std::size_t>(y) * kSize + x] =
            static_cast<std::uint8_t>(std::lround(coverage * 255.0F));
      }
    }
    return std::shared_ptr<const patchy::BrushTip>(std::move(tip));
  };
  static const auto round_tip = make(false);
  static const auto square_tip = make(true);
  return square ? square_tip : round_tip;
}

}  // namespace patchy::ui
