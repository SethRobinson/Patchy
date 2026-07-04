#include "ui/tool_cursors.hpp"

#include <cmath>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>

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

}  // namespace

QCursor eyedropper_cursor() {
  // Built once and reused: update_tool_cursor() runs on every mouse move.
  static const QCursor cursor = build_eyedropper_cursor();
  return cursor;
}

}  // namespace patchy::ui
