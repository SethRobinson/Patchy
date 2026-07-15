// CanvasWidget's brush implementation, split out of canvas_widget.cpp: the
// brush settings and tip cache (set_brush_* plus the scaled-tip LRU and the
// Round-dynamics disc tip), the brush cursor and hover-outline machinery, the
// Alt+Right-drag brush adjust gesture and its overlay painters, and the stroke
// engine (stroke tracking, axis constraint, smoothing, dab stamping, the
// per-stroke snapshot compositor, mask painting, smudge, and clone). Pure
// function moves from canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kMaxBrushStampSpacing = 64.0;

double point_distance(QPointF a, QPointF b) {
  return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QPointF midpoint(QPointF a, QPointF b) {
  return QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
}

bool brush_smoothing_should_preserve_corner(QPointF previous_segment, QPointF next_segment) {
  const auto previous_length = std::hypot(previous_segment.x(), previous_segment.y());
  const auto next_length = std::hypot(next_segment.x(), next_segment.y());
  if (previous_length <= 0.01 || next_length <= 0.01) {
    return false;
  }

  const auto cosine =
      (previous_segment.x() * next_segment.x() + previous_segment.y() * next_segment.y()) /
      (previous_length * next_length);
  return cosine < 0.35;
}

QPointF quadratic_point(QPointF start, QPointF control, QPointF end, double t) {
  const auto inverse = 1.0 - t;
  const auto start_weight = inverse * inverse;
  const auto control_weight = 2.0 * inverse * t;
  const auto end_weight = t * t;
  return QPointF(start.x() * start_weight + control.x() * control_weight + end.x() * end_weight,
                 start.y() * start_weight + control.y() * control_weight + end.y() * end_weight);
}

QRect united_dirty_rect(QRect a, QRect b) {
  if (a.isEmpty()) {
    return b;
  }
  if (b.isEmpty()) {
    return a;
  }
  return a.united(b);
}

template <typename Callback>
void visit_pixel_line(QPoint from, QPoint to, Callback&& callback) {
  auto x0 = from.x();
  auto y0 = from.y();
  const auto x1 = to.x();
  const auto y1 = to.y();
  const auto dx = std::abs(x1 - x0);
  const auto sx = x0 < x1 ? 1 : -1;
  const auto dy = -std::abs(y1 - y0);
  const auto sy = y0 < y1 ? 1 : -1;
  auto error = dx + dy;

  while (true) {
    callback(QPoint(x0, y0));
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const auto doubled_error = error * 2;
    if (doubled_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

float brush_coverage(double distance_squared, int radius, int softness) {
  if (radius <= 0) {
    return distance_squared <= 0.0 ? 1.0F : 0.0F;
  }

  const auto radius_squared = static_cast<double>(radius) * static_cast<double>(radius);
  if (distance_squared > radius_squared) {
    return 0.0F;
  }

  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return 1.0F;
  }

  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  const auto inner_radius = std::max(0.0, static_cast<double>(radius) - edge_width);
  const auto distance = std::sqrt(distance_squared);
  if (distance <= inner_radius) {
    return 1.0F;
  }
  const auto t = std::clamp((distance - inner_radius) / edge_width, 0.0, 1.0);
  const auto smooth = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  return static_cast<float>(1.0 - smooth);
}

float brush_shape_coverage(double distance_x, double distance_y, int radius, int softness, int roundness_percent,
                           double angle_degrees) {
  roundness_percent = std::clamp(roundness_percent, 1, 100);
  if (radius <= 0 || roundness_percent >= 99) {
    return brush_coverage(distance_x * distance_x + distance_y * distance_y, radius, softness);
  }

  const auto major_radius = static_cast<double>(radius);
  const auto minor_radius = std::max(0.5, major_radius * static_cast<double>(roundness_percent) / 100.0);
  const auto angle = angle_degrees * kPi / 180.0;
  const auto c = std::cos(angle);
  const auto s = std::sin(angle);
  const auto major_axis = c * distance_x + s * distance_y;
  const auto minor_axis = -s * distance_x + c * distance_y;
  const auto normalized_distance_squared =
      (major_axis * major_axis) / (major_radius * major_radius) +
      (minor_axis * minor_axis) / (minor_radius * minor_radius);
  return brush_coverage(normalized_distance_squared * major_radius * major_radius, radius, softness);
}

std::array<double, 3> healing_ring_tone(const QImage& snapshot, QPoint center, int radius) {
  constexpr std::array<std::array<int, 2>, 8> kDirections{{
      {{-1, -1}}, {{0, -1}}, {{1, -1}}, {{-1, 0}},
      {{1, 0}},   {{-1, 1}}, {{0, 1}},  {{1, 1}},
  }};
  std::array<double, 3> sum{};
  double alpha_weight = 0.0;
  for (const auto& direction : kDirections) {
    const auto x = std::clamp(center.x() + direction[0] * radius, 0, snapshot.width() - 1);
    const auto y = std::clamp(center.y() + direction[1] * radius, 0, snapshot.height() - 1);
    const auto* pixel = snapshot.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
    const auto alpha = static_cast<double>(pixel[3]) / 255.0;
    alpha_weight += alpha;
    for (std::size_t channel = 0; channel < sum.size(); ++channel) {
      sum[channel] += static_cast<double>(pixel[channel]) * alpha;
    }
  }
  if (alpha_weight > std::numeric_limits<double>::epsilon()) {
    for (auto& channel : sum) {
      channel /= alpha_weight;
    }
    return sum;
  }

  const auto x = std::clamp(center.x(), 0, snapshot.width() - 1);
  const auto y = std::clamp(center.y(), 0, snapshot.height() - 1);
  const auto* pixel = snapshot.constScanLine(y) + static_cast<std::size_t>(x) * 4U;
  return {static_cast<double>(pixel[0]), static_cast<double>(pixel[1]), static_cast<double>(pixel[2])};
}

std::array<std::uint8_t, 4> healing_sample(const QImage& snapshot, QPoint source, QPoint destination,
                                           int tone_radius) {
  const auto source_tone = healing_ring_tone(snapshot, source, tone_radius);
  const auto destination_tone = healing_ring_tone(snapshot, destination, tone_radius);
  const auto* source_pixel = snapshot.constScanLine(source.y()) + static_cast<std::size_t>(source.x()) * 4U;
  std::array<std::uint8_t, 4> result{};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    // Classic frequency-separation healing: carry sampled detail into the
    // destination's local tone. This is deliberately a fixed local operation,
    // not patch search, synthesis, or a gradient-domain optimization.
    result[channel] = clamp_byte(destination_tone[channel] + static_cast<double>(source_pixel[channel]) -
                                 source_tone[channel]);
  }
  result[3] = source_pixel[3];
  return result;
}

void blend_straight_rgba(std::uint8_t* dst, const std::uint8_t* src, float amount) {
  amount = std::clamp(amount, 0.0F, 1.0F);
  if (amount <= 0.0F) {
    return;
  }
  if (amount >= 0.999F) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    return;
  }

  const auto source_alpha = static_cast<float>(src[3]) / 255.0F;
  const auto destination_alpha = static_cast<float>(dst[3]) / 255.0F;
  const auto out_alpha = source_alpha * amount + destination_alpha * (1.0F - amount);
  if (out_alpha <= 0.0F) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 0;
    return;
  }

  for (int channel = 0; channel < 3; ++channel) {
    const auto source_premultiplied = static_cast<float>(src[channel]) * source_alpha;
    const auto destination_premultiplied = static_cast<float>(dst[channel]) * destination_alpha;
    dst[channel] =
        clamp_byte((source_premultiplied * amount + destination_premultiplied * (1.0F - amount)) / out_alpha);
  }
  dst[3] = clamp_byte(out_alpha * 255.0F);
}

}  // namespace

void CanvasWidget::set_brush_size(int size) {
  const QRect previous_outline =
      brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
  brush_size_ = std::clamp(size, 1, kMaxBrushSize);
  update_tool_cursor();
  invalidate_brush_hover_outline(previous_outline);
}

int CanvasWidget::brush_size() const noexcept {
  return brush_size_;
}

void CanvasWidget::set_brush_opacity(int opacity) {
  brush_opacity_ = std::clamp(opacity, 1, 100);
}

int CanvasWidget::brush_opacity() const noexcept {
  return brush_opacity_;
}

void CanvasWidget::set_brush_flow(int flow) {
  brush_flow_ = std::clamp(flow, 1, 100);
}

int CanvasWidget::brush_flow() const noexcept {
  return brush_flow_;
}

void CanvasWidget::set_brush_softness(int softness) {
  brush_softness_ = std::clamp(softness, 0, 100);
  update_tool_cursor();
}

int CanvasWidget::brush_softness() const noexcept {
  return brush_softness_;
}

void CanvasWidget::set_brush_build_up(bool build_up) noexcept {
  brush_build_up_ = build_up;
  if (!brush_build_up_) {
    airbrush_timer_.stop();
  }
}

void CanvasWidget::set_brush_tip(std::shared_ptr<const patchy::BrushTip> tip, const QString& tip_id) {
  if (tip != nullptr && tip->empty()) {
    tip = nullptr;
  }
  brush_tip_ = std::move(tip);
  brush_tip_id_ = brush_tip_ != nullptr ? tip_id : QString();
  brush_tip_mips_ = brush_tip_ != nullptr ? patchy::build_brush_tip_mips(*brush_tip_) : patchy::BrushTipMipChain{};
  brush_tip_scaled_cache_.clear();
  brush_tip_stroke_state_ = {};
  update_tool_cursor();
}

const QString& CanvasWidget::brush_tip_id() const noexcept {
  return brush_tip_id_;
}

bool CanvasWidget::has_brush_tip() const noexcept {
  return brush_tip_ != nullptr;
}

void CanvasWidget::set_brush_dynamics(const patchy::BrushDynamics& dynamics) noexcept {
  brush_dynamics_ = dynamics;
}

const patchy::BrushDynamics& CanvasWidget::brush_dynamics() const noexcept {
  return brush_dynamics_;
}

void CanvasWidget::set_brush_base_shape(double angle_degrees, int roundness) noexcept {
  brush_base_angle_degrees_ = std::clamp(angle_degrees, -180.0, 360.0);
  brush_base_roundness_ = std::clamp(roundness, 1, 100);
}

double CanvasWidget::brush_base_angle_degrees() const noexcept {
  return brush_base_angle_degrees_;
}

int CanvasWidget::brush_base_roundness() const noexcept {
  return brush_base_roundness_;
}

void CanvasWidget::set_brush_dynamics_test_seed(std::optional<quint32> seed) noexcept {
  brush_dynamics_test_seed_ = seed;
}

