#include "ui/brush_context_preview.hpp"

#include "core/brush_tip.hpp"
#include "core/splitmix64.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"
#include "ui/transparency_checkerboard.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace patchy::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
// The two tiles are this tall; the footprint tile is square.
constexpr int kTile = 72;
constexpr int kTileGap = 8;
constexpr int kTop = 2;
// Room left around a footprint drawn at its real size; a larger tip scales down to this.
constexpr int kFootprintInset = 10;
constexpr int kChecker = 6;
constexpr int kStrokeSamples = 96;
constexpr int kMaxBrushSizeForGlyph = 1024;

// A uniform draw in [-1, 1] from a fixed index, the same on every toolchain.
[[nodiscard]] double jitter_draw(std::uint64_t index) {
  const auto bits = patchy::splitmix64(0x7342ULL * 1000003ULL + index) >> 11U;
  return static_cast<double>(bits) / static_cast<double>(1ULL << 53U) * 2.0 - 1.0;
}

// Coverage of a footprint point `distance` from its center for a footprint of `radius`, as the
// procedural brush computes it (core brush_coverage: a smootherstep over the soft band), with a
// one-pixel antialiased edge for a hard footprint.
[[nodiscard]] double footprint_coverage(double distance, double radius, int softness) {
  if (softness <= 0) {
    return std::clamp(radius + 0.5 - distance, 0.0, 1.0);
  }
  if (distance > radius) {
    return 0.0;
  }
  const auto edge = std::max(1.0, radius * static_cast<double>(softness) / 100.0);
  const auto inner = std::max(0.0, radius - edge);
  if (distance <= inner) {
    return 1.0;
  }
  const auto t = std::clamp((distance - inner) / edge, 0.0, 1.0);
  return 1.0 - t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] QRgb ink_pixel(const QColor& ink, double coverage) {
  const auto alpha = static_cast<int>(std::lround(std::clamp(coverage, 0.0, 1.0) * ink.alphaF() * 255.0));
  return qPremultiply(qRgba(ink.red(), ink.green(), ink.blue(), alpha));
}

void paint_checker(QPainter& painter, const QRect& tile) {
  painter.save();
  painter.setClipRect(tile);
  painter.fillRect(tile, checkerboard_light_square());
  for (int y = tile.top(); y <= tile.bottom(); y += kChecker) {
    for (int x = tile.left(); x <= tile.right(); x += kChecker) {
      if ((((x - tile.left()) / kChecker) + ((y - tile.top()) / kChecker)) % 2 != 0) {
        painter.fillRect(QRect(x, y, kChecker, kChecker), checkerboard_dark_square());
      }
    }
  }
  painter.restore();
}

}  // namespace

bool BrushContextPreviewState::operator==(const BrushContextPreviewState& other) const {
  return size == other.size && softness == other.softness && roundness == other.roundness &&
         angle_degrees == other.angle_degrees && tip_shape == other.tip_shape && square == other.square &&
         tip == other.tip && ink == other.ink && smoothing_shown == other.smoothing_shown &&
         smoothing == other.smoothing;
}

