#include "ui/eyedropper_sample_overlay.hpp"

#include "ui/canvas_widget_shared.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::ui {

bool eyedropper_sample_outline_visible(int sample_size, double zoom) noexcept {
  return sample_size > 1 && static_cast<double>(sample_size) * zoom >= kEyedropperSampleOutlineMinScreenPixels;
}

QRect eyedropper_sample_rect(QPoint point, int sample_size, QSize document_size) noexcept {
  // The same square pick_color averages: odd sides centered on the pixel.
  const int half = std::max(1, sample_size) / 2;
  return QRect(point.x() - half, point.y() - half, 2 * half + 1, 2 * half + 1)
      .intersected(QRect(QPoint(0, 0), document_size));
}

SampleLoupeGeometry sample_loupe_geometry(QPointF pointer, QRectF viewport) noexcept {
  constexpr double kReach = kSampleLoupeBandOuterRadius + kSampleLoupeMargin;
  // Slide inward rather than clip; a viewport narrower than the loupe centers it.
  const auto fit = [](double value, double low, double high) {
    return high - low < 2.0 * kReach ? (low + high) / 2.0 : std::clamp(value, low + kReach, high - kReach);
  };
  SampleLoupeGeometry geometry;
  geometry.center = QPointF(fit(pointer.x(), viewport.left(), viewport.left() + viewport.width()),
                            fit(pointer.y(), viewport.top(), viewport.top() + viewport.height()));
  geometry.shifted = std::abs(geometry.center.x() - pointer.x()) > 0.5 ||
                     std::abs(geometry.center.y() - pointer.y()) > 0.5;
  return geometry;
}

QRectF SampleLoupeGeometry::bounds() const {
  constexpr double kReach = kSampleLoupeBandOuterRadius + kSampleLoupeMargin;
  return QRectF(center.x() - kReach, center.y() - kReach, 2.0 * kReach, 2.0 * kReach);
}

int sample_loupe_view_pixels(int sample_size) noexcept {
  // Three pixels of context on each side, and never fewer than nine across, so a
  // point sample still shows its neighbors as a readable grid.
  return std::max(9, (std::clamp(sample_size, 1, 101) | 1) + 6);
}

QRect sample_loupe_view_rect(QPoint point, int sample_size) noexcept {
  const int side = sample_loupe_view_pixels(sample_size);
  return QRect(point.x() - side / 2, point.y() - side / 2, side, side);
}

namespace {

// Cursor ink: the brush outline's dark and light pair.
const QColor kInkDark(25, 25, 25);
const QColor kInkLight(255, 255, 255);

// A band segment from start_degrees (Qt's counterclockwise angles) spanning span_degrees.
QPainterPath band_segment(QPointF center, double start_degrees, double span_degrees) {
  const QRectF outer(center.x() - kSampleLoupeBandOuterRadius, center.y() - kSampleLoupeBandOuterRadius,
                     2.0 * kSampleLoupeBandOuterRadius, 2.0 * kSampleLoupeBandOuterRadius);
  const QRectF inner(center.x() - kSampleLoupeBandInnerRadius, center.y() - kSampleLoupeBandInnerRadius,
                     2.0 * kSampleLoupeBandInnerRadius, 2.0 * kSampleLoupeBandInnerRadius);
  QPainterPath path;
  path.arcMoveTo(outer, start_degrees);
  path.arcTo(outer, start_degrees, span_degrees);
  path.arcTo(inner, start_degrees + span_degrees, -span_degrees);
  path.closeSubpath();
  return path;
}

void stroke_in_ink(QPainter& painter, const QPainterPath& path) {
  painter.setBrush(Qt::NoBrush);
  QColor dark = kInkDark;
  dark.setAlpha(215);
  painter.setPen(QPen(dark, 2.5));
  painter.drawPath(path);
  painter.setPen(QPen(kInkLight, 1.0));
  painter.drawPath(path);
}

}  // namespace