namespace {

// Photoshop's default spacing for round brushes; only used while the Round brush stamps.
constexpr double kRoundDynamicsTipSpacing = 0.25;

// Hard AA disc the procedural Round brush stamps through while per-dab dynamics are active
// (the capsule renderer has no dab loop, so scatter/count/jitter need a bitmap path). Built
// once; the Soft slider feathers the scaled stamp exactly like any bitmap tip.
const patchy::BrushTipMipChain& round_dynamics_tip_mips() {
  static const patchy::BrushTipMipChain chain = [] {
    constexpr std::int32_t kSize = 256;
    constexpr float kRadius = 127.0F;
    patchy::BrushTip tip;
    tip.width = kSize;
    tip.height = kSize;
    tip.default_spacing = kRoundDynamicsTipSpacing;
    tip.mask.resize(static_cast<std::size_t>(kSize) * kSize);
    for (std::int32_t y = 0; y < kSize; ++y) {
      for (std::int32_t x = 0; x < kSize; ++x) {
        const auto dx = static_cast<float>(x) + 0.5F - 128.0F;
        const auto dy = static_cast<float>(y) + 0.5F - 128.0F;
        const auto coverage = std::clamp(kRadius + 0.5F - std::sqrt(dx * dx + dy * dy), 0.0F, 1.0F);
        tip.mask[static_cast<std::size_t>(y) * kSize + x] =
            static_cast<std::uint8_t>(std::lround(coverage * 255.0F));
      }
    }
    return patchy::build_brush_tip_mips(tip);
  }();
  return chain;
}

}  // namespace

std::shared_ptr<const patchy::ScaledBrushTip> CanvasWidget::scaled_brush_tip_for(int size,
                                                                                 int softness) const {
  const auto* mips = &brush_tip_mips_;
  if (brush_tip_ == nullptr || brush_tip_mips_.empty()) {
    // The Round brush goes through the stamp path only while session dynamics are active; the
    // cache never mixes sources because set_brush_tip clears it on any tip change.
    if (!brush_dynamics_.active()) {
      return nullptr;
    }
    mips = &round_dynamics_tip_mips();
  }
  size = std::max(1, size);
  softness = std::clamp(softness, 0, 100);
  // The Soft setting feathers the stamp outward; a quarter of the procedural edge width reads
  // similarly soft without ballooning the stamp.
  const auto feather = static_cast<int>(std::lround(static_cast<double>(size) * softness / 400.0));
  const auto key = std::make_pair(size, feather);
  for (std::size_t index = 0; index < brush_tip_scaled_cache_.size(); ++index) {
    if (brush_tip_scaled_cache_[index].first == key) {
      auto entry = brush_tip_scaled_cache_[index];
      brush_tip_scaled_cache_.erase(brush_tip_scaled_cache_.begin() + static_cast<std::ptrdiff_t>(index));
      brush_tip_scaled_cache_.push_back(entry);
      return entry.second;
    }
  }
  auto scaled = std::make_shared<patchy::ScaledBrushTip>(patchy::make_scaled_brush_tip(*mips, size));
  if (scaled->empty()) {
    return nullptr;
  }
  patchy::soften_scaled_brush_tip(*scaled, feather);
  constexpr std::size_t kMaxScaledTips = 8;
  if (brush_tip_scaled_cache_.size() >= kMaxScaledTips) {
    brush_tip_scaled_cache_.erase(brush_tip_scaled_cache_.begin());
  }
  brush_tip_scaled_cache_.emplace_back(key, scaled);
  return scaled;
}

void CanvasWidget::apply_brush_tip_to_options(EditOptions& options, int brush_size, int brush_softness) const {
  auto scaled = scaled_brush_tip_for(brush_size, brush_softness);
  if (scaled == nullptr) {
    return;
  }
  // The cache's shared_ptr keeps the stamp alive for the duration of the paint call.
  options.brush_tip = scaled.get();
  options.brush_tip_spacing =
      brush_tip_ != nullptr ? brush_tip_->default_spacing : kRoundDynamicsTipSpacing;

  if (!brush_dynamics_.active()) {
    return;
  }
  options.brush_dynamics = brush_dynamics_;
  options.brush_dynamics.seed = stroke_dynamics_seed_;
  if (pen_input_settings_.enabled && active_pen_input_sample_.has_value()) {
    // Fill every pen input; the core selects per control (missing inputs stay at their
    // full-value defaults so a mouse paints like Photoshop does without a pen).
    const auto& sample = *active_pen_input_sample_;
    auto& dynamics = options.brush_dynamics;
    if (sample.pressure_available) {
      dynamics.pen_pressure = std::clamp(static_cast<double>(sample.pressure), 0.0, 1.0);
    }
    if (sample.tilt_available) {
      dynamics.pen_tilt =
          std::clamp(std::hypot(static_cast<double>(sample.x_tilt), static_cast<double>(sample.y_tilt)) / 90.0,
                     0.0, 1.0);
      if (std::abs(sample.x_tilt) > std::numeric_limits<float>::epsilon() ||
          std::abs(sample.y_tilt) > std::numeric_limits<float>::epsilon()) {
        dynamics.pen_tilt_azimuth_degrees =
            std::atan2(static_cast<double>(sample.y_tilt), static_cast<double>(sample.x_tilt)) * 180.0 / kPi;
        dynamics.pen_tilt_azimuth_valid = true;
      }
    }
    if (sample.rotation_available) {
      dynamics.pen_rotation_degrees = sample.rotation_degrees;
      dynamics.pen_rotation_valid = true;
    }
    if (sample.tangential_pressure_available) {
      // Qt reports the airbrush wheel as -1..1; Photoshop's Stylus Wheel control wants 0..1.
      dynamics.pen_wheel =
          std::clamp((static_cast<double>(sample.tangential_pressure) + 1.0) / 2.0, 0.0, 1.0);
    }
  }
}

bool CanvasWidget::brush_build_up() const noexcept {
  return brush_build_up_;
}

void CanvasWidget::notify_brush_settings_changed() {
  if (brush_settings_changed_callback_) {
    brush_settings_changed_callback_();
  }
}

// Grayscale coverage image of the stamp at document resolution (from the per-size cache).
QImage CanvasWidget::brush_tip_stamp_image(int size, int softness) const {
  const auto scaled = scaled_brush_tip_for(size, softness);
  if (scaled == nullptr || scaled->empty()) {
    return {};
  }
  QImage image(scaled->width, scaled->height, QImage::Format_Grayscale8);
  for (std::int32_t y = 0; y < scaled->height; ++y) {
    std::copy_n(scaled->mask.data() + static_cast<std::size_t>(y) * scaled->width, scaled->width,
                image.scanLine(y));
  }
  return image;
}

