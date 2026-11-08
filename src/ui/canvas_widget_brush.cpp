// CanvasWidget's brush implementation, split out of canvas_widget.cpp: the
// brush settings and tip cache (set_brush_* plus the scaled-tip LRU and the
// Round-dynamics disc tip), the brush cursor and hover-outline machinery, the
// Alt+Right-drag brush adjust gesture and its overlay painters, and the stroke
// engine (stroke tracking, axis constraint, smoothing, dab stamping, the
// per-stroke snapshot compositor, mask painting, smudge, and clone). Pure
// function moves from canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"

#include "core/artboard.hpp"
#include "core/blend_math.hpp"
#include "core/brush_dynamics.hpp"
#include "core/brush_tip.hpp"
#include "core/clone_source_transform.hpp"
#include "core/layer.hpp"
#include "core/palette.hpp"
#include "core/pixel_buffer.hpp"
#include "core/pixel_depth.hpp"
#include "core/pixel_tools.hpp"
#include "core/smart_object.hpp"
#include "core/stroke_stabilizer.hpp"
#include "core/worker_budget.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/dodge_burn_curves.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/theme_palette.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QImage>
#include <QPainter>
#include <QLine>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QRect>
#include <QRectF>
#include <QRgb>
#include <QScopeGuard>
#include <QSize>
#include <QTransform>
#include <QString>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kMaxBrushStampSpacing = 64.0;

int positive_modulo(int value, int divisor) noexcept {
  const auto result = value % divisor;
  return result < 0 ? result + divisor : result;
}

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

// The columns a Clone/Healing stamp segment visits in each row (GitHub issue 597: a 529 px
// brush walked its whole bounding box for every mouse sample). The capsule of radius r
// around from..to is convex, so a row meets it in one run, padded a pixel as
// paint_brush_segment pads it; pixels outside have coverage 0. When the segment continues
// the previous one from its end with the same round brush (radius, softness and opacity
// equal, roundness at least 99), two parts of that run cannot change: the half-plane behind
// `from`, where `from` is every pixel's closest point, so it gets exactly the coverage the
// previous segment's end gave it, and the disk around `from` the brush covers fully
// (shrunk a pixel against rounding). The stroke's alpha cap already holds those targets,
// so capped_stroke_coverage would return 0 there without touching the cap; the bytes, the
// caps and the dirty rect are the same as the full walk's.
class CloneSegmentRows {
 public:
  struct Run {
    int first{0};
    int end{0};  // exclusive
  };
  static constexpr std::size_t kMaxRuns = 3;

  CloneSegmentRows(QPoint from, QPoint to, int radius, bool walk_spans, bool continues_previous,
                   double full_coverage_radius)
      : from_(from), to_(to), dx_(to.x() - from.x()), dy_(to.y() - from.y()), radius_(static_cast<double>(radius)),
        walk_spans_(walk_spans), continues_previous_(continues_previous),
        covered_radius_(full_coverage_radius - 1.0) {
    const auto length = std::sqrt(static_cast<double>(dx_ * dx_ + dy_ * dy_));
    has_band_ = length > std::numeric_limits<double>::epsilon();
    const auto normal_x = has_band_ ? -static_cast<double>(dy_) / length * radius_ : 0.0;
    const auto normal_y = has_band_ ? static_cast<double>(dx_) / length * radius_ : 0.0;
    const auto x0 = static_cast<double>(from.x());
    const auto y0 = static_cast<double>(from.y());
    const auto x1 = static_cast<double>(to.x());
    const auto y1 = static_cast<double>(to.y());
    band_ = {std::pair{x0 + normal_x, y0 + normal_y}, std::pair{x1 + normal_x, y1 + normal_y},
             std::pair{x1 - normal_x, y1 - normal_y}, std::pair{x0 - normal_x, y0 - normal_y}};
  }

  // The runs of row y within columns [left, right]; returns how many were written.
  std::size_t runs(int y, int left, int right, std::array<Run, kMaxRuns>& out) const {
    int first = left;
    int end = right + 1;
    if (walk_spans_) {
      const auto row_y = static_cast<double>(y);
      auto span_low = std::numeric_limits<double>::infinity();
      auto span_high = -std::numeric_limits<double>::infinity();
      const auto radius_squared = radius_ * radius_;
      const auto add_disk = [&](QPoint center) {
        const auto row_offset = row_y - static_cast<double>(center.y());
        if (std::abs(row_offset) > radius_ + 1.0) {
          return;
        }
        const auto half_width = std::sqrt(std::max(0.0, radius_squared - row_offset * row_offset));
        span_low = std::min(span_low, static_cast<double>(center.x()) - half_width);
        span_high = std::max(span_high, static_cast<double>(center.x()) + half_width);
      };
      add_disk(from_);
      add_disk(to_);
      if (has_band_) {
        for (std::size_t edge = 0; edge < band_.size(); ++edge) {
          const auto [from_x, from_y] = band_[edge];
          const auto [to_x, to_y] = band_[(edge + 1) % band_.size()];
          if ((from_y - row_y) * (to_y - row_y) > 0.0) {
            continue;
          }
          if (from_y == to_y) {
            span_low = std::min({span_low, from_x, to_x});
            span_high = std::max({span_high, from_x, to_x});
            continue;
          }
          const auto crossing = from_x + (row_y - from_y) * (to_x - from_x) / (to_y - from_y);
          span_low = std::min(span_low, crossing);
          span_high = std::max(span_high, crossing);
        }
      }
      if (span_low > span_high) {
        return 0;
      }
      first = std::max(left, static_cast<int>(std::floor(span_low)) - 1);
      end = std::min(right + 1, static_cast<int>(std::ceil(span_high)) + 2);
    }
    if (first >= end) {
      return 0;
    }
    if (!continues_previous_) {
      out[0] = Run{first, end};
      return 1;
    }

    // Closed column intervals that cannot change, at most two, in ascending order of start.
    std::array<std::pair<std::int64_t, std::int64_t>, 2> skips{};
    std::size_t skip_count = 0;
    constexpr auto kFar = std::int64_t{1} << 40;
    const auto row_offset = static_cast<std::int64_t>(y) - from_.y();
    // Behind `from`: (x - from.x) * dx + (y - from.y) * dy <= 0.
    if (dx_ == 0) {
      if (row_offset * dy_ <= 0) {
        return 0;  // the whole row (a repeated point: the whole segment)
      }
    } else {
      const auto bound = static_cast<std::int64_t>(from_.x()) * dx_ - row_offset * dy_;
      if (dx_ > 0) {
        skips[skip_count++] = {-kFar, floor_div(bound, dx_)};
      } else {
        skips[skip_count++] = {ceil_div(bound, dx_), kFar};
      }
    }
    if (covered_radius_ > 0.0) {
      const auto remaining = covered_radius_ * covered_radius_ - static_cast<double>(row_offset * row_offset);
      if (remaining >= 0.0) {
        const auto half_width = std::sqrt(remaining);
        const auto low = static_cast<std::int64_t>(std::ceil(static_cast<double>(from_.x()) - half_width));
        const auto high = static_cast<std::int64_t>(std::floor(static_cast<double>(from_.x()) + half_width));
        if (low <= high) {
          skips[skip_count++] = {low, high};
        }
      }
    }
    if (skip_count == 2 && skips[1].first < skips[0].first) {
      std::swap(skips[0], skips[1]);
    }
    std::size_t count = 0;
    std::int64_t cursor = first;
    for (std::size_t index = 0; index < skip_count; ++index) {
      const auto [low, high] = skips[index];
      if (high < cursor) {
        continue;
      }
      if (low > cursor) {
        const auto run_end = std::min<std::int64_t>(low, end);
        if (run_end > cursor) {
          out[count++] = Run{static_cast<int>(cursor), static_cast<int>(run_end)};
        }
      }
      cursor = std::max(cursor, high + 1);
      if (cursor >= end) {
        return count;
      }
    }
    if (cursor < end) {
      out[count++] = Run{static_cast<int>(cursor), end};
    }
    return count;
  }

 private:
  static std::int64_t floor_div(std::int64_t value, std::int64_t divisor) noexcept {
    auto quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
      --quotient;
    }
    return quotient;
  }
  static std::int64_t ceil_div(std::int64_t value, std::int64_t divisor) noexcept {
    auto quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) == (divisor < 0))) {
      ++quotient;
    }
    return quotient;
  }

  QPoint from_;
  QPoint to_;
  std::int64_t dx_{0};
  std::int64_t dy_{0};
  double radius_{0.0};
  bool walk_spans_{true};
  bool continues_previous_{false};
  double covered_radius_{0.0};
  bool has_band_{false};
  std::array<std::pair<double, double>, 4> band_{};
};

// capped_stroke_coverage on the cap `cap_at()` returns, called only once the target is
// known to be positive (as the map entry was created only then). Shared by the member and
// the parallel Clone stamp's workers.
template <typename CapAt>
float capped_alpha_coverage(CapAt&& cap_at, float coverage, float source_alpha) {
  if (source_alpha <= 0.0F) {
    return 0.0F;  // Opacity 0: nothing to cap toward, and no division below
  }
  source_alpha = std::clamp(source_alpha, 1.0F / 255.0F, 1.0F);
  const auto target_alpha = std::clamp(source_alpha * std::clamp(coverage, 0.0F, 1.0F), 0.0F, 1.0F);
  if (target_alpha <= 0.0F) {
    return 0.0F;
  }

  float& previous_alpha = cap_at();
  if (target_alpha <= previous_alpha + 0.0005F) {
    return 0.0F;
  }

  const auto incremental_alpha = (target_alpha - previous_alpha) / std::max(0.0005F, 1.0F - previous_alpha);
  previous_alpha = target_alpha;
  return std::clamp(incremental_alpha / source_alpha, 0.0F, 1.0F);
}

// The radius inside which a round brush of this radius and softness has coverage 1
// (brush_coverage): all of it when hard, the inner core when soft.
double full_coverage_radius(int radius, int softness) noexcept {
  softness = std::clamp(softness, 0, 100);
  if (softness <= 0) {
    return static_cast<double>(radius);
  }
  const auto edge_width = std::max(1.0, static_cast<double>(radius) * static_cast<double>(softness) / 100.0);
  return std::max(0.0, static_cast<double>(radius) - edge_width);
}

// The calibrated curve of a Dodge or Burn Range (ui/dodge_burn_curves.hpp, GitHub #661).
const dodge_burn::Knots& dodge_burn_knots(bool dodge, LocalToneRange range, bool protect_tones) noexcept {
  switch (range) {
    case LocalToneRange::Shadows:
      return protect_tones ? (dodge ? dodge_burn::kProtectDodgeShadows : dodge_burn::kProtectBurnShadows)
                           : (dodge ? dodge_burn::kDodgeShadows : dodge_burn::kBurnShadows);
    case LocalToneRange::Highlights:
      return protect_tones ? (dodge ? dodge_burn::kProtectDodgeHighlights : dodge_burn::kProtectBurnHighlights)
                           : (dodge ? dodge_burn::kDodgeHighlights : dodge_burn::kBurnHighlights);
    case LocalToneRange::Midtones:
      break;
  }
  return protect_tones ? (dodge ? dodge_burn::kProtectDodgeMidtones : dodge_burn::kProtectBurnMidtones)
                       : (dodge ? dodge_burn::kDodgeMidtones : dodge_burn::kBurnMidtones);
}

// Sharpen's per-dab step (GitHub issue 660, second follow-up; docs/brushes-local-adjustments.md, "Blur and
// Sharpen"): the dab moves every channel of a pixel by the same amount, kSharpenGain times the
// pixel's lightness minus the lightness under a fixed 21-tap binomial blur (radius
// kSharpenRadius), so colors keep their hue and JPEG chroma noise is not amplified. Fitted to
// Photoshop 2026's default Sharpen (Protect Detail on) on photos over one to five passes; the
// 3x3, 0.75-per-channel step that matched Protect Detail off turned JPEG noise into colored
// speckle within two passes.
constexpr int kSharpenRadius = 10;
constexpr double kSharpenGain = 0.2;
constexpr std::array<double, 2 * kSharpenRadius + 1> kSharpenKernel = [] {
  std::array<double, 2 * kSharpenRadius + 1> weights{};
  std::array<double, 2 * kSharpenRadius + 1> row{};
  row[0] = 1.0;
  for (int n = 1; n <= 2 * kSharpenRadius; ++n) {
    for (int k = n; k > 0; --k) {
      row[static_cast<std::size_t>(k)] += row[static_cast<std::size_t>(k - 1)];
    }
  }
  double total = 0.0;
  for (const auto value : row) {
    total += value;
  }
  for (std::size_t index = 0; index < row.size(); ++index) {
    weights[index] = row[index] / total;
  }
  return weights;
}();

// The lightness Sharpen works on, from display-encoded RGB (the same weights as the toning brushes).
constexpr double sharpen_lightness(double red, double green, double blue) noexcept {
  return (54.0 * red + 183.0 * green + 19.0 * blue) / 256.0;
}

}  // namespace

// The Dodge or Burn curve of the current Range and Protect Tones setting, sampled at every
// value the 8-bit walk reads it at, kept until the setting changes (GitHub #660).
const CanvasWidget::DodgeBurnStrokeTable& CanvasWidget::dodge_burn_stroke_table(bool dodge) {
  const auto& knots = dodge_burn_knots(dodge, local_tone_range_, local_protect_tones_);
  auto& table = dodge_burn_stroke_table_;
  if (table.knots == &knots && table.protect_tones == local_protect_tones_) {
    return table;
  }
  table.knots = &knots;
  table.protect_tones = local_protect_tones_;
  if (local_protect_tones_) {
    table.lightness_change.resize(54 * 255 + 183 * 255 + 19 * 255 + 1);
    for (std::size_t sum = 0; sum < table.lightness_change.size(); ++sum) {
      table.lightness_change[sum] = dodge_burn::curve_at(knots, static_cast<double>(sum) / (256.0 * 255.0));
    }
  } else {
    for (int value = 0; value < 256; ++value) {
      const auto level = static_cast<double>(value);
      table.channel[static_cast<std::size_t>(value)] =
          clamp_byte(static_cast<float>(level + dodge_burn::curve_at(knots, level / 255.0)));
    }
  }
  return table;
}

namespace {

// One row of a stroke segment and the runs of it a CloneSegmentRows walk visits.
struct SegmentRowRuns {
  int y{0};
  std::size_t count{0};
  std::array<CloneSegmentRows::Run, CloneSegmentRows::kMaxRuns> runs{};
};

// The rows of `stroke_rect` a segment reaches; `pixel_count` gets how many pixels they hold.
std::vector<SegmentRowRuns> segment_row_runs(const CloneSegmentRows& rows, const QRect& stroke_rect,
                                             std::int64_t& pixel_count) {
  std::vector<SegmentRowRuns> result;
  result.reserve(static_cast<std::size_t>(stroke_rect.height()));
  pixel_count = 0;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    SegmentRowRuns row;
    row.y = y;
    row.count = rows.runs(y, stroke_rect.left(), stroke_rect.right(), row.runs);
    for (std::size_t run = 0; run < row.count; ++run) {
      pixel_count += row.runs[run].end - row.runs[run].first;
    }
    if (row.count > 0) {
      result.push_back(row);
    }
  }
  return result;
}

// Paints a segment's rows, `paint_row(row, cap_at)` returning each row's dirty rect, with
// the Clone stamp's fan-out (GitHub issues 597 and 660): a segment of 8,192+ pixels over two
// or more 64-row bands runs as strips of whole bands of about equal pixel counts, so no two
// workers share a tile of the stroke's alpha caps (each gets a StrokePixelMap::Cursor;
// the tiles are reserved here first). Only for a paint_row whose pixels depend on nothing
// but their own bytes, their own cap and frozen inputs, so the strips give the sequential
// walk's bytes; the caller detaches the layer bytes on this thread first.
// PATCHY_RENDER_SINGLE_THREADED keeps every row here. `tick` runs on this thread.
template <typename PaintRow, typename Tick>
QRect walk_segment_rows(const std::vector<SegmentRowRuns>& segment_rows, std::int64_t segment_pixels,
                        StrokePixelMap<float>& caps, PaintRow&& paint_row, Tick&& tick) {
  constexpr std::int64_t kParallelPixels = 8192;
  const auto band_of = [](int y) { return y >> StrokePixelMap<float>::kTileRowsShift; };
  const auto bands = segment_rows.empty() ? 0 : band_of(segment_rows.back().y) - band_of(segment_rows.front().y) + 1;
  const auto workers = segment_pixels >= kParallelPixels && bands >= 2
                           ? patchy::max_blocking_fanout_workers(
                                 std::clamp(std::min(bands, patchy::hardware_worker_threads()), 1, 16))
                           : 1;
  QRect dirty;
  if (workers < 2 || qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    for (const auto& row : segment_rows) {
      dirty = dirty.united(paint_row(row, caps));
      tick();
    }
    return dirty;
  }
  std::vector<std::pair<std::size_t, std::size_t>> strips;
  const auto share = (segment_pixels + workers - 1) / workers;
  std::size_t strip_start = 0;
  std::int64_t strip_pixels = 0;
  auto band_left = std::numeric_limits<int>::max();
  auto band_right = std::numeric_limits<int>::min();
  for (std::size_t index = 0; index < segment_rows.size(); ++index) {
    const auto& row = segment_rows[index];
    band_left = std::min(band_left, row.runs[0].first);
    band_right = std::max(band_right, row.runs[row.count - 1].end);
    for (std::size_t run = 0; run < row.count; ++run) {
      strip_pixels += row.runs[run].end - row.runs[run].first;
    }
    const auto last = index + 1 == segment_rows.size();
    if (last || band_of(segment_rows[index + 1].y) != band_of(row.y)) {
      // The band ends here: its tiles exist before any worker runs.
      const auto band_top = band_of(row.y) << StrokePixelMap<float>::kTileRowsShift;
      caps.reserve_tiles(band_left, band_top, band_right, band_top + (1 << StrokePixelMap<float>::kTileRowsShift));
      band_left = std::numeric_limits<int>::max();
      band_right = std::numeric_limits<int>::min();
      if (last || strip_pixels >= share) {
        strips.emplace_back(strip_start, index + 1);
        strip_start = index + 1;
        strip_pixels = 0;
      }
    }
  }
  std::vector<std::future<QRect>> futures;
  futures.reserve(strips.size());
  for (const auto& [first_row, end_row] : strips) {
    futures.push_back(std::async(std::launch::async, [&segment_rows, &caps, &paint_row, first = first_row,
                                                      end = end_row] {
      StrokePixelMap<float>::Cursor cursor(caps);
      QRect strip_dirty;
      for (auto index = first; index < end; ++index) {
        strip_dirty = strip_dirty.united(paint_row(segment_rows[index], cursor));
      }
      return strip_dirty;
    }));
  }
  for (auto& future : futures) {
    dirty = dirty.united(future.get());
  }
  tick();
  return dirty;
}

}  // namespace

void CanvasWidget::set_brush_size(int size) {
  const QRect previous_outline =
      brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
  brush_size_ = std::clamp(size, 1, kMaxBrushSize);
  update_tool_cursor();
  invalidate_brush_hover_outline(previous_outline);
  // [ and ] resize without a mouse move: repaint the overlay drawn at the old size
  // too, or a shrink leaves its rim on screen (GitHub issue 516).
  invalidate_clone_overlay();
  invalidate_brush_overlay();
}

int CanvasWidget::brush_size() const noexcept {
  return brush_size_;
}

void CanvasWidget::set_brush_opacity(int opacity) {
  // 0 is a valid options-bar value (issue 253): the stroke paints nothing.
  brush_opacity_ = std::clamp(opacity, 0, 100);
}

int CanvasWidget::brush_opacity() const noexcept {
  return brush_opacity_;
}

void CanvasWidget::set_brush_flow(int flow) {
  brush_flow_ = std::clamp(flow, 0, 100);  // 0 meters nothing per dab (issue 253)
}

int CanvasWidget::brush_flow() const noexcept {
  return brush_flow_;
}