void paint_sample_loupe(QPainter& painter, const SampleLoupeGeometry& geometry, const SampleLoupeContent& content,
                        QPointF pointer) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QPointF center = geometry.center;
  // Two arcs split just below the horizontal, with small gaps: the larger top arc
  // is the live sample, the smaller bottom arc the color it replaces.
  constexpr double kSplitBelowHorizontal = 20.0;
  constexpr double kGap = 6.0;
  const auto top = band_segment(center, -kSplitBelowHorizontal + kGap / 2.0, 180.0 + 2.0 * kSplitBelowHorizontal - kGap);
  const auto bottom =
      band_segment(center, 180.0 + kSplitBelowHorizontal + kGap / 2.0, 180.0 - 2.0 * kSplitBelowHorizontal - kGap);
  QPainterPath lens;
  lens.addEllipse(center, kSampleLoupeLensRadius, kSampleLoupeLensRadius);

  // A soft shadow under the pieces, so the loupe lifts off busy artwork.
  QPainterPath shape = top;
  shape.addPath(bottom);
  shape.addPath(lens);
  painter.save();
  painter.translate(0.0, 2.0);
  painter.setBrush(Qt::NoBrush);
  for (const auto& [width, alpha] : {std::pair{7.0, 14}, std::pair{4.0, 22}}) {
    painter.setPen(QPen(QColor(0, 0, 0, alpha), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(shape);
  }
  painter.fillPath(shape, QColor(0, 0, 0, 50));
  painter.restore();

  painter.fillPath(top, content.sample);
  painter.fillPath(bottom, content.previous);

  // The lens: the pixels around the pointer, enlarged on whole document pixels.
  const auto& view = content.view_rect;
  if (!view.isEmpty()) {
    painter.save();
    painter.setClipPath(lens, Qt::IntersectClip);
    painter.fillPath(lens, content.backdrop);
    const double cell = 2.0 * kSampleLoupeLensRadius / static_cast<double>(view.width());
    const QPointF origin = center - QPointF(view.width() * cell / 2.0, view.height() * cell / 2.0);
    const auto lens_rect = [&](QRect area) {
      return QRectF(origin + QPointF(area.x() - view.x(), area.y() - view.y()) * cell,
                    QSizeF(area.width() * cell, area.height() * cell));
    };
    if (!content.source_rect.isEmpty()) {
      const auto source = lens_rect(content.source_rect);
      constexpr double kChecker = 6.0;
      for (double y = source.top(); y < source.bottom(); y += kChecker) {
        for (double x = source.left(); x < source.right(); x += kChecker) {
          const bool dark = (static_cast<int>((x - source.left()) / kChecker) +
                             static_cast<int>((y - source.top()) / kChecker)) % 2 == 0;
          painter.fillRect(QRectF(x, y, kChecker, kChecker).intersected(source),
                           dark ? checkerboard_dark_square() : checkerboard_light_square());
        }
      }
      if (!content.pixels.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, cell < 1.0);
        painter.drawImage(source, content.pixels);
      }
      if (cell >= 5.0) {
        // The pixel grid, faint, so the sampled pixels can be counted.
        painter.setPen(QPen(QColor(0, 0, 0, 46), 1.0));
        for (int column = content.source_rect.left(); column <= content.source_rect.right() + 1; ++column) {
          const double x = origin.x() + (column - view.x()) * cell;
          painter.drawLine(QPointF(x, source.top()), QPointF(x, source.bottom()));
        }
        for (int row = content.source_rect.top(); row <= content.source_rect.bottom() + 1; ++row) {
          const double y = origin.y() + (row - view.y()) * cell;
          painter.drawLine(QPointF(source.left(), y), QPointF(source.right(), y));
        }
      }
    }
    if (!content.sample_rect.isEmpty()) {
      QPainterPath sample;
      sample.addRect(lens_rect(content.sample_rect));
      stroke_in_ink(painter, sample);
    }
    painter.restore();
  }
  stroke_in_ink(painter, lens);
  stroke_in_ink(painter, top);
  stroke_in_ink(painter, bottom);

  if (geometry.shifted) {
    // The loupe slid off the pointer near the edge: mark where the pointer is.
    QPainterPath cross;
    for (const auto& [from, to] : {std::pair{QPointF(-6.0, 0.0), QPointF(-2.0, 0.0)},
                                   std::pair{QPointF(2.0, 0.0), QPointF(6.0, 0.0)},
                                   std::pair{QPointF(0.0, -6.0), QPointF(0.0, -2.0)},
                                   std::pair{QPointF(0.0, 2.0), QPointF(0.0, 6.0)}}) {
      cross.moveTo(pointer + from);
      cross.lineTo(pointer + to);
    }
    painter.setPen(QPen(kInkDark, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(cross);
    painter.setPen(QPen(kInkLight, 1.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(cross);
  }
  painter.restore();
}

}  // namespace patchy::ui