// Outline of a stamp scaled to target size: inside pixels (low threshold so sparse tips still
// read) that touch an outside neighbor. White halo under a black line, readable on any
// background; a 1px transparent margin keeps the halo unclipped.
[[nodiscard]] static QImage build_tip_outline_image(const QImage& stamp, int target_width,
                                                    int target_height) {
  const auto scaled = stamp.scaled(std::max(3, target_width), std::max(3, target_height),
                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QImage image(scaled.width() + 2, scaled.height() + 2, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  constexpr int kInsideThreshold = 40;
  const auto inside = [&scaled](int x, int y) {
    if (x < 0 || y < 0 || x >= scaled.width() || y >= scaled.height()) {
      return false;
    }
    return scaled.constScanLine(y)[x] >= kInsideThreshold;
  };
  std::vector<QPoint> outline;
  outline.reserve(1024);
  for (int y = 0; y < scaled.height(); ++y) {
    for (int x = 0; x < scaled.width(); ++x) {
      if (!inside(x, y)) {
        continue;
      }
      if (!inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1)) {
        outline.push_back(QPoint(x + 1, y + 1));
      }
    }
  }
  if (outline.empty()) {
    return {};
  }
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(QPen(QColor(255, 255, 255), 1));
  for (const auto& point : outline) {
    painter.drawPoint(point + QPoint(1, 0));
    painter.drawPoint(point + QPoint(-1, 0));
    painter.drawPoint(point + QPoint(0, 1));
    painter.drawPoint(point + QPoint(0, -1));
  }
  painter.setPen(QPen(QColor(25, 25, 25), 1));
  for (const auto& point : outline) {
    painter.drawPoint(point);
  }
  painter.end();
  return image;
}

bool CanvasWidget::apply_brush_tip_cursor() {
  const auto stamp = brush_tip_stamp_image(brush_size_, brush_softness_);
  if (stamp.isNull()) {
    return false;
  }
  // Cursor pixmaps are capped; footprints past the cap use the hover overlay path instead
  // (brush_outline_uses_overlay), so this scaling only trims rounding overshoot.
  const auto display_width = std::max(3.0, static_cast<double>(stamp.width()) * zoom_);
  const auto display_height = std::max(3.0, static_cast<double>(stamp.height()) * zoom_);
  const auto fit = std::min(1.0, 155.0 / std::max(display_width, display_height));
  const auto outline = build_tip_outline_image(stamp, static_cast<int>(std::round(display_width * fit)),
                                               static_cast<int>(std::round(display_height * fit)));
  if (outline.isNull()) {
    return false;
  }

  const auto extent = std::clamp(std::max(outline.width(), outline.height()) + 7, 17, 168);
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const QPoint center(extent / 2, extent / 2);
  painter.drawImage(QPoint(center.x() - outline.width() / 2, center.y() - outline.height() / 2), outline);
  painter.setPen(QPen(QColor(255, 255, 255), 1));
  painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
  painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
  painter.end();
  setCursor(QCursor(pixmap, center.x(), center.y()));
  return true;
}

int CanvasWidget::active_outline_brush_size() const noexcept {
  return tool_ == CanvasTool::QuickSelect ? quick_select_size_ : brush_size_;
}

QSize CanvasWidget::brush_outline_display_size() const {
  if (brush_tip_ != nullptr && tool_ != CanvasTool::QuickSelect) {
    const auto scaled = scaled_brush_tip_for(brush_size_, brush_softness_);
    if (scaled != nullptr && !scaled->empty()) {
      return QSize(std::max(3, static_cast<int>(std::round(scaled->width * zoom_))),
                   std::max(3, static_cast<int>(std::round(scaled->height * zoom_))));
    }
  }
  const auto diameter =
      std::max(3, static_cast<int>(std::round(static_cast<double>(active_outline_brush_size()) * zoom_)));
  return QSize(diameter, diameter);
}

bool CanvasWidget::brush_outline_uses_overlay() const {
  if (tool_ != CanvasTool::Brush && tool_ != CanvasTool::Eraser && tool_ != CanvasTool::QuickSelect) {
    return false;
  }
  if (active_outline_brush_size() <= 1) {
    return false;
  }
  const auto display = brush_outline_display_size();
  return std::max(display.width(), display.height()) > 155;
}

QRect CanvasWidget::brush_hover_outline_rect() const {
  const auto display = brush_outline_display_size();
  const auto half_width = display.width() / 2 + 4;
  const auto half_height = display.height() / 2 + 4;
  return QRect(brush_hover_widget_position_.x() - half_width, brush_hover_widget_position_.y() - half_height,
               2 * half_width + 1, 2 * half_height + 1);
}

void CanvasWidget::track_brush_hover_position(QPoint widget_position) {
  if (tool_ != CanvasTool::Brush && tool_ != CanvasTool::Eraser && tool_ != CanvasTool::QuickSelect) {
    brush_hover_position_valid_ = false;
    return;
  }
  const auto old_rect =
      brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
  brush_hover_widget_position_ = widget_position;
  brush_hover_position_valid_ = true;
  if (brush_outline_uses_overlay()) {
    update(old_rect.united(brush_hover_outline_rect()).adjusted(-2, -2, 2, 2));
  }
}

// Repaint the hover outline after the brush (or Quick Select) size changes while
// the pointer is stationary — pressing [ / ]. update_tool_cursor() only invalidates
// the new rect, so shrinking a large brush would strand the old, larger rings
// outside it (they never get repainted). Erase the previous rect and, if the brush
// is still large enough to use the overlay, the new one too.
void CanvasWidget::invalidate_brush_hover_outline(const QRect& previous_outline) {
  QRect region = previous_outline;
  if (brush_hover_position_valid_ && brush_outline_uses_overlay()) {
    region = region.united(brush_hover_outline_rect());
  }
  if (!region.isEmpty()) {
    update(region.adjusted(-2, -2, 2, 2));
  }
}

void CanvasWidget::draw_brush_hover_outline(QPainter& painter) const {
  if (!brush_hover_position_valid_ || !brush_outline_uses_overlay() || brush_adjust_dragging_ ||
      spacebar_panning_) {
    return;
  }
  painter.save();
  const QPoint center = brush_hover_widget_position_;
  if (brush_tip_ != nullptr && tool_ != CanvasTool::QuickSelect) {
    const auto display = brush_outline_display_size();
    const auto key = QStringLiteral("%1:%2x%3:%4:%5")
                         .arg(reinterpret_cast<quintptr>(brush_tip_.get()))
                         .arg(display.width())
                         .arg(display.height())
                         .arg(brush_size_)
                         .arg(brush_softness_);
    if (brush_outline_overlay_key_ != key) {
      const auto stamp = brush_tip_stamp_image(brush_size_, brush_softness_);
      brush_outline_overlay_image_ =
          stamp.isNull() ? QImage() : build_tip_outline_image(stamp, display.width(), display.height());
      brush_outline_overlay_key_ = key;
    }
    if (!brush_outline_overlay_image_.isNull()) {
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.drawImage(QPoint(center.x() - brush_outline_overlay_image_.width() / 2,
                               center.y() - brush_outline_overlay_image_.height() / 2),
                        brush_outline_overlay_image_);
    }
  } else {
    // Large procedural brush: the same circle the cursor draws, just unbounded.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto radius = std::max(2.0, static_cast<double>(active_outline_brush_size()) * zoom_ / 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
    painter.drawEllipse(QPointF(center), radius, radius);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
    painter.drawEllipse(QPointF(center), std::max(1.0, radius - 1.0), std::max(1.0, radius - 1.0));
    if (brush_softness_ > 0 && tool_ != CanvasTool::Eraser && tool_ != CanvasTool::QuickSelect) {
      const auto edge_width =
          std::max(1.0, radius * static_cast<double>(std::clamp(brush_softness_, 0, 100)) / 100.0);
      QPen softness_pen(QColor(105, 150, 210, 175), 1, Qt::DashLine);
      painter.setPen(softness_pen);
      const auto inner_radius = std::max(1.0, radius - edge_width);
      painter.drawEllipse(QPointF(center), inner_radius, inner_radius);
    }
  }
  painter.setPen(QPen(QColor(255, 255, 255), 1));
  painter.drawLine(center + QPoint(-3, 0), center + QPoint(3, 0));
  painter.drawLine(center + QPoint(0, -3), center + QPoint(0, 3));
  painter.restore();
}

void CanvasWidget::begin_brush_adjust_drag(QPoint widget_position, bool from_tablet) {
  brush_adjust_dragging_ = true;
  brush_adjust_from_tablet_ = from_tablet;
  brush_adjust_origin_widget_ = widget_position;
  brush_adjust_current_widget_ = widget_position;
  brush_adjust_start_size_ = brush_size_;
  brush_adjust_start_softness_ = brush_softness_;
  clear_move_hover_outline();
  // The anchored overlay is the size readout; hide the pointer so it does not
  // fight with the preview circle (Photoshop hides it too).
  setCursor(Qt::BlankCursor);
  update();
}

void CanvasWidget::update_brush_adjust_drag(QPoint widget_position) {
  brush_adjust_current_widget_ = widget_position;
  last_mouse_position_ = widget_position;
  const auto delta = widget_position - brush_adjust_origin_widget_;
  // Horizontal drag resizes. For a mouse the disc is anchored at the press
  // point and the diameter grows by twice the dragged distance, so its rim
  // tracks the pointer. A pen-driven disc is centered on the pen instead
  // (the pen cannot be warped back at the end like a mouse, so the disc must
  // finish under it); its radius has to outgrow the pen's own travel or the
  // trailing edge stays pinned and the disc reads as growing from a corner.
  const auto document_dx = static_cast<double>(delta.x()) / std::max(zoom_, 0.0001);
  const auto diameter_per_pixel = brush_adjust_from_tablet_ ? 4.0 : 2.0;
  const auto new_size = std::clamp(
      brush_adjust_start_size_ + static_cast<int>(std::lround(document_dx * diameter_per_pixel)),
      1, kMaxBrushSize);
  // Vertical drag adjusts edge softness: up softens, down hardens (the
  // Photoshop hardness directions, inverted because softness is 100-hardness).
  const auto new_softness =
      std::clamp(brush_adjust_start_softness_ - static_cast<int>(std::lround(static_cast<double>(delta.y()) * 0.4)),
                 0, 100);
  if (new_size == brush_size_ && new_softness == brush_softness_) {
    if (brush_adjust_from_tablet_) {
      update();  // the pen-centered preview moves even when the values do not
    }
    return;
  }
  brush_size_ = new_size;
  brush_softness_ = new_softness;
  update();
}

void CanvasWidget::end_brush_adjust_drag(bool commit) {
  if (!brush_adjust_dragging_) {
    return;
  }
  brush_adjust_dragging_ = false;
  if (!commit) {
    brush_size_ = brush_adjust_start_size_;
    brush_softness_ = brush_adjust_start_softness_;
  }
  if (!brush_adjust_from_tablet_) {
    // A mouse is a relative device, so snap the pointer back to the gesture
    // anchor and the brush stays centered on the spot that was being adjusted
    // (Photoshop does the same). A pen is absolute — its pointer cannot be
    // moved — so its preview is centered on the pen during the drag and the
    // gesture already ends with the brush under the pen.
    QCursor::setPos(mapToGlobal(brush_adjust_origin_widget_));
    last_mouse_position_ = brush_adjust_origin_widget_;
  }
  update_tool_cursor();
  if (commit) {
    notify_brush_settings_changed();
  }
  update();
}

void CanvasWidget::draw_brush_adjust_overlay(QPainter& painter) const {
  if (!brush_adjust_dragging_) {
    return;
  }
  // Mouse gestures anchor the disc at the press point (the pointer is warped
  // back there at the end). Pen gestures center the disc on the pen itself so
  // the gesture finishes with the brush already under the pen — the radius
  // grows faster than the pen travels (see update_brush_adjust_drag), which
  // keeps both edges expanding instead of pinning the trailing one.
  const QPointF center(brush_adjust_from_tablet_ ? brush_adjust_current_widget_ : brush_adjust_origin_widget_);
  const auto radius = std::max(1.5, static_cast<double>(brush_size_) * zoom_ / 2.0);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  if ((tool_ == CanvasTool::Brush || tool_ == CanvasTool::Eraser) && brush_tip_ != nullptr) {
    // A bitmap tip previews as its actual red-tinted footprint instead of a disc.
    const auto stamp = brush_tip_stamp_image(brush_size_, brush_softness_);
    if (!stamp.isNull()) {
      QImage tinted(stamp.size(), QImage::Format_ARGB32_Premultiplied);
      for (int y = 0; y < stamp.height(); ++y) {
        const auto* mask_row = stamp.constScanLine(y);
        auto* out = reinterpret_cast<QRgb*>(tinted.scanLine(y));
        for (int x = 0; x < stamp.width(); ++x) {
          const auto alpha = mask_row[x] * 190 / 255;
          out[x] = qRgba(232 * alpha / 255, 64 * alpha / 255, 64 * alpha / 255, alpha);
        }
      }
      const auto display_width = std::max(3, static_cast<int>(std::round(stamp.width() * zoom_)));
      const auto display_height = std::max(3, static_cast<int>(std::round(stamp.height() * zoom_)));
      const auto display =
          tinted.scaled(display_width, display_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      painter.drawImage(QPointF(center.x() - display.width() / 2.0, center.y() - display.height() / 2.0),
                        display);
      draw_brush_adjust_readout(painter, center, std::max(display_width, display_height) / 2.0);
      painter.restore();
      return;
    }
  }
  // Soft red footprint preview; the solid core shrinks as softness grows. A
  // hard brush gets a plain solid fill: a gradient would need a solid stop and
  // a transparent stop both at 1.0, and QGradient::setColorAt replaces the
  // color when the position already exists, turning the disc into a fade.
  const QColor fill(232, 64, 64, 150);
  painter.setPen(Qt::NoPen);
  if (brush_softness_ <= 0) {
    painter.setBrush(fill);
  } else {
    QRadialGradient gradient(center, radius);
    const auto solid_extent = std::clamp(1.0 - static_cast<double>(brush_softness_) / 100.0, 0.0, 1.0);
    gradient.setColorAt(0.0, fill);
    gradient.setColorAt(solid_extent, fill);
    gradient.setColorAt(1.0, QColor(232, 64, 64, 0));
    painter.setBrush(gradient);
  }
  painter.drawEllipse(center, radius, radius);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(20, 23, 28, 200), 2.4));
  painter.drawEllipse(center, radius, radius);
  painter.setPen(QPen(QColor(245, 248, 252, 235), 1.2));
  painter.drawEllipse(center, radius, radius);
  draw_brush_adjust_readout(painter, center, radius);
  painter.restore();
}

void CanvasWidget::draw_brush_adjust_readout(QPainter& painter, QPointF center, double radius) const {
  const auto readout = tr("Size: %1 px  Soft: %2%").arg(brush_size_).arg(brush_softness_);
  const auto metrics = painter.fontMetrics();
  const auto text_width = metrics.horizontalAdvance(readout);
  QPointF text_position(center.x() - static_cast<double>(text_width) / 2.0,
                        center.y() + radius + static_cast<double>(metrics.ascent()) + 8.0);
  text_position.setX(std::clamp(text_position.x(), 4.0, std::max(4.0, static_cast<double>(width() - text_width - 4))));
  text_position.setY(std::clamp(text_position.y(), static_cast<double>(metrics.ascent()) + 4.0,
                                std::max(static_cast<double>(metrics.ascent()) + 4.0,
                                         static_cast<double>(height()) - static_cast<double>(metrics.descent()) - 4.0)));
  painter.setPen(QColor(20, 23, 28, 220));
  for (const auto& offset : {QPointF(-1.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, -1.0), QPointF(0.0, 1.0)}) {
    painter.drawText(text_position + offset, readout);
  }
  painter.setPen(QColor(245, 248, 252));
  painter.drawText(text_position, readout);
}

