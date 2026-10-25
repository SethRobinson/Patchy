#include "ui/raw_develop_histogram.hpp"

#include "filters/auto_levels_math.hpp"
#include "formats/raw_document_io.hpp"
#include "ui/curves_editor.hpp"
#include "ui/theme_palette.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

// Keeps the fit from trading large opposite slider moves for a tiny gain.
constexpr double kSliderPenalty = 1e-4;

double srgb_to_linear(double value) {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double square(double value) { return value * value; }

}  // namespace

RawPreviewHistogram raw_preview_histogram(std::span<const std::uint8_t> interleaved_rgb, int width, int height) {
  RawPreviewHistogram histogram;
  if (width <= 0 || height <= 0) return histogram;
  const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (interleaved_rgb.size() < pixels * 3) return histogram;
  std::uint64_t step = 1;
  while (pixels / (step * step) > kRawHistogramMaxSamples) ++step;
  for (std::uint64_t y = 0; y < static_cast<std::uint64_t>(height); y += step) {
    const auto* row = interleaved_rgb.data() + y * static_cast<std::uint64_t>(width) * 3;
    for (std::uint64_t x = 0; x < static_cast<std::uint64_t>(width); x += step) {
      const auto* pixel = row + x * 3;
      const auto r = pixel[0];
      const auto g = pixel[1];
      const auto b = pixel[2];
      ++histogram.red[r];
      ++histogram.green[g];
      ++histogram.blue[b];
      ++histogram.luminance[(30U * r + 59U * g + 11U * b + 50U) / 100U];
      ++histogram.samples;
    }
  }
  return histogram;
}

std::pair<double, double> raw_histogram_region_span(RawHistogramRegion region) {
  switch (region) {
    case RawHistogramRegion::Shadows:
      return {0.0, 0.30};
    case RawHistogramRegion::Exposure:
      return {0.30, 0.65};
    case RawHistogramRegion::Highlights:
      return {0.65, 1.0};
    case RawHistogramRegion::None:
      break;
  }
  return {0.0, 0.0};
}

RawHistogramRegion raw_histogram_region_at(double fraction) {
  if (!(fraction >= 0.0) || fraction > 1.0) return RawHistogramRegion::None;
  for (const auto region : {RawHistogramRegion::Shadows, RawHistogramRegion::Exposure}) {
    if (fraction < raw_histogram_region_span(region).second) return region;
  }
  return RawHistogramRegion::Highlights;
}

QString raw_histogram_region_token(RawHistogramRegion region) {
  switch (region) {
    case RawHistogramRegion::Shadows:
      return QStringLiteral("shadows");
    case RawHistogramRegion::Exposure:
      return QStringLiteral("exposure");
    case RawHistogramRegion::Highlights:
      return QStringLiteral("highlights");
    case RawHistogramRegion::None:
      break;
  }
  return QStringLiteral("none");
}

RawToneModel::RawToneModel(raw::RenderingProfile profile, int processing_version) {
  if (profile == raw::RenderingProfile::Natural) {
    const auto lut = raw::build_natural_profile_lut(processing_version);
    profile_lut_.assign(lut.begin(), lut.end());
  }
}

double RawToneModel::profile(double value) const {
  const auto clamped = std::clamp(value, 0.0, 1.0);
  if (profile_lut_.empty()) return clamped;
  return profile_lut_[static_cast<std::size_t>(std::lround(clamped * 65535.0))] / 65535.0;
}

double RawToneModel::before_profile(double output) const {
  const auto clamped = std::clamp(output, 0.0, 1.0);
  if (profile_lut_.empty()) return clamped;
  const auto code = static_cast<std::uint16_t>(std::lround(clamped * 65535.0));
  const auto found = std::lower_bound(profile_lut_.begin(), profile_lut_.end(), code);
  return static_cast<double>(found - profile_lut_.begin()) / 65535.0;
}

