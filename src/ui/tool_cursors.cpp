#include "ui/tool_cursors.hpp"

#include <array>
#include <cmath>
#include <numbers>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>

namespace patchy::ui {
namespace {

// An eyedropper glyph: a diagonal barrel from a tip in the lower-left up to a
// round bulb in the upper-right, matching tool-eyedropper.svg. Drawn in two
// passes (dark halo, then light ink) so it stays legible over any background,
// with the hotspot on the lower-left tip (the pixel that gets sampled).
QCursor build_eyedropper_cursor() {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  const QPointF tip(5.0, 27.0);       // lower-left sampling point (hotspot)
  const QPointF collar(15.6, 16.4);   // metal band where the barrel meets the bulb
  const QPointF bulb(20.5, 11.5);     // squeeze-bulb centre
  const QPointF perp(0.707, 0.707);   // across the barrel, for the collar tick

  // Two passes: a dark halo, then the lighter body on top, so the whole glyph
  // stays legible over any background. A solid (not hollow) bulb plus the collar
  // tick keep it reading as an eyedropper, not the zoom magnifier.
  const auto pass = [&](const QColor& ink, double barrel_width, double collar_width, double bulb_radius) {
    painter.setPen(QPen(ink, barrel_width, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(tip, collar);
    painter.setPen(QPen(ink, collar_width, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(collar - perp * 3.0, collar + perp * 3.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawEllipse(bulb, bulb_radius, bulb_radius);
  };
  pass(QColor(20, 23, 28), 5.0, 6.0, 5.5);       // dark halo
  pass(QColor(245, 248, 252), 2.6, 3.2, 4.3);    // light body (leaves a ~1px dark rim)

  // A small blue drip at the very tip signals "pick a colour" and echoes the icon.
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(116, 192, 255));
  painter.drawEllipse(tip, 1.9, 1.9);
  painter.end();

  return QCursor(pixmap, static_cast<int>(std::round(tip.x())), static_cast<int>(std::round(tip.y())));
}

// The classic rotate horseshoe: a 120-degree arc over the top with a solid
// triangular head at each end pointing down-and-outward. Same two-pass
// halo/ink treatment as the other tool cursors so it reads over any artwork;
// hotspot at the center.
QCursor build_crop_rotate_cursor() {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  const QPointF center(16.0, 17.5);
  constexpr double kRadius = 8.5;
  const QRectF arc_rect(center.x() - kRadius, center.y() - kRadius, 2.0 * kRadius, 2.0 * kRadius);
  struct Head {
    QPointF end;
    QPointF direction;  // outward tangent the solid head points along
  };
  const auto head_for = [&](double degrees, double direction_x) {
    const auto radians = degrees * std::numbers::pi / 180.0;
    return Head{center + QPointF(kRadius * std::cos(radians), -kRadius * std::sin(radians)),
                QPointF(direction_x, 0.866)};
  };
  const std::array<Head, 2> heads = {head_for(30.0, 0.5), head_for(150.0, -0.5)};

  const auto pass = [&](const QColor& color, double arc_width, double head_rim) {
    painter.setPen(QPen(color, arc_width, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(arc_rect, 30 * 16, 120 * 16);
    painter.setPen(QPen(color, head_rim, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    for (const auto& head : heads) {
      const QPointF normal(-head.direction.y(), head.direction.x());
      painter.drawPolygon(QPolygonF(
          {head.end + head.direction * 4.8, head.end + normal * 2.7, head.end - normal * 2.7}));
    }
  };
  pass(QColor(20, 23, 28), 4.0, 2.8);       // dark halo
  pass(QColor(245, 248, 252), 2.0, 0.8);    // light core
  painter.end();

  return QCursor(pixmap, 16, 16);
}

}  // namespace

QCursor eyedropper_cursor() {
  // Built once and reused: update_tool_cursor() runs on every mouse move.
  static const QCursor cursor = build_eyedropper_cursor();
  return cursor;
}

QCursor crop_rotate_cursor() {
  // Built once and reused: crop hover re-applies it on every mouse move.
  static const QCursor cursor = build_crop_rotate_cursor();
  return cursor;
}

}  // namespace patchy::ui