void CanvasWidget::set_brush_softness(int softness) {
  invalidate_brush_overlay();
  brush_softness_ = std::clamp(softness, 0, 100);
  update_tool_cursor();
  invalidate_brush_overlay();
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

void CanvasWidget::set_mixer_wet(int wet) noexcept {
  mixer_wet_ = std::clamp(wet, 0, 100);
}

int CanvasWidget::mixer_wet() const noexcept {
  return mixer_wet_;
}

void CanvasWidget::set_mixer_load(int load) noexcept {
  mixer_load_ = std::clamp(load, 1, 100);
}

int CanvasWidget::mixer_load() const noexcept {
  return mixer_load_;
}

void CanvasWidget::set_mixer_mix(int mix) noexcept {
  mixer_mix_ = std::clamp(mix, 0, 100);
}

int CanvasWidget::mixer_mix() const noexcept {
  return mixer_mix_;
}

void CanvasWidget::set_mixer_flow(int flow) noexcept {
  mixer_flow_ = std::clamp(flow, 1, 100);
}

int CanvasWidget::mixer_flow() const noexcept {
  return mixer_flow_;
}

void CanvasWidget::set_mixer_sample_all_layers(bool sample_all_layers) noexcept {
  mixer_sample_all_layers_ = sample_all_layers;
}

bool CanvasWidget::mixer_sample_all_layers() const noexcept {
  return mixer_sample_all_layers_;
}

void CanvasWidget::set_brush_paint_mode(PaintMode mode) noexcept {
  brush_paint_mode_ = mode.kind == PaintModeKind::Blend && !blend_mode_is_paint_mode(mode.blend) ? PaintMode{} : mode;
}

PaintMode CanvasWidget::brush_paint_mode() const noexcept {
  return brush_paint_mode_;
}

void CanvasWidget::set_brush_smoothing(int percent) noexcept {
  brush_smoothing_ = std::clamp(percent, 0, 100);
}

int CanvasWidget::brush_smoothing() const noexcept {
  return brush_smoothing_;
}

void CanvasWidget::set_brush_smoothing_pulled_string(bool enabled) noexcept {
  brush_smoothing_pulled_string_ = enabled;
}

bool CanvasWidget::brush_smoothing_pulled_string() const noexcept {
  return brush_smoothing_pulled_string_;
}

void CanvasWidget::set_brush_smoothing_catch_up(bool enabled) noexcept {
  brush_smoothing_catch_up_ = enabled;
}

bool CanvasWidget::brush_smoothing_catch_up() const noexcept {
  return brush_smoothing_catch_up_;
}

void CanvasWidget::set_brush_smoothing_catch_up_end(bool enabled) noexcept {
  brush_smoothing_catch_up_end_ = enabled;
}

bool CanvasWidget::brush_smoothing_catch_up_end() const noexcept {
  return brush_smoothing_catch_up_end_;
}

void CanvasWidget::set_brush_smoothing_zoom_adjust(bool enabled) noexcept {
  brush_smoothing_zoom_adjust_ = enabled;
}

bool CanvasWidget::brush_smoothing_zoom_adjust() const noexcept {
  return brush_smoothing_zoom_adjust_;
}

void CanvasWidget::set_brush_tip(std::shared_ptr<const patchy::BrushTip> tip, const QString& tip_id) {
  script_brush_spacing_.reset();
  if (tip != nullptr && tip->empty()) {
    tip = nullptr;
  }
  brush_tip_ = std::move(tip);
  brush_tip_id_ = brush_tip_ != nullptr ? tip_id : QString();
  brush_tip_mips_ = brush_tip_ != nullptr ? patchy::build_brush_tip_mips(*brush_tip_) : patchy::BrushTipMipChain{};
  brush_tip_scaled_cache_.clear();
  brush_tip_stroke_state_ = {};
  update_tool_cursor();
  invalidate_brush_overlay();
}

const QString& CanvasWidget::brush_tip_id() const noexcept {
  return brush_tip_id_;
}

bool CanvasWidget::has_brush_tip() const noexcept {
  return brush_tip_ != nullptr;
}

void CanvasWidget::set_brush_shape(patchy::BrushShape shape) {
  if (brush_shape_ == shape) {
    return;
  }
  brush_shape_ = shape;
  brush_tip_scaled_cache_.clear();  // the dynamics stamp is shape-specific
  brush_tip_stroke_state_ = {};
  brush_cursor_cache_.reset();
  update_tool_cursor();
  invalidate_brush_overlay();
}

patchy::BrushShape CanvasWidget::brush_shape() const noexcept {
  return brush_shape_;
}

void CanvasWidget::set_brush_dynamics(const patchy::BrushDynamics& dynamics) noexcept {
  // Switched-off sections become their defaults here, once, so the global-pen precedence
  // checks below see what the engine paints (a Shape Dynamics section that is off leaves the
  // global pen size preference in charge, as a brush without one does).
  brush_dynamics_ = patchy::effective_brush_dynamics(dynamics);
}

const patchy::BrushDynamics& CanvasWidget::brush_dynamics() const noexcept {
  return brush_dynamics_;
}

void CanvasWidget::set_brush_base_shape(double angle_degrees, int roundness) noexcept {
  invalidate_brush_overlay();
  brush_base_angle_degrees_ = std::clamp(angle_degrees, -180.0, 360.0);
  brush_base_roundness_ = std::clamp(roundness, 1, 100);
  invalidate_brush_overlay();
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

// The Square counterpart: a solid 256px square, so scatter, count, and jitter stamp squares.
const patchy::BrushTipMipChain& square_dynamics_tip_mips() {
  static const patchy::BrushTipMipChain chain = [] {
    constexpr std::int32_t kSize = 256;
    patchy::BrushTip tip;
    tip.width = kSize;
    tip.height = kSize;
    tip.default_spacing = kRoundDynamicsTipSpacing;
    tip.mask.assign(static_cast<std::size_t>(kSize) * kSize, std::uint8_t{255});
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
    mips = brush_shape_ == patchy::BrushShape::Square ? &square_dynamics_tip_mips() : &round_dynamics_tip_mips();
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
  // The procedural footprint rides along even when no stamp applies (no tip, no dynamics).
  options.brush_shape = brush_tip_ == nullptr && tool_paints_with_brush_tip(tool_) ? brush_shape_
                                                                                   : patchy::BrushShape::Round;
  auto scaled = scaled_brush_tip_for(brush_size, brush_softness);
  if (scaled == nullptr) {
    return;
  }
  // The cache's shared_ptr keeps the stamp alive for the duration of the paint call.
  options.brush_tip = scaled.get();
  options.brush_tip_spacing =
      script_brush_spacing_.value_or(brush_tip_ != nullptr ? brush_tip_->default_spacing : kRoundDynamicsTipSpacing);

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

  // A rotated view turns the tip on screen with the document (docs/view-navigation-rotation.md).
  const bool turned = view_rotation_degrees_ != 0.0;
  const auto span = turned ? static_cast<int>(std::ceil(std::hypot(outline.width(), outline.height())))
                           : std::max(outline.width(), outline.height());
  const auto extent = std::clamp(span + 7, 17, 168);
  QPixmap pixmap(extent, extent);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const QPoint center(extent / 2, extent / 2);
  if (turned) {
    painter.save();
    painter.translate(center);
    painter.rotate(view_rotation_degrees_);
    painter.drawImage(QPoint(-outline.width() / 2, -outline.height() / 2), outline);
    painter.restore();
  } else {
    painter.drawImage(QPoint(center.x() - outline.width() / 2, center.y() - outline.height() / 2), outline);
  }
  paint_brush_center_cross(painter, center);
  painter.end();
  setCursor(QCursor(pixmap, center.x(), center.y()));
  return true;
}

int CanvasWidget::active_outline_brush_size() const noexcept {
  return tool_ == CanvasTool::QuickSelect ? quick_select_size_ : brush_size_;
}

QSize CanvasWidget::brush_outline_display_size() const {
  if (brush_tip_ != nullptr && tool_paints_with_brush_tip(tool_)) {
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
  if (tool_ != CanvasTool::QuickSelect && !tool_uses_brush_footprint_cursor(tool_)) {
    return false;
  }
  if (brush_outline_hidden()) {
    return false;  // Show Outline is off: no outline at any size (GitHub issue 517)
  }
  if (transient_read_callback_) {
    return false;  // a dialog's picker owns the pointer (GitHub issue 638)
  }
  // Where a Clone or Healing press would be refused (no source yet) the pointer is the
  // not-allowed cursor, so the footprint is painted on the canvas at every size: the
  // circle stays while [ and ] resize it (GitHub issue 516).
  if (retouch_source_tool_active() && !painting_ && retouch_press_would_refuse()) {
    return true;
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
  if (tool_ != CanvasTool::QuickSelect && !tool_uses_brush_footprint_cursor(tool_)) {
    brush_hover_position_valid_ = false;
    return;
  }
  const auto old_rect =
      (brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect())
          .united(brush_outline_drawn_rect_);
  const auto old_overlay = clone_overlay_drawn_rect_;
  const auto old_brush_overlay = brush_overlay_drawn_rect_;
  brush_hover_widget_position_ = widget_position;
  brush_hover_position_valid_ = true;
  if (brush_outline_uses_overlay()) {
    update(old_rect.united(brush_hover_outline_rect()).adjusted(-2, -2, 2, 2));
  } else if (!old_rect.isEmpty()) {
    update(old_rect.adjusted(-2, -2, 2, 2));  // an outline painted before the cursor took over
  }
  if (clone_overlay_active()) {
    update(old_overlay.united(clone_overlay_widget_rect()));
  } else if (!old_overlay.isEmpty()) {
    update(old_overlay);
  }
  if (brush_overlay_active()) {
    update(old_brush_overlay.united(brush_overlay_widget_rect()));
  } else if (!old_brush_overlay.isEmpty()) {
    update(old_brush_overlay);
  }
}

// Repaint the hover outline after the brush (or Quick Select) size changes while
// the pointer is stationary — pressing [ / ]. update_tool_cursor() only invalidates
// the new rect, so shrinking a large brush would strand the old, larger rings
// outside it (they never get repainted). Erase the previous rect and, if the brush
// is still large enough to use the overlay, the new one too.
void CanvasWidget::invalidate_brush_hover_outline(const QRect& previous_outline) {
  QRect region = previous_outline.united(brush_outline_drawn_rect_);
  if (brush_hover_position_valid_ && brush_outline_uses_overlay()) {
    region = region.united(brush_hover_outline_rect());
  }
  if (!region.isEmpty()) {
    update(region.adjusted(-2, -2, 2, 2));
  }
}

void CanvasWidget::draw_brush_hover_outline(QPainter& painter) const {
  brush_outline_drawn_rect_ = QRect();
  if (!brush_hover_position_valid_ || !brush_outline_uses_overlay() || brush_adjust_dragging_ ||
      spacebar_panning_ || !preview_points_.empty()) {
    return;
  }
  if (over_ruler_bars(brush_hover_widget_position_)) {
    return;  // the arrow stands alone over the rulers (GitHub issue 472)
  }
  painter.save();
  const QPoint center = brush_hover_widget_position_;
  if (brush_tip_ != nullptr && tool_paints_with_brush_tip(tool_)) {
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
      if (view_rotation_degrees_ != 0.0) {
        // A rotated view turns the tip with the document.
        painter.save();
        painter.translate(QPointF(center));
        painter.rotate(view_rotation_degrees_);
        painter.drawImage(QPoint(-brush_outline_overlay_image_.width() / 2, -brush_outline_overlay_image_.height() / 2),
                          brush_outline_overlay_image_);
        painter.restore();
      } else {
        painter.drawImage(QPoint(center.x() - brush_outline_overlay_image_.width() / 2,
                                 center.y() - brush_outline_overlay_image_.height() / 2),
                          brush_outline_overlay_image_);
      }
    }
  } else {
    // Large procedural brush: the same outline the cursor draws, just unbounded.
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto radius = std::max(2.0, static_cast<double>(active_outline_brush_size()) * zoom_ / 2.0);
    const bool square = brush_shape_ == patchy::BrushShape::Square && tool_paints_with_brush_tip(tool_);
    const auto draw_footprint = [&](double half) {
      if (square) {
        painter.save();
        painter.translate(QPointF(center));
        painter.rotate(brush_base_angle_degrees_ + view_rotation_degrees_);
        painter.drawRect(QRectF(-half, -half, 2.0 * half, 2.0 * half));
        painter.restore();
      } else {
        painter.drawEllipse(QPointF(center), half, half);
      }
    };
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(255, 255, 255) : QColor(25, 25, 25), 1));
    draw_footprint(radius);
    painter.setPen(QPen(tool_ == CanvasTool::Eraser ? QColor(25, 25, 25) : QColor(255, 255, 255), 1));
    draw_footprint(std::max(1.0, radius - 1.0));
    if (brush_softness_ > 0 && tool_ != CanvasTool::Eraser && tool_ != CanvasTool::QuickSelect) {
      const auto edge_width =
          std::max(1.0, radius * static_cast<double>(std::clamp(brush_softness_, 0, 100)) / 100.0);
      QPen softness_pen(QColor(105, 150, 210, 175), 1, Qt::DashLine);
      painter.setPen(softness_pen);
      draw_footprint(std::max(1.0, radius - edge_width));
    }
  }
  if (!brush_center_cross_cursor_shown(cursor())) {
    // The pointer carries the cross itself; under any other pointer (not-allowed, Alt's
    // eyedropper, Ctrl's Move) the overlay marks the center (GitHub issue 533).
    paint_brush_center_cross(painter, center);
  }
  painter.restore();
  brush_outline_drawn_rect_ = brush_hover_outline_rect();
}

void CanvasWidget::begin_brush_adjust_drag(QPoint widget_position, bool from_tablet) {
  brush_adjust_dragging_ = true;
  brush_adjust_from_tablet_ = from_tablet;
  brush_adjust_origin_widget_ = widget_position;
  brush_adjust_current_widget_ = widget_position;
  brush_adjust_start_size_ = active_outline_brush_size();
  brush_adjust_start_softness_ = brush_softness_;
  brush_adjust_pointer_centered_ = from_tablet;
  brush_adjust_pointer_locked_ = false;
  brush_adjust_rebase_pointer_ = false;
  brush_adjust_drag_position_ = QPointF(widget_position);
  brush_adjust_pointer_offset_ = QPointF();
  brush_adjust_pointer_lock_.reset();
  if (!from_tablet) {
    // A mouse gesture ends with the pointer back on the press point. Where the app cannot
    // move the pointer (Wayland), lock it there for the drag instead; the compositor sends
    // the motion as relative deltas (GitHub issue 637).
    PointerLockEvents events;
    events.locked = [this]() {
      if (brush_adjust_dragging_) {
        brush_adjust_pointer_locked_ = true;
      }
    };
    events.unlocked = [this]() {
      if (brush_adjust_pointer_locked_) {
        brush_adjust_pointer_locked_ = false;
        brush_adjust_rebase_pointer_ = true;
      }
    };
    events.relative_motion = [this](QPointF delta) { brush_adjust_pointer_moved_relative(delta); };
    brush_adjust_pointer_lock_ = lock_pointer_at(*this, widget_position, std::move(events));
    if (brush_adjust_pointer_lock_ == nullptr && !platform_can_move_pointer()) {
      // Neither moved back nor locked: center the disc on the pointer, as for a pen, so the
      // gesture ends with the brush under the pointer at the size it shows.
      brush_adjust_pointer_centered_ = true;
    }
  }
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
  const auto diameter_per_pixel = brush_adjust_pointer_centered_ ? 4.0 : 2.0;
  // Quick Select sizes its own brush (GitHub issue 428) and has no softness to drag.
  const bool quick_select = tool_ == CanvasTool::QuickSelect;
  const auto new_size = std::clamp(
      brush_adjust_start_size_ + static_cast<int>(std::lround(document_dx * diameter_per_pixel)),
      1, quick_select ? kMaxQuickSelectSize : kMaxBrushSize);
  // Vertical drag adjusts edge softness: up softens, down hardens (the
  // Photoshop hardness directions, inverted because softness is 100-hardness).
  const auto new_softness =
      quick_select
          ? brush_softness_
          : std::clamp(
                brush_adjust_start_softness_ - static_cast<int>(std::lround(static_cast<double>(delta.y()) * 0.4)),
                0, 100);
  if (new_size == active_outline_brush_size() && new_softness == brush_softness_) {
    if (brush_adjust_pointer_centered_) {
      update();  // the pointer-centered preview moves even when the values do not
    }
    return;
  }
  if (quick_select) {
    quick_select_size_ = new_size;
  } else {
    brush_size_ = new_size;
    brush_softness_ = new_softness;
  }
  update();
}

void CanvasWidget::brush_adjust_pointer_moved(QPoint widget_position) {
  if (brush_adjust_pointer_locked_) {
    // A locked pointer does not move; Qt's synthesized moves carry the stale position.
    return;
  }
  if (brush_adjust_rebase_pointer_) {
    // The lock ended mid-drag with the pointer still where it was locked: carry on from
    // the drag position instead of jumping back to it.
    brush_adjust_rebase_pointer_ = false;
    brush_adjust_pointer_offset_ = brush_adjust_drag_position_ - QPointF(widget_position);
  }
  brush_adjust_drag_position_ = QPointF(widget_position) + brush_adjust_pointer_offset_;
  update_brush_adjust_drag(brush_adjust_drag_position_.toPoint());
}

void CanvasWidget::brush_adjust_pointer_moved_relative(QPointF delta) {
  if (!brush_adjust_dragging_ || !brush_adjust_pointer_locked_) {
    return;
  }
  brush_adjust_drag_position_ += delta;
  update_brush_adjust_drag(brush_adjust_drag_position_.toPoint());
}

void CanvasWidget::end_brush_adjust_drag(bool commit) {
  if (!brush_adjust_dragging_) {
    return;
  }
  brush_adjust_dragging_ = false;
  // Ending the lock leaves the pointer on the press point: it never moved while locked,
  // and the compositor applies the position hint set there.
  const bool pointer_held_by_lock = brush_adjust_pointer_locked_;
  brush_adjust_pointer_locked_ = false;
  brush_adjust_rebase_pointer_ = false;
  brush_adjust_pointer_lock_.reset();
  if (!commit) {
    if (tool_ == CanvasTool::QuickSelect) {
      quick_select_size_ = brush_adjust_start_size_;
    } else {
      brush_size_ = brush_adjust_start_size_;
      brush_softness_ = brush_adjust_start_softness_;
    }
  }
  if (!brush_adjust_from_tablet_ && brush_adjust_pointer_centered_) {
    // The disc followed the pointer: the brush stays under it.
    last_mouse_position_ = brush_adjust_current_widget_;
    brush_hover_widget_position_ = brush_adjust_current_widget_;
  } else if (!brush_adjust_from_tablet_ && !pointer_held_by_lock && !platform_can_move_pointer()) {
    // The lock never held (or ended early) and the pointer cannot be moved back: the
    // brush goes where the pointer really is.
    const auto pointer = (brush_adjust_drag_position_ - brush_adjust_pointer_offset_).toPoint();
    last_mouse_position_ = pointer;
    brush_hover_widget_position_ = pointer;
  } else if (!brush_adjust_from_tablet_) {
    // A mouse is a relative device, so snap the pointer back to the gesture
    // anchor and the brush stays centered on the spot that was being adjusted
    // (Photoshop does the same). A pen is absolute (its pointer cannot be
    // moved), so its preview is centered on the pen during the drag and the
    // gesture already ends with the brush under the pen.
    if (!pointer_held_by_lock) {
      move_pointer_to_global_position(mapToGlobal(brush_adjust_origin_widget_));
    }
    last_mouse_position_ = brush_adjust_origin_widget_;
    // The hovered brush (and the clone overlay under it) is back on the anchor at
    // once, before the warped pointer's move arrives (GitHub issue 516).
    brush_hover_widget_position_ = brush_adjust_origin_widget_;
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
  const QPointF center(brush_adjust_pointer_centered_ ? brush_adjust_current_widget_ : brush_adjust_origin_widget_);
  const auto radius = std::max(1.5, static_cast<double>(active_outline_brush_size()) * zoom_ / 2.0);
  const auto softness = tool_ == CanvasTool::QuickSelect ? 0 : brush_softness_;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  if ((tool_ == CanvasTool::Brush || tool_ == CanvasTool::MixerBrush ||
       tool_ == CanvasTool::PatternStamp ||
       tool_ == CanvasTool::Eraser) &&
      brush_tip_ != nullptr) {
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
  if (softness <= 0) {
    painter.setBrush(fill);
  } else {
    QRadialGradient gradient(center, radius);
    const auto solid_extent = std::clamp(1.0 - static_cast<double>(softness) / 100.0, 0.0, 1.0);
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
  // The drag size readout's HUD pill, centered just below the disc and kept in
  // view (GitHub issue 422: outlined text alone vanished on busy artwork).
  // Quick Select has no softness, so its readout is the size alone (GitHub issue 428).
  const auto readout =
      tool_ == CanvasTool::QuickSelect
          ? QCoreApplication::translate("patchy::ui::MainWindow", "Size: %1 px").arg(quick_select_size_)
          : tr("Size: %1 px  Soft: %2%").arg(brush_size_).arg(brush_softness_);
  const auto size = canvas_hud_pill_size(painter.fontMetrics(), readout);
  QRect pill(QPoint(static_cast<int>(std::lround(center.x())) - size.width() / 2,
                    static_cast<int>(std::lround(center.y() + radius)) + 8),
             size);
  const int left = rulers_visible_ ? kLeftRulerWidth : 0;
  const int top = rulers_visible_ ? kTopRulerHeight : 0;
  const QRect bounds(left, top, std::max(1, width() - left), std::max(1, height() - top));
  if (pill.right() > bounds.right() - 4) {
    pill.moveRight(bounds.right() - 4);
  }
  if (pill.bottom() > bounds.bottom() - 4) {
    pill.moveBottom(bounds.bottom() - 4);
  }
  if (pill.left() < bounds.left() + 4) {
    pill.moveLeft(bounds.left() + 4);
  }
  if (pill.top() < bounds.top() + 4) {
    pill.moveTop(bounds.top() + 4);
  }
  paint_canvas_hud_pill(painter, pill, readout);
}

void CanvasWidget::clear_brush_stroke_tracking() noexcept {
  airbrush_timer_.stop();
  // The stabilizer catch-up timer stops everywhere the airbrush timer stops:
  // release, focus loss, tool change, document/target change, and escape all
  // funnel through here. Erase any leash overlay left on screen with it.
  stabilizer_timer_.stop();
  if (!stroke_leash_overlay_rect_.isEmpty()) {
    update(stroke_leash_overlay_rect_);
    stroke_leash_overlay_rect_ = QRect();
  }
  brush_stroke_pixels_.clear();
  brush_stroke_alpha_caps_.clear();
  healing_membrane_stroke_ = false;
  healing_stroke_original_pixels_.reset();
  healing_stroke_footprint_ = QRect();
  brush_stroke_accumulated_alpha_.clear();
  brush_stroke_union_coverage_.clear();
  brush_stroke_wet_edge_primary_.clear();
  brush_stroke_wet_edge_pending_rect_ = {};
  brush_stroke_layer_snapshot_.reset();
  local_adjustment_alt_swap_ = false;
  toning_dab_placed_ = false;
  toning_dab_distance_ = 0.0;
  toning_pending_point_.reset();
  toning_tail_timer_.stop();
  brush_stroke_paint_mode_ = {};
  brush_stroke_last_stamp_position_.reset();
  brush_stroke_hard_round_end_.reset();
  clone_stroke_segment_end_.reset();
  brush_stroke_distance_since_last_stamp_ = 0.0;
  mixer_brush_state_ = {};
  mixer_composite_snapshot_ = QImage();
  brush_tip_stroke_state_ = {};
  held_first_dab_pen_sample_.reset();
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

bool CanvasWidget::tool_uses_stroke_stabilizer(CanvasTool tool) noexcept {
  // Photoshop scopes Smoothing to the Brush, Mixer Brush, and Eraser tools (a
  // pen's eraser end resolves to Eraser through effective_tool_for_input()).
  // Smudge, Pattern Stamp, shapes, and mask/selection tools stay unsmoothed.
  return tool == CanvasTool::Brush || tool == CanvasTool::MixerBrush || tool == CanvasTool::Eraser;
}

double CanvasWidget::stroke_stabilizer_leash_radius() const noexcept {
  // The one place the Smoothing percent becomes a leash radius: 0..100% maps
  // to 0..100 px. With Adjust for Zoom ON that is DOCUMENT pixels (Photoshop's
  // zoom compensation: constant precision relative to the image at any zoom);
  // OFF it is SCREEN pixels, so divide by the view zoom for document units.
  const auto radius = static_cast<double>(brush_smoothing_);
  if (brush_smoothing_zoom_adjust_) {
    return radius;
  }
  return radius / std::max(zoom_, 0.0001);
}

void CanvasWidget::begin_stroke_stabilizer(QPointF document_point, CanvasTool effective_tool) {
  patchy::StrokeStabilizerConfig config;
  config.leash_radius =
      tool_uses_stroke_stabilizer(effective_tool) ? stroke_stabilizer_leash_radius() : 0.0;
  config.pulled_string = brush_smoothing_pulled_string_;
  config.catch_up = brush_smoothing_catch_up_;
  config.catch_up_on_end = brush_smoothing_catch_up_end_;
  stroke_stabilizer_.begin(document_point.x(), document_point.y(), config);
  stroke_leash_overlay_rect_ = QRect();
  if (stroke_stabilizer_.active()) {
    invalidate_stroke_leash_overlay();
    if (brush_smoothing_catch_up_) {
      stabilizer_timer_.start(kStrokeStabilizerTimerIntervalMs, this);
    }
  }
}

QPointF CanvasWidget::stabilized_stroke_point(QPointF constrained_point, CanvasTool effective_tool) {
  // Smoothing 0 (or a non-smoothed tool) never consults the stabilizer, so no
  // conversion can perturb the historical stroke path.
  if (!stroke_stabilizer_.active() || !tool_uses_stroke_stabilizer(effective_tool)) {
    return constrained_point;
  }
  const auto smoothed = stroke_stabilizer_.move(constrained_point.x(), constrained_point.y());
  invalidate_stroke_leash_overlay();
  return QPointF(smoothed.x, smoothed.y);
}

QRect CanvasWidget::stroke_leash_overlay_rect() const {
  if (!painting_ || !stroke_stabilizer_.active() || brush_smoothing_ <= 0) {
    return {};
  }
  const auto raw = stroke_stabilizer_.raw();
  const auto output = stroke_stabilizer_.output();
  const auto raw_widget = widget_position_for_stroke_point(QPointF(raw.x, raw.y));
  const auto output_widget = widget_position_for_stroke_point(QPointF(output.x, output.y));
  const auto radius = stroke_stabilizer_leash_radius() * zoom_;
  auto bounds = QRectF(raw_widget, output_widget).normalized();
  bounds = bounds.united(
      QRectF(raw_widget.x() - radius, raw_widget.y() - radius, radius * 2.0, radius * 2.0));
  return bounds.toAlignedRect().adjusted(-3, -3, 3, 3);
}

// Old-union-new, the brush-outline gotcha: repainting only the new rect would
// strand the previous leash line and circle whenever they shrink or move.
void CanvasWidget::invalidate_stroke_leash_overlay() {
  const auto current = stroke_leash_overlay_rect();
  const auto region = stroke_leash_overlay_rect_.united(current);
  stroke_leash_overlay_rect_ = current;
  if (!region.isEmpty()) {
    update(region);
  }
}

void CanvasWidget::draw_stroke_leash_overlay(QPainter& painter) const {
  if (!painting_ || !stroke_stabilizer_.active() || brush_smoothing_ <= 0 ||
      !tool_uses_stroke_stabilizer(effective_tool_for_input())) {
    return;
  }
  const auto raw = stroke_stabilizer_.raw();
  const auto output = stroke_stabilizer_.output();
  const auto raw_widget = widget_position_for_stroke_point(QPointF(raw.x, raw.y));
  const auto output_widget = widget_position_for_stroke_point(QPointF(output.x, output.y));
  const auto radius = stroke_stabilizer_leash_radius() * zoom_;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(theme().brush_leash, 1.0));
  painter.drawLine(output_widget, raw_widget);
  painter.drawEllipse(raw_widget, radius, radius);
  painter.restore();
}

bool CanvasWidget::tool_supports_shift_click_stroke_connect(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Brush:
    case CanvasTool::MixerBrush:
    case CanvasTool::PatternStamp:
    case CanvasTool::Clone:
    case CanvasTool::Healing:
    case CanvasTool::SpotHealing:
    case CanvasTool::Smudge:
    case CanvasTool::Dodge:
    case CanvasTool::Burn:
    case CanvasTool::Sponge:
    case CanvasTool::BlurBrush:
    case CanvasTool::SharpenBrush:
    case CanvasTool::Eraser:
      return true;
    default:
      return false;
  }
}

std::optional<QLineF> CanvasWidget::stroke_connect_preview_line() const {
  // Shift alone: Ctrl is the painting tools' temporary Move and Alt picks a color
  // or sets a clone source, so a click with either would not draw this line. On Dodge,
  // Burn, Blur and Sharpen Alt swaps the tool instead (GitHub issue 662), and
  // Alt+Shift+click draws the swapped tool's line, so the line shows with Alt too.
  const auto held = stroke_connect_preview_modifiers_ & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
  const auto alt_swaps = alt_swapped_toning_tool(effective_tool_for_input()) != effective_tool_for_input();
  const auto line_modifiers = held == Qt::ShiftModifier || (alt_swaps && held == (Qt::ShiftModifier | Qt::AltModifier));
  if (!line_modifiers || !stroke_connect_preview_pointer_inside_ || !last_stroke_end_document_.has_value() ||
      document_ == nullptr || painting_ || spacebar_panning_ || panning_ || brush_adjust_dragging_ ||
      move_drag_pending_ || moving_layer_ || !isVisible() || transient_read_callback_ ||
      !tool_supports_shift_click_stroke_connect(effective_tool_for_input())) {
    return std::nullopt;
  }
  // The line runs where the Shift+click paints it. Stroke points are pixel indices with the dab
  // centered on the pixel's center (the core brushes measure coverage from the index), so each end
  // maps through that center, not the pixel's top-left corner (GitHub issue 505). The 1 px brush and
  // the other tools step whole pixels from the pixel under the pointer; larger Brush, Mixer, Pattern
  // Stamp and Eraser strokes keep the pointer's fractional stroke point (stroke_position_f).
  const auto tool = effective_tool_for_input();
  const auto fractional = (tool == CanvasTool::Brush || tool == CanvasTool::MixerBrush ||
                           tool == CanvasTool::PatternStamp || tool == CanvasTool::Eraser) &&
                          effective_brush_input().size != 1;
  const auto pointer = stroke_connect_preview_pointer_;
  const auto from = fractional ? *last_stroke_end_document_ : QPointF(last_stroke_end_document_->toPoint());
  const auto to = fractional ? stroke_position_f(QPointF(pointer)) : QPointF(document_position(pointer));
  // Ends snap to widget pixel centers so the 1 px line stays crisp on a pixel row.
  const auto view_point = [this](QPointF stroke_point) {
    const auto widget = widget_position_for_stroke_point(stroke_point);
    return QPointF(std::floor(widget.x()) + 0.5, std::floor(widget.y()) + 0.5);
  };
  return QLineF(view_point(from), view_point(to));
}

QRect CanvasWidget::stroke_connect_preview_rect() const {
  const auto line = stroke_connect_preview_line();
  if (!line.has_value()) {
    return {};
  }
  return QRectF(line->p1(), line->p2()).normalized().toAlignedRect().adjusted(-3, -3, 4, 4);
}

// Old-union-new, like the leash: the previous line must be erased wherever it was.
void CanvasWidget::refresh_stroke_connect_preview() {
  const auto current = stroke_connect_preview_rect();
  const auto region = stroke_connect_preview_rect_.united(current);
  stroke_connect_preview_rect_ = current;
  if (!region.isEmpty()) {
    update(region);
  }
}

void CanvasWidget::track_stroke_connect_preview(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  stroke_connect_preview_pointer_ = widget_position;
  stroke_connect_preview_pointer_inside_ = rect().contains(widget_position);
  stroke_connect_preview_modifiers_ = modifiers;
  refresh_stroke_connect_preview();
}

void CanvasWidget::set_stroke_connect_preview_modifiers(Qt::KeyboardModifiers modifiers) {
  stroke_connect_preview_modifiers_ = modifiers;
  refresh_stroke_connect_preview();
}

void CanvasWidget::hide_stroke_connect_preview() {
  stroke_connect_preview_pointer_inside_ = false;
  refresh_stroke_connect_preview();
}

void CanvasWidget::draw_stroke_connect_preview(QPainter& painter) const {
  const auto line = stroke_connect_preview_line();
  if (!line.has_value() || line->length() < 1.0) {
    return;
  }
  // A dark halo under a light line, the clone-source marker's pair, reads over any artwork.
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const auto& pen : {QPen(QColor(20, 23, 28, 200), 3.0), QPen(QColor(245, 248, 252, 235), 1.0)}) {
    painter.setPen(pen);
    painter.drawLine(*line);
  }
  painter.restore();
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
    if (script_brush_progress_ && script_brush_progress_(dirty)) return dirty;
    previous = current;
  }
  return dirty;
}

double CanvasWidget::brush_stamp_spacing(const EffectiveBrushInput& brush) const noexcept {
  if (script_brush_spacing_) return std::max(.01, brush.size * *script_brush_spacing_);
  return std::clamp(static_cast<double>(std::max(1, brush.size)) * 0.125, 1.0, kMaxBrushStampSpacing);
}

bool CanvasWidget::brush_uses_dab_stroke(const EffectiveBrushInput& brush, bool erase) const noexcept {
  if (brush_tip_ != nullptr) {
    return false;
  }
  if (script_brush_spacing_ && (erase || !brush_dynamics_.active())) return true;
  if (!erase && tool_ == CanvasTool::MixerBrush) {
    // The mixer evolves once per spatial dab. The procedural capsule path has no dab cadence.
    return true;
  }
  if (!erase && tool_ == CanvasTool::Brush && brush_dynamics_.active()) {
    return false;
  }
  // Flow must be tied to a spatial dab cadence, not to the number of mouse
  // move events delivered by the platform. Airbrush also uses this path so its
  // moving stroke and stationary timer share the same flat stamp footprint.
  if (!erase && (tool_ == CanvasTool::Brush || tool_ == CanvasTool::PatternStamp) &&
      (brush_flow_ < 100 || (tool_ == CanvasTool::Brush && brush_build_up_))) {
    return true;
  }
  return brush.size > 1 && brush.softness > 0;
}

QRect CanvasWidget::draw_brush_dab(QPointF point, bool erase, EditOptions& options) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }
  const auto dirty = to_qrect(patchy::paint_brush_dab(
      *document_, *document_->active_layer_id(), point.x(), point.y(), options, erase));
  return finalize_pending_wet_edges(dirty);
}

