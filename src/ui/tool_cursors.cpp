#include "ui/tool_cursors.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

namespace patchy::ui {
namespace {

// An eyedropper glyph: a diagonal barrel from a tip in the lower-left up to a
// round bulb in the upper-right, matching tool-eyedropper.svg. Drawn in two
// passes (dark halo, then light ink) so it stays legible over any background,
// with the hotspot on the lower-left tip (the pixel that gets sampled).
// The swatch badge for the Eyedropper tool's target (GitHub issue 151): the
// foreground square upper-left, the background square lower-right, as in the
// tool palette's color well. The target is solid and drawn on top; the other is
// a hollow outline. Both carry the dark halo the glyph uses.
void draw_eyedropper_target_badge(QPainter& painter, bool background) {
  const QRectF foreground(19.5, 19.5, 7.0, 7.0);
  const QRectF back(23.5, 23.5, 7.0, 7.0);
  const auto hollow = [&painter](const QRectF& square) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(20, 23, 28), 3.0));
    painter.drawRect(square);
    painter.setPen(QPen(QColor(245, 248, 252), 1.2));
    painter.drawRect(square);
  };
  const auto solid = [&painter](const QRectF& square) {
    painter.setPen(QPen(QColor(20, 23, 28), 1.6));
    painter.setBrush(QColor(245, 248, 252));
    painter.drawRect(square);
  };
  if (background) {
    hollow(foreground);
    solid(back);
  } else {
    hollow(back);
    solid(foreground);
  }
}

QCursor build_eyedropper_cursor(int badge = -1) {
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
  if (badge >= 0) {
    draw_eyedropper_target_badge(painter, badge == 1);
  }
  painter.end();

  return QCursor(pixmap, static_cast<int>(std::round(tip.x())), static_cast<int>(std::round(tip.y())));
}