BrushContextPreview::BrushContextPreview(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("canvasBrushPreview"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

BrushContextPreview::~BrushContextPreview() = default;

QSize BrushContextPreview::sizeHint() const {
  return {kTile + kTileGap + 220, kTile + 2 * kTop};
}

QSize BrushContextPreview::minimumSizeHint() const {
  return {kTile + kTileGap + 120, kTile + 2 * kTop};
}

void BrushContextPreview::set_state(const BrushContextPreviewState& state) {
  if (state == state_) {
    return;
  }
  state_ = state;
  state_.size = std::max(1, state_.size);
  state_.softness = std::clamp(state_.softness, 0, 100);
  state_.roundness = std::clamp(state_.roundness, 1, 100);
  state_.smoothing = std::clamp(state_.smoothing, 0, 100);
  dirty_ = true;
  update();
}

QRect BrushContextPreview::footprint_tile() const {
  return {0, kTop, kTile, kTile};
}

QRect BrushContextPreview::stroke_tile() const {
  return {kTile + kTileGap, kTop, std::max(kTile, width() - kTile - kTileGap), kTile};
}

double BrushContextPreview::displayed_diameter() const {
  return std::min(static_cast<double>(state_.size), static_cast<double>(kTile - 2 * kFootprintInset));
}

QSizeF BrushContextPreview::footprint_extent() const {
  const auto diameter = displayed_diameter();
  const auto flatten = state_.tip_shape ? static_cast<double>(state_.roundness) / 100.0 : 1.0;
  return {diameter, diameter * flatten};
}

QImage BrushContextPreview::render_tip(double diameter_device) const {
  const auto diameter = std::max(1.0, diameter_device);
  const auto flatten = state_.tip_shape ? std::max(0.01, static_cast<double>(state_.roundness) / 100.0) : 1.0;
  const auto angle = state_.tip_shape ? state_.angle_degrees : 0.0;
  if (state_.tip != nullptr && mips_ != nullptr && !mips_->empty()) {
    // A bitmap tip: the engine's own scaled stamp and Soft feather, turned and flattened.
    auto scaled = patchy::make_scaled_brush_tip(*mips_, std::max(1, static_cast<int>(std::lround(diameter))));
    if (scaled.empty()) {
      return {};
    }
    const auto feather = static_cast<int>(std::lround(diameter * static_cast<double>(state_.softness) / 400.0));
    if (feather > 0) {
      patchy::soften_scaled_brush_tip(scaled, feather);
    }
    QImage mask(scaled.width, scaled.height, QImage::Format_ARGB32_Premultiplied);
    for (std::int32_t y = 0; y < scaled.height; ++y) {
      auto* line = reinterpret_cast<QRgb*>(mask.scanLine(y));
      for (std::int32_t x = 0; x < scaled.width; ++x) {
        line[x] = ink_pixel(state_.ink,
                            scaled.mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(scaled.width) + x] / 255.0);
      }
    }
    const auto side = static_cast<int>(std::ceil(std::max(scaled.width, scaled.height) * 1.5)) + 2;
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(side / 2.0, side / 2.0);
    painter.rotate(angle);
    painter.scale(1.0, flatten);
    painter.drawImage(QPointF(-scaled.anchor_x, -scaled.anchor_y), mask);
    return image;
  }
  // The procedural round or square footprint, sampled per pixel.
  const auto radius = diameter / 2.0;
  const auto side = static_cast<int>(std::ceil(diameter * 1.5)) + 2;
  QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
  const auto center = side / 2.0;
  const auto radians = angle * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  for (int y = 0; y < side; ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < side; ++x) {
      const auto dx = x + 0.5 - center;
      const auto dy = y + 0.5 - center;
      // Back through the turn QPainter::rotate(angle) makes, then undo the flattening.
      const auto u = dx * cosine + dy * sine;
      const auto v = (-dx * sine + dy * cosine) / flatten;
      double coverage = 0.0;
      if (state_.square) {
        coverage = state_.softness <= 0
                       ? std::min(std::clamp(radius + 0.5 - std::abs(u), 0.0, 1.0),
                                  std::clamp(radius + 0.5 - std::abs(v), 0.0, 1.0))
                       : footprint_coverage(std::max(std::abs(u), std::abs(v)), radius, state_.softness);
      } else {
        coverage = footprint_coverage(std::hypot(u, v), radius, state_.softness);
      }
      line[x] = ink_pixel(state_.ink, coverage);
    }
  }
  return image;
}

void BrushContextPreview::render_now(double device_pixel_ratio) {
  dirty_ = true;
  ensure_rendered(device_pixel_ratio);
}