QRect CanvasWidget::draw_brush_segment_with_dabs(QPointF from, QPointF to, bool erase,
                                                 const EffectiveBrushInput& brush,
                                                 bool stamp_endpoint) {
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }

  auto options = current_brush_edit_options(brush);
  options.brush_shape = dab_stroke_brush_shape();
  install_brush_stroke_compositor(options, erase);

  const auto spacing = brush_stamp_spacing(brush);
  // The end of the segment still needs a dab unless the last one already covers it: for a
  // 1 px brush that is the same pixel (a dab paints the pixel nearest it), so a stroke's last
  // pixel is painted without a second dab building up on it (local-040).
  const auto one_pixel = std::max(1, brush.size) / 2 == 0;
  const auto endpoint_needs_stamp = [&]() {
    if (!brush_stroke_last_stamp_position_.has_value()) {
      return true;
    }
    const auto last = *brush_stroke_last_stamp_position_;
    if (one_pixel) {
      return std::floor(last.x() + 0.5) != std::floor(to.x() + 0.5) ||
             std::floor(last.y() + 0.5) != std::floor(to.y() + 0.5);
    }
    return point_distance(last, to) > 0.001;
  };
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
    if (stamp_endpoint && endpoint_needs_stamp()) {
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
  unsigned progress_steps = 0;
  while (next_stamp_distance <= segment_length + 0.001) {
    stamp(QPointF(from.x() + dx * next_stamp_distance, from.y() + dy * next_stamp_distance));
    last_stamp_distance = next_stamp_distance;
    next_stamp_distance += spacing;
    if (script_brush_progress_ && ++progress_steps % 64 == 0 && script_brush_progress_(dirty)) return dirty;
  }

  if (last_stamp_distance >= 0.0) {
    brush_stroke_distance_since_last_stamp_ = std::max(0.0, segment_length - last_stamp_distance);
  } else {
    brush_stroke_distance_since_last_stamp_ = starting_distance + segment_length;
  }
  if (brush_stroke_distance_since_last_stamp_ >= spacing) {
    brush_stroke_distance_since_last_stamp_ = std::fmod(brush_stroke_distance_since_last_stamp_, spacing);
  }

  if (stamp_endpoint && endpoint_needs_stamp()) {
    stamp(to);
  }
  return dirty;
}

float CanvasWidget::capped_stroke_coverage(std::int32_t x, std::int32_t y, float coverage, float source_alpha) {
  return capped_alpha_coverage([&]() -> float& { return brush_stroke_alpha_caps_(x, y); }, coverage, source_alpha);
}