void CanvasWidget::clear_brush_stroke_tracking() noexcept {
  airbrush_timer_.stop();
  brush_stroke_pixels_.clear();
  brush_stroke_alpha_caps_.clear();
  brush_stroke_accumulated_alpha_.clear();
  brush_stroke_layer_snapshot_.reset();
  brush_stroke_last_stamp_position_.reset();
  brush_stroke_distance_since_last_stamp_ = 0.0;
  brush_tip_stroke_state_ = {};
  stroke_dynamics_seed_ = brush_dynamics_test_seed_.has_value()
                              ? *brush_dynamics_test_seed_
                              : QRandomGenerator::global()->generate();
}

void CanvasWidget::begin_axis_constrained_stroke(QPointF document_point) noexcept {
  stroke_constraint_start_ = document_point;
  stroke_constraint_axis_ = StrokeConstraintAxis::None;
}

void CanvasWidget::reset_axis_constrained_stroke() noexcept {
  stroke_constraint_start_ = {};
  stroke_constraint_axis_ = StrokeConstraintAxis::None;
}

QPointF CanvasWidget::axis_constrained_stroke_point(QPointF document_point,
                                                    Qt::KeyboardModifiers modifiers) noexcept {
  if ((modifiers & Qt::ShiftModifier) == 0) {
    stroke_constraint_axis_ = StrokeConstraintAxis::None;
    return document_point;
  }

  if (stroke_constraint_axis_ == StrokeConstraintAxis::None) {
    const auto dx = std::abs(document_point.x() - stroke_constraint_start_.x());
    const auto dy = std::abs(document_point.y() - stroke_constraint_start_.y());
    if (dx <= std::numeric_limits<double>::epsilon() && dy <= std::numeric_limits<double>::epsilon()) {
      return document_point;
    }
    stroke_constraint_axis_ = dx >= dy ? StrokeConstraintAxis::Horizontal : StrokeConstraintAxis::Vertical;
  }

  if (stroke_constraint_axis_ == StrokeConstraintAxis::Horizontal) {
    return QPointF(document_point.x(), stroke_constraint_start_.y());
  }
  return QPointF(stroke_constraint_start_.x(), document_point.y());
}

QPoint CanvasWidget::axis_constrained_stroke_point(QPoint document_point,
                                                   Qt::KeyboardModifiers modifiers) noexcept {
  const auto constrained = axis_constrained_stroke_point(QPointF(document_point), modifiers);
  return QPoint(static_cast<int>(std::lround(constrained.x())),
                static_cast<int>(std::lround(constrained.y())));
}

QPoint CanvasWidget::axis_constrained_move_delta(QPoint raw_delta,
                                                 Qt::KeyboardModifiers modifiers) noexcept {
  const auto constrained =
      axis_constrained_stroke_point(QPointF(move_start_ + raw_delta), modifiers) - QPointF(move_start_);
  return QPoint(static_cast<int>(std::lround(constrained.x())),
                static_cast<int>(std::lround(constrained.y())));
}

void CanvasWidget::begin_brush_smoothing(QPointF document_point) noexcept {
  brush_smoothing_active_ = true;
  brush_smoothing_had_movement_ = false;
  brush_smoothing_last_input_position_ = document_point;
  brush_smoothing_last_rendered_position_ = document_point;
}

void CanvasWidget::reset_brush_smoothing() noexcept {
  brush_smoothing_active_ = false;
  brush_smoothing_had_movement_ = false;
  brush_smoothing_last_input_position_ = {};
  brush_smoothing_last_rendered_position_ = {};
}

QRect CanvasWidget::advance_smoothed_brush_stroke(QPointF document_point, bool erase) {
  constexpr auto kMinimumMovement = 0.01;
  if (!brush_smoothing_active_) {
    begin_brush_smoothing(document_point);
    return {};
  }
  if (point_distance(brush_smoothing_last_input_position_, document_point) <= kMinimumMovement) {
    return {};
  }
  const auto first_movement = !brush_smoothing_had_movement_;
  brush_smoothing_had_movement_ = true;

  const auto previous_vector = brush_smoothing_last_input_position_ - brush_smoothing_last_rendered_position_;
  const auto next_vector = document_point - brush_smoothing_last_input_position_;
  if (brush_smoothing_should_preserve_corner(previous_vector, next_vector)) {
    QRect dirty;
    if (point_distance(brush_smoothing_last_rendered_position_, brush_smoothing_last_input_position_) >
        kMinimumMovement) {
      dirty = united_dirty_rect(
          dirty, draw_brush_segment(brush_smoothing_last_rendered_position_,
                                    brush_smoothing_last_input_position_, erase));
    }
    dirty = united_dirty_rect(
        dirty, draw_brush_segment(brush_smoothing_last_input_position_, document_point, erase));
    brush_smoothing_last_input_position_ = document_point;
    brush_smoothing_last_rendered_position_ = document_point;
    return dirty;
  }

  const auto end = midpoint(brush_smoothing_last_input_position_, document_point);
  const auto dirty = draw_smoothed_brush_curve(brush_smoothing_last_rendered_position_,
                                               brush_smoothing_last_input_position_, end, erase,
                                               first_movement);
  brush_smoothing_last_input_position_ = document_point;
  brush_smoothing_last_rendered_position_ = end;
  return dirty;
}

QRect CanvasWidget::finish_smoothed_brush_stroke(QPointF document_point, bool erase) {
  constexpr auto kMinimumMovement = 0.01;
  if (!brush_smoothing_active_) {
    return {};
  }

  QRect dirty;
  auto should_stamp_endpoint = brush_smoothing_had_movement_;
  if (point_distance(brush_smoothing_last_rendered_position_, brush_smoothing_last_input_position_) <=
      kMinimumMovement) {
    if (point_distance(brush_smoothing_last_rendered_position_, document_point) > kMinimumMovement) {
      dirty = draw_brush_segment(brush_smoothing_last_rendered_position_, document_point, erase);
      should_stamp_endpoint = true;
    }
  } else {
    if (point_distance(brush_smoothing_last_input_position_, document_point) > kMinimumMovement) {
      dirty = united_dirty_rect(dirty, advance_smoothed_brush_stroke(document_point, erase));
    }
    if (point_distance(brush_smoothing_last_rendered_position_, document_point) > kMinimumMovement) {
      dirty = united_dirty_rect(
          dirty, draw_smoothed_brush_curve(brush_smoothing_last_rendered_position_,
                                           brush_smoothing_last_input_position_, document_point, erase));
    }
  }
  if (should_stamp_endpoint) {
    dirty = united_dirty_rect(
        dirty, draw_brush_at(QPoint(static_cast<int>(std::lround(document_point.x())),
                                    static_cast<int>(std::lround(document_point.y()))),
                             erase));
  }
  reset_brush_smoothing();
  return dirty;
}

QRect CanvasWidget::draw_smoothed_brush_curve(QPointF start, QPointF control, QPointF end, bool erase,
                                              bool stamp_endpoint) {
  const auto curve_length = std::max(point_distance(start, end),
                                     point_distance(start, control) + point_distance(control, end));
  const auto step_length = std::max(1.0, static_cast<double>(std::max(1, effective_brush_input().size)) * 0.125);
  const auto steps = std::max(1, static_cast<int>(std::ceil(curve_length / step_length)));

  QRect dirty;
  auto previous = start;
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    const auto current = quadratic_point(start, control, end, t);
    dirty = united_dirty_rect(dirty, draw_brush_segment(previous, current, erase,
                                                       stamp_endpoint && step == steps));
    previous = current;
  }
  return dirty;
}

double CanvasWidget::brush_stamp_spacing(const EffectiveBrushInput& brush) const noexcept {
  return std::clamp(static_cast<double>(std::max(1, brush.size)) * 0.125, 1.0, kMaxBrushStampSpacing);
}

bool CanvasWidget::brush_uses_dab_stroke(const EffectiveBrushInput& brush, bool erase) const noexcept {
  if (brush_tip_ != nullptr) {
    return false;
  }
  if (!erase && tool_ == CanvasTool::Brush && brush_dynamics_.active()) {
    return false;
  }
  // Flow must be tied to a spatial dab cadence, not to the number of mouse
  // move events delivered by the platform. Airbrush also uses this path so its
  // moving stroke and stationary timer share the same flat stamp footprint.
  if (!erase && tool_ == CanvasTool::Brush && (brush_flow_ < 100 || brush_build_up_)) {
    return true;
  }
  return brush.size > 1 && brush.softness > 0;
}

QRect CanvasWidget::draw_brush_dab(QPointF point, bool erase, EditOptions& options) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }
  return to_qrect(patchy::paint_brush_dab(*document_, *document_->active_layer_id(), point.x(), point.y(),
                                          options, erase));
}

QRect CanvasWidget::draw_brush_segment_with_dabs(QPointF from, QPointF to, bool erase,
                                                 const EffectiveBrushInput& brush,
                                                 bool stamp_endpoint) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = current_brush_edit_options(brush);
  install_brush_stroke_compositor(options, erase);

  const auto spacing = brush_stamp_spacing(brush);
  auto dirty = QRect{};
  const auto stamp = [&](QPointF point) {
    dirty = united_dirty_rect(dirty, draw_brush_dab(point, erase, options));
    brush_stroke_last_stamp_position_ = point;
    brush_stroke_distance_since_last_stamp_ = 0.0;
  };

  if (!brush_stroke_last_stamp_position_.has_value()) {
    if (stamp_endpoint) {
      brush_stroke_last_stamp_position_ = from;
      brush_stroke_distance_since_last_stamp_ = 0.0;
    } else {
      stamp(from);
    }
  }

  const auto segment_length = point_distance(from, to);
  if (segment_length <= std::numeric_limits<double>::epsilon()) {
    if (stamp_endpoint &&
        (!brush_stroke_last_stamp_position_.has_value() ||
         point_distance(*brush_stroke_last_stamp_position_, to) > 0.001)) {
      stamp(to);
    }
    return dirty;
  }

  const auto dx = (to.x() - from.x()) / segment_length;
  const auto dy = (to.y() - from.y()) / segment_length;
  const auto starting_distance = std::clamp(brush_stroke_distance_since_last_stamp_, 0.0, spacing);
  auto next_stamp_distance = spacing - starting_distance;
  if (next_stamp_distance <= 0.001) {
    next_stamp_distance = spacing;
  }

  auto last_stamp_distance = -1.0;
  while (next_stamp_distance <= segment_length + 0.001) {
    stamp(QPointF(from.x() + dx * next_stamp_distance, from.y() + dy * next_stamp_distance));
    last_stamp_distance = next_stamp_distance;
    next_stamp_distance += spacing;
  }

  if (last_stamp_distance >= 0.0) {
    brush_stroke_distance_since_last_stamp_ = std::max(0.0, segment_length - last_stamp_distance);
  } else {
    brush_stroke_distance_since_last_stamp_ = starting_distance + segment_length;
  }
  if (brush_stroke_distance_since_last_stamp_ >= spacing) {
    brush_stroke_distance_since_last_stamp_ = std::fmod(brush_stroke_distance_since_last_stamp_, spacing);
  }

  if (stamp_endpoint &&
      (!brush_stroke_last_stamp_position_.has_value() ||
       point_distance(*brush_stroke_last_stamp_position_, to) > 0.001)) {
    stamp(to);
  }
  return dirty;
}