double RawToneModel::expose(double value, double exposure_ev) {
  // raw_document_io turns exposure correction off within 0.001 EV of zero.
  if (std::abs(exposure_ev) <= 1e-3) return std::clamp(value, 0.0, 1.0);
  const auto shift = std::clamp(std::exp2(exposure_ev), raw::kMinExposureShift, raw::kMaxExposureShift);
  // LibRaw's exp_bef on its 16-bit table (TBLN = 65535): linear below x1, then
  // a cube-root shoulder that keeps some highlight detail when brightening.
  constexpr double kTable = 65535.0;
  const auto linear = srgb_to_linear(std::clamp(value, 0.0, 1.0)) * kTable;
  double exposed = linear * shift;
  if (shift > 1.0) {
    const auto smooth = raw::kExposureHighlightPreservation;
    const auto room_linear = std::exp2(std::log2(shift) * 2.0);
    const auto x2 = kTable;
    const auto x1 = (x2 + 1.0) / room_linear - 1.0;
    const auto y1 = x1 * shift;
    const auto y2 = x2 * (1.0 + (1.0 - smooth) * (shift - 1.0));
    const auto cube_root = std::cbrt(x1 * x1 * x2);
    const auto b = (y2 - y1 + shift * (3.0 * x1 - 3.0 * cube_root)) / (x2 + 2.0 * x1 - 3.0 * cube_root);
    const auto a = (shift - b) * 3.0 * std::cbrt(x1 * x1);
    const auto offset = y2 - a * std::cbrt(x2) - b * x2;
    if (linear >= x1) exposed = a * std::cbrt(linear) + b * linear + offset;
  }
  return linear_to_srgb(std::clamp(exposed / kTable, 0.0, 1.0));
}

double RawToneModel::map(double baseline, const RawToneAutoValues& values) const {
  const auto exposed = profile(expose(before_profile(baseline), values.exposure_ev));
  return raw::tone_curve_value(exposed, raw::ToneParams{values.contrast, values.highlights, values.shadows});
}

std::optional<RawToneAutoValues> raw_auto_tone(const RawPreviewHistogram& baseline, const RawToneModel& model) {
  if (baseline.samples == 0) return std::nullopt;
  const auto clip = auto_levels_clip_scan(baseline.luminance, baseline.samples);
  if (!clip) return std::nullopt;
  const auto black = clip->black_input / 255.0;
  const auto white = clip->white_input / 255.0;
  struct Bin {
    double weight;
    double target;
    double before_profile;
  };
  std::vector<Bin> bins;
  for (std::size_t code = 0; code < baseline.luminance.size(); ++code) {
    const auto count = baseline.luminance[code];
    if (count == 0) continue;
    const auto value = static_cast<double>(code) / 255.0;
    bins.push_back({static_cast<double>(count) / static_cast<double>(baseline.samples),
                    std::clamp((value - black) / (white - black), 0.0, 1.0), model.before_profile(value)});
  }
  // Each bin after Exposure and the profile curve; the Tone curve comes last.
  const auto exposed_for = [&](double exposure_ev) {
    std::vector<double> exposed(bins.size());
    for (std::size_t index = 0; index < bins.size(); ++index)
      exposed[index] = model.profile(RawToneModel::expose(bins[index].before_profile, exposure_ev));
    return exposed;
  };
  const auto cost_of = [&](const std::vector<double>& exposed, const RawToneAutoValues& values) {
    const raw::ToneParams tone{values.contrast, values.highlights, values.shadows};
    auto cost = kSliderPenalty * (square(values.exposure_ev / 5.0) + square(values.contrast / 200.0) +
                                  square(values.highlights / 200.0) + square(values.shadows / 200.0));
    for (std::size_t index = 0; index < bins.size(); ++index)
      cost += bins[index].weight * square(raw::tone_curve_value(exposed[index], tone) - bins[index].target);
    return cost;
  };

  // Coordinate descent over the sliders' own steps, in a fixed order, so the
  // same baseline always gives the same values.
  RawToneAutoValues best;
  auto exposed = exposed_for(best.exposure_ev);
  auto best_cost = cost_of(exposed, best);
  for (int round = 0; round < 16; ++round) {
    bool changed = false;
    for (int step = -200; step <= 300; ++step) {
      auto candidate = best;
      candidate.exposure_ev = step / 100.0;
      if (candidate == best) continue;
      auto candidate_exposed = exposed_for(candidate.exposure_ev);
      const auto cost = cost_of(candidate_exposed, candidate);
      if (cost < best_cost) {
        best = candidate;
        best_cost = cost;
        exposed = std::move(candidate_exposed);
        changed = true;
      }
    }
    for (auto member : {&RawToneAutoValues::contrast, &RawToneAutoValues::highlights, &RawToneAutoValues::shadows}) {
      for (int step = -100; step <= 100; ++step) {
        auto candidate = best;
        candidate.*member = step;
        if (candidate == best) continue;
        const auto cost = cost_of(exposed, candidate);
        if (cost < best_cost) {
          best = candidate;
          best_cost = cost;
          changed = true;
        }
      }
    }
    if (!changed) break;
  }
  return best;
}