float CanvasWidget::accumulating_stroke_coverage(std::int32_t x, std::int32_t y,
                                                 float coverage, float opacity,
                                                 float flow) {
  if (opacity <= 0.0F || flow <= 0.0F) {
    return 0.0F;  // Opacity 0 or Flow 0 paints nothing (issue 253); the clamps below floor the math
  }
  opacity = std::clamp(opacity, 1.0F / 255.0F, 1.0F);
  flow = std::clamp(flow, 1.0F / 100.0F, 1.0F);
  const auto dab_alpha = std::clamp(opacity * flow * std::clamp(coverage, 0.0F, 1.0F),
                                    0.0F, opacity);
  if (dab_alpha <= 0.0F) {
    return 0.0F;
  }

  auto& accumulated_alpha = brush_stroke_accumulated_alpha_(x, y);
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
    if (pixels.format().bit_depth != BitDepth::UInt8 && pixels.format().channels >= 3) {
      // 16/32-bit: the display bytes (deep strokes read brush_stroke_original_deep).
      return display_rgba8_at(pixels, x - snapshot.bounds.x, y - snapshot.bounds.y);
    }
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

// 16/32-bit strokes (docs/high-bit-depth.md): render_brush_stroke_pixel_from_snapshot_target's
// arithmetic on the stroke-start pixel at its depth, without byte rounding. 32-bit layers
// blend in linear light, so the 8-bit paint colors decode first and take their Intensity.
bool CanvasWidget::render_brush_stroke_pixel_deep(std::int32_t x, std::int32_t y, std::uint8_t* pixel,
                                                  EditColor primary, EditColor secondary,
                                                  bool lock_transparent_pixels, float target_alpha,
                                                  bool erase) const {
  const auto& snapshot = *brush_stroke_layer_snapshot_;
  const auto format = snapshot.pixels.format();
  const bool linear = format.bit_depth == BitDepth::Float32;
  std::array<float, 4> original{0.0F, 0.0F, 0.0F, 0.0F};
  if (snapshot.bounds.contains(x, y) && !snapshot.pixels.empty()) {
    original = load_pixel(format, snapshot.pixels.pixel(x - snapshot.bounds.x, y - snapshot.bounds.y));
  } else if (snapshot.background_extension) {
    original = {255.0F, 255.0F, 255.0F, 255.0F};
  }
  const auto channels = format.channels;
  const auto locked_alpha = lock_transparent_pixels && channels >= 4;
  if (locked_alpha && original[3] <= 0.0F) {
    return false;
  }
  const auto color_of = [linear](EditColor color, double stops) {
    const auto gain = color_intensity_gain(stops);
    const auto channel = [linear, gain](std::uint8_t v) {
      return linear ? static_cast<float>(srgb_decode(static_cast<double>(v) / 255.0) * 255.0) * gain
                    : static_cast<float>(v);
    };
    return std::array<float, 3>{channel(color.r), channel(color.g), channel(color.b)};
  };
  const auto before = load_pixel(format, pixel);
  auto value = original;
  if (erase) {
    if (channels >= 4 && !locked_alpha) {
      value[3] = original[3] * (1.0F - target_alpha);
    } else {
      // No alpha channel, or locked transparency: erase to the background color (Photoshop).
      const auto background = color_of(secondary, paint_intensity_.background);
      for (std::size_t c = 0; c < 3U; ++c) {
        value[c] = background[c] * target_alpha + original[c] * (1.0F - target_alpha);
      }
    }
  } else if (!brush_stroke_paint_mode_.is_normal()) {
    value = patchy::composite_paint_deep(original, color_of(primary, paint_intensity_.foreground), target_alpha, brush_stroke_paint_mode_, x, y,
                                         channels >= 4, locked_alpha, linear ? DeepDomain::Linear : DeepDomain::Encoded);
  } else {
    const auto paint = color_of(primary, paint_intensity_.foreground);
    if (channels >= 4 && !locked_alpha) {
      const auto destination_alpha = original[3] / 255.0F;
      const auto out_alpha = target_alpha + destination_alpha * (1.0F - target_alpha);
      if (out_alpha <= 0.0F) {
        return false;
      }
      for (std::size_t c = 0; c < 3U; ++c) {
        value[c] = destination_alpha <= 0.0F
                       ? paint[c]
                       : (paint[c] * target_alpha + original[c] * destination_alpha * (1.0F - target_alpha)) / out_alpha;
      }
      value[3] = out_alpha * 255.0F;
    } else {
      for (std::size_t c = 0; c < 3U; ++c) {
        value[c] = target_alpha >= 0.999F ? paint[c] : paint[c] * target_alpha + original[c] * (1.0F - target_alpha);
      }
      value[3] = original[3];
    }
  }
  store_pixel(format, pixel, value);
  return load_pixel(format, pixel) != before;
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
  const auto flow = static_cast<float>(tool_ == CanvasTool::MixerBrush ? mixer_flow_ : brush_flow_) /
                    100.0F;
  if (palette_snap == nullptr || palette_snap->lut == nullptr || palette_snap->lut->empty()) {
    return write_brush_stroke_pixel_from_snapshot_blend(x, y, pixel, channels, primary, secondary,
                                                        lock_transparent_pixels, coverage, erase, flow);
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
                                                     lock_transparent_pixels, 1.0F, erase, flow);
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
                                                                 float coverage, bool erase, float flow) {
  coverage = std::clamp(coverage, 0.0F, 1.0F);
  const auto source_alpha = static_cast<float>(primary.a) / 255.0F;
  if (coverage <= 0.0F || source_alpha <= 0.0F) {
    return false;
  }

  const auto original = brush_stroke_original_pixel(x, y);
  const auto locked_alpha = lock_transparent_pixels && channels >= 4;
  if (locked_alpha && original[3] == 0) {
    return false;
  }

  if (flow <= 0.0F) {
    return false;  // Flow 0 meters no paint (issue 253)
  }
  flow = std::clamp(flow, 0.01F, 1.0F);
  const auto dab_alpha = std::clamp(source_alpha * flow * coverage, 0.0F, source_alpha);
  auto& accumulated_alpha = brush_stroke_accumulated_alpha_(x, y);
  const auto target_alpha =
      std::min(source_alpha, 1.0F - (1.0F - accumulated_alpha) * (1.0F - dab_alpha));
  if (target_alpha <= accumulated_alpha + 0.0005F) {
    return false;
  }
  accumulated_alpha = target_alpha;

  return render_brush_stroke_pixel_from_snapshot_target(
      x, y, pixel, channels, primary, secondary, lock_transparent_pixels, target_alpha, erase);
}

bool CanvasWidget::render_brush_stroke_pixel_from_snapshot_target(
    std::int32_t x, std::int32_t y, std::uint8_t* pixel, std::uint16_t channels,
    EditColor primary, EditColor secondary, bool lock_transparent_pixels, float target_alpha,
    bool erase) const {
  target_alpha = std::clamp(target_alpha, 0.0F, 1.0F);
  if (brush_stroke_layer_snapshot_.has_value() &&
      brush_stroke_layer_snapshot_->pixels.format().bit_depth != BitDepth::UInt8) {
    return render_brush_stroke_pixel_deep(x, y, pixel, primary, secondary, lock_transparent_pixels,
                                          target_alpha, erase);
  }
  const auto original = brush_stroke_original_pixel(x, y);
  const auto locked_alpha = lock_transparent_pixels && channels >= 4;
  if (locked_alpha && original[3] == 0) {
    return false;
  }

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
    if (channels >= 4 && !locked_alpha) {
      pixel[0] = original[0];
      pixel[1] = original[1];
      pixel[2] = original[2];
      pixel[3] = clamp_byte(static_cast<float>(original[3]) * (1.0F - target_alpha));
      return changed();
    }

    // No alpha channel, or locked transparency: erase to the background color (Photoshop).
    if (channels >= 4) {
      pixel[3] = original[3];
    }
    pixel[0] = clamp_byte(static_cast<float>(secondary.r) * target_alpha +
                          static_cast<float>(original[0]) * (1.0F - target_alpha));
    pixel[1] = clamp_byte(static_cast<float>(secondary.g) * target_alpha +
                          static_cast<float>(original[1]) * (1.0F - target_alpha));
    pixel[2] = clamp_byte(static_cast<float>(secondary.b) * target_alpha +
                          static_cast<float>(original[2]) * (1.0F - target_alpha));
    return changed();
  }

  if (!brush_stroke_paint_mode_.is_normal()) {
    const auto painted = patchy::composite_paint_rgba8(original, {primary.r, primary.g, primary.b}, target_alpha,
                                                       brush_stroke_paint_mode_, x, y, channels >= 4, locked_alpha);
    for (std::uint16_t channel = 0; channel < channels && channel < painted.size(); ++channel) {
      pixel[channel] = painted[channel];
    }
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

void CanvasWidget::observe_wet_edge_coverage(std::int32_t x, std::int32_t y, float coverage,
                                             EditColor primary) {
  const auto key = stroke_pixel_key(x, y);
  auto& union_coverage = brush_stroke_union_coverage_(x, y);
  union_coverage = std::max(union_coverage, std::clamp(coverage, 0.0F, 1.0F));
  brush_stroke_wet_edge_primary_[key] = primary;
  const auto pixel_rect = QRect(QPoint(x, y), QSize(1, 1));
  brush_stroke_wet_edge_pending_rect_ = brush_stroke_wet_edge_pending_rect_.isEmpty()
                                             ? pixel_rect
                                             : brush_stroke_wet_edge_pending_rect_.united(pixel_rect);
}

QRect CanvasWidget::finalize_pending_wet_edges(QRect dirty) {
  if (brush_stroke_wet_edge_pending_rect_.isEmpty() || document_ == nullptr ||
      !document_->active_layer_id().has_value()) {
    return dirty;
  }
  auto region = brush_stroke_wet_edge_pending_rect_.adjusted(-2, -2, 2, 2);
  brush_stroke_wet_edge_pending_rect_ = {};
  auto* layer = document_->find_layer(*document_->active_layer_id());
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return dirty;
  }
  region = region.intersected(to_qrect(layer->bounds()));
  if (region.isEmpty()) {
    return dirty;
  }

  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  const auto coverage_at = [this](std::int32_t x, std::int32_t y) {
    const auto* found = brush_stroke_union_coverage_.find(x, y);
    return found != nullptr ? *found : 0.0F;
  };
  const auto secondary = edit_color(secondary_color_);
  const auto locked = active_layer_locks_transparent_pixels();
  for (int y = region.top(); y <= region.bottom(); ++y) {
    auto row = pixels.row(y - bounds.y);
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto* union_found = brush_stroke_union_coverage_.find(x, y);
      const auto* alpha_found = brush_stroke_accumulated_alpha_.find(x, y);
      if (union_found == nullptr || alpha_found == nullptr) {
        continue;
      }

      const auto center = *union_found;
      auto gradient = 0.0F;
      for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
          if (ox == 0 && oy == 0) {
            continue;
          }
          gradient = std::max(gradient, std::abs(center - coverage_at(x + ox, y + oy)));
        }
      }
      // Photoshop 2026 calibration, observed from a scripted 100 px hard Round stroke on white:
      // the solid interior keeps about 58.6% of normal paint, while the two antialiased boundary
      // pixels retain about 65.1% and 30.7% absolute alpha (the outer pixel is 20.5% without Wet
      // Edges). This coverage-only curve lightens the wash and gently boosts its low-coverage rim.
      // It never reads canvas color or introduces fluid, pickup, paper, pigment, or drying state.
      const auto boundary = std::clamp(gradient * 4.0F, 0.0F, 1.0F);
      const auto wet_factor =
          std::clamp(0.585F + boundary * (0.035F + 1.12F * (1.0F - center)), 0.0F, 1.5F);
      const auto primary_found = brush_stroke_wet_edge_primary_.find(stroke_pixel_key(x, y));
      const auto primary = primary_found != brush_stroke_wet_edge_primary_.end()
                               ? primary_found->second
                               : edit_color(primary_color_);
      auto* pixel = row.data() + static_cast<std::size_t>(x - bounds.x) * static_cast<std::size_t>(bytes_per_pixel(pixels.format()));
      if (render_brush_stroke_pixel_from_snapshot_target(
              x, y, pixel, channels, primary, secondary, locked,
              std::clamp(*alpha_found * wet_factor, 0.0F, 1.0F), false)) {
        dirty = united_dirty_rect(dirty, QRect(QPoint(x, y), QSize(1, 1)));
      }
    }
  }
  return dirty;
}

void CanvasWidget::begin_mixer_brush_stroke() {
  patchy::begin_mixer_brush_stroke(mixer_brush_state_);
  // Sample All Layers: one merged-document snapshot per stroke, captured at
  // stroke start like the retouch tools' retouch_source_snapshot(). The pickup
  // feed then reads this snapshot instead of the active-layer snapshot; still
  // exactly one canvas-only source per stroke.
  mixer_composite_snapshot_ = QImage();
  if (mixer_sample_all_layers_ && document_ != nullptr) {
    mixer_composite_snapshot_ = render_document_image_with_processing();
  }
}

// Straight-alpha average of the STROKE-START SNAPSHOT of the active layer under
// the brush footprint (fixed 9x9 disc grid, alpha-weighted, off-layer taps
// transparent, Background off-bounds extends white). Feeds the mixer's single
// running pickup average (constraint block in core/pixel_tools.hpp). Reading
// the snapshot rather than the live layer keeps the feed canvas-only AND
// convergent: a live read would re-ingest the stroke's own fresh deposit, and
// without Photoshop's paint-amount reservoir (excluded by the patent boundary) that feedback
// loop would sustain a smear forever instead of letting it fade out.
patchy::EditColor CanvasWidget::sample_mixer_pickup(double x, double y, int brush_size) const {
  const auto radius = std::max(0.5, static_cast<double>(std::max(1, brush_size)) / 2.0);
  constexpr int kGrid = 9;
  double sum_pr = 0.0;
  double sum_pg = 0.0;
  double sum_pb = 0.0;
  double sum_alpha = 0.0;
  int taps = 0;
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      const auto offset_x = (static_cast<double>(gx) / (kGrid - 1) - 0.5) * 2.0;
      const auto offset_y = (static_cast<double>(gy) / (kGrid - 1) - 0.5) * 2.0;
      if (offset_x * offset_x + offset_y * offset_y > 1.0) {
        continue;
      }
      ++taps;
      const auto sample_x = static_cast<std::int32_t>(std::lround(x + offset_x * radius));
      const auto sample_y = static_cast<std::int32_t>(std::lround(y + offset_y * radius));
      std::array<std::uint8_t, 4> pixel{0, 0, 0, 0};
      if (!mixer_composite_snapshot_.isNull()) {
        if (sample_x >= 0 && sample_y >= 0 && sample_x < mixer_composite_snapshot_.width() &&
            sample_y < mixer_composite_snapshot_.height()) {
          const auto* source = mixer_composite_snapshot_.constScanLine(sample_y) +
                               static_cast<std::size_t>(sample_x) * 4U;
          pixel = {source[0], source[1], source[2], source[3]};
        }
      } else {
        pixel = brush_stroke_original_pixel(sample_x, sample_y);
      }
      const auto alpha = static_cast<double>(pixel[3]) / 255.0;
      sum_pr += static_cast<double>(pixel[0]) * alpha;
      sum_pg += static_cast<double>(pixel[1]) * alpha;
      sum_pb += static_cast<double>(pixel[2]) * alpha;
      sum_alpha += alpha;
    }
  }
  if (taps == 0 || sum_alpha <= 0.0) {
    return {0, 0, 0, 0};
  }
  const auto to_byte = [](double value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
  };
  return {to_byte(sum_pr / sum_alpha), to_byte(sum_pg / sum_alpha), to_byte(sum_pb / sum_alpha),
          to_byte(sum_alpha * 255.0 / taps)};
}

void CanvasWidget::install_brush_stroke_compositor(EditOptions& options, bool erase) {
  if (script_brush_progress_) options.stroke_progress = [this](Rect dirty) {
    return script_brush_progress_(finalize_pending_wet_edges(to_qrect(dirty)));
  };
  if (document_ == nullptr || !document_->active_layer_id().has_value()) {
    return;
  }

  const auto layer_id = *document_->active_layer_id();
  const auto* layer = document_->find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return;
  }

  ensure_brush_stroke_layer_snapshot(layer_id, *layer);
  brush_stroke_paint_mode_ = !erase && tool_ == CanvasTool::Brush ? brush_paint_mode_ : PaintMode{};
  const auto primary = options.primary;
  const auto secondary = options.secondary;
  const auto lock_transparent_pixels = options.lock_transparent_pixels;
  const auto source_alpha =
      std::clamp(static_cast<float>(std::clamp<int>(primary.a, 1, 255)) / 255.0F, 1.0F / 255.0F, 1.0F);

  // Opacity 0 (issue 253): the options-bar value, not primary.a, which edit_options floors at
  // 1/255 for the core writers. The stroke still runs (snapshot, accumulators, undo entry) but
  // no pixel is gated in or written, so pressure, Flow, Airbrush and dynamics cannot leak paint.
  // The Mixer Brush has no Opacity field (current_brush_edit_options forces 100).
  if (brush_opacity_ <= 0 && tool_ != CanvasTool::MixerBrush) {
    options.stroke_pixel_gate = [](std::int32_t, std::int32_t) { return false; };
    options.stroke_pixel_writer = [](std::int32_t, std::int32_t, std::uint8_t*, std::uint16_t, float,
                                     const EditColor&) { return false; };
    return;
  }

  if (!erase && tool_ == CanvasTool::MixerBrush) {
    const auto brush_size = options.brush_size;
    options.dab_primary_provider =
        [this, brush_size](double x, double y, const EditColor& loaded_color) {
          const auto picked = sample_mixer_pickup(x, y, brush_size);
          return patchy::mixer_brush_dab_color(mixer_brush_state_, x, y, brush_size, loaded_color,
                                               picked, mixer_wet_, mixer_load_, mixer_mix_);
        };
  }

  // Wet Edges is derived from one accumulated stroke footprint. Palette editing intentionally
  // keeps its hard snapped write semantics instead of introducing off-palette edge tones.
  if (!erase && tool_ == CanvasTool::Brush && options.brush_dynamics.wet_edges &&
      options.palette_snap == nullptr) {
    options.stroke_coverage_observer =
        [this](std::int32_t x, std::int32_t y, float coverage, const EditColor& dab_primary) {
          observe_wet_edge_coverage(x, y, coverage, dab_primary);
        };
  }

  if (!erase && tool_ == CanvasTool::PatternStamp && pattern_stamp_pattern_.has_value() &&
      pattern_stamp_origin_.has_value()) {
    // Patent constraint: this is a direct, static tile lookup. Never derive an
    // illumination map from the destination or synthesize/weight texture
    // channels from stroke input here. See docs/brushes.md.
    const auto tile = pattern_stamp_pattern_->tile;
    const auto origin = *pattern_stamp_origin_;
    const auto width = tile.width();
    const auto height = tile.height();
    if (width > 0 && height > 0 && tile.format() == PixelFormat::rgba8()) {
      options.stroke_pixel_gate = [this, tile, origin, width, height, source_alpha](std::int32_t x,
                                                                                   std::int32_t y) {
        const auto* source = tile.pixel(positive_modulo(x - origin.x(), width),
                                        positive_modulo(y - origin.y(), height));
        const auto pattern_alpha = static_cast<float>(source[3]) / 255.0F;
        const auto cap = source_alpha * pattern_alpha;
        const auto* found = brush_stroke_accumulated_alpha_.find(x, y);
        return cap > 0.0F && (found == nullptr || *found < cap - 0.0005F);
      };
      const auto* palette_snap = options.palette_snap;
      options.stroke_pixel_writer =
          [this, tile, origin, width, height, source_alpha, secondary, lock_transparent_pixels,
           palette_snap](std::int32_t x, std::int32_t y, std::uint8_t* pixel,
                         std::uint16_t channels, float coverage, const EditColor&) {
            const auto* source = tile.pixel(positive_modulo(x - origin.x(), width),
                                            positive_modulo(y - origin.y(), height));
            const auto alpha = std::clamp(
                static_cast<int>(std::lround(static_cast<float>(source[3]) * source_alpha)), 0, 255);
            if (alpha == 0) {
              return false;
            }
            const EditColor sampled{source[0], source[1], source[2], static_cast<std::uint8_t>(alpha)};
            return write_brush_stroke_pixel_from_snapshot(
                x, y, pixel, channels, sampled, secondary, lock_transparent_pixels, coverage, false,
                palette_snap);
          };
      return;
    }
  }

  options.stroke_pixel_gate = [this, source_alpha](std::int32_t x, std::int32_t y) {
    const auto* found = brush_stroke_accumulated_alpha_.find(x, y);
    return found == nullptr || *found < source_alpha - 0.0005F;
  };
  const auto* palette_snap = options.palette_snap;
  options.stroke_pixel_writer = [this, secondary, lock_transparent_pixels, erase, palette_snap](
                                    std::int32_t x, std::int32_t y, std::uint8_t* pixel,
                                    std::uint16_t channels, float coverage,
                                    const EditColor& dab_primary) {
    return write_brush_stroke_pixel_from_snapshot(x, y, pixel, channels, dab_primary, secondary,
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
    // A non-zero Opacity keeps its 1% floor under light pressure; 0% stays 0 (issue 253).
    brush.opacity = std::clamp(static_cast<int>(std::lround(static_cast<double>(brush_opacity_) * scale)),
                               brush_opacity_ > 0 ? 1 : 0, 100);
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

// The dab-spaced path (Soft, Flow, Build-up, Mixer; never a bitmap tip) stamps the procedural
// footprint, so it takes the Square the way apply_brush_tip_to_options gives it to the other
// paths: on the Brush and Eraser only (docs/brushes.md).
patchy::BrushShape CanvasWidget::dab_stroke_brush_shape() const noexcept {
  return tool_ == CanvasTool::Brush || tool_ == CanvasTool::Eraser ? brush_shape_ : patchy::BrushShape::Round;
}

EditOptions CanvasWidget::current_brush_edit_options(const EffectiveBrushInput& brush) const {
  const auto opacity = tool_ == CanvasTool::MixerBrush ? 100 : brush.opacity;
  return edit_options(primary_color_, secondary_color_, brush.size, opacity, brush.softness, fill_shapes_,
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
  if (tool_ != CanvasTool::PatternStamp || brush_tip_ != nullptr) {
    apply_brush_tip_to_options(options, brush.size, brush.softness);
  }
  if (tool_ == CanvasTool::PatternStamp) {
    options.brush_dynamics = {};
  }
  if (tool_ == CanvasTool::MixerBrush) {
    // Mixer accepts imported/static tip bitmaps, but ordinary Brush dynamics are a separate
    // engine and must not perturb its deliberately simple one-sample color model.
    options.brush_dynamics = {};
  }
  if (erase) {
    options.brush_dynamics = {};  // v1: dynamics are Brush-only; erase strokes stay predictable
    if (brush_tip_ == nullptr) {
      options.brush_tip = nullptr;  // Round + session dynamics: the eraser stays procedural
    }
  }
  if (options.brush_tip != nullptr) {
    install_brush_stroke_compositor(options, erase);
    const auto was_holding = brush_tip_stroke_state_.first_dab_pending;
    const auto dirty = to_qrect(
        patchy::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                    options, erase, brush_tip_stroke_state_));
    remember_held_first_dab_pen_sample(was_holding);
    return finalize_pending_wet_edges(dirty);
  }

  install_brush_stroke_compositor(options, erase);
  // A hard round segment through the stroke gate leaves its end disk at full coverage, so the
  // next one starting there skips that disk (GitHub issue 135). Anything that could make those
  // pixels change again (another color or opacity, a feathered selection, per-pixel observers)
  // breaks the chain.
  const auto chains = hard_round_skip_enabled_ && (tool_ == CanvasTool::Brush || tool_ == CanvasTool::Eraser) &&
                      brush_opacity_ > 0 &&
                      options.brush_tip == nullptr && options.brush_shape == patchy::BrushShape::Round &&
                      std::clamp(options.brush_roundness, 1, 100) >= 99 && options.brush_softness <= 0 &&
                      options.brush_size > 1 && options.stroke_pixel_gate && !options.selection_coverage &&
                      !options.stroke_coverage_observer && !options.dab_primary_provider;
  const auto radius = std::max(1, options.brush_size) / 2;
  if (const auto& end = brush_stroke_hard_round_end_;
      chains && end.has_value() && end->point.x() == from.x() && end->point.y() == from.y() &&
      end->erase == erase && end->primary.r == options.primary.r &&
      end->primary.g == options.primary.g && end->primary.b == options.primary.b &&
      end->primary.a == options.primary.a) {
    options.stroke_covered_disk =
        patchy::EditOptions::CoveredDisk{from.x(), from.y(), static_cast<double>(end->radius)};
  }
  auto dirty = to_qrect(
      patchy::paint_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(), to.x(), to.y(),
                                  options, erase));
  if (chains) {
    brush_stroke_hard_round_end_ = HardRoundSegmentEnd{to, radius, options.primary, erase};
  } else {
    brush_stroke_hard_round_end_.reset();
  }
  if (stamp_endpoint) {
    dirty = united_dirty_rect(
        dirty, to_qrect(patchy::paint_brush(*document_, *document_->active_layer_id(),
                                            static_cast<int>(std::lround(to.x())),
                                            static_cast<int>(std::lround(to.y())), options, erase)));
  }
  return finalize_pending_wet_edges(dirty);
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
    options.brush_shape = dab_stroke_brush_shape();
    install_brush_stroke_compositor(options, erase);
    const auto point_f = QPointF(point);
    const auto dirty = draw_brush_dab(point_f, erase, options);
    brush_stroke_last_stamp_position_ = point_f;
    brush_stroke_distance_since_last_stamp_ = 0.0;
    return dirty;
  }
  if (tool_ != CanvasTool::PatternStamp || brush_tip_ != nullptr) {
    apply_brush_tip_to_options(options, brush.size, brush.softness);
  }
  if (tool_ == CanvasTool::PatternStamp) {
    options.brush_dynamics = {};
  }
  if (tool_ == CanvasTool::MixerBrush) {
    options.brush_dynamics = {};
  }
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
    // point (invisible for static stamps, visibly double-jittered with dynamics). A tip whose
    // angle follows the stroke direction holds this dab until the pointer moves (#221).
    const auto was_holding = brush_tip_stroke_state_.first_dab_pending;
    const auto dirty = to_qrect(patchy::paint_brush_segment(
        *document_, *document_->active_layer_id(), point.x(), point.y(), point.x(), point.y(),
        options, erase, brush_tip_stroke_state_));
    remember_held_first_dab_pen_sample(was_holding);
    return finalize_pending_wet_edges(dirty);
  }
  install_brush_stroke_compositor(options, erase);
  const auto dirty = to_qrect(
      patchy::paint_brush(*document_, *document_->active_layer_id(), point.x(), point.y(), options, erase));
  return finalize_pending_wet_edges(dirty);
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
  const auto dirty = to_qrect(patchy::paint_stationary_airbrush_dab(
      *document_, *document_->active_layer_id(), point.x(), point.y(), options,
      brush_tip_stroke_state_));
  return finalize_pending_wet_edges(dirty);
}