void BrushContextPreview::ensure_rendered(double device_pixel_ratio) {
  const auto ratio = std::max(1.0, device_pixel_ratio);
  const auto stroke_size = stroke_tile().size();
  if (!dirty_ && ratio == rendered_ratio_ && stroke_size == rendered_stroke_size_) {
    return;
  }
  dirty_ = false;
  rendered_ratio_ = ratio;
  rendered_stroke_size_ = stroke_size;
  ++render_count_;
  if (state_.tip.get() != mips_tip_) {
    mips_tip_ = state_.tip.get();
    mips_ = state_.tip != nullptr
                ? std::make_shared<const patchy::BrushTipMipChain>(patchy::build_brush_tip_mips(*state_.tip))
                : nullptr;
  }

  footprint_ = render_tip(displayed_diameter() * ratio);
  footprint_.setDevicePixelRatio(ratio);

  // The sample stroke: a gentle wave across the tile, stamped with the footprint at a size that
  // fits the tile. A tool with Smooth paints it from a jittery pointer path, pulled toward the
  // clean wave as Smooth rises.
  const auto width = static_cast<double>(stroke_size.width());
  const auto height = static_cast<double>(stroke_size.height());
  const auto dab_diameter = std::clamp(displayed_diameter(), 2.0, height * 0.42);
  const auto margin = dab_diameter / 2.0 + 6.0;
  const auto amplitude = std::max(0.0, (height / 2.0 - margin - 2.0) * 0.75);
  const auto jitter = std::min(6.0, height / 10.0);
  const auto pull = state_.smoothing_shown ? static_cast<double>(state_.smoothing) / 100.0 : 1.0;
  QPolygonF stroke_path;
  raw_path_.clear();
  for (int index = 0; index <= kStrokeSamples; ++index) {
    const auto t = static_cast<double>(index) / kStrokeSamples;
    const QPointF clean(margin + (width - 2.0 * margin) * t, height / 2.0 - std::sin(t * 2.0 * kPi) * amplitude);
    // Two draws averaged: a hand's wobble, not white noise. The ends stay put.
    const auto wobble = (jitter_draw(static_cast<std::uint64_t>(index)) +
                         jitter_draw(static_cast<std::uint64_t>(index) + 977U)) *
                        0.5 * jitter * std::sin(t * kPi);
    const QPointF raw(clean.x(), clean.y() + wobble);
    raw_path_.push_back(raw);
    stroke_path.push_back(raw + (clean - raw) * pull);
  }
  stroke_ = QImage(static_cast<int>(std::ceil(width * ratio)), static_cast<int>(std::ceil(height * ratio)),
                   QImage::Format_ARGB32_Premultiplied);
  stroke_.fill(Qt::transparent);
  stroke_.setDevicePixelRatio(ratio);
  auto dab = render_tip(dab_diameter * ratio);
  if (!dab.isNull()) {
    dab.setDevicePixelRatio(ratio);
    const QSizeF dab_size(dab.width() / ratio, dab.height() / ratio);
    QPainter painter(&stroke_);
    const auto spacing = std::max(0.75, dab_diameter * 0.12);
    double carry = 0.0;
    painter.drawImage(stroke_path.front() - QPointF(dab_size.width() / 2.0, dab_size.height() / 2.0), dab);
    for (int index = 1; index < stroke_path.size(); ++index) {
      const auto from = stroke_path[index - 1];
      const auto to = stroke_path[index];
      const auto length = std::hypot(to.x() - from.x(), to.y() - from.y());
      auto at = spacing - carry;
      while (at <= length) {
        const auto point = from + (to - from) * (at / std::max(length, 1e-9));
        painter.drawImage(point - QPointF(dab_size.width() / 2.0, dab_size.height() / 2.0), dab);
        at += spacing;
      }
      carry = length - (at - spacing);
    }
  }
}

void BrushContextPreview::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  ensure_rendered(painter.device() != nullptr ? painter.device()->devicePixelRatioF() : devicePixelRatioF());
  const auto& colors = theme();

  const auto footprint = footprint_tile();
  paint_checker(painter, footprint);
  if (!footprint_.isNull()) {
    const QSizeF size(footprint_.width() / footprint_.devicePixelRatio(),
                      footprint_.height() / footprint_.devicePixelRatio());
    painter.drawImage(QPointF(footprint.center().x() + 0.5 - size.width() / 2.0,
                              footprint.center().y() + 0.5 - size.height() / 2.0),
                      footprint_);
  }
  const auto stroke = stroke_tile();
  paint_checker(painter, stroke);
  if (!stroke_.isNull()) {
    painter.drawImage(stroke.topLeft(), stroke_);
  }
  painter.setRenderHint(QPainter::Antialiasing);
  if (state_.smoothing_shown && raw_path_.size() > 1) {
    // The pointer path the stroke smooths: a light line in a dark casing, as the canvas
    // readouts draw over artwork, so it reads over the paint in any color.
    const auto path = raw_path_.translated(stroke.topLeft());
    painter.setBrush(Qt::NoBrush);
    auto casing = colors.canvas_hud_bg;
    casing.setAlpha(170);
    QPen casing_pen(casing, 3.0);
    casing_pen.setJoinStyle(Qt::RoundJoin);
    casing_pen.setCapStyle(Qt::RoundCap);
    painter.setPen(casing_pen);
    painter.drawPolyline(path);
    QPen line_pen(colors.canvas_hud_text, 1.25);
    line_pen.setJoinStyle(Qt::RoundJoin);
    line_pen.setCapStyle(Qt::RoundCap);
    painter.setPen(line_pen);
    painter.drawPolyline(path);
  }

  // The size on a chip in the footprint tile's corner, as the canvas readouts show it.
  auto chip_font = font();
  chip_font.setPointSizeF(std::max(6.0, chip_font.pointSizeF() * 0.85));
  painter.setFont(chip_font);
  const auto text = QString::number(state_.size) + pixel_suffix();
  const QFontMetricsF metrics(chip_font);
  const QSizeF chip_size(metrics.horizontalAdvance(text) + 8.0, metrics.height() + 2.0);
  const QRectF chip(footprint.right() + 1.0 - 3.0 - chip_size.width(), footprint.bottom() + 1.0 - 3.0 - chip_size.height(),
                    chip_size.width(), chip_size.height());
  painter.setPen(QPen(colors.canvas_hud_border, 1.0));
  painter.setBrush(colors.canvas_hud_bg);
  painter.drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
  painter.setPen(colors.canvas_hud_text);
  painter.drawText(chip, Qt::AlignCenter, text);

  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(colors.swatch_border, 1.0));
  painter.drawRect(footprint.adjusted(0, 0, -1, -1));
  painter.drawRect(stroke.adjusted(0, 0, -1, -1));
}