float CanvasWidget::capped_stroke_coverage(std::int32_t x, std::int32_t y, float coverage, float source_alpha) {
  source_alpha = std::clamp(source_alpha, 1.0F / 255.0F, 1.0F);
  const auto target_alpha = std::clamp(source_alpha * std::clamp(coverage, 0.0F, 1.0F), 0.0F, 1.0F);
  if (target_alpha <= 0.0F) {
    return 0.0F;
  }

  auto& previous_alpha = brush_stroke_alpha_caps_[stroke_pixel_key(x, y)];
  if (target_alpha <= previous_alpha + 0.0005F) {
    return 0.0F;
  }

  const auto incremental_alpha = (target_alpha - previous_alpha) / std::max(0.0005F, 1.0F - previous_alpha);
  previous_alpha = target_alpha;
  return std::clamp(incremental_alpha / source_alpha, 0.0F, 1.0F);
}

float CanvasWidget::accumulating_stroke_coverage(std::int32_t x, std::int32_t y,
                                                 float coverage, float opacity,
                                                 float flow) {
  opacity = std::clamp(opacity, 1.0F / 255.0F, 1.0F);
  flow = std::clamp(flow, 1.0F / 100.0F, 1.0F);
  const auto dab_alpha = std::clamp(opacity * flow * std::clamp(coverage, 0.0F, 1.0F),
                                    0.0F, opacity);
  if (dab_alpha <= 0.0F) {
    return 0.0F;
  }

  auto& accumulated_alpha = brush_stroke_accumulated_alpha_[stroke_pixel_key(x, y)];
  const auto target_alpha =
      std::min(opacity, 1.0F - (1.0F - accumulated_alpha) * (1.0F - dab_alpha));
  if (target_alpha <= accumulated_alpha + 0.0005F) {
    return 0.0F;
  }

  const auto incremental_alpha =
      (target_alpha - accumulated_alpha) / std::max(0.0005F, 1.0F - accumulated_alpha);
  accumulated_alpha = target_alpha;
  return std::clamp(incremental_alpha / opacity, 0.0F, 1.0F);
}

void CanvasWidget::ensure_brush_stroke_layer_snapshot(LayerId layer_id, const Layer& layer) {
  if (brush_stroke_layer_snapshot_.has_value() && brush_stroke_layer_snapshot_->layer_id == layer_id) {
    return;
  }

  const auto& pixels = static_cast<const Layer&>(layer).pixels();
  BrushStrokeLayerSnapshot snapshot;
  snapshot.layer_id = layer_id;
  snapshot.bounds = layer.bounds();
  snapshot.pixels = pixels;
  snapshot.background_extension = layer.name() == "Background" && !pixels.empty();
  brush_stroke_layer_snapshot_ = std::move(snapshot);
}

std::array<std::uint8_t, 4> CanvasWidget::brush_stroke_original_pixel(std::int32_t x,
                                                                      std::int32_t y) const {
  if (!brush_stroke_layer_snapshot_.has_value()) {
    return {0, 0, 0, 0};
  }

  const auto& snapshot = *brush_stroke_layer_snapshot_;
  const auto& pixels = snapshot.pixels;
  if (snapshot.bounds.contains(x, y) && !pixels.empty()) {
    const auto* source = pixels.pixel(x - snapshot.bounds.x, y - snapshot.bounds.y);
    const auto channels = pixels.format().channels;
    if (channels >= 3) {
      return {source[0], source[1], source[2], channels >= 4 ? source[3] : std::uint8_t{255}};
    }
    if (channels == 1) {
      return {source[0], source[0], source[0], std::uint8_t{255}};
    }
  }

  if (snapshot.background_extension) {
    return {255, 255, 255, 255};
  }
  return {0, 0, 0, 0};
}

const PaletteSnapContext* CanvasWidget::palette_snap_context() const {
  if (document_ == nullptr) {
    return nullptr;
  }
  const auto& editing = document_->palette_editing();
  if (!editing.has_value() || editing->palette.colors.empty()) {
    return nullptr;
  }
  if (palette_lut_revision_ != editing->palette_revision || palette_lut_.empty()) {
    palette_lut_.build(editing->palette.colors);
    palette_lut_revision_ = editing->palette_revision;
  }
  palette_snap_context_.lut = &palette_lut_;
  palette_snap_context_.alpha_threshold = editing->alpha_threshold;
  return &palette_snap_context_;
}

const PaletteSnapContext* CanvasWidget::palette_snap_for_edits() const {
  if (editing_grayscale_target()) {
    return nullptr;
  }
  return palette_snap_context();
}

void CanvasWidget::quantize_image_for_palette_display(QImage& image) const {
  const auto* snap = palette_snap_context();
  if (snap == nullptr || snap->lut == nullptr || snap->lut->empty() || image.isNull()) {
    return;
  }
  if (image.format() != QImage::Format_RGBA8888) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
  }
  const auto threshold = snap->alpha_threshold;
  for (int y = 0; y < image.height(); ++y) {
    auto* line = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      auto* px = line + static_cast<std::size_t>(x) * 4U;
      if (px[3] < threshold) {
        px[3] = 0;
        continue;
      }
      px[3] = 255;
      const auto snapped = snap->lut->snap(px[0], px[1], px[2]);
      px[0] = snapped.red;
      px[1] = snapped.green;
      px[2] = snapped.blue;
    }
  }
}

bool CanvasWidget::write_brush_stroke_pixel_from_snapshot(std::int32_t x, std::int32_t y,
                                                          std::uint8_t* pixel,
                                                          std::uint16_t channels,
                                                          EditColor primary,
                                                          EditColor secondary,
                                                          bool lock_transparent_pixels,
                                                          float coverage, bool erase,
                                                          const PaletteSnapContext* palette_snap) {
  if (palette_snap == nullptr || palette_snap->lut == nullptr || palette_snap->lut->empty()) {
    return write_brush_stroke_pixel_from_snapshot_blend(x, y, pixel, channels, primary, secondary,
                                                        lock_transparent_pixels, coverage, erase);
  }
  // Palette mode: hard coverage, full-strength blend, then snap. The accumulated
  // alpha gate inside the blend keeps repeat dabs on one pixel idempotent.
  if (coverage < palette_snap->coverage_threshold) {
    return false;
  }
  std::array<std::uint8_t, 4> before{};
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    before[channel] = pixel[channel];
  }
  (void)write_brush_stroke_pixel_from_snapshot_blend(x, y, pixel, channels, primary, secondary,
                                                     lock_transparent_pixels, 1.0F, erase);
  const auto blend_wrote = [&]() {
    for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
      if (pixel[channel] != before[channel]) {
        return true;
      }
    }
    return false;
  }();
  if (!blend_wrote) {
    // Locked or gated pixels stay untouched: never snap what the blend refused.
    return false;
  }
  patchy::snap_pixel_to_palette(pixel, channels, *palette_snap);
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    if (pixel[channel] != before[channel]) {
      return true;
    }
  }
  return false;
}

bool CanvasWidget::write_brush_stroke_pixel_from_snapshot_blend(std::int32_t x, std::int32_t y,
                                                                std::uint8_t* pixel,
                                                                std::uint16_t channels,
                                                                EditColor primary,
                                                                EditColor secondary,
                                                                bool lock_transparent_pixels,
                                                                float coverage, bool erase) {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  const auto source_alpha =
      std::clamp(static_cast<float>(std::clamp<int>(primary.a, 1, 255)) / 255.0F, 1.0F / 255.0F, 1.0F);
  if (coverage <= 0.0F) {
    return false;
  }

  const auto original = brush_stroke_original_pixel(x, y);
  const auto locked_alpha = lock_transparent_pixels && channels >= 4;
  if ((erase && locked_alpha) || (locked_alpha && original[3] == 0)) {
    return false;
  }

  const auto flow = static_cast<float>(brush_flow_) / 100.0F;
  const auto dab_alpha = std::clamp(source_alpha * flow * coverage, 0.0F, source_alpha);
  auto& accumulated_alpha = brush_stroke_accumulated_alpha_[stroke_pixel_key(x, y)];
  const auto target_alpha =
      std::min(source_alpha, 1.0F - (1.0F - accumulated_alpha) * (1.0F - dab_alpha));
  if (target_alpha <= accumulated_alpha + 0.0005F) {
    return false;
  }
  accumulated_alpha = target_alpha;

  std::array<std::uint8_t, 4> before{};
  for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
    before[channel] = pixel[channel];
  }
  const auto changed = [&]() {
    for (std::uint16_t channel = 0; channel < channels && channel < before.size(); ++channel) {
      if (pixel[channel] != before[channel]) {
        return true;
      }
    }
    return false;
  };

  if (erase) {
    if (channels >= 4) {
      pixel[0] = original[0];
      pixel[1] = original[1];
      pixel[2] = original[2];
      pixel[3] = clamp_byte(static_cast<float>(original[3]) * (1.0F - target_alpha));
      return changed();
    }

    pixel[0] = clamp_byte(static_cast<float>(secondary.r) * target_alpha +
                          static_cast<float>(original[0]) * (1.0F - target_alpha));
    pixel[1] = clamp_byte(static_cast<float>(secondary.g) * target_alpha +
                          static_cast<float>(original[1]) * (1.0F - target_alpha));
    pixel[2] = clamp_byte(static_cast<float>(secondary.b) * target_alpha +
                          static_cast<float>(original[2]) * (1.0F - target_alpha));
    return changed();
  }

  if (channels >= 4) {
    if (locked_alpha) {
      if (target_alpha >= 0.999F) {
        pixel[0] = primary.r;
        pixel[1] = primary.g;
        pixel[2] = primary.b;
      } else {
        pixel[0] = clamp_byte(static_cast<float>(primary.r) * target_alpha +
                              static_cast<float>(original[0]) * (1.0F - target_alpha));
        pixel[1] = clamp_byte(static_cast<float>(primary.g) * target_alpha +
                              static_cast<float>(original[1]) * (1.0F - target_alpha));
        pixel[2] = clamp_byte(static_cast<float>(primary.b) * target_alpha +
                              static_cast<float>(original[2]) * (1.0F - target_alpha));
      }
      pixel[3] = original[3];
      return changed();
    }

    const auto destination_alpha = static_cast<float>(original[3]) / 255.0F;
    const auto out_alpha = target_alpha + destination_alpha * (1.0F - target_alpha);
    if (out_alpha <= 0.0F) {
      return false;
    }
    if (destination_alpha <= 0.0F) {
      pixel[0] = primary.r;
      pixel[1] = primary.g;
      pixel[2] = primary.b;
      pixel[3] = std::max<std::uint8_t>(1, clamp_byte(target_alpha * 255.0F));
      return changed();
    }

    pixel[0] = clamp_byte((static_cast<float>(primary.r) * target_alpha +
                           static_cast<float>(original[0]) * destination_alpha * (1.0F - target_alpha)) /
                          out_alpha);
    pixel[1] = clamp_byte((static_cast<float>(primary.g) * target_alpha +
                           static_cast<float>(original[1]) * destination_alpha * (1.0F - target_alpha)) /
                          out_alpha);
    pixel[2] = clamp_byte((static_cast<float>(primary.b) * target_alpha +
                           static_cast<float>(original[2]) * destination_alpha * (1.0F - target_alpha)) /
                          out_alpha);
    pixel[3] = clamp_byte(out_alpha * 255.0F);
    return changed();
  }

  if (target_alpha >= 0.999F) {
    pixel[0] = primary.r;
    pixel[1] = primary.g;
    pixel[2] = primary.b;
  } else {
    pixel[0] = clamp_byte(static_cast<float>(primary.r) * target_alpha +
                          static_cast<float>(original[0]) * (1.0F - target_alpha));
    pixel[1] = clamp_byte(static_cast<float>(primary.g) * target_alpha +
                          static_cast<float>(original[1]) * (1.0F - target_alpha));
    pixel[2] = clamp_byte(static_cast<float>(primary.b) * target_alpha +
                          static_cast<float>(original[2]) * (1.0F - target_alpha));
  }
  return changed();
}