void CanvasWidget::remember_held_first_dab_pen_sample(bool was_holding) {
  if (!was_holding && brush_tip_stroke_state_.first_dab_pending) {
    held_first_dab_pen_sample_ = active_pen_input_sample_;
  }
}

QRect CanvasWidget::finish_brush_tip_stroke(bool erase) {
  if (!brush_tip_stroke_state_.first_dab_pending || editing_grayscale_target() ||
      document_ == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }
  // A click's dab uses the press's pen sample: a tablet release reports zero pressure.
  const auto release_sample = active_pen_input_sample_;
  active_pen_input_sample_ = held_first_dab_pen_sample_;
  const auto restore_sample =
      qScopeGuard([this, release_sample] { active_pen_input_sample_ = release_sample; });
  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  apply_brush_tip_to_options(options, brush.size, brush.softness);
  install_brush_stroke_compositor(options, erase);
  const auto dirty = to_qrect(patchy::finish_brush_tip_stroke(
      *document_, *document_->active_layer_id(), options, erase, brush_tip_stroke_state_));
  return finalize_pending_wet_edges(dirty);
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
      blend_mask_at(target->pixels->format(), value, paint_value, coverage);
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
      blend_mask_at(target->pixels->format(), value, paint_value, coverage);
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
  if (smudge_strength_ <= 0) {
    return {};  // Strength 0 smudges nothing (issue 253)
  }

  const auto brush = effective_brush_input();
  auto options = current_brush_edit_options(brush);
  options.smudge_strength = static_cast<float>(smudge_strength_) / 100.0F;  // GitHub issue 675
  smudge_state_.single_threaded = qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED");
  return to_qrect(patchy::smudge_brush_segment(*document_, *document_->active_layer_id(), from.x(), from.y(),
                                                  to.x(), to.y(), options, smudge_state_));
}

CanvasTool CanvasWidget::local_adjustment_operation() const noexcept {
  return local_adjustment_alt_swap_ ? alt_swapped_toning_tool(tool_) : tool_;
}

// The most a stroke of `operation` applies (the stroke's alpha cap): Strength, except that
// Dodge and Burn map it the way Photoshop's Exposure builds up over a stroke (GitHub #661).
float CanvasWidget::local_adjustment_stroke_amount(CanvasTool operation) const noexcept {
  const auto strength = static_cast<float>(local_adjustment_strength_) / 100.0F;
  if (operation != CanvasTool::Dodge && operation != CanvasTool::Burn) {
    return strength;
  }
  return static_cast<float>(dodge_burn::stroke_amount(static_cast<double>(strength), local_protect_tones_));
}

// Blur and Sharpen paint per brush dab (GitHub issue 660 follow-ups; docs/brushes-local-adjustments.md, "Blur and
// Sharpen"). Dabs sit every quarter of the brush diameter along the stroke, measured by distance
// so mouse-event density does not matter; each dab reads the pixels as they are now (a copy of
// its rectangle plus the kernel's reach), applies the fixed alpha-aware 3x3 binomial blur once,
// or for Sharpen moves every channel by kSharpenGain times the lightness minus its fixed 21-tap
// blur, and mixes the result in at Strength times the footprint's coverage. Overlapping dabs and repeat passes build up, as Photoshop's
// tools do. On a layer with alpha whose transparent pixels are not locked, Blur blurs alpha with
// the same kernel (premultiplied), so an edge against transparency softens and the layer grows to
// the stroke like a brush stroke (desk card local-039). Same patent boundary as
// local_adjustment_brush_segment: one fixed kernel, the footprint alone picks the pixels, nothing
// measures or ranks the image.
QRect CanvasWidget::blur_sharpen_brush_segment(QPoint from, QPoint to, CanvasTool operation) {
  auto* layer = active_pixel_layer();
  if (document_ == nullptr || layer == nullptr) {
    return {};
  }
  const bool sharpen = operation == CanvasTool::SharpenBrush;
  const bool layer_alpha_free =
      std::as_const(*layer).pixels().format().channels >= 4 && !active_layer_locks_transparent_pixels();
  const bool spread_alpha = !sharpen && layer_alpha_free;
  const bool sharpen_alpha = sharpen && layer_alpha_free;
  if (spread_alpha) {
    // Grow the layer to the segment's dabs and the kernel's reach, clipped to the canvas, so a
    // blur past the layer's stored edge has pixels to spread into.
    const auto reach = std::max(1, effective_brush_input().size) / 2 + 2;
    const auto segment_rect = QRect(QPoint(std::min(from.x(), to.x()) - reach, std::min(from.y(), to.y()) - reach),
                                    QPoint(std::max(from.x(), to.x()) + reach, std::max(from.y(), to.y()) + reach))
                                  .intersected(QRect(0, 0, document_->width(), document_->height()));
    if (!segment_rect.isEmpty()) {
      patchy::expand_layer_to_include_rect(*layer, to_core_rect(segment_rect));
    }
  }
  auto& pixels = layer->pixels();
  if (std::exchange(retouch_press_times_layer_detach_, false)) {
    const auto started = std::chrono::steady_clock::now();
    static_cast<void>(pixels.data());
    last_retouch_press_timings_.layer_detach_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  }
  const auto format = pixels.format();
  if (pixels.empty() || format.channels < 3) {
    return {};
  }
  const bool linear = format.bit_depth == BitDepth::Float32;
  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  const auto spacing = std::max(1.0, static_cast<double>(std::max(1, brush.size)) * 0.25);
  const auto layer_bounds = layer->bounds();
  const auto layer_rect = to_qrect(layer_bounds);
  const auto paint_rect = QRect(0, 0, document_->width(), document_->height()).intersected(layer_rect);
  if (paint_rect.isEmpty()) {
    return {};
  }
  const auto strength = static_cast<double>(local_adjustment_strength_) / 100.0;
  const auto* palette_snap = palette_snap_for_edits();
  const auto selection_active = has_selection();
  auto* layer_bytes = pixels.data().data();  // detaches shared storage on this thread
  const auto stride = pixels.stride_bytes();
  const auto pixel_bytes = bytes_per_pixel(format);
  const auto encode = [linear](float value) {
    return linear ? srgb_encode(static_cast<double>(value) / 255.0) * 255.0 : static_cast<double>(value);
  };
  const auto decode = [linear](double value) {
    return static_cast<float>(linear ? srgb_decode(std::clamp(value, 0.0, 255.0) / 255.0) * 255.0
                                     : std::clamp(value, 0.0, 255.0));
  };

  QRect dirty;
  std::vector<double> source;
  std::vector<double> sharpen_plane;  // per copy pixel: lightness times alpha, alpha
  std::vector<double> sharpen_rows;   // per copy row and dab column: weighted lightness, weight
  const auto dab = [&](double center_x, double center_y) {
    const auto left = static_cast<int>(std::floor(center_x)) - radius - 1;
    const auto top = static_cast<int>(std::floor(center_y)) - radius - 1;
    const auto dab_rect = QRect(left, top, radius * 2 + 3, radius * 2 + 3).intersected(paint_rect);
    if (dab_rect.isEmpty()) {
      return;
    }
    // The dab reads the pixels as they were before it: its rectangle and the kernel's reach.
    const auto reach = sharpen ? kSharpenRadius : 1;
    const auto copy_rect = dab_rect.adjusted(-reach, -reach, reach, reach).intersected(layer_rect);
    const auto copy_width = copy_rect.width();
    source.resize(static_cast<std::size_t>(copy_width) * static_cast<std::size_t>(copy_rect.height()) * 4U);
    if (sharpen) {
      // Per copy pixel: lightness times alpha, and alpha (0..1).
      sharpen_plane.resize(source.size() / 2U);
    }
    for (int y = copy_rect.top(); y <= copy_rect.bottom(); ++y) {
      const auto* row = layer_bytes + static_cast<std::size_t>(y - layer_bounds.y) * stride;
      for (int x = copy_rect.left(); x <= copy_rect.right(); ++x) {
        const auto values = load_pixel(format, row + static_cast<std::size_t>(x - layer_bounds.x) * pixel_bytes);
        auto* out = source.data() + (static_cast<std::size_t>(y - copy_rect.top()) * static_cast<std::size_t>(copy_width) +
                                     static_cast<std::size_t>(x - copy_rect.left())) * 4U;
        out[0] = encode(values[0]);
        out[1] = encode(values[1]);
        out[2] = encode(values[2]);
        out[3] = static_cast<double>(values[3]);
        if (sharpen) {
          auto* plane = sharpen_plane.data() + (out - source.data()) / 2;
          plane[1] = out[3] / 255.0;
          plane[0] = sharpen_lightness(out[0], out[1], out[2]) * plane[1];
        }
      }
    }
    const auto sample = [&](int x, int y) {
      x = std::clamp(x, copy_rect.left(), copy_rect.right());
      y = std::clamp(y, copy_rect.top(), copy_rect.bottom());
      return source.data() + (static_cast<std::size_t>(y - copy_rect.top()) * static_cast<std::size_t>(copy_width) +
                              static_cast<std::size_t>(x - copy_rect.left())) * 4U;
    };
    // Sharpen: the alpha-weighted lightness of every copy row blurred across the dab's columns
    // (the first half of the separable kernel); the bands below finish it down the columns.
    const auto dab_width = dab_rect.width();
    const auto copy_rows = copy_rect.height();
    const auto blur_rows_across = [&](int row_begin, int row_end) {
      for (int y = row_begin; y < row_end; ++y) {
        auto* out = sharpen_rows.data() +
                    static_cast<std::size_t>(y - copy_rect.top()) * static_cast<std::size_t>(dab_width) * 2U;
        for (int x = dab_rect.left(); x <= dab_rect.right(); ++x) {
          double weighted = 0.0;
          double weights = 0.0;
          const auto* plane_row = sharpen_plane.data() +
                                  static_cast<std::size_t>(y - copy_rect.top()) * static_cast<std::size_t>(copy_width) * 2U;
          for (int k = -kSharpenRadius; k <= kSharpenRadius; ++k) {
            const auto column = std::clamp(x + k, copy_rect.left(), copy_rect.right()) - copy_rect.left();
            const auto weight = kSharpenKernel[static_cast<std::size_t>(k + kSharpenRadius)];
            weighted += weight * plane_row[static_cast<std::size_t>(column) * 2U];
            weights += weight * plane_row[static_cast<std::size_t>(column) * 2U + 1U];
          }
          out[static_cast<std::size_t>(x - dab_rect.left()) * 2U] = weighted;
          out[static_cast<std::size_t>(x - dab_rect.left()) * 2U + 1U] = weights;
        }
      }
      return QRect();
    };
    // One band of the dab's rows; each pixel reads only the copy and writes only itself.
    const auto band = [&](int row_begin, int row_end) {
      QRect band_dirty;
      for (int y = row_begin; y < row_end; ++y) {
        auto* row = layer_bytes + static_cast<std::size_t>(y - layer_bounds.y) * stride;
        for (int x = dab_rect.left(); x <= dab_rect.right(); ++x) {
          auto coverage = brush_shape_coverage(static_cast<double>(x) - center_x, static_cast<double>(y) - center_y,
                                               radius, brush.softness, brush.roundness, brush.angle_degrees);
          if (coverage <= 0.0F) {
            continue;
          }
          const QPoint point(x, y);
          if (selection_active) {
            if (!selection_allows(point)) {
              continue;
            }
            coverage *= static_cast<float>(selection_alpha_at(point)) / 255.0F;
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
          const auto* center = sample(x, y);
          if (format.channels >= 4 && center[3] <= 0.0 && !spread_alpha) {
            continue;
          }
          if (sharpen) {
            // `weights` ends as the 21-tap blur of alpha (0..1): the kernel sums to 1.
            double weighted = 0.0;
            double weights = 0.0;
            for (int k = -kSharpenRadius; k <= kSharpenRadius; ++k) {
              const auto source_row = std::clamp(y + k, copy_rect.top(), copy_rect.bottom()) - copy_rect.top();
              const auto* blurred_row = sharpen_rows.data() +
                                        (static_cast<std::size_t>(source_row) * static_cast<std::size_t>(dab_width) +
                                         static_cast<std::size_t>(x - dab_rect.left())) * 2U;
              const auto weight = kSharpenKernel[static_cast<std::size_t>(k + kSharpenRadius)];
              weighted += weight * blurred_row[0];
              weights += weight * blurred_row[1];
            }
            if (weights <= std::numeric_limits<double>::epsilon()) {
              continue;
            }
            const auto change =
                kSharpenGain * (sharpen_lightness(center[0], center[1], center[2]) - weighted / weights);
            auto* destination = row + static_cast<std::size_t>(x - layer_bounds.x) * pixel_bytes;
            const auto before = load_pixel(format, destination);
            const auto amount = strength * static_cast<double>(coverage);
            auto value = before;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
              // The same change on every channel, so the pixel keeps its color unless it clips.
              value[channel] = decode(std::clamp(center[channel] + change * amount, 0.0, 255.0));
            }
            if (sharpen_alpha) {
              // Alpha takes the same step against its own blur, so a soft edge against
              // transparency steepens (desk card local-039 follow-up). A transparent pixel
              // never rises: its alpha is at or below any blur of it.
              const auto alpha_change = kSharpenGain * (center[3] - weights * 255.0);
              if (std::abs(alpha_change) > 1e-9) {
                value[3] = static_cast<float>(std::clamp(center[3] + alpha_change * amount, 0.0, 255.0));
              }
            }
            store_pixel(format, destination, value);
            if (palette_snap != nullptr) {
              patchy::snap_pixel_to_palette(destination, format.channels, *palette_snap);
            }
            if (load_pixel(format, destination) != before) {
              band_dirty = band_dirty.united(QRect(point, QSize(1, 1)));
            }
            continue;
          }
          constexpr std::array<double, 3> kBinomial{1.0, 2.0, 1.0};
          std::array<double, 3> premultiplied{};
          double alpha_weight = 0.0;
          double alpha_sum = 0.0;  // the binomial sum of alpha (0..255 each); exact in doubles
          for (int offset_y = -1; offset_y <= 1; ++offset_y) {
            for (int offset_x = -1; offset_x <= 1; ++offset_x) {
              const auto* neighbor = sample(x + offset_x, y + offset_y);
              const auto kernel = kBinomial[static_cast<std::size_t>(offset_x + 1)] *
                                  kBinomial[static_cast<std::size_t>(offset_y + 1)];
              const auto weight = kernel * (neighbor[3] / 255.0);
              alpha_weight += weight;
              alpha_sum += kernel * neighbor[3];
              for (std::size_t channel = 0; channel < premultiplied.size(); ++channel) {
                premultiplied[channel] += weight * neighbor[channel];
              }
            }
          }
          if (alpha_weight <= std::numeric_limits<double>::epsilon()) {
            continue;
          }
          auto* destination = row + static_cast<std::size_t>(x - layer_bounds.x) * pixel_bytes;
          const auto before = load_pixel(format, destination);
          const auto amount = strength * static_cast<double>(coverage);
          auto value = before;
          const auto blurred_alpha = alpha_sum / 16.0;
          if (spread_alpha && blurred_alpha != center[3]) {
            // Alpha moves too: mix premultiplied, so a transparent pixel takes its neighbors' color.
            const auto blurred_part = blurred_alpha * amount;
            const auto center_part = center[3] * (1.0 - amount);
            const auto mixed_alpha = blurred_part + center_part;
            if (mixed_alpha <= 0.0) {
              continue;
            }
            for (std::size_t channel = 0; channel < premultiplied.size(); ++channel) {
              const auto blurred = std::clamp(premultiplied[channel] / alpha_weight, 0.0, 255.0);
              value[channel] = decode((blurred * blurred_part + center[channel] * center_part) / mixed_alpha);
            }
            value[3] = static_cast<float>(std::clamp(mixed_alpha, 0.0, 255.0));
          } else {
            for (std::size_t channel = 0; channel < premultiplied.size(); ++channel) {
              const auto blurred = premultiplied[channel] / alpha_weight;
              value[channel] = decode(std::clamp(blurred, 0.0, 255.0) * amount + center[channel] * (1.0 - amount));
            }
          }
          store_pixel(format, destination, value);
          if (palette_snap != nullptr) {
            patchy::snap_pixel_to_palette(destination, format.channels, *palette_snap);
          }
          if (load_pixel(format, destination) != before) {
            band_dirty = band_dirty.united(QRect(point, QSize(1, 1)));
          }
        }
      }
      return band_dirty;
    };
    constexpr std::int64_t kParallelPixels = 8192;
    constexpr int kMinRowsPerBand = 16;
    const auto dab_pixels = static_cast<std::int64_t>(dab_rect.width()) * dab_rect.height();
    render_cache_diagnostics_.toning_segment_pixels += dab_pixels;
    auto workers = 1;
    if (dab_pixels >= kParallelPixels && dab_rect.height() >= 2 * kMinRowsPerBand &&
        !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
      workers = patchy::max_blocking_fanout_workers(
          std::clamp(std::min(dab_rect.height() / kMinRowsPerBand, patchy::hardware_worker_threads()), 1, 16));
    }
    // Rows [first, last] through `rows`, over `workers` bands when there is more than one.
    const auto run_rows = [workers](int first, int last, const auto& rows) {
      if (workers < 2) {
        return rows(first, last + 1);
      }
      const auto rows_per_band = (last - first + 1 + workers - 1) / workers;
      std::vector<std::future<QRect>> futures;
      futures.reserve(static_cast<std::size_t>(workers));
      for (auto row_begin = first; row_begin <= last; row_begin += rows_per_band) {
        const auto row_end = std::min(row_begin + rows_per_band, last + 1);
        futures.push_back(std::async(std::launch::async, [&rows, row_begin, row_end] { return rows(row_begin, row_end); }));
      }
      QRect united;
      for (auto& future : futures) {
        united = united.united(future.get());
      }
      return united;
    };
    if (sharpen) {
      sharpen_rows.assign(static_cast<std::size_t>(copy_rows) * static_cast<std::size_t>(dab_width) * 2U, 0.0);
      run_rows(copy_rect.top(), copy_rect.bottom(), blur_rows_across);
    }
    dirty = dirty.united(run_rows(dab_rect.top(), dab_rect.bottom(), band));
  };

  // Dabs by distance along the stroke: the first at the press, then every `spacing`.
  if (!toning_dab_placed_) {
    dab(static_cast<double>(from.x()), static_cast<double>(from.y()));
    toning_dab_placed_ = true;
    toning_dab_distance_ = 0.0;
  }
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto length = std::sqrt(dx * dx + dy * dy);
  if (length > 0.0) {
    auto along = spacing - toning_dab_distance_;
    while (along <= length) {
      dab(static_cast<double>(from.x()) + dx * along / length, static_cast<double>(from.y()) + dy * along / length);
      along += spacing;
    }
    toning_dab_distance_ = length - (along - spacing);
  }
  tick_processing_operation();
  return dirty;
}