namespace {

constexpr double kClipShare = 0.001;
constexpr double kMidtoneTarget = 128.0 / 255.0;

// Same shape as the tone curve's (formats/raw_tone.cpp), so the auto values aim
// at where the sliders actually act.
double smoothstep(double edge0, double edge1, double x) {
  const auto t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

struct LuminanceScan {
  int black{0};
  int white{255};
  double mean{0.0};
  double bottom_share{0.0};
  double top_share{0.0};
};

std::optional<LuminanceScan> scan_luminance(const RawPreviewHistogram& histogram) {
  if (histogram.samples == 0) return std::nullopt;
  const auto clip = auto_levels_clip_scan(histogram.luminance, histogram.samples);
  if (!clip) return std::nullopt;
  std::uint64_t weighted = 0;
  for (std::size_t bin = 0; bin < histogram.luminance.size(); ++bin) weighted += bin * histogram.luminance[bin];
  const auto samples = static_cast<double>(histogram.samples);
  LuminanceScan scan;
  scan.black = clip->black_input;
  scan.white = clip->white_input;
  scan.mean = static_cast<double>(weighted) / (255.0 * samples);
  scan.bottom_share = histogram.luminance.front() / samples;
  scan.top_share = histogram.luminance.back() / samples;
  return scan;
}

double clamp_round(double value, double minimum, double maximum, double scale) {
  return std::clamp(std::round(value * scale) / scale, minimum, maximum);
}

}  // namespace

std::optional<double> raw_auto_exposure_ev(const RawPreviewHistogram& histogram, double rendered) {
  const auto scan = scan_luminance(histogram);
  if (!scan) return std::nullopt;
  const auto mean_linear = srgb_to_linear(scan->mean);
  if (!(mean_linear > 0.0)) return std::nullopt;
  auto delta = std::log2(srgb_to_linear(kMidtoneTarget) / mean_linear);
  if (delta > 0.0) {
    const auto headroom = scan->white < 255 ? std::log2(1.0 / srgb_to_linear(scan->white / 255.0)) : 0.0;
    delta = std::min(delta, headroom);
  } else if (scan->top_share <= kClipShare) {
    // A bright scene that does not clip keeps its exposure (snow stays white).
    delta = 0.0;
  }
  return clamp_round(rendered + delta, -2.0, 3.0, 100.0);
}

std::optional<double> raw_auto_contrast(const RawPreviewHistogram& histogram, double rendered) {
  const auto scan = scan_luminance(histogram);
  if (!scan) return std::nullopt;
  const auto spread = static_cast<double>(scan->white - scan->black) / 255.0;
  // The smoothstep S-curve's slope at mid gray is 1 + amount / 2; Auto
  // Contrast's stretch has slope 1 / spread.
  const auto amount = spread < 1.0 ? std::min(1.0, 2.0 * (1.0 / spread - 1.0)) : 0.0;
  return clamp_round(rendered + amount * 100.0, -100.0, 100.0, 1.0);
}

std::optional<double> raw_auto_highlights(const RawPreviewHistogram& histogram, double rendered) {
  const auto scan = scan_luminance(histogram);
  if (!scan) return std::nullopt;
  double amount = 0.0;
  if (scan->top_share > kClipShare) {
    amount = -std::min(1.0, scan->top_share * 20.0);
  } else {
    const auto x = scan->white / 255.0;
    const auto weight = smoothstep(0.35, 0.85, x);
    if (x < 1.0 && weight > 0.05) amount = std::min(1.0, (1.0 - x) / (0.25 * weight));
  }
  return clamp_round(rendered + amount * 100.0, -100.0, 100.0, 1.0);
}

std::optional<double> raw_auto_shadows(const RawPreviewHistogram& histogram, double rendered) {
  const auto scan = scan_luminance(histogram);
  if (!scan) return std::nullopt;
  double amount = 0.0;
  if (scan->bottom_share > kClipShare) {
    amount = std::min(1.0, scan->bottom_share * 20.0);
  } else {
    const auto x = scan->black / 255.0;
    const auto weight = smoothstep(0.0, 0.15, x) * (1.0 - smoothstep(0.15, 0.65, x));
    if (x > 0.0 && weight > 0.05) amount = -std::min(1.0, x / (0.30 * weight));
  }
  return clamp_round(rendered + amount * 100.0, -100.0, 100.0, 1.0);
}

RawHistogramWidget::RawHistogramWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setCursor(Qt::SizeHorCursor);
  setToolTip(QObject::tr("Point at the histogram to see which setting that part adjusts, then drag left or right to change it."));
  publish_state();
}