BrushSettingGlyph::BrushSettingGlyph(BrushSettingGlyphKind kind, int value, QWidget* parent)
    : QWidget(parent), kind_(kind), value_(value) {
  setFixedSize(sizeHint());
  setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize BrushSettingGlyph::sizeHint() const {
  return {16, 16};
}

void BrushSettingGlyph::set_value(int value) {
  if (value_ == value) {
    return;
  }
  value_ = value;
  update();
}

void BrushSettingGlyph::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto& colors = theme();
  auto ink = isEnabled() ? colors.text_primary : colors.text_disabled;
  const QPointF center(width() / 2.0, height() / 2.0);
  constexpr double kRadius = 6.5;
  switch (kind_) {
    case BrushSettingGlyphKind::Size: {
      // The dot grows with the size on the same curve the slider uses at its low end.
      const auto fraction = std::clamp(std::log(static_cast<double>(std::max(1, value_))) /
                                           std::log(static_cast<double>(kMaxBrushSizeForGlyph)),
                                       0.0, 1.0);
      const auto radius = 1.5 + (kRadius - 1.5) * fraction;
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawEllipse(center, radius, radius);
      break;
    }
    case BrushSettingGlyphKind::Softness: {
      QRadialGradient gradient(center, kRadius);
      const auto hard = std::clamp(1.0 - static_cast<double>(value_) / 100.0, 0.0, 0.98);
      auto clear = ink;
      clear.setAlpha(0);
      gradient.setColorAt(0.0, ink);
      gradient.setColorAt(hard, ink);
      gradient.setColorAt(1.0, clear);
      painter.setPen(Qt::NoPen);
      painter.setBrush(gradient);
      painter.drawEllipse(center, kRadius, kRadius);
      break;
    }
    case BrushSettingGlyphKind::Roundness: {
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawEllipse(center, kRadius, std::max(1.0, kRadius * std::clamp(value_, 1, 100) / 100.0));
      break;
    }
    case BrushSettingGlyphKind::Angle: {
      // A faint level line and the tip's long axis turned by the angle, as the cursor turns it.
      auto faint = ink;
      faint.setAlphaF(ink.alphaF() * 0.35);
      painter.setPen(QPen(faint, 1.0));
      painter.drawLine(QPointF(center.x() - kRadius, center.y()), QPointF(center.x() + kRadius, center.y()));
      painter.translate(center);
      painter.rotate(value_);
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(0.0, 0.0), kRadius + 0.5, 2.25);
      break;
    }
    case BrushSettingGlyphKind::Smoothing: {
      // A zigzag that straightens as Smooth rises.
      const auto wobble = 4.0 * (1.0 - std::clamp(value_, 0, 100) / 100.0);
      QPainterPath path(QPointF(1.5, center.y()));
      for (int step = 1; step <= 6; ++step) {
        const auto x = 1.5 + step * 13.0 / 6.0;
        const auto y = center.y() + (step % 2 == 0 ? wobble : -wobble) * (step == 6 ? 0.0 : 1.0);
        path.lineTo(x, y);
      }
      QPen pen(ink, 1.6);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(path);
      break;
    }
  }
}

}  // namespace patchy::ui