QRect CanvasWidget::local_adjustment_brush_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  // Any return before the walk below ends the chain of continued segments.
  auto continued_from_end = std::exchange(clone_stroke_segment_end_, std::nullopt);
  if (document_ == nullptr || layer == nullptr || !document_->active_layer_id().has_value()) {
    return {};
  }
  if (const auto detail_operation = local_adjustment_operation();
      detail_operation == CanvasTool::BlurBrush || detail_operation == CanvasTool::SharpenBrush) {
    return blur_sharpen_brush_segment(from, to, detail_operation);  // per dab, builds up
  }

  const auto layer_id = *document_->active_layer_id();
  ensure_brush_stroke_layer_snapshot(layer_id, std::as_const(*layer));
  if (!brush_stroke_layer_snapshot_.has_value() || brush_stroke_layer_snapshot_->layer_id != layer_id) {
    return {};
  }
  const auto& snapshot = *brush_stroke_layer_snapshot_;
  const auto& source_pixels = snapshot.pixels;
  const auto channels = source_pixels.format().channels;
  if (source_pixels.empty() || channels < 3) {
    return {};
  }
  if (source_pixels.format().bit_depth != BitDepth::UInt8) {
    clone_stroke_segment_end_ = std::move(continued_from_end);
    return local_adjustment_brush_segment_deep(from, to);
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

  // Every pixel the walk visits lies in the layer (stroke_rect), so the snapshot is read by row.
  const auto* source_bytes = std::as_const(source_pixels).data().data();
  const auto source_stride = source_pixels.stride_bytes();
  const auto source_pixel = [&](int document_x, int document_y) {
    const auto* pixel = source_bytes + static_cast<std::size_t>(document_y - snapshot.bounds.y) * source_stride +
                        static_cast<std::size_t>(document_x - snapshot.bounds.x) * channels;
    return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2],
                                       channels >= 4 ? pixel[3] : std::uint8_t{255}};
  };

  // Patent boundary (July 2026): these are deliberately fixed local operations.
  // Do not add edge ranking, patch matching, deconvolution, automatic boundary
  // isolation, or stroke-start color classification here. US 7724980, US 8687913,
  // US 9142009, and the abandoned US 2007/0188510 all cover variants of those
  // adaptive techniques. The brush footprint alone chooses which pixels change.
  const auto operation = local_adjustment_operation();
  // The Range's calibrated curve (GitHub #661), sampled once per stroke at every value it can be
  // read at (GitHub #660): with Protect Tones the lightness change for every 54R + 183G + 19B,
  // without it each channel's adjusted byte. The same doubles curve_at gives per pixel.
  const auto* tone_table = (operation == CanvasTool::Dodge || operation == CanvasTool::Burn)
                               ? &dodge_burn_stroke_table(operation == CanvasTool::Dodge)
                               : nullptr;
  const auto adjusted_source_pixel = [&](QPoint point) {
    const auto center = source_pixel(point.x(), point.y());
    std::array<std::uint8_t, 3> result{center[0], center[1], center[2]};

    if (tone_table != nullptr) {
      if (local_protect_tones_) {
        const auto lightness_change =
            tone_table->lightness_change[static_cast<std::size_t>(54 * center[0] + 183 * center[1] + 19 * center[2])];
        for (std::size_t channel = 0; channel < result.size(); ++channel) {
          result[channel] = clamp_byte(static_cast<float>(static_cast<double>(center[channel]) + lightness_change));
        }
      } else {
        for (std::size_t channel = 0; channel < result.size(); ++channel) {
          result[channel] = tone_table->channel[center[channel]];
        }
      }
      return result;
    }

    const auto red = static_cast<double>(center[0]);
    const auto green = static_cast<double>(center[1]);
    const auto blue = static_cast<double>(center[2]);
    const auto lightness = (54.0 * red + 183.0 * green + 19.0 * blue) / (256.0 * 255.0);

    if (operation == CanvasTool::Sponge) {
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
  if (std::exchange(retouch_press_times_layer_detach_, false)) {
    // The press's first segment: the copy-on-write detach on its own (patchy_perf_tests
    // toningstroke, GitHub #660; it happens on the first write below anyway).
    const auto started = std::chrono::steady_clock::now();
    static_cast<void>(pixels.data());
    last_retouch_press_timings_.layer_detach_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  }
  const auto mutable_channels = pixels.format().channels;
  const auto strength = local_adjustment_stroke_amount(operation);
  const auto* palette_snap = palette_snap_for_edits();
  const auto selection_active = has_selection();
  // One pixel: `destination` is its layer bytes, `cap_at(x, y)` its stroke alpha cap (the
  // map, or a band worker's Cursor). True when its color changed.
  const auto adjust_pixel = [&](int x, int y, float coverage, std::uint8_t* destination, auto& cap_at) -> bool {
    const QPoint point(x, y);
    // Without a selection selection_allows() is always true.
    if (selection_active) {
      if (!selection_allows(point)) {
        return false;
      }
      coverage *= static_cast<float>(selection_alpha_at(point)) / 255.0F;
    }
    if (coverage <= 0.0F) {
      return false;
    }
    if (palette_snap != nullptr) {
      if (coverage < palette_snap->coverage_threshold) {
        return false;
      }
      coverage = 1.0F;
    }
    coverage = capped_alpha_coverage([&]() -> float& { return cap_at(x, y); }, coverage, strength);
    if (coverage <= 0.0F) {
      return false;
    }

    if (mutable_channels >= 4 && destination[3] == 0) {
      return false;
    }
    const auto adjusted = adjusted_source_pixel(point);
    const auto amount = strength * coverage;
    const auto before = std::array<std::uint8_t, 3>{destination[0], destination[1], destination[2]};
    for (std::size_t channel = 0; channel < adjusted.size(); ++channel) {
      destination[channel] = clamp_byte(static_cast<float>(adjusted[channel]) * amount +
                                        static_cast<float>(destination[channel]) * (1.0F - amount));
    }
    if (palette_snap != nullptr) {
      patchy::snap_pixel_to_palette(destination, mutable_channels, *palette_snap);
    }
    return destination[0] != before[0] || destination[1] != before[1] || destination[2] != before[2];
  };

  auto* layer_bytes = pixels.data().data();  // detaches shared storage on this thread
  const auto stride = pixels.stride_bytes();
  const auto destination_at = [&](int x, int y) {
    return layer_bytes + static_cast<std::size_t>(y - snapshot.bounds.y) * stride +
           static_cast<std::size_t>(x - snapshot.bounds.x) * mutable_channels;
  };
  if (radius == 0) {
    QRect dirty;
    visit_pixel_line(from, to, [&](QPoint point) {
      if (stroke_rect.contains(point) &&
          adjust_pixel(point.x(), point.y(), 1.0F, destination_at(point.x(), point.y()), brush_stroke_alpha_caps_)) {
        dirty = dirty.united(QRect(point, QSize(1, 1)));
      }
    });
    return dirty;
  }

  // Only the segment's capsule, and of a segment continuing the last one only what can
  // change, fanned out over bands like the Clone stamp (GitHub issues 597 and 660). Each
  // pixel reads only the stroke-start snapshot and writes only itself, so the bytes equal
  // the whole-rectangle walk on one thread.
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto segment_length_squared = dx * dx + dy * dy;
  auto chain_brush = brush;
  chain_brush.opacity = local_adjustment_strength_;
  clone_stroke_segment_end_ = std::move(continued_from_end);
  const CloneSegmentRows rows(from, to, radius, clone_stamp_skip_enabled_,
                              clone_stamp_continues_previous_segment(from, radius, chain_brush),
                              full_coverage_radius(radius, brush.softness));
  std::int64_t segment_pixels = 0;
  const auto segment_rows = segment_row_runs(rows, stroke_rect, segment_pixels);
  render_cache_diagnostics_.toning_segment_pixels += segment_pixels;
  const auto paint_row = [&](const SegmentRowRuns& row, auto& cap_at) -> QRect {
    auto row_dirty_left = std::numeric_limits<int>::max();
    auto row_dirty_right = std::numeric_limits<int>::min();
    const auto y = row.y;
    for (std::size_t run = 0; run < row.count; ++run) {
      for (int x = row.runs[run].first; x < row.runs[run].end; ++x) {
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
        if (adjust_pixel(x, y, coverage, destination_at(x, y), cap_at)) {
          row_dirty_left = std::min(row_dirty_left, x);
          row_dirty_right = std::max(row_dirty_right, x);
        }
      }
    }
    return row_dirty_left <= row_dirty_right ? QRect(row_dirty_left, y, row_dirty_right - row_dirty_left + 1, 1)
                                             : QRect();
  };
  const auto dirty = walk_segment_rows(segment_rows, segment_pixels, brush_stroke_alpha_caps_, paint_row,
                                       [this] { tick_processing_operation(); });
  remember_clone_stroke_segment_end(to, radius, chain_brush);
  return dirty;
}

// local_adjustment_brush_segment for 16/32-bit layers (docs/high-bit-depth.md): the same
// fixed local operations on unrounded values. 32-bit layers adjust their display-encoded
// values (and decode the result), so the tools behave as they do at 8 and 16 bits.
QRect CanvasWidget::local_adjustment_brush_segment_deep(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  // Any return before the walk below ends the chain of continued segments.
  auto continued_from_end = std::exchange(clone_stroke_segment_end_, std::nullopt);
  if (document_ == nullptr || layer == nullptr || !brush_stroke_layer_snapshot_.has_value()) {
    return {};
  }
  const auto& snapshot = *brush_stroke_layer_snapshot_;
  const auto& source_pixels = snapshot.pixels;
  const auto format = source_pixels.format();
  const bool linear = format.bit_depth == BitDepth::Float32;
  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  const auto layer_rect = to_qrect(snapshot.bounds);
  const auto document_rect = QRect(0, 0, document_->width(), document_->height());
  auto stroke_rect = QRect(std::min(from.x(), to.x()) - radius, std::min(from.y(), to.y()) - radius,
                           std::abs(to.x() - from.x()) + radius * 2 + 1, std::abs(to.y() - from.y()) + radius * 2 + 1)
                         .intersected(document_rect)
                         .intersected(layer_rect);
  if (stroke_rect.isEmpty()) {
    return {};
  }
  const auto encode = [linear](float value) {
    return linear ? srgb_encode(static_cast<double>(value) / 255.0) * 255.0 : static_cast<double>(value);
  };
  const auto decode = [linear](double value) {
    return static_cast<float>(linear ? srgb_decode(std::clamp(value, 0.0, 255.0) / 255.0) * 255.0
                                     : std::clamp(value, 0.0, 255.0));
  };
  const auto source_pixel = [&](int document_x, int document_y) {
    const auto x = std::clamp(document_x, layer_rect.left(), layer_rect.right()) - snapshot.bounds.x;
    const auto y = std::clamp(document_y, layer_rect.top(), layer_rect.bottom()) - snapshot.bounds.y;
    const auto values = load_pixel(format, source_pixels.pixel(x, y));
    return std::array<double, 4>{encode(values[0]), encode(values[1]), encode(values[2]),
                                 static_cast<double>(values[3])};
  };
  // Same patent boundary as the 8-bit path: fixed local operations only.
  const auto operation = local_adjustment_operation();
  const auto adjusted_source_pixel = [&](QPoint point) {
    const auto center = source_pixel(point.x(), point.y());
    std::array<double, 3> result{center[0], center[1], center[2]};
    const auto lightness = (54.0 * center[0] + 183.0 * center[1] + 19.0 * center[2]) / (256.0 * 255.0);
    if (operation == CanvasTool::Dodge || operation == CanvasTool::Burn) {
      const auto& knots = dodge_burn_knots(operation == CanvasTool::Dodge, local_tone_range_, local_protect_tones_);
      const auto lightness_change = local_protect_tones_ ? dodge_burn::curve_at(knots, lightness) : 0.0;
      for (std::size_t channel = 0; channel < result.size(); ++channel) {
        const auto value = center[channel];
        result[channel] = value + (local_protect_tones_ ? lightness_change : dodge_burn::curve_at(knots, value / 255.0));
      }
      return result;
    }
    if (operation == CanvasTool::Sponge) {
      const auto maximum = std::max({center[0], center[1], center[2]});
      const auto minimum = std::min({center[0], center[1], center[2]});
      const auto saturation = (maximum - minimum) / 255.0;
      const auto vibrance_scale = sponge_vibrance_ ? 1.0 - saturation : 1.0;
      const auto luma = lightness * 255.0;
      const auto chroma_scale = sponge_mode_ == SpongeMode::Saturate ? 1.0 + vibrance_scale : 1.0 - vibrance_scale;
      for (std::size_t channel = 0; channel < result.size(); ++channel) {
        result[channel] = luma + (center[channel] - luma) * chroma_scale;
      }
    }
    return result;
  };

  auto& pixels = layer->pixels();
  const auto strength = local_adjustment_stroke_amount(operation);
  const auto selection_active = has_selection();
  const auto adjust_pixel = [&](int x, int y, float coverage, std::uint8_t* destination, auto& cap_at) -> bool {
    const QPoint point(x, y);
    if (selection_active) {
      if (!selection_allows(point)) {
        return false;
      }
      coverage *= static_cast<float>(selection_alpha_at(point)) / 255.0F;
    }
    coverage = coverage <= 0.0F
                   ? 0.0F
                   : capped_alpha_coverage([&]() -> float& { return cap_at(x, y); }, coverage, strength);
    if (coverage <= 0.0F) {
      return false;
    }
    const auto before = load_pixel(format, destination);
    if (format.channels >= 4 && before[3] <= 0.0F) {
      return false;
    }
    const auto adjusted = adjusted_source_pixel(point);
    const auto amount = static_cast<double>(strength * coverage);
    auto value = before;
    for (std::size_t channel = 0; channel < adjusted.size(); ++channel) {
      value[channel] = decode(std::clamp(adjusted[channel], 0.0, 255.0) * amount + encode(before[channel]) * (1.0 - amount));
    }
    store_pixel(format, destination, value);
    return load_pixel(format, destination) != before;
  };
  auto* layer_bytes = pixels.data().data();  // detaches shared storage on this thread
  const auto stride = pixels.stride_bytes();
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  const auto destination_at = [&](int x, int y) {
    return layer_bytes + static_cast<std::size_t>(y - snapshot.bounds.y) * stride +
           static_cast<std::size_t>(x - snapshot.bounds.x) * pixel_bytes;
  };
  if (radius == 0) {
    QRect dirty;
    visit_pixel_line(from, to, [&](QPoint point) {
      if (stroke_rect.contains(point) &&
          adjust_pixel(point.x(), point.y(), 1.0F, destination_at(point.x(), point.y()), brush_stroke_alpha_caps_)) {
        dirty = dirty.united(QRect(point, QSize(1, 1)));
      }
    });
    return dirty;
  }
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto segment_length_squared = dx * dx + dy * dy;
  auto chain_brush = brush;
  chain_brush.opacity = local_adjustment_strength_;
  clone_stroke_segment_end_ = std::move(continued_from_end);
  const CloneSegmentRows rows(from, to, radius, clone_stamp_skip_enabled_,
                              clone_stamp_continues_previous_segment(from, radius, chain_brush),
                              full_coverage_radius(radius, brush.softness));
  std::int64_t segment_pixels = 0;
  const auto segment_rows = segment_row_runs(rows, stroke_rect, segment_pixels);
  render_cache_diagnostics_.toning_segment_pixels += segment_pixels;
  const auto paint_row = [&](const SegmentRowRuns& row, auto& cap_at) -> QRect {
    auto row_dirty_left = std::numeric_limits<int>::max();
    auto row_dirty_right = std::numeric_limits<int>::min();
    const auto y = row.y;
    for (std::size_t run = 0; run < row.count; ++run) {
      for (int x = row.runs[run].first; x < row.runs[run].end; ++x) {
        const auto along = segment_length_squared <= std::numeric_limits<double>::epsilon()
                               ? 0.0
                               : std::clamp((static_cast<double>(x - from.x()) * dx +
                                             static_cast<double>(y - from.y()) * dy) /
                                                segment_length_squared,
                                            0.0, 1.0);
        const auto coverage = brush_shape_coverage(static_cast<double>(x) - (static_cast<double>(from.x()) + dx * along),
                                                   static_cast<double>(y) - (static_cast<double>(from.y()) + dy * along),
                                                   radius, brush.softness, brush.roundness, brush.angle_degrees);
        if (adjust_pixel(x, y, coverage, destination_at(x, y), cap_at)) {
          row_dirty_left = std::min(row_dirty_left, x);
          row_dirty_right = std::max(row_dirty_right, x);
        }
      }
    }
    return row_dirty_left <= row_dirty_right ? QRect(row_dirty_left, y, row_dirty_right - row_dirty_left + 1, 1)
                                             : QRect();
  };
  const auto dirty = walk_segment_rows(segment_rows, segment_pixels, brush_stroke_alpha_caps_, paint_row,
                                       [this] { tick_processing_operation(); });
  remember_clone_stroke_segment_end(to, radius, chain_brush);
  return dirty;
}

void CanvasWidget::set_clone_source(QPoint point) {
  if (!document_contains(point)) {
    clear_clone_source();  // Alt-click on the pasteboard turns the source off (GitHub issue 516)
    return;
  }
  clone_source_point_ = point;
  clone_source_set_ = true;
  clone_aligned_offset_set_ = false;
  update();
  update_tool_cursor();  // the not-allowed cursor gives way once a source exists
  if (status_callback_) {
    status_callback_((tool_ == CanvasTool::Healing ? tr("Healing source set at %1, %2")
                                                   : tr("Clone source set at %1, %2"))
                         .arg(point.x())
                         .arg(point.y()));
  }
  notify_clone_source_changed();
}

void CanvasWidget::clear_clone_source() {
  if (!clone_source_set_ || painting_) {
    return;
  }
  invalidate_clone_overlay();
  clone_source_set_ = false;
  clone_source_offset_ = QPoint();  // the offset goes with the point (GitHub issue 632)
  clone_aligned_offset_set_ = false;
  update();              // the source marker goes
  update_tool_cursor();  // a press is refused again: the not-allowed cursor returns
  invalidate_brush_hover_outline(QRect());
  if (status_callback_) {
    status_callback_(tool_ == CanvasTool::Healing ? tr("Healing source cleared") : tr("Clone source cleared"));
  }
  notify_clone_source_changed();
}

void CanvasWidget::notify_clone_source_changed() {
  if (clone_source_changed_callback_) {
    clone_source_changed_callback_();
  }
}

CanvasWidget::CloneSourceSlot CanvasWidget::clone_source_slot_state(int slot) const {
  if (slot == clone_source_slot_) {
    return CloneSourceSlot{.set = clone_source_set_,
                           .point = clone_source_point_,
                           .offset = clone_source_offset_,
                           .offset_set = clone_aligned_offset_set_,
                           .transform = clone_source_transform_};
  }
  if (slot < 0 || slot >= kCloneSourceSlotCount) {
    return {};
  }
  return clone_source_slots_[static_cast<std::size_t>(slot)];
}

void CanvasWidget::set_clone_source_slot(int slot) {
  slot = std::clamp(slot, 0, kCloneSourceSlotCount - 1);
  if (slot == clone_source_slot_ || painting_) {
    return;
  }
  invalidate_clone_overlay();
  clone_source_slots_[static_cast<std::size_t>(clone_source_slot_)] = clone_source_slot_state(clone_source_slot_);
  clone_source_slot_ = slot;
  const auto& next = clone_source_slots_[static_cast<std::size_t>(slot)];
  clone_source_set_ = next.set;
  clone_source_point_ = next.point;
  clone_source_offset_ = next.offset;
  clone_aligned_offset_set_ = next.offset_set;
  clone_source_transform_ = next.transform;
  update();  // the source marker moves
  update_tool_cursor();
  notify_clone_source_changed();
}

void CanvasWidget::set_clone_source_offset(QPoint offset) {
  if (painting_ || (clone_aligned_offset_set_ && clone_source_offset_ == offset)) {
    return;
  }
  invalidate_clone_overlay();
  clone_source_offset_ = offset;
  clone_aligned_offset_set_ = true;
  invalidate_clone_overlay();
  notify_clone_source_changed();
}

void CanvasWidget::set_clone_source_transform(const patchy::CloneSourceTransform& transform) {
  const auto value = transform.normalized();
  if (painting_ || value == clone_source_transform_) {
    return;
  }
  clone_source_transform_ = value;
  invalidate_clone_overlay();
  notify_clone_source_changed();
}

void CanvasWidget::set_clone_overlay_shown(bool shown) {
  if (clone_overlay_shown_ == shown) {
    return;
  }
  invalidate_clone_overlay();
  clone_overlay_shown_ = shown;
  invalidate_clone_overlay();
}

void CanvasWidget::set_clone_overlay_opacity(int percent) {
  percent = std::clamp(percent, 1, 100);
  if (clone_overlay_opacity_ == percent) {
    return;
  }
  clone_overlay_opacity_ = percent;
  invalidate_clone_overlay();
}

// Hidden while Alt aims a new source (GitHub issue 573), except during the Alt+right-drag
// resize, where it is the size preview (issue 516).
bool CanvasWidget::clone_overlay_active() const {
  return clone_overlay_shown_ && retouch_source_tool_active() && clone_source_set_ && !painting_ &&
         !transient_read_callback_ &&
         (!clone_overlay_alt_held_ || brush_adjust_dragging_) && brush_hover_position_valid_ &&
         !spacebar_panning_ && document_ != nullptr && !render_cache_.isNull() &&
         render_cache_.size() == QSize(document_->width(), document_->height());
}

// Where the overlay is centred: the hovered brush, or during an Alt+right-drag resize the
// size disc, which a mouse gesture anchors at the press point while the pointer moves on
// (GitHub issue 516: the overlay followed the pointer and showed beside the disc).
QPoint CanvasWidget::clone_overlay_center_widget() const {
  if (brush_adjust_dragging_) {
    return brush_adjust_pointer_centered_ ? brush_adjust_current_widget_ : brush_adjust_origin_widget_;
  }
  return brush_hover_widget_position_;
}

QRect CanvasWidget::clone_overlay_widget_rect() const {
  const auto radius =
      static_cast<int>(std::ceil(static_cast<double>(std::max(1, active_outline_brush_size())) * zoom_ / 2.0)) + 3;
  const auto center = clone_overlay_center_widget();
  return QRect(center.x() - radius, center.y() - radius, 2 * radius + 1, 2 * radius + 1);
}

void CanvasWidget::set_clone_overlay_alt_held(bool held) {
  if (clone_overlay_alt_held_ == held) {
    return;
  }
  invalidate_clone_overlay();
  clone_overlay_alt_held_ = held;
  invalidate_clone_overlay();
}

void CanvasWidget::invalidate_clone_overlay() {
  QRect region = clone_overlay_drawn_rect_;
  if (clone_overlay_active()) {
    region = region.united(clone_overlay_widget_rect());
  }
  if (!region.isEmpty()) {
    update(region);
  }
}

// The source under the hovered brush, at the overlay opacity and clipped to the brush
// footprint (GitHub issue 424). It reads the displayed composite (render_cache_) through
// the transform the next stroke would use; only the source pixels under the brush are
// drawn, so the cost follows the brush, not the document.
void CanvasWidget::draw_clone_source_overlay(QPainter& painter) const {
  clone_overlay_drawn_rect_ = QRect();
  if (!clone_overlay_active()) {
    return;
  }
  const auto center = clone_overlay_center_widget();
  const auto hover = document_position(center);
  // The offset the next stroke takes: the kept one while Aligned has one, else the
  // source point lands under the pointer.
  const auto offset = clone_aligned_ && clone_aligned_offset_set_ ? clone_source_offset_ : clone_source_point_ - hover;
  const auto transform = tool_ == CanvasTool::Clone ? clone_source_transform_ : patchy::CloneSourceTransform{};
  const auto anchor = QPointF(clone_source_point_ - offset) + QPointF(0.5, 0.5);
  const auto source_center = QPointF(clone_source_point_) + QPointF(0.5, 0.5);
  // Source document coordinates to widget coordinates (the last call applies first).
  auto to_widget = view_transform().to_widget();
  to_widget.translate(anchor.x(), anchor.y());
  to_widget.rotate(transform.rotation_degrees);
  // The flips mirror before the scale and the turn, as CloneSourceMapping does.
  const auto scale = transform.scale_percent / 100.0;
  to_widget.scale(transform.flip_horizontal ? -scale : scale, transform.flip_vertical ? -scale : scale);
  to_widget.translate(-source_center.x(), -source_center.y());

  const auto brush = effective_brush_input();
  const auto radius = std::max(1.0, static_cast<double>(std::max(1, brush.size)) * zoom_ / 2.0);
  QPainterPath footprint;
  footprint.addEllipse(QPointF(0.0, 0.0), radius,
                       std::max(0.5, radius * static_cast<double>(brush.roundness) / 100.0));
  QTransform footprint_place;
  footprint_place.translate(center.x(), center.y());
  footprint_place.rotate(brush.angle_degrees);
  QPainterPath document_area;
  document_area.addRect(QRectF(widget_position_f(QPointF(0.0, 0.0)),
                               widget_position_f(QPointF(document_->width(), document_->height()))));
  const auto clip = footprint_place.map(footprint).intersected(document_area);
  if (clip.isEmpty()) {
    return;
  }
  const auto clip_rect = clip.boundingRect();
  bool invertible = false;
  const auto to_source = to_widget.inverted(&invertible);
  if (!invertible) {
    return;
  }
  const auto source_rect = to_source.mapRect(clip_rect)
                               .toAlignedRect()
                               .adjusted(-1, -1, 1, 1)
                               .intersected(QRect(QPoint(0, 0), render_cache_.size()));
  if (source_rect.isEmpty()) {
    return;
  }
  painter.save();
  painter.setClipPath(clip);
  painter.setOpacity(static_cast<double>(clone_overlay_opacity_) / 100.0);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, !transform.is_identity() || zoom_ < 1.0);
  painter.setTransform(to_widget);
  painter.drawImage(QRectF(source_rect), render_cache_, QRectF(source_rect));
  painter.restore();
  clone_overlay_drawn_rect_ = clip_rect.toAlignedRect().adjusted(-2, -2, 2, 2);
}