QSize RawHistogramWidget::sizeHint() const { return {256, 104}; }

QSize RawHistogramWidget::minimumSizeHint() const { return {160, 104}; }

QRect RawHistogramWidget::plot_rect() const { return rect().adjusted(1, 1, -1, -1); }

void RawHistogramWidget::set_histogram(std::optional<RawPreviewHistogram> histogram) {
  histogram_ = std::move(histogram);
  publish_state();
  update();
}

void RawHistogramWidget::set_hover_callback(std::function<void(RawHistogramRegion)> callback) {
  hover_changed_ = std::move(callback);
}

void RawHistogramWidget::set_drag_callback(
    std::function<void(RawHistogramRegion, double, RawHistogramDragPhase)> callback) {
  dragged_ = std::move(callback);
}

RawHistogramRegion RawHistogramWidget::region_at(QPointF position) const {
  const auto plot = plot_rect();
  if (plot.width() <= 0 || !QRectF(rect()).contains(position)) return RawHistogramRegion::None;
  return raw_histogram_region_at(std::clamp((position.x() - plot.left()) / plot.width(), 0.0, 1.0));
}

void RawHistogramWidget::set_hovered(RawHistogramRegion region) {
  if (region == hovered_) return;
  hovered_ = region;
  publish_state();
  update();
  if (hover_changed_) hover_changed_(hovered_);
}

void RawHistogramWidget::publish_state() {
  setProperty("rawHistogramRegion", raw_histogram_region_token(hovered_));
  setProperty("rawHistogramDragging", dragging_ != RawHistogramRegion::None);
  setProperty("rawHistogramSamples", histogram_ ? static_cast<qulonglong>(histogram_->samples) : 0ULL);
}

void RawHistogramWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QWidget::mousePressEvent(event);
    return;
  }
  const auto region = region_at(event->position());
  set_hovered(region);
  if (region == RawHistogramRegion::None) return;
  dragging_ = region;
  press_x_ = event->position().x();
  last_travel_ = 0.0;
  publish_state();
  update();
  if (dragged_) dragged_(region, 0.0, RawHistogramDragPhase::Begin);
  event->accept();
}

void RawHistogramWidget::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_ != RawHistogramRegion::None) {
    last_travel_ = (event->position().x() - press_x_) / std::max(1, plot_rect().width());
    if (dragged_) dragged_(dragging_, last_travel_, RawHistogramDragPhase::Move);
    event->accept();
    return;
  }
  set_hovered(region_at(event->position()));
  QWidget::mouseMoveEvent(event);
}

void RawHistogramWidget::finish_drag(double travel, QPointF position) {
  const auto region = dragging_;
  dragging_ = RawHistogramRegion::None;
  publish_state();
  if (dragged_) dragged_(region, travel, RawHistogramDragPhase::End);
  set_hovered(region_at(position));
  update();
}

void RawHistogramWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && dragging_ != RawHistogramRegion::None) {
    finish_drag((event->position().x() - press_x_) / std::max(1, plot_rect().width()), event->position());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void RawHistogramWidget::leaveEvent(QEvent* event) {
  if (dragging_ == RawHistogramRegion::None) set_hovered(RawHistogramRegion::None);
  QWidget::leaveEvent(event);
}

void RawHistogramWidget::changeEvent(QEvent* event) {
  // Disabling mid-drag (Open locks the controls) still ends the gesture, so the
  // host's slider is never left held down.
  if (event->type() == QEvent::EnabledChange && !isEnabled() && dragging_ != RawHistogramRegion::None)
    finish_drag(last_travel_, QPointF(-1.0, -1.0));
  QWidget::changeEvent(event);
}

void RawHistogramWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  const auto& palette = theme();
  painter.fillRect(rect(), palette.preview_pane_bg);
  const auto plot = plot_rect();

  const auto active = dragging_ != RawHistogramRegion::None ? dragging_ : hovered_;
  if (active != RawHistogramRegion::None && isEnabled()) {
    const auto [begin, end] = raw_histogram_region_span(active);
    auto highlight = palette.accent_control;
    highlight.setAlpha(dragging_ != RawHistogramRegion::None ? 80 : 50);
    const auto left = plot.left() + static_cast<int>(std::lround(begin * plot.width()));
    const auto right = plot.left() + static_cast<int>(std::lround(end * plot.width()));
    painter.fillRect(QRect(left, plot.top(), right - left, plot.height()), highlight);
  }

  if (histogram_ && histogram_->samples > 0 && plot.width() > 0) {
    const auto width = plot.width();
    const auto column_count = [&](const std::array<std::uint32_t, 256>& bins, int x) {
      const auto first = std::clamp((x * 256) / width, 0, 255);
      const auto last = std::clamp(((x + 1) * 256) / width, first + 1, 256);
      std::uint32_t count = 0;
      for (int bin = first; bin < last; ++bin) count = std::max(count, bins[static_cast<std::size_t>(bin)]);
      return count;
    };
    const auto ceiling_for = [](const std::array<std::uint32_t, 256>& bins) {
      std::uint64_t total = 0;
      std::uint64_t non_empty = 0;
      for (const auto count : bins) {
        total += count;
        non_empty += count > 0 ? 1 : 0;
      }
      return histogram_display_ceiling(total, non_empty);
    };
    const auto bar_height = [&](std::uint32_t count, double ceiling) {
      if (count == 0 || !(ceiling > 0.0)) return 0;
      return std::max(1, static_cast<int>(std::lround(histogram_display_fraction(count, ceiling) *
                                                      static_cast<double>(plot.height() - 1))));
    };
    // The channel colors depict the channels themselves, like the Curves and
    // Levels histograms; they are data, not chrome, so they stay fixed.
    struct Channel {
      const std::array<std::uint32_t, 256>* bins;
      QColor color;
    };
    const std::array<Channel, 3> channels = {{{&histogram_->red, QColor(230, 60, 60, 95)},
                                              {&histogram_->green, QColor(60, 200, 90, 95)},
                                              {&histogram_->blue, QColor(70, 120, 240, 95)}}};
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const auto& channel : channels) {
      const auto ceiling = ceiling_for(*channel.bins);
      for (int x = 0; x < width; ++x) {
        const auto height = bar_height(column_count(*channel.bins, x), ceiling);
        if (height > 0) painter.fillRect(QRect(plot.left() + x, plot.bottom() - height + 1, 1, height), channel.color);
      }
    }
    const auto luminance_ceiling = ceiling_for(histogram_->luminance);
    QPainterPath outline;
    outline.moveTo(plot.left(), plot.bottom() + 1);
    for (int x = 0; x < width; ++x) {
      const auto top = plot.bottom() + 1 - bar_height(column_count(histogram_->luminance, x), luminance_ceiling);
      outline.lineTo(plot.left() + x, top);
      outline.lineTo(plot.left() + x + 1, top);
    }
    outline.lineTo(plot.left() + width, plot.bottom() + 1);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette.text_secondary, 1.0));
    painter.drawPath(outline);
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
  auto divider = palette.preview_pane_muted_text;
  divider.setAlpha(70);
  painter.setPen(QPen(divider, 1.0, Qt::DotLine));
  for (const auto region : {RawHistogramRegion::Shadows, RawHistogramRegion::Exposure}) {
    const auto x = plot.left() + static_cast<int>(std::lround(raw_histogram_region_span(region).second * plot.width()));
    painter.drawLine(x, plot.top(), x, plot.bottom());
  }
  painter.setPen(QPen(palette.field_inset_border, 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

}  // namespace patchy::ui