// The classic rotate horseshoe: a 120-degree arc over the top with a solid
// triangular head at each end pointing down-and-outward. Same two-pass
// halo/ink treatment as the other tool cursors so it reads over any artwork;
// hotspot at the center.
QCursor build_rotate_cursor() {
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

// The corner rotate hint, drawn for the top-right corner and turned by
// `quarter_turns` clockwise about the hotspot for the others: a quarter arc
// whose middle sits on the hotspot and bows up and to the right (away from the
// box), with a solid head at each end following the arc. Same dark-halo,
// light-ink passes as the other tool cursors.
QCursor build_corner_rotate_cursor(int quarter_turns) {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QPointF hotspot(16.0, 16.0);
  painter.translate(hotspot);
  painter.rotate(90.0 * quarter_turns);

  constexpr double kRadius = 10.0;
  constexpr double kSpanDegrees = 84.0;
  // Arc center down-left of the hotspot, so the arc's midpoint (45 degrees) is the hotspot.
  const QPointF center(-kRadius / std::numbers::sqrt2, kRadius / std::numbers::sqrt2);
  const QRectF arc_rect(center.x() - kRadius, center.y() - kRadius, 2.0 * kRadius, 2.0 * kRadius);
  const auto start_degrees = 45.0 - kSpanDegrees / 2.0;
  const auto point_at = [&](double degrees) {
    const auto radians = degrees * std::numbers::pi / 180.0;
    return center + QPointF(kRadius * std::cos(radians), -kRadius * std::sin(radians));
  };
  // Each head points along the arc, away from its middle: clockwise (down) at the
  // low-angle end, counterclockwise (left) at the high-angle end.
  const auto head_direction = [](double degrees, double sign) {
    const auto radians = degrees * std::numbers::pi / 180.0;
    return QPointF(-std::sin(radians), -std::cos(radians)) * sign;
  };
  struct Head {
    QPointF end;
    QPointF direction;
  };
  const std::array<Head, 2> heads = {Head{point_at(start_degrees), head_direction(start_degrees, -1.0)},
                                     Head{point_at(start_degrees + kSpanDegrees),
                                          head_direction(start_degrees + kSpanDegrees, 1.0)}};
  const auto pass = [&](const QColor& color, double arc_width, double head_rim) {
    painter.setPen(QPen(color, arc_width, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(arc_rect, static_cast<int>(std::lround(start_degrees * 16.0)),
                    static_cast<int>(std::lround(kSpanDegrees * 16.0)));
    painter.setPen(QPen(color, head_rim, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    for (const auto& head : heads) {
      const QPointF normal(-head.direction.y(), head.direction.x());
      painter.drawPolygon(QPolygonF(
          {head.end + head.direction * 4.8, head.end + normal * 2.9, head.end - normal * 2.9}));
    }
  };
  pass(QColor(20, 23, 28), 4.0, 2.8);     // dark halo
  pass(QColor(245, 248, 252), 2.0, 0.8);  // light core
  painter.end();

  return QCursor(pixmap, 16, 16);
}

// A plain arrow pointer (tip at the hotspot) with a four-way move badge below
// right of it: four short arms, each ending in a solid arrowhead. Same
// dark-halo/light-ink passes as the other tool cursors.
// The pointer arrow the Move and transform-gesture cursors share: light fill, dark
// outline, tip at (2.5, 2.5) for a (3, 3) hotspot.
void draw_pointer_arrow(QPainter& painter) {
  const QPolygonF arrow({QPointF(2.5, 2.5), QPointF(2.5, 18.5), QPointF(6.5, 14.8), QPointF(9.4, 21.2),
                         QPointF(12.0, 20.0), QPointF(9.2, 13.8), QPointF(14.5, 13.8)});
  painter.setPen(QPen(QColor(20, 23, 28), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(QColor(245, 248, 252));
  painter.drawPolygon(arrow);
}

// Path Select: the pointer arrow solid dark with a light edge.
QCursor build_path_select_cursor() {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QPolygonF arrow({QPointF(2.5, 2.5), QPointF(2.5, 18.5), QPointF(6.5, 14.8), QPointF(9.4, 21.2),
                         QPointF(12.0, 20.0), QPointF(9.2, 13.8), QPointF(14.5, 13.8)});
  painter.setPen(QPen(QColor(245, 248, 252), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(QColor(20, 23, 28));
  painter.drawPolygon(arrow);
  painter.end();
  return QCursor(pixmap, 3, 3);
}

// Direct Select: the light pointer arrow with a hollow anchor square at its lower right.
QCursor build_direct_select_cursor() {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  draw_pointer_arrow(painter);
  const QRectF anchor(17.5, 17.5, 6.0, 6.0);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(20, 23, 28), 3.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawRect(anchor);
  painter.setPen(QPen(QColor(245, 248, 252), 1.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawRect(anchor);
  painter.end();
  return QCursor(pixmap, 3, 3);
}

// The arrow plus a closed outline badge at the lower right (a halo pass, then ink).
QCursor build_transform_gesture_cursor(const QPolygonF& badge) {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  draw_pointer_arrow(painter);
  painter.setBrush(Qt::NoBrush);
  for (const auto& [color, width] : {std::pair{QColor(20, 23, 28), 3.6}, std::pair{QColor(245, 248, 252), 1.6}}) {
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(badge);
  }
  painter.end();
  return QCursor(pixmap, 3, 3);
}

// The arrow plus a dashed box at the lower right: a dark halo, a light outline, then dark
// dashes over it so the box reads as a selection edge. Add, subtract and intersect put their
// glyph inside the box, light ink over a dark halo like the arrow.
QCursor build_load_selection_pointer_cursor(int operation) {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  draw_pointer_arrow(painter);
  const QRectF box(16.5, 16.5, 13.0, 12.0);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(20, 23, 28), 3.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawRect(box);
  painter.setPen(QPen(QColor(245, 248, 252), 1.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawRect(box);
  QPen dashes(QColor(20, 23, 28), 1.2, Qt::CustomDashLine, Qt::FlatCap, Qt::MiterJoin);
  dashes.setDashPattern({2.0, 2.0});
  painter.setPen(dashes);
  painter.drawRect(box);
  const auto center = box.center();
  constexpr double kGlyphHalf = 3.0;
  for (const auto& [color, width] : {std::pair{QColor(20, 23, 28), 3.4}, std::pair{QColor(245, 248, 252), 1.4}}) {
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    if (operation == 1 || operation == 2) {
      painter.drawLine(center + QPointF(-kGlyphHalf, 0.0), center + QPointF(kGlyphHalf, 0.0));
    }
    if (operation == 1) {
      painter.drawLine(center + QPointF(0.0, -kGlyphHalf), center + QPointF(0.0, kGlyphHalf));
    }
    if (operation == 3) {
      painter.drawLine(center + QPointF(-kGlyphHalf, -kGlyphHalf), center + QPointF(kGlyphHalf, kGlyphHalf));
      painter.drawLine(center + QPointF(-kGlyphHalf, kGlyphHalf), center + QPointF(kGlyphHalf, -kGlyphHalf));
    }
  }
  painter.end();
  return QCursor(pixmap, 3, 3);
}

QCursor build_move_pointer_cursor() {
  constexpr int kSize = 32;
  QPixmap pixmap(kSize, kSize);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  draw_pointer_arrow(painter);

  const QPointF center(22.5, 22.5);
  constexpr double kArm = 6.5;
  const std::array<QPointF, 4> directions = {QPointF(1.0, 0.0), QPointF(-1.0, 0.0), QPointF(0.0, 1.0),
                                             QPointF(0.0, -1.0)};
  const auto pass = [&](const QColor& color, double arm_width, double head_rim) {
    painter.setPen(QPen(color, arm_width, Qt::SolidLine, Qt::FlatCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(center - QPointF(kArm - 2.0, 0.0), center + QPointF(kArm - 2.0, 0.0));
    painter.drawLine(center - QPointF(0.0, kArm - 2.0), center + QPointF(0.0, kArm - 2.0));
    painter.setPen(QPen(color, head_rim, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    for (const auto& direction : directions) {
      const QPointF normal(-direction.y(), direction.x());
      const auto tip = center + direction * kArm;
      const auto base = center + direction * (kArm - 3.0);
      painter.drawPolygon(QPolygonF({tip, base + normal * 2.6, base - normal * 2.6}));
    }
  };
  pass(QColor(20, 23, 28), 3.6, 2.6);     // dark halo
  pass(QColor(245, 248, 252), 1.6, 0.6);  // light core
  painter.end();

  return QCursor(pixmap, 3, 3);
}

}  // namespace

QCursor eyedropper_cursor() {
  // Built once and reused: update_tool_cursor() runs on every mouse move.
  static const QCursor cursor = build_eyedropper_cursor();
  return cursor;
}

QCursor eyedropper_target_cursor(bool background) {
  static const QCursor foreground_cursor = build_eyedropper_cursor(0);
  static const QCursor background_cursor = build_eyedropper_cursor(1);
  return background ? background_cursor : foreground_cursor;
}

QCursor rotate_cursor() {
  // Built once and reused: crop hover re-applies it on every mouse move.
  static const QCursor cursor = build_rotate_cursor();
  return cursor;
}

QCursor corner_rotate_cursor(double corner_direction_degrees) {
  // Index 0 is the top-right art (outward direction 315 degrees), each next one
  // a quarter turn clockwise: bottom right, bottom left, top left.
  static const std::array<QCursor, 4> cursors = [] {
    std::array<QCursor, 4> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
      result[i] = build_corner_rotate_cursor(static_cast<int>(i));
    }
    return result;
  }();
  const auto quarter = static_cast<long>(std::lround((corner_direction_degrees - 315.0) / 90.0));
  return cursors[static_cast<std::size_t>(((quarter % 4) + 4) % 4)];
}

QCursor transform_gesture_cursor(TransformGestureCursor gesture) {
  // Built once and reused: the session hover re-applies one on every mouse move.
  static const std::array<QCursor, 3> cursors = {
      // Skew: a parallelogram leaning right.
      build_transform_gesture_cursor(
          QPolygonF({QPointF(19.5, 17.5), QPointF(29.0, 17.5), QPointF(26.0, 28.5), QPointF(16.5, 28.5)})),
      // Distort: four free corners.
      build_transform_gesture_cursor(
          QPolygonF({QPointF(17.0, 18.5), QPointF(29.0, 16.5), QPointF(27.0, 29.0), QPointF(19.5, 26.5)})),
      // Perspective: a trapezoid, the far side short.
      build_transform_gesture_cursor(
          QPolygonF({QPointF(20.5, 17.5), QPointF(25.5, 17.5), QPointF(29.5, 28.5), QPointF(16.5, 28.5)})),
  };
  return cursors[static_cast<std::size_t>(gesture)];
}

QCursor load_selection_pointer_cursor(int operation) {
  // Built once and reused: the panels re-apply it on every mouse move.
  static const std::array<QCursor, 4> cursors = {
      build_load_selection_pointer_cursor(0), build_load_selection_pointer_cursor(1),
      build_load_selection_pointer_cursor(2), build_load_selection_pointer_cursor(3)};
  return cursors[static_cast<std::size_t>(operation >= 0 && operation < 4 ? operation : 0)];
}

QCursor path_select_cursor() {
  static const QCursor cursor = build_path_select_cursor();
  return cursor;
}

QCursor direct_select_cursor() {
  static const QCursor cursor = build_direct_select_cursor();
  return cursor;
}

QCursor move_pointer_cursor() {
  // Built once and reused: the Move tool re-applies it on every mouse move.
  static const QCursor cursor = build_move_pointer_cursor();
  return cursor;
}

}  // namespace patchy::ui