void CanvasWidget::set_brush_overlay_shown(bool shown) {
  if (brush_overlay_shown_ == shown) {
    return;
  }
  invalidate_brush_overlay();
  brush_overlay_shown_ = shown;
  invalidate_brush_overlay();
}

void CanvasWidget::set_brush_outline_shown(bool shown) {
  if (brush_outline_shown_ == shown) {
    return;
  }
  // Erase a large brush's canvas outline before it goes, and repaint it when it returns.
  const auto previous =
      brush_hover_position_valid_ && brush_outline_uses_overlay() ? brush_hover_outline_rect() : QRect();
  brush_outline_shown_ = shown;
  update_tool_cursor();
  invalidate_brush_hover_outline(previous);
}

bool CanvasWidget::brush_outline_hidden() const noexcept {
  return !brush_outline_shown_ && tool_ == CanvasTool::Brush;
}

void CanvasWidget::set_brush_overlay_opacity(int percent) {
  percent = std::clamp(percent, 1, 100);
  if (brush_overlay_opacity_ == percent) {
    return;
  }
  brush_overlay_opacity_ = percent;
  invalidate_brush_overlay();
}

// Only for the Brush tool, and hidden while painting (the stroke shows the paint itself),
// panning, or drawing a polygonal preview (GitHub issue 517).
bool CanvasWidget::brush_overlay_active() const {
  return brush_overlay_shown_ && tool_ == CanvasTool::Brush && !painting_ && brush_hover_position_valid_ &&
         !transient_read_callback_ &&
         !spacebar_panning_ && preview_points_.empty() && document_ != nullptr;
}

// Where the dab lands, in widget coordinates: the hovered brush (or the drag-resize disc,
// as for the clone overlay) plus the engine's half-pixel offset, since a dab at document
// position x covers pixel i by its coverage at i - x. From 100% zoom up it snaps to the
// pixel under the pointer, so the cached dab's pixels sit on the document's.
QPointF CanvasWidget::brush_overlay_center_widget() const {
  auto position = document_position_f(QPointF(clone_overlay_center_widget()));
  if (zoom_ >= 1.0) {
    position = QPointF(std::floor(position.x()), std::floor(position.y()));
  }
  return widget_position_f(position + QPointF(0.5, 0.5));
}

// A generous bound of the dab around its centre: a rotated square or tip reaches the
// half diagonal, and a soft tip grows by its feather (scaled_brush_tip_for).
QRect CanvasWidget::brush_overlay_widget_rect() const {
  const auto size = static_cast<double>(std::max(1, brush_size_));
  const auto half = ((size / 2.0 + size * brush_softness_ / 400.0 + 2.0) * 1.42 + 2.0) * zoom_ + 3.0;
  const auto center = brush_overlay_center_widget();
  return QRectF(center.x() - half, center.y() - half, 2.0 * half, 2.0 * half).toAlignedRect();
}

void CanvasWidget::invalidate_brush_overlay() {
  QRect region = brush_overlay_drawn_rect_;
  if (brush_overlay_active()) {
    region = region.united(brush_overlay_widget_rect());
  }
  if (!region.isEmpty()) {
    update(region);
  }
}

// One dab of the current tip in the foreground color (GitHub issue 517), from the coverage
// the paint engine stamps (patchy::brush_dab_coverage): size, Soft, roundness, angle, Round
// or Square, or the sampled tip. Per-dab dynamics (scatter, jitter, pen input) are not
// previewed. The dab is rendered once per setting and zoom, at document resolution from 100%
// up (so its pixels match the paint's) and at screen resolution below it, capped so a huge
// brush stays a bounded image; a repaint only draws that image, clipped to the canvas, so
// the cost follows the brush, not the document.
void CanvasWidget::draw_brush_dab_overlay(QPainter& painter) const {
  brush_overlay_drawn_rect_ = QRect();
  if (!brush_overlay_active()) {
    return;
  }
  const auto scaled_tip = scaled_brush_tip_for(brush_size_, brush_softness_);
  patchy::EditOptions options;
  options.brush_size = brush_size_;
  options.brush_softness = brush_softness_;
  options.brush_roundness = brush_base_roundness_;
  options.brush_angle_degrees = brush_base_angle_degrees_;
  options.brush_shape = brush_tip_ == nullptr ? brush_shape_ : patchy::BrushShape::Round;
  options.brush_tip = scaled_tip.get();
  // Document pixels the dab can reach from its position.
  auto reach = static_cast<double>(std::max(1, brush_size_) / 2) * 1.42 + 2.0;
  if (scaled_tip != nullptr) {
    reach = std::hypot(std::max(scaled_tip->anchor_x, scaled_tip->width - scaled_tip->anchor_x),
                       std::max(scaled_tip->anchor_y, scaled_tip->height - scaled_tip->anchor_y)) +
            2.0;
  }
  constexpr double kMaxHalfExtent = 1024.0;
  const auto scale = std::min({1.0, zoom_, kMaxHalfExtent / reach});
  const auto gray = qGray(primary_color_.rgb());
  const auto color = editing_grayscale_target() ? QColor(gray, gray, gray) : primary_color_;
  const auto key = QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8:%9")
                       .arg(reinterpret_cast<quintptr>(scaled_tip.get()))
                       .arg(brush_size_)
                       .arg(brush_softness_)
                       .arg(brush_base_roundness_)
                       .arg(brush_base_angle_degrees_)
                       .arg(static_cast<int>(options.brush_shape))
                       .arg(color.rgb())
                       .arg(scale)
                       .arg(scaled_tip != nullptr ? scaled_tip->width : 0);
  if (brush_overlay_key_ != key) {
    brush_overlay_key_ = key;
    const auto half = static_cast<int>(std::ceil(reach * scale)) + 1;
    const auto extent = 2 * half + 1;
    QImage image(extent, extent, QImage::Format_ARGB32_Premultiplied);
    // Below 100% each screen pixel averages four samples, so a hard edge does not stair-step.
    // Each sample reads the document pixel it falls in, the coverage the paint writes there, so
    // a dab of a pixel or two still shows as the fraction of the screen pixel it paints.
    const bool supersample = scale < 1.0 && extent <= 1025;
    constexpr std::array<double, 2> kSubOffsets = {-0.25, 0.25};
    const auto pixel_offset = [scale](double image_offset) { return std::floor(image_offset / scale + 0.5); };
    for (int y = 0; y < extent; ++y) {
      auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < extent; ++x) {
        float coverage = 0.0F;
        if (supersample) {
          for (const auto sub_y : kSubOffsets) {
            for (const auto sub_x : kSubOffsets) {
              coverage += patchy::brush_dab_coverage(pixel_offset(x - half + sub_x), pixel_offset(y - half + sub_y),
                                                     options);
            }
          }
          coverage /= 4.0F;
        } else {
          coverage = patchy::brush_dab_coverage((x - half) / scale, (y - half) / scale, options);
        }
        const auto alpha = std::clamp(static_cast<int>(std::lround(coverage * 255.0F)), 0, 255);
        row[x] = qRgba(color.red() * alpha / 255, color.green() * alpha / 255, color.blue() * alpha / 255, alpha);
      }
    }
    brush_overlay_image_ = std::move(image);
    brush_overlay_image_center_ = QPointF(half + 0.5, half + 0.5);
    brush_overlay_image_scale_ = scale;
  }
  if (brush_overlay_image_.isNull()) {
    return;
  }
  const auto ratio = zoom_ / brush_overlay_image_scale_;
  const auto center = brush_overlay_center_widget();
  const QRectF target(center.x() - brush_overlay_image_center_.x() * ratio,
                      center.y() - brush_overlay_image_center_.y() * ratio, brush_overlay_image_.width() * ratio,
                      brush_overlay_image_.height() * ratio);
  if (view_rotation_degrees_ != 0.0) {
    // A rotated view: the dab image is document-aligned, so it turns about the pointer and
    // clips to the document's on-screen outline.
    const auto document_outline =
        view_transform().map_document_rect(QRectF(0.0, 0.0, document_->width(), document_->height()));
    QPainterPath area;
    area.addPolygon(document_outline);
    area.closeSubpath();
    QTransform turn;
    turn.translate(center.x(), center.y());
    turn.rotate(view_rotation_degrees_);
    turn.translate(-center.x(), -center.y());
    const auto drawn = turn.mapRect(target).intersected(document_outline.boundingRect());
    if (drawn.isEmpty()) {
      return;
    }
    painter.save();
    painter.setClipPath(area, Qt::IntersectClip);
    painter.setOpacity(static_cast<double>(brush_overlay_opacity_) / 100.0);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setTransform(turn, true);
    painter.drawImage(target, brush_overlay_image_);
    painter.restore();
    brush_overlay_drawn_rect_ = drawn.toAlignedRect().adjusted(-2, -2, 2, 2);
    return;
  }
  const QRectF document_area(widget_position_f(QPointF(0.0, 0.0)),
                             widget_position_f(QPointF(document_->width(), document_->height())));
  const auto clip = target.intersected(document_area);
  if (clip.isEmpty()) {
    return;
  }
  painter.save();
  painter.setClipRect(clip, Qt::IntersectClip);
  painter.setOpacity(static_cast<double>(brush_overlay_opacity_) / 100.0);
  // A document-resolution dab shows its pixels when zoomed in; only a capped one is smoothed.
  painter.setRenderHint(QPainter::SmoothPixmapTransform, brush_overlay_image_scale_ < std::min(1.0, zoom_));
  painter.drawImage(target, brush_overlay_image_);
  painter.restore();
  brush_overlay_drawn_rect_ = clip.toAlignedRect().adjusted(-2, -2, 2, 2);
}

bool CanvasWidget::begin_pattern_stamp_stroke(QPoint point) {
  if (!pattern_stamp_pattern_.has_value() || pattern_stamp_pattern_->tile.empty()) {
    return false;
  }
  if (!pattern_stamp_aligned_ || !pattern_stamp_origin_.has_value()) {
    const auto& tile = pattern_stamp_pattern_->tile;
    pattern_stamp_origin_ = point - QPoint(tile.width() / 2, tile.height() / 2);
  }
  return true;
}

QRect CanvasWidget::clone_brush_at(QPoint point) {
  return clone_brush_segment(point, point);
}

namespace {
// The lazy Clone/Healing source's tile edge, in document pixels.
constexpr int kLazyCloneSourceTile = 64;
}  // namespace

void CanvasWidget::take_clone_source_snapshot() {
  lazy_clone_source_.reset();
  clone_source_cache_ = QImage();
  if (document_ == nullptr) {
    return;
  }
  const QSize size(document_->width(), document_->height());
  if (!lazy_clone_source_enabled_) {
    clone_source_cache_ = retouch_source_snapshot();
    return;
  }
  const Layer* current_layer = nullptr;
  if (retouch_effective_sample_layers() == SampleLayers::CurrentLayer) {
    current_layer = active_pixel_layer();
    if (current_layer == nullptr) {
      return;  // as retouch_source_snapshot: no source
    }
  } else {
    clone_source_cache_ = retouch_source_snapshot();
    if (render_cache_.isNull() || clone_source_cache_.isNull() ||
        clone_source_cache_.cacheKey() != render_cache_.cacheKey() || clone_source_cache_.size() != size ||
        clone_source_cache_.format() != QImage::Format_RGBA8888) {
      // A rendered snapshot of its own (Current & Below, an unsettled cache): the stroke
      // copies nothing when it writes, so there is nothing to defer.
      return;
    }
  }
  // Uninitialized: every tile is filled before anything reads it.
  QImage lazy(size, QImage::Format_RGBA8888);
  if (lazy.isNull()) {
    if (current_layer != nullptr) {
      clone_source_cache_ = retouch_source_snapshot();
    }
    return;
  }
  LazyCloneSource source;
  if (current_layer != nullptr) {
    source.held_layer.emplace(std::as_const(*current_layer));
  } else {
    source.held = std::move(clone_source_cache_);
  }
  clone_source_cache_ = std::move(lazy);
  source.tiles_x = (size.width() + kLazyCloneSourceTile - 1) / kLazyCloneSourceTile;
  source.tiles_y = (size.height() + kLazyCloneSourceTile - 1) / kLazyCloneSourceTile;
  source.filled.assign(static_cast<std::size_t>(source.tiles_x) * static_cast<std::size_t>(source.tiles_y), 0U);
  lazy_clone_source_ = std::move(source);
}

void CanvasWidget::end_lazy_clone_source() noexcept {
  lazy_clone_source_.reset();
}

void CanvasWidget::fill_clone_source_rect(QRect source_rect) {
  if (!lazy_clone_source_.has_value()) {
    return;
  }
  auto& lazy = *lazy_clone_source_;
  const auto rect = source_rect.intersected(clone_source_cache_.rect());
  if (rect.isEmpty()) {
    return;
  }
  const auto first_x = rect.left() / kLazyCloneSourceTile;
  const auto last_x = rect.right() / kLazyCloneSourceTile;
  const auto first_y = rect.top() / kLazyCloneSourceTile;
  const auto last_y = rect.bottom() / kLazyCloneSourceTile;
  const auto bytes_per_line = static_cast<std::size_t>(clone_source_cache_.bytesPerLine());
  // Non-const bits() once: the lazy image is never shared, so this does not copy it.
  auto* destination = clone_source_cache_.bits();
  for (int tile_y = first_y; tile_y <= last_y; ++tile_y) {
    for (int tile_x = first_x; tile_x <= last_x; ++tile_x) {
      auto& filled = lazy.filled[static_cast<std::size_t>(tile_y) * static_cast<std::size_t>(lazy.tiles_x) +
                                 static_cast<std::size_t>(tile_x)];
      if (filled != 0U) {
        continue;
      }
      filled = 1U;
      ++render_cache_diagnostics_.retouch_source_tiles_filled;
      const auto tile = QRect(tile_x * kLazyCloneSourceTile, tile_y * kLazyCloneSourceTile, kLazyCloneSourceTile,
                              kLazyCloneSourceTile)
                            .intersected(clone_source_cache_.rect());
      const auto row_bytes = static_cast<std::size_t>(tile.width()) * 4U;
      if (lazy.held_layer.has_value()) {
        // The same per-pixel conversion active_layer_sample_image runs over the whole
        // document, for this tile.
        const auto sampled = active_layer_sample_region(*lazy.held_layer, tile);
        for (int y = 0; y < tile.height(); ++y) {
          std::memcpy(destination + static_cast<std::size_t>(tile.top() + y) * bytes_per_line +
                          static_cast<std::size_t>(tile.left()) * 4U,
                      sampled.constScanLine(y), row_bytes);
        }
      } else {
        for (int y = tile.top(); y <= tile.bottom(); ++y) {
          std::memcpy(destination + static_cast<std::size_t>(y) * bytes_per_line +
                          static_cast<std::size_t>(tile.left()) * 4U,
                      lazy.held.constScanLine(y) + static_cast<std::size_t>(tile.left()) * 4U, row_bytes);
        }
      }
    }
  }
}

void CanvasWidget::fill_clone_source_for_destination(QRect destination_rect) {
  if (!lazy_clone_source_.has_value() || destination_rect.isEmpty()) {
    return;
  }
  const auto& mapping = clone_stroke_mapping_;
  if (mapping.identity()) {
    fill_clone_source_rect(destination_rect.translated(clone_source_offset_));
    return;
  }
  // The mapping is affine, so the corners bound every Q8 position inside; each sample
  // reads its pixel and the next one (clone_source_taps), and two more pixels cover the
  // Q24 rounding.
  auto min_x = std::numeric_limits<std::int64_t>::max();
  auto min_y = std::numeric_limits<std::int64_t>::max();
  auto max_x = std::numeric_limits<std::int64_t>::min();
  auto max_y = std::numeric_limits<std::int64_t>::min();
  for (const auto& corner : {destination_rect.topLeft(), destination_rect.topRight(), destination_rect.bottomLeft(),
                             destination_rect.bottomRight()}) {
    const auto position = mapping.map_q8(corner.x(), corner.y());
    min_x = std::min(min_x, position[0]);
    min_y = std::min(min_y, position[1]);
    max_x = std::max(max_x, position[0]);
    max_y = std::max(max_y, position[1]);
  }
  const auto clamp_coordinate = [](std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(value, -(1LL << 30), 1LL << 30));
  };
  const auto left = clamp_coordinate((min_x >> 8) - 2);
  const auto top = clamp_coordinate((min_y >> 8) - 2);
  const auto right = clamp_coordinate((max_x >> 8) + 3);
  const auto bottom = clamp_coordinate((max_y >> 8) + 3);
  fill_clone_source_rect(QRect(QPoint(left, top), QPoint(right, bottom)));
}

bool CanvasWidget::retouch_source_tool_active() const noexcept {
  return tool_ == CanvasTool::Clone || tool_ == CanvasTool::Healing;
}

bool CanvasWidget::retouch_press_would_refuse() const {
  if (document_ == nullptr || !clone_source_set_ || editing_grayscale_target()) {
    return true;
  }
  // On an artboard document a press can retarget to the clicked artboard's layer
  // (retarget_paint_to_clicked_artboard), so only the missing source refuses there.
  if (document_has_artboards(std::as_const(*document_))) {
    return false;
  }
  const auto* layer = active_pixel_layer();
  return layer == nullptr || layer_effectively_locks_image_pixels(*layer);
}

QRect CanvasWidget::retouch_source_indicator_widget_rect(QPoint document_point) const {
  const auto center = widget_position_f(QPointF(document_point) + QPointF(0.5, 0.5));
  constexpr int kReach = 9;
  return QRect(static_cast<int>(std::floor(center.x())) - kReach, static_cast<int>(std::floor(center.y())) - kReach,
               2 * kReach + 2, 2 * kReach + 2);
}