void CanvasWidget::install_brush_stroke_compositor(EditOptions& options, bool erase) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return;
  }

  const auto layer_id = *document_->active_layer_id();
  const auto* layer = document_->find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return;
  }

  ensure_brush_stroke_layer_snapshot(layer_id, *layer);
  const auto primary = options.primary;
  const auto secondary = options.secondary;
  const auto lock_transparent_pixels = options.lock_transparent_pixels;
  const auto source_alpha =
      std::clamp(static_cast<float>(std::clamp<int>(primary.a, 1, 255)) / 255.0F, 1.0F / 255.0F, 1.0F);

  options.stroke_pixel_gate = [this, source_alpha](std::int32_t x, std::int32_t y) {
    const auto found = brush_stroke_accumulated_alpha_.find(stroke_pixel_key(x, y));
    return found == brush_stroke_accumulated_alpha_.end() || found->second < source_alpha - 0.0005F;
  };
  const auto* palette_snap = options.palette_snap;
  options.stroke_pixel_writer = [this, primary, secondary, lock_transparent_pixels, erase, palette_snap](
                                    std::int32_t x, std::int32_t y, std::uint8_t* pixel,
                                    std::uint16_t channels, float coverage) {
    return write_brush_stroke_pixel_from_snapshot(x, y, pixel, channels, primary, secondary,
                                                  lock_transparent_pixels, coverage, erase, palette_snap);
  };
}

CanvasWidget::EffectiveBrushInput CanvasWidget::effective_brush_input() const noexcept {
  // The static tip shape (Photoshop Brush Tip Shape angle/roundness) is the baseline; pen tilt
  // may override it below while the pen is tilted.
  EffectiveBrushInput brush{brush_size_, brush_opacity_, brush_softness_, brush_base_roundness_,
                            brush_base_angle_degrees_};
  if (!pen_input_settings_.enabled || !active_pen_input_sample_.has_value()) {
    return brush;
  }

  const auto& sample = *active_pen_input_sample_;
  // A brush whose dynamics set an explicit control for an aspect (anything but GlobalDefault,
  // including Off) owns that aspect, so the global pen preference for it is suppressed. Eraser
  // strokes (toolbar tool or pen eraser end), Clone/Smudge, and mask painting never run
  // dynamics and keep the full global behavior.
  const auto brush_dynamics_authoritative =
      !editing_grayscale_target() && effective_tool_for_input() == CanvasTool::Brush;
  const auto pressure = sample.pressure_available ? std::clamp(sample.pressure, 0.0F, 1.0F) : 1.0F;
  if (pen_input_settings_.pressure_size &&
      !(brush_dynamics_authoritative &&
        brush_dynamics_.size_control != patchy::BrushDynamicControl::GlobalDefault)) {
    const auto minimum = static_cast<double>(std::clamp(pen_input_settings_.pressure_size_min_percent, 1, 100)) / 100.0;
    const auto scale = minimum + static_cast<double>(pressure) * (1.0 - minimum);
    brush.size = std::clamp(static_cast<int>(std::lround(static_cast<double>(brush_size_) * scale)),
                            1, kMaxBrushSize);
    const auto stamps = brush_tip_ != nullptr ||
                        (brush_dynamics_.active() && tool_ == CanvasTool::Brush);  // Round + dynamics
    if (stamps && brush.size > 4) {
      brush.size &= ~1;  // 2px steps so pressure oscillation reuses cached scaled stamps
    }
  }
  if (pen_input_settings_.pressure_opacity &&
      !(brush_dynamics_authoritative &&
        brush_dynamics_.opacity_control != patchy::BrushDynamicControl::GlobalDefault)) {
    const auto minimum =
        static_cast<double>(std::clamp(pen_input_settings_.pressure_opacity_min_percent, 1, 100)) / 100.0;
    const auto scale = minimum + static_cast<double>(pressure) * (1.0 - minimum);
    brush.opacity = std::clamp(static_cast<int>(std::lround(static_cast<double>(brush_opacity_) * scale)), 1, 100);
  }
  if (pen_input_settings_.tilt_shape && sample.tilt_available) {
    const auto tilt = std::clamp(std::hypot(static_cast<double>(sample.x_tilt), static_cast<double>(sample.y_tilt)) /
                                     90.0,
                                 0.0, 1.0);
    const auto minimum_roundness = std::clamp(pen_input_settings_.tilt_min_roundness_percent, 1, 100);
    if (!(brush_dynamics_authoritative &&
          brush_dynamics_.roundness_control != patchy::BrushDynamicControl::GlobalDefault)) {
      brush.roundness = std::clamp(static_cast<int>(std::lround(100.0 - tilt * (100.0 - minimum_roundness))), 1, 100);
    }
    // When the active dynamics drive the angle from the pen (PenTilt/PenRotation), the per-dab
    // path owns it; applying the tilt angle here too would rotate the stamp twice. This covers
    // the Round brush as well, whose session dynamics stamp through the disc tip.
    const auto dynamics_own_angle =
        brush_dynamics_authoritative && brush_dynamics_.active() &&
        (brush_dynamics_.angle_control == patchy::BrushDynamicControl::PenTilt ||
         brush_dynamics_.angle_control == patchy::BrushDynamicControl::PenRotation);
    if (!dynamics_own_angle) {
      if (sample.rotation_available) {
        brush.angle_degrees = sample.rotation_degrees;
      } else if (std::abs(sample.x_tilt) > std::numeric_limits<float>::epsilon() ||
                 std::abs(sample.y_tilt) > std::numeric_limits<float>::epsilon()) {
        brush.angle_degrees = std::atan2(static_cast<double>(sample.y_tilt), static_cast<double>(sample.x_tilt)) *
                              180.0 / kPi;
      }
    }
  }
  return brush;
}

EditOptions CanvasWidget::current_brush_edit_options(const EffectiveBrushInput& brush) const {
  return edit_options(primary_color_, secondary_color_, brush.size, brush.opacity, brush.softness, fill_shapes_,
                      active_layer_locks_transparent_pixels(), *this, brush.roundness, brush.angle_degrees);
}

QRect CanvasWidget::draw_brush_segment(QPointF from, QPointF to, bool erase, bool stamp_endpoint) {
  if (editing_grayscale_target()) {
    return draw_mask_brush_segment(from, to, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  if (brush_uses_dab_stroke(brush, erase)) {
    return draw_brush_segment_with_dabs(from, to, erase, brush, stamp_endpoint);
  }
  auto options = current_brush_edit_options(brush);
  apply_brush_tip_to_options(options, brush.size, brush.softness);
  if (erase) {
    options.brush_dynamics = {};  // v1: dynamics are Brush-only; erase strokes stay predictable
    if (brush_tip_ == nullptr) {
      options.brush_tip = nullptr;  // Round + session dynamics: the eraser stays procedural
    }
  }
  if (options.brush_tip != nullptr) {
    install_brush_stroke_compositor(options, erase);
    return to_qrect(
        patchy::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                    options, erase, brush_tip_stroke_state_));
  }

  install_brush_stroke_compositor(options, erase);
  auto dirty = to_qrect(
      patchy::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                  options, erase));
  if (stamp_endpoint) {
    dirty = united_dirty_rect(
        dirty, to_qrect(patchy::paint_brush(*document_, *document_->active_layer_id(),
                                            static_cast<int>(std::lround(to.x())),
                                            static_cast<int>(std::lround(to.y())), options, erase)));
  }
  return dirty;
}

QRect CanvasWidget::draw_brush_segment(QPoint from, QPoint to, bool erase, bool stamp_endpoint) {
  return draw_brush_segment(QPointF(from), QPointF(to), erase, stamp_endpoint);
}

QRect CanvasWidget::draw_brush_at(QPoint point, bool erase) {
  if (editing_grayscale_target()) {
    return draw_mask_brush_at(point, erase);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  if (brush_uses_dab_stroke(brush, erase)) {
    install_brush_stroke_compositor(options, erase);
    const auto point_f = QPointF(point);
    const auto dirty = draw_brush_dab(point_f, erase, options);
    brush_stroke_last_stamp_position_ = point_f;
    brush_stroke_distance_since_last_stamp_ = 0.0;
    return dirty;
  }
  apply_brush_tip_to_options(options, brush.size, brush.softness);
  if (erase) {
    options.brush_dynamics = {};
    if (brush_tip_ == nullptr) {
      options.brush_tip = nullptr;  // Round + session dynamics: the eraser stays procedural
    }
  }
  if (options.brush_tip != nullptr) {
    install_brush_stroke_compositor(options, erase);
    // Stateful zero-length segment: stamps exactly the press dab and starts the stroke's dab
    // spacing + dynamics RNG stream, so the first move segment does not re-stamp the press
    // point (invisible for static stamps, visibly double-jittered with dynamics).
    return to_qrect(patchy::paint_brush_segment(*document_, *document_->active_layer_id(), point.x(),
                                                point.y(), point.x(), point.y(), options, erase,
                                                brush_tip_stroke_state_));
  }
  install_brush_stroke_compositor(options, erase);
  return to_qrect(
      patchy::paint_brush(*document_, *document_->active_layer_id(), point.x(), point.y(), options, erase));
}

QRect CanvasWidget::draw_airbrush_dab(QPointF point) {
  if (editing_grayscale_target()) {
    return draw_mask_brush_at(QPoint(static_cast<int>(std::lround(point.x())),
                                     static_cast<int>(std::lround(point.y()))),
                              false);
  }
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  apply_brush_tip_to_options(options, brush.size, brush.softness);
  install_brush_stroke_compositor(options, false);
  return to_qrect(patchy::paint_stationary_airbrush_dab(
      *document_, *document_->active_layer_id(), point.x(), point.y(), options,
      brush_tip_stroke_state_));
}

QRect CanvasWidget::draw_mask_brush_segment(QPointF from, QPointF to, bool erase) {
  if (document_ == nullptr) {
    return {};
  }

  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  if (radius == 0) {
    const auto start = QPoint(static_cast<int>(std::floor(from.x())), static_cast<int>(std::floor(from.y())));
    const auto end = QPoint(static_cast<int>(std::floor(to.x())), static_cast<int>(std::floor(to.y())));
    auto stroke_rect = QRect(std::min(start.x(), end.x()),
                             std::min(start.y(), end.y()),
                             std::abs(end.x() - start.x()) + 1,
                             std::abs(end.y() - start.y()) + 1)
                           .intersected(QRect(0, 0, document_->width(), document_->height()));
    auto target = active_grayscale_edit_target(stroke_rect);
    if (stroke_rect.isEmpty() || !target.has_value() || target->pixels == nullptr) {
      return {};
    }

    const auto bounds = target->bounds;
    const auto paint_value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
    const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
    QRect dirty;
    visit_pixel_line(start, end, [&](QPoint document_point) {
      if (!QRect(0, 0, document_->width(), document_->height()).contains(document_point) ||
          !bounds.contains(document_point) || !selection_allows(document_point)) {
        return;
      }
      auto coverage = selection_clips_grayscale_edits()
                          ? static_cast<float>(selection_alpha_at(document_point)) /
                                255.0F
                          : 1.0F;
      if (coverage <= 0.0F) {
        return;
      }
      coverage = brush_flow_ < 100 || brush_build_up_
                     ? accumulating_stroke_coverage(document_point.x(), document_point.y(),
                                                    coverage, opacity,
                                                    static_cast<float>(brush_flow_) / 100.0F)
                     : capped_stroke_coverage(document_point.x(), document_point.y(), coverage,
                                              opacity);
      if (coverage <= 0.0F) {
        return;
      }
      coverage *= opacity;

      auto* value = target->pixels->pixel(document_point.x() - bounds.x(), document_point.y() - bounds.y());
      *value = blend_mask_value(*value, paint_value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    });
    return dirty;
  }

  const auto left = static_cast<int>(std::floor(std::min(from.x(), to.x()) - static_cast<double>(radius)));
  const auto top = static_cast<int>(std::floor(std::min(from.y(), to.y()) - static_cast<double>(radius)));
  const auto right = static_cast<int>(std::ceil(std::max(from.x(), to.x()) + static_cast<double>(radius))) + 1;
  const auto bottom = static_cast<int>(std::ceil(std::max(from.y(), to.y()) + static_cast<double>(radius))) + 1;
  auto stroke_rect = QRect(left, top, right - left, bottom - top)
                         .intersected(QRect(0, 0, document_->width(), document_->height()));
  auto target = active_grayscale_edit_target(stroke_rect);
  if (stroke_rect.isEmpty() || !target.has_value() || target->pixels == nullptr) {
    return {};
  }

  const auto bounds = target->bounds;
  stroke_rect = stroke_rect.intersected(bounds);
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = dx * dx + dy * dy;
  const auto paint_value = erase ? std::uint8_t{0} : mask_value_from_color(primary_color_);
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;

  QRect dirty;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      const auto along =
          segment_length_squared <= std::numeric_limits<double>::epsilon()
              ? 0.0
              : std::clamp(((static_cast<double>(x) - from.x()) * dx + (static_cast<double>(y) - from.y()) * dy) /
                                segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = from.x() + dx * along;
      const auto closest_y = from.y() + dy * along;
      const auto distance_x = static_cast<double>(x) - closest_x;
      const auto distance_y = static_cast<double>(y) - closest_y;
      auto coverage = brush_shape_coverage(distance_x, distance_y, radius, brush.softness, brush.roundness,
                                           brush.angle_degrees);
      if (selection_clips_grayscale_edits()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      }
      if (coverage <= 0.0F) {
        continue;
      }
      coverage = brush_flow_ < 100 || brush_build_up_
                     ? accumulating_stroke_coverage(x, y, coverage, opacity,
                                                    static_cast<float>(brush_flow_) / 100.0F)
                     : capped_stroke_coverage(x, y, coverage, opacity);
      if (coverage <= 0.0F) {
        continue;
      }
      coverage *= opacity;

      auto* value = target->pixels->pixel(x - bounds.x(), y - bounds.y());
      *value = blend_mask_value(*value, paint_value, coverage);
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

QRect CanvasWidget::draw_mask_brush_segment(QPoint from, QPoint to, bool erase) {
  return draw_mask_brush_segment(QPointF(from), QPointF(to), erase);
}

QRect CanvasWidget::draw_mask_brush_at(QPoint point, bool erase) {
  return draw_mask_brush_segment(point, point, erase);
}

QRect CanvasWidget::smudge_brush_segment(QPoint from, QPoint to) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  return to_qrect(patchy::smudge_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(),
                                                  to.x(), to.y(), options, smudge_state_));
}

QRect CanvasWidget::local_adjustment_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  const auto layer_id = *document_->active_layer_id();
  ensure_brush_stroke_layer_snapshot(layer_id, std::as_const(*layer));
  if (!brush_stroke_layer_snapshot_.has_value() || brush_stroke_layer_snapshot_->layer_id != layer_id) {
    return {};
  }
  const auto& snapshot = *brush_stroke_layer_snapshot_;
  const auto& source_pixels = snapshot.pixels;
  const auto channels = source_pixels.format().channels;
  if (source_pixels.empty() || source_pixels.format().bit_depth != BitDepth::UInt8 || channels < 3) {
    return {};
  }

  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  const auto layer_rect = to_qrect(snapshot.bounds);
  const auto document_rect = QRect(0, 0, document_->width(), document_->height());
  auto stroke_rect = QRect(std::min(from.x(), to.x()) - radius,
                           std::min(from.y(), to.y()) - radius,
                           std::abs(to.x() - from.x()) + radius * 2 + 1,
                           std::abs(to.y() - from.y()) + radius * 2 + 1)
                         .intersected(document_rect)
                         .intersected(layer_rect);
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto source_pixel = [&](int document_x, int document_y) {
    const auto x = std::clamp(document_x, layer_rect.left(), layer_rect.right()) - snapshot.bounds.x;
    const auto y = std::clamp(document_y, layer_rect.top(), layer_rect.bottom()) - snapshot.bounds.y;
    const auto* pixel = source_pixels.pixel(x, y);
    return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2],
                                       channels >= 4 ? pixel[3] : std::uint8_t{255}};
  };

  // Patent boundary (July 2026): these are deliberately fixed local operations.
  // Do not add edge ranking, patch matching, deconvolution, automatic boundary
  // isolation, or stroke-start color classification here. US 7724980, US 8687913,
  // US 9142009, and the abandoned US 2007/0188510 all cover variants of those
  // adaptive techniques. The brush footprint alone chooses which pixels change.
  const auto adjusted_source_pixel = [&](QPoint point) {
    const auto center = source_pixel(point.x(), point.y());
    std::array<std::uint8_t, 3> result{center[0], center[1], center[2]};

    if (tool_ == CanvasTool::BlurBrush || tool_ == CanvasTool::SharpenBrush) {
      constexpr std::array<int, 3> kGaussian{1, 2, 1};
      std::array<double, 3> premultiplied{};
      double alpha_weight = 0.0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample = source_pixel(point.x() + offset_x, point.y() + offset_y);
          const auto weight = static_cast<double>(kGaussian[static_cast<std::size_t>(offset_x + 1)] *
                                                  kGaussian[static_cast<std::size_t>(offset_y + 1)]);
          const auto alpha = static_cast<double>(sample[3]) / 255.0;
          alpha_weight += weight * alpha;
          for (std::size_t channel = 0; channel < premultiplied.size(); ++channel) {
            premultiplied[channel] += weight * alpha * static_cast<double>(sample[channel]);
          }
        }
      }
      if (alpha_weight <= std::numeric_limits<double>::epsilon()) {
        return result;
      }
      for (std::size_t channel = 0; channel < result.size(); ++channel) {
        const auto blurred = premultiplied[channel] / alpha_weight;
        result[channel] = tool_ == CanvasTool::BlurBrush
                              ? clamp_byte(static_cast<float>(blurred))
                              : clamp_byte(static_cast<float>(static_cast<double>(center[channel]) * 2.0 - blurred));
      }
      return result;
    }

    const auto red = static_cast<double>(center[0]);
    const auto green = static_cast<double>(center[1]);
    const auto blue = static_cast<double>(center[2]);
    const auto lightness = (54.0 * red + 183.0 * green + 19.0 * blue) / (256.0 * 255.0);
    if (tool_ == CanvasTool::Dodge || tool_ == CanvasTool::Burn) {
      double range_weight = 1.0;
      switch (local_tone_range_) {
        case LocalToneRange::Shadows:
          range_weight = 1.0 - lightness;
          break;
        case LocalToneRange::Midtones:
          range_weight = 1.0 - std::abs(lightness * 2.0 - 1.0);
          break;
        case LocalToneRange::Highlights:
          range_weight = lightness;
          break;
      }
      const auto source_lightness = lightness * 255.0;
      const auto target_lightness = tool_ == CanvasTool::Dodge
                                        ? source_lightness + (255.0 - source_lightness) * range_weight
                                        : source_lightness * (1.0 - range_weight);
      for (std::size_t channel = 0; channel < result.size(); ++channel) {
        const auto value = static_cast<double>(center[channel]);
        const auto adjusted = local_protect_tones_
                                  ? value + (target_lightness - source_lightness)
                                  : (tool_ == CanvasTool::Dodge
                                         ? value + (255.0 - value) * range_weight
                                         : value * (1.0 - range_weight));
        result[channel] = clamp_byte(static_cast<float>(adjusted));
      }
      return result;
    }

    if (tool_ == CanvasTool::Sponge) {
      const auto maximum = static_cast<double>(std::max({center[0], center[1], center[2]}));
      const auto minimum = static_cast<double>(std::min({center[0], center[1], center[2]}));
      const auto saturation = (maximum - minimum) / 255.0;
      const auto vibrance_scale = sponge_vibrance_ ? 1.0 - saturation : 1.0;
      const auto luma = lightness * 255.0;
      const auto chroma_scale = sponge_mode_ == SpongeMode::Saturate ? 1.0 + vibrance_scale
                                                                     : 1.0 - vibrance_scale;
      for (std::size_t channel = 0; channel < result.size(); ++channel) {
        result[channel] = clamp_byte(static_cast<float>(
            luma + (static_cast<double>(center[channel]) - luma) * chroma_scale));
      }
    }
    return result;
  };

  auto& pixels = layer->pixels();
  const auto mutable_channels = pixels.format().channels;
  const auto strength = static_cast<float>(local_adjustment_strength_) / 100.0F;
  const auto* palette_snap = palette_snap_for_edits();
  QRect dirty;
  const auto adjust_pixel = [&](QPoint point, float coverage) {
    if (!stroke_rect.contains(point) || !selection_allows(point)) {
      return;
    }
    if (has_selection()) {
      coverage *= static_cast<float>(selection_alpha_at(point)) / 255.0F;
    }
    if (coverage <= 0.0F) {
      return;
    }
    if (palette_snap != nullptr) {
      if (coverage < palette_snap->coverage_threshold) {
        return;
      }
      coverage = 1.0F;
    }
    coverage = capped_stroke_coverage(point.x(), point.y(), coverage, strength);
    if (coverage <= 0.0F) {
      return;
    }

    auto* destination = pixels.pixel(point.x() - snapshot.bounds.x, point.y() - snapshot.bounds.y);
    if (mutable_channels >= 4 && destination[3] == 0) {
      return;
    }
    const auto adjusted = adjusted_source_pixel(point);
    const auto amount = strength * coverage;
    const auto before = std::array<std::uint8_t, 4>{destination[0], destination[1], destination[2],
                                                    mutable_channels >= 4 ? destination[3] : std::uint8_t{255}};
    for (std::size_t channel = 0; channel < adjusted.size(); ++channel) {
      destination[channel] = clamp_byte(static_cast<float>(adjusted[channel]) * amount +
                                        static_cast<float>(destination[channel]) * (1.0F - amount));
    }
    if (palette_snap != nullptr) {
      patchy::snap_pixel_to_palette(destination, mutable_channels, *palette_snap);
    }
    if (destination[0] != before[0] || destination[1] != before[1] || destination[2] != before[2]) {
      dirty = dirty.united(QRect(point, QSize(1, 1)));
    }
  };

  if (radius == 0) {
    visit_pixel_line(from, to, [&](QPoint point) { adjust_pixel(point, 1.0F); });
    return dirty;
  }

  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto segment_length_squared = dx * dx + dy * dy;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const auto along = segment_length_squared <= std::numeric_limits<double>::epsilon()
                             ? 0.0
                             : std::clamp((static_cast<double>(x - from.x()) * dx +
                                           static_cast<double>(y - from.y()) * dy) /
                                              segment_length_squared,
                                          0.0, 1.0);
      const auto closest_x = static_cast<double>(from.x()) + dx * along;
      const auto closest_y = static_cast<double>(from.y()) + dy * along;
      const auto coverage = brush_shape_coverage(static_cast<double>(x) - closest_x,
                                                 static_cast<double>(y) - closest_y,
                                                 radius, brush.softness, brush.roundness,
                                                 brush.angle_degrees);
      adjust_pixel(QPoint(x, y), coverage);
    }
    tick_processing_operation();
  }
  return dirty;
}