void CanvasWidget::set_retouch_source_indicator(std::optional<QPoint> document_point) {
  if (retouch_source_indicator_ == document_point) {
    return;
  }
  const bool shown_before = retouch_source_indicator_.has_value();
  if (shown_before != document_point.has_value()) {
    // The stroke starts or ends: the Alt-click marker hides or comes back as well.
    retouch_source_indicator_ = document_point;
    update();
    return;
  }
  update(retouch_source_indicator_widget_rect(*retouch_source_indicator_));
  retouch_source_indicator_ = document_point;
  update(retouch_source_indicator_widget_rect(*retouch_source_indicator_));
}

QRect CanvasWidget::clone_brush_segment(QPoint from, QPoint to) {
  if (painting_) {
    if (clone_stroke_mapping_.identity()) {
      set_retouch_source_indicator(to + clone_source_offset_);
    } else {
      const auto nearest = clone_stroke_mapping_.map_nearest(to.x(), to.y());
      set_retouch_source_indicator(QPoint(nearest[0], nearest[1]));
    }
  }
  const auto dirty = clone_brush_stamp_segment(from, to);
  if (healing_membrane_stroke_) {
    // The Healing Brush stroke's footprint, healed once on release
    // (finish_healing_brush_stroke); the stamp itself is the raw-source preview.
    healing_stroke_footprint_ = healing_stroke_footprint_.united(dirty);
  }
  return dirty;
}

bool CanvasWidget::clone_stamp_continues_previous_segment(QPoint from, int radius,
                                                          const EffectiveBrushInput& brush) const noexcept {
  const auto& end = clone_stroke_segment_end_;
  return clone_stamp_skip_enabled_ && end.has_value() && end->point == from && end->radius == radius &&
         end->softness == brush.softness && end->opacity == brush.opacity &&
         std::clamp(brush.roundness, 1, 100) >= 99;
}

void CanvasWidget::remember_clone_stroke_segment_end(QPoint to, int radius,
                                                     const EffectiveBrushInput& brush) noexcept {
  if (radius <= 0 || std::clamp(brush.roundness, 1, 100) < 99) {
    clone_stroke_segment_end_.reset();
    return;
  }
  clone_stroke_segment_end_ = CloneSegmentEnd{to, radius, brush.softness, brush.opacity};
}

// The Clone stamp, also the Healing Brush's live preview: the source as sampled,
// written by opacity and coverage. The Healing Brush heals on release instead of per
// dab (finish_healing_brush_stroke, docs/healing.md), so nothing here is tone matched.
QRect CanvasWidget::clone_brush_stamp_segment(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  if (document_ != nullptr && layer != nullptr && std::as_const(*layer).pixels().format().bit_depth != BitDepth::UInt8) {
    return clone_brush_segment_deep(from, to);
  }
  // Any return before the walk below ends the chain of continued segments.
  auto continued_from_end = std::exchange(clone_stroke_segment_end_, std::nullopt);
  if (document_ == nullptr || layer == nullptr || clone_source_cache_.isNull() ||
      layer->pixels().format().bit_depth != BitDepth::UInt8 || layer->pixels().format().channels < 3) {
    return {};
  }
  const auto* palette_snap = palette_snap_for_edits();
  // The source pixel a destination pixel takes, or nullptr outside the source. At the
  // identity transform it is the plain offset read (byte-identical to before GitHub
  // issue 424); otherwise a bilinear sample through the stroke's mapping.
  // A transformed sample lands in `sample` (each worker has its own).
  const auto& mapping = clone_stroke_mapping_;
  std::array<std::uint8_t, 4> transformed_sample{};
  const auto source_pixel = [&](QPoint document_point,
                                std::array<std::uint8_t, 4>& sample) -> const std::uint8_t* {
    if (mapping.identity()) {
      const auto source_point = document_point + clone_source_offset_;
      if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= clone_source_cache_.width() ||
          source_point.y() >= clone_source_cache_.height()) {
        return nullptr;
      }
      return clone_source_cache_.constScanLine(source_point.y()) + static_cast<std::size_t>(source_point.x()) * 4U;
    }
    const auto position = mapping.map_q8(document_point.x(), document_point.y());
    const auto taps =
        patchy::clone_source_taps(position[0], position[1], clone_source_cache_.width(), clone_source_cache_.height());
    if (!taps.has_value()) {
      return nullptr;
    }
    sample = patchy::sample_clone_source_rgba8(
        clone_source_cache_.constBits(), static_cast<std::size_t>(clone_source_cache_.bytesPerLine()), *taps);
    return sample.data();
  };

  const auto brush = effective_brush_input();
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
    fill_clone_source_for_destination(path_rect);

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
      const auto* src = source_pixel(document_point, transformed_sample);
      if (src == nullptr) {
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
  if (std::exchange(retouch_press_times_layer_detach_, false)) {
    // The press's first stamp: the copy-on-write detach on its own (it happens on the
    // first write below anyway).
    const auto started = std::chrono::steady_clock::now();
    static_cast<void>(pixels.data());
    last_retouch_press_timings_.layer_detach_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  }
  const auto bounds = layer->bounds();
  const auto channels = pixels.format().channels;
  stroke_rect = stroke_rect.intersected(to_qrect(bounds));
  if (stroke_rect.isEmpty()) {
    return {};
  }
  // Before the workers below read it.
  fill_clone_source_for_destination(stroke_rect);

  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  const auto segment_length_squared = static_cast<double>(dx) * static_cast<double>(dx) +
                                      static_cast<double>(dy) * static_cast<double>(dy);
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
  clone_stroke_segment_end_ = std::move(continued_from_end);
  const CloneSegmentRows rows(from, to, radius, clone_stamp_skip_enabled_,
                              clone_stamp_continues_previous_segment(from, radius, brush),
                              full_coverage_radius(radius, brush.softness));

  // One pixel of the stamp: `dst` is its layer bytes, `cap_at(x, y)` its stroke alpha cap.
  // True when it was written.
  const auto selection_active = has_selection();
  const auto stamp_pixel = [&](int x, int y, std::uint8_t* dst, std::array<std::uint8_t, 4>& sample,
                               auto& cap_at) -> bool {
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
      return false;
    }
    const QPoint document_point(x, y);
    // Without a selection selection_allows() is always true.
    if (selection_active) {
      if (!selection_allows(document_point)) {
        return false;
      }
      coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
      if (coverage <= 0.0F) {
        return false;
      }
    }
    if (palette_snap != nullptr) {
      if (coverage < palette_snap->coverage_threshold) {
        return false;
      }
      coverage = 1.0F;
    }

    const auto* src = source_pixel(document_point, sample);
    if (src == nullptr) {
      return false;
    }
    if (lock_transparent_pixels && channels >= 4 && dst[3] == 0) {
      return false;
    }
    coverage = capped_alpha_coverage([&]() -> float& { return cap_at(x, y); }, coverage, opacity);
    if (coverage <= 0.0F) {
      return false;
    }

    const auto covered_opacity = opacity * coverage;
    if (channels >= 4 && !lock_transparent_pixels) {
      blend_straight_rgba(dst, src, covered_opacity);
    } else {
      const auto effective_opacity = covered_opacity * (static_cast<float>(src[3]) / 255.0F);
      if (effective_opacity <= 0.0F) {
        return false;
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
    return true;
  };

  struct RowRuns {
    int y{0};
    std::size_t count{0};
    std::array<CloneSegmentRows::Run, CloneSegmentRows::kMaxRuns> runs{};
  };
  // The dirty columns of one row's runs, stamped through `cap_at`.
  const auto stamp_row = [&](const RowRuns& row_runs, std::uint8_t* row_bytes, std::array<std::uint8_t, 4>& sample,
                             auto& cap_at) -> QRect {
    auto row_dirty_left = std::numeric_limits<int>::max();
    auto row_dirty_right = std::numeric_limits<int>::min();
    for (std::size_t run = 0; run < row_runs.count; ++run) {
      for (int x = row_runs.runs[run].first; x < row_runs.runs[run].end; ++x) {
        auto* dst = row_bytes + static_cast<std::size_t>(x - bounds.x) * channels;
        if (stamp_pixel(x, row_runs.y, dst, sample, cap_at)) {
          row_dirty_left = std::min(row_dirty_left, x);
          row_dirty_right = std::max(row_dirty_right, x);
        }
      }
    }
    return row_dirty_left <= row_dirty_right
               ? QRect(row_dirty_left, row_runs.y, row_dirty_right - row_dirty_left + 1, 1)
               : QRect();
  };

  std::vector<RowRuns> segment_rows;
  segment_rows.reserve(static_cast<std::size_t>(stroke_rect.height()));
  std::int64_t segment_pixels = 0;
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    RowRuns row_runs;
    row_runs.y = y;
    row_runs.count = rows.runs(y, stroke_rect.left(), stroke_rect.right(), row_runs.runs);
    for (std::size_t run = 0; run < row_runs.count; ++run) {
      segment_pixels += row_runs.runs[run].end - row_runs.runs[run].first;
    }
    if (row_runs.count > 0) {
      segment_rows.push_back(row_runs);
    }
  }

  // A big segment (a soft 529 px brush tests about 110,000 pixels per mouse sample, a 204 px
  // one about 16,000; GitHub issue 597) fans out over strips of whole 64-row bands, so no two
  // workers share a tile of the stroke's alpha caps (StrokePixelMap::Cursor). Each pixel
  // depends only on its own bytes, cap and the frozen source, so the strips give the
  // sequential walk's bytes. The layer bytes are detached here, on the launching thread,
  // before any worker writes.
  constexpr std::int64_t kCloneParallelPixels = 8192;
  const auto band_of = [](int y) { return y >> StrokePixelMap<float>::kTileRowsShift; };
  const auto bands = segment_rows.empty() ? 0 : band_of(segment_rows.back().y) - band_of(segment_rows.front().y) + 1;
  const auto workers = segment_pixels >= kCloneParallelPixels && bands >= 2
                           ? patchy::max_blocking_fanout_workers(
                                 std::clamp(std::min(bands, patchy::hardware_worker_threads()), 1, 16))
                           : 1;
  QRect dirty;
  if (workers >= 2 && !qEnvironmentVariableIsSet("PATCHY_RENDER_SINGLE_THREADED")) {
    // Strips: runs of rows cut only where a band starts, each about an equal share of pixels.
    std::vector<std::pair<std::size_t, std::size_t>> strips;
    const auto share = (segment_pixels + workers - 1) / workers;
    std::size_t strip_start = 0;
    std::int64_t strip_pixels = 0;
    auto band_left = std::numeric_limits<int>::max();
    auto band_right = std::numeric_limits<int>::min();
    for (std::size_t index = 0; index < segment_rows.size(); ++index) {
      const auto& row_runs = segment_rows[index];
      band_left = std::min(band_left, row_runs.runs[0].first);
      band_right = std::max(band_right, row_runs.runs[row_runs.count - 1].end);
      for (std::size_t run = 0; run < row_runs.count; ++run) {
        strip_pixels += row_runs.runs[run].end - row_runs.runs[run].first;
      }
      const auto last = index + 1 == segment_rows.size();
      if (last || band_of(segment_rows[index + 1].y) != band_of(row_runs.y)) {
        // The band ends here: its tiles exist before any worker runs.
        const auto band_top = band_of(row_runs.y) << StrokePixelMap<float>::kTileRowsShift;
        brush_stroke_alpha_caps_.reserve_tiles(band_left, band_top, band_right,
                                               band_top + (1 << StrokePixelMap<float>::kTileRowsShift));
        band_left = std::numeric_limits<int>::max();
        band_right = std::numeric_limits<int>::min();
        if (last || strip_pixels >= share) {
          strips.emplace_back(strip_start, index + 1);
          strip_start = index + 1;
          strip_pixels = 0;
        }
      }
    }
    auto* layer_bytes = pixels.data().data();  // detaches shared storage on this thread
    const auto stride = pixels.stride_bytes();
    std::vector<std::future<QRect>> futures;
    futures.reserve(strips.size());
    for (const auto& strip : strips) {
      const auto first_row = strip.first;
      const auto end_row = strip.second;
      futures.push_back(std::async(std::launch::async, [&, first_row, end_row] {
        StrokePixelMap<float>::Cursor caps(brush_stroke_alpha_caps_);
        std::array<std::uint8_t, 4> sample{};
        QRect strip_dirty;
        for (auto index = first_row; index < end_row; ++index) {
          const auto& row_runs = segment_rows[index];
          auto* row_bytes = layer_bytes + static_cast<std::size_t>(row_runs.y - bounds.y) * stride;
          strip_dirty = strip_dirty.united(stamp_row(row_runs, row_bytes, sample, caps));
        }
        return strip_dirty;
      }));
    }
    for (auto& future : futures) {
      dirty = dirty.united(future.get());
    }
    tick_processing_operation();
  } else {
    auto& caps = brush_stroke_alpha_caps_;
    for (const auto& row_runs : segment_rows) {
      auto row = pixels.row(row_runs.y - bounds.y);
      dirty = dirty.united(stamp_row(row_runs, row.data(), transformed_sample, caps));
      tick_processing_operation();
    }
  }
  remember_clone_stroke_segment_end(to, radius, brush);
  return dirty;
}

// clone_brush_stamp_segment for 16/32-bit layers (docs/high-bit-depth.md): the same stamp
// on the depth-native source (clone_source_deep_), in float.
QRect CanvasWidget::clone_brush_segment_deep(QPoint from, QPoint to) {
  auto* layer = active_pixel_layer();
  const auto& source = clone_source_deep_;
  // Any return before the walk below ends the chain of continued segments.
  auto continued_from_end = std::exchange(clone_stroke_segment_end_, std::nullopt);
  if (document_ == nullptr || layer == nullptr || source.empty() || std::as_const(*layer).pixels().format().channels < 3) {
    return {};
  }
  const auto brush = effective_brush_input();
  const auto radius = std::max(1, brush.size) / 2;
  const auto document_rect = QRect(0, 0, document_->width(), document_->height());
  auto stroke_rect = QRect(std::min(from.x(), to.x()) - radius, std::min(from.y(), to.y()) - radius,
                           std::abs(to.x() - from.x()) + radius * 2 + 1, std::abs(to.y() - from.y()) + radius * 2 + 1)
                         .intersected(document_rect);
  if (stroke_rect.isEmpty()) {
    return {};
  }
  const auto lock_transparent_pixels = active_layer_locks_transparent_pixels();
  if (!lock_transparent_pixels) {
    patchy::expand_layer_to_include_rect(*layer, to_core_rect(stroke_rect));
  }
  auto& pixels = layer->pixels();
  const auto bounds = layer->bounds();
  const auto format = pixels.format();
  stroke_rect = stroke_rect.intersected(to_qrect(bounds));
  if (stroke_rect.isEmpty()) {
    return {};
  }
  const auto sample = [&source](int x, int y) {
    x = std::clamp(x, 0, source.width() - 1);
    y = std::clamp(y, 0, source.height() - 1);
    return load_pixel(source.format(), source.pixel(x, y));
  };
  // A transformed source (GitHub issue 424) takes a bilinear sample, weighted in
  // premultiplied form like sample_clone_source_rgba8.
  const auto& mapping = clone_stroke_mapping_;
  const auto sample_transformed = [&source](const patchy::CloneSourceTaps& taps) {
    const std::array<std::array<float, 4>, 4> corners = {
        load_pixel(source.format(), source.pixel(taps.x0, taps.y0)),
        load_pixel(source.format(), source.pixel(taps.x1, taps.y0)),
        load_pixel(source.format(), source.pixel(taps.x0, taps.y1)),
        load_pixel(source.format(), source.pixel(taps.x1, taps.y1))};
    const auto fx = static_cast<float>(taps.wx) / 256.0F;
    const auto fy = static_cast<float>(taps.wy) / 256.0F;
    const std::array<float, 4> weights = {(1.0F - fx) * (1.0F - fy), fx * (1.0F - fy), (1.0F - fx) * fy, fx * fy};
    std::array<float, 4> out{0.0F, 0.0F, 0.0F, 0.0F};
    for (std::size_t i = 0; i < corners.size(); ++i) {
      const auto weighted_alpha = weights[i] * corners[i][3];
      out[3] += weighted_alpha;
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] += weighted_alpha * corners[i][c];
      }
    }
    if (out[3] > 0.0F) {
      for (std::size_t c = 0; c < 3U; ++c) {
        out[c] /= out[3];
      }
    }
    return out;
  };
  const auto opacity = static_cast<float>(brush.opacity) / 100.0F;
  const auto dx = static_cast<double>(to.x() - from.x());
  const auto dy = static_cast<double>(to.y() - from.y());
  const auto segment_length_squared = dx * dx + dy * dy;
  clone_stroke_segment_end_ = std::move(continued_from_end);
  const CloneSegmentRows rows(from, to, radius, clone_stamp_skip_enabled_,
                              clone_stamp_continues_previous_segment(from, radius, brush),
                              full_coverage_radius(radius, brush.softness));
  QRect dirty;
  std::array<CloneSegmentRows::Run, CloneSegmentRows::kMaxRuns> runs{};
  for (int y = stroke_rect.top(); y <= stroke_rect.bottom(); ++y) {
    const auto run_count = rows.runs(y, stroke_rect.left(), stroke_rect.right(), runs);
    auto row_dirty_left = std::numeric_limits<int>::max();
    auto row_dirty_right = std::numeric_limits<int>::min();
    for (std::size_t run = 0; run < run_count; ++run) {
      for (int x = runs[run].first; x < runs[run].end; ++x) {
        const auto along = segment_length_squared <= 0.0
                               ? 0.0
                               : std::clamp((static_cast<double>(x - from.x()) * dx + static_cast<double>(y - from.y()) * dy) /
                                                segment_length_squared,
                                            0.0, 1.0);
        auto coverage = brush_shape_coverage(static_cast<double>(x) - (static_cast<double>(from.x()) + dx * along),
                                             static_cast<double>(y) - (static_cast<double>(from.y()) + dy * along),
                                             radius, brush.softness, brush.roundness, brush.angle_degrees);
        const QPoint document_point(x, y);
        if (coverage <= 0.0F || !selection_allows(document_point)) {
          continue;
        }
        if (has_selection()) {
          coverage *= static_cast<float>(selection_alpha_at(document_point)) / 255.0F;
          if (coverage <= 0.0F) {
            continue;
          }
        }
        std::array<float, 4> src{};
        if (mapping.identity()) {
          const auto source_point = document_point + clone_source_offset_;
          if (source_point.x() < 0 || source_point.y() < 0 || source_point.x() >= source.width() ||
              source_point.y() >= source.height()) {
            continue;
          }
          src = sample(source_point.x(), source_point.y());
        } else {
          const auto position = mapping.map_q8(x, y);
          const auto taps = patchy::clone_source_taps(position[0], position[1], source.width(), source.height());
          if (!taps.has_value()) {
            continue;
          }
          src = sample_transformed(*taps);
        }
        auto* dst = pixels.pixel(x - bounds.x, y - bounds.y);
        const auto before = load_pixel(format, dst);
        if (lock_transparent_pixels && format.channels >= 4 && before[3] <= 0.0F) {
          continue;
        }
        coverage = capped_stroke_coverage(x, y, coverage, opacity);
        if (coverage <= 0.0F) {
          continue;
        }
        const auto amount = std::clamp(opacity * coverage, 0.0F, 1.0F);
        auto value = before;
        if (format.channels >= 4 && !lock_transparent_pixels) {
          // blend_straight_rgba in float.
          const auto source_alpha = src[3] / 255.0F;
          const auto destination_alpha = before[3] / 255.0F;
          const auto out_alpha = source_alpha * amount + destination_alpha * (1.0F - amount);
          if (amount >= 0.999F) {
            value = src;
          } else if (out_alpha <= 0.0F) {
            value = {src[0], src[1], src[2], 0.0F};
          } else {
            for (std::size_t c = 0; c < 3U; ++c) {
              value[c] = (src[c] * source_alpha * amount + before[c] * destination_alpha * (1.0F - amount)) / out_alpha;
            }
            value[3] = out_alpha * 255.0F;
          }
        } else {
          const auto effective = amount * (src[3] / 255.0F);
          if (effective <= 0.0F) {
            continue;
          }
          for (std::size_t c = 0; c < 3U; ++c) {
            value[c] = src[c] * effective + before[c] * (1.0F - effective);
          }
        }
        store_pixel(format, dst, value);
        if (load_pixel(format, dst) != before) {
          row_dirty_left = std::min(row_dirty_left, x);
          row_dirty_right = std::max(row_dirty_right, x);
        }
      }
    }
    if (row_dirty_left <= row_dirty_right) {
      dirty = dirty.united(QRect(row_dirty_left, y, row_dirty_right - row_dirty_left + 1, 1));
    }
    tick_processing_operation();
  }
  remember_clone_stroke_segment_end(to, radius, brush);
  return dirty;
}

}  // namespace patchy::ui