void CanvasWidget::set_clone_source(QPoint point) {
  if (!document_contains(point)) {
    return;
  }
  clone_source_point_ = point;
  clone_source_set_ = true;
  clone_aligned_offset_set_ = false;
  update();
  if (status_callback_) {
    status_callback_((tool_ == CanvasTool::Healing ? tr("Healing source set at %1, %2")
                                                   : tr("Clone source set at %1, %2"))
                         .arg(point.x())
                         .arg(point.y()));
  }
}

QRect CanvasWidget::clone_brush_at(QPoint point) {
  return clone_brush_segment(point, point);
}

QRect CanvasWidget::clone_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr || clone_source_cache_.isNull() ||
      layer->pixels().format().bit_depth != BitDepth::UInt8 || layer->pixels().format().channels < 3) {
    return {};
  }
  const auto* palette_snap = palette_snap_for_edits();

  const auto brush = effective_brush_input();
  const auto healing = tool_ == CanvasTool::Healing;
  const auto healing_tone_radius =
      std::max(1, (brush.size * (9 - std::clamp(healing_diffusion_, 1, 7)) + 15) / 16);
  const auto radius = std::max(1, brush.size) / 2;
  if (radius == 0) {
    const auto path_rect = QRect(std::min(from.x(), to.x()),
                                 std::min(from.y(), to.y()),
                                 std::abs(to.x() - from.x()) + 1,
                                 std::abs(to.y() - from.y()) + 1)
                               .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (path_rect.isEmpty()) {
      return {};
    }

    const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
    if (!lock_transparent_pixels) {
      patchy::expand_layer_to_include_rect(*layer, to_core_rect(path_rect));
    }

    auto& pixels = layer->pixels();
    const auto bounds = layer->bounds();
    const auto channels = pixels.format().channels;
    const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
    QRect dirty;
    visit_pixel_line(from, to, [&](QPoint document_point) {
      if (!QRect(0, 0, document_->width(), document_->height()).contains(document_point) ||
          !to_qrect(bounds).contains(document_point) || !selection_allows(document_point)) {
        return;
      }
      auto coverage = has_selection() ? static_cast<float>(selection_alpha_at(document_point)) / 255.0F : 1.0F;
      if (coverage <= 0.0F) {
        return;
      }
      if (palette_snap != nullptr) {
        if (coverage < palette_snap->coverage_threshold) {
          return;
        }
        coverage = 1.0F;
      }
      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        return;
      }

      auto row = pixels.row(document_point.y() - bounds.y);
      auto* dst = row.data() + static_cast<std::size_t>(document_point.x() - bounds.x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        return;
      }
      coverage = capped_stroke_coverage(document_point.x(), document_point.y(), coverage, opacity);
      if (coverage <= 0.0F) {
        return;
      }

      std::array<std::uint8_t, 4> healed{};
      const auto* src = clone_source_cache_.constScanLine(source_point.y()) +
                        static_cast<std::size_t>(source_point.x()) * 4U;
      if (healing) {
        healed = healing_sample(clone_source_cache_, source_point, document_point, healing_tone_radius);
        src = healed.data();
      }
      const auto covered_opacity = opacity * coverage;
      if (channels >= 4 && !lock_transparent_pixels) {
        blend_straight_rgba(dst, src, covered_opacity);
      } else {
        const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
        if (effective_opacity <= 0.0F) {
          return;
        }
        dst[0] = clamp_byte(static_cast<float>(src[0]) * effective_opacity +
                            static_cast<float>(dst[0]) * (1.0F - effective_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * effective_opacity +
                            static_cast<float>(dst[1]) * (1.0F - effective_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * effective_opacity +
                            static_cast<float>(dst[2]) * (1.0F - effective_opacity));
      }
      if (palette_snap != nullptr) {
        patchy::snap_pixel_to_palette(dst, channels, *palette_snap);
      }
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    });
    return dirty;
  }

  const auto left = std::min(from.x(), to.x()) - radius;
  const auto top = std::min(from.y(), to.y()) - radius;
  const auto right = std::max(from.x(), to.x()) + radius + 1;
  const auto bottom = std::max(from.y(), to.y()) + radius + 1;
  auto stroke_rect = QRect(left, top, right - left, bottom - top).intersected(
      QRect(0, 0, document_->width(), document_->height()));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(*layer, to_core_rect(stroke_rect));
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  stroke_rect = stroke_rect.intersected(to_qrect(bounds));
  if (stroke_rect.isEmpty()) {
    return {};
  }

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = static_cast<double>(dx) * static_cast<double>(dx) +
                                      static_cast<double>(dy) * static_cast<double>(dy);
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;

  QRect dirty;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    auto row = pixels.row(y - bounds.y);
    for (int x = stroke_rect.left(); x <= stroke_rect.right(); ++x) {
      const auto along =
          segment_length_squared <= 0.0
              ? 0.0
              : std::clamp((static_cast<double>(x - from.x()) * static_cast<double>(dx) +
                            static_cast<double>(y - from.y()) * static_cast<double>(dy)) /
                               segment_length_squared,
                           0.0, 1.0);
      const auto closest_x = static_cast<double>(from.x()) + static_cast<double>(dx) * along;
      const auto closest_y = static_cast<double>(from.y()) + static_cast<double>(dy) * along;
      const auto distance_x = static_cast<double>(x) - closest_x;
      const auto distance_y = static_cast<double>(y) - closest_y;
      auto coverage = brush_shape_coverage(distance_x, distance_y, radius, brush.softness, brush.roundness,
                                           brush.angle_degrees);
      if (coverage <= 0.0F) {
        continue;
      }
      const QPoint document_point(x, y);
      if (!selection_allows(document_point)) {
        continue;
      }
      if (has_selection()) {
        coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
        if (coverage <= 0.0F) {
          continue;
        }
      }
      if (palette_snap != nullptr) {
        if (coverage < palette_snap->coverage_threshold) {
          continue;
        }
        coverage = 1.0F;
      }

      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        continue;
      }

      const auto local_x = x - bounds.x;
      auto* dst = row.data() + static_cast<std::size_t>(local_x) * channels;
      if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
        continue;
      }
      coverage = capped_stroke_coverage(document_point.x(), document_point.y(), coverage, opacity);
      if (coverage <= 0.0F) {
        continue;
      }

      std::array<std::uint8_t, 4> healed{};
      const auto* src = clone_source_cache_.constScanLine(source_point.y()) +
                        static_cast<std::size_t>(source_point.x()) * 4U;
      if (healing) {
        healed = healing_sample(clone_source_cache_, source_point, document_point, healing_tone_radius);
        src = healed.data();
      }
      const auto covered_opacity = opacity * coverage;
      if (channels >= 4 && !lock_transparent_pixels) {
        blend_straight_rgba(dst, src, covered_opacity);
      } else {
        const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
        if (effective_opacity <= 0.0F) {
          continue;
        }
        dst[0] = clamp_byte(static_cast<float>(src[0]) * effective_opacity +
                            static_cast<float>(dst[0]) * (1.0F - effective_opacity));
        dst[1] = clamp_byte(static_cast<float>(src[1]) * effective_opacity +
                            static_cast<float>(dst[1]) * (1.0F - effective_opacity));
        dst[2] = clamp_byte(static_cast<float>(src[2]) * effective_opacity +
                            static_cast<float>(dst[2]) * (1.0F - effective_opacity));
      }
      if (palette_snap != nullptr) {
        patchy::snap_pixel_to_palette(dst, channels, *palette_snap);
      }
      dirty = dirty.united(QRect(document_point, QSize(1, 1)));
    }
    tick_processing_operation();
  }
  return dirty;
}

}  // namespace patchy::ui
