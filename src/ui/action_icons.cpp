#include "ui/action_icons.hpp"

#include <QFile>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>

#include <algorithm>
#include <cmath>

int qInitResources_icons();

namespace patchy::ui {

namespace {

void ensure_icon_resources() {
  static const int initialized = qInitResources_icons();
  (void)initialized;
}

QString resource_icon_key(const QString& value) {
  if (value == QStringLiteral("CT")) {
    return QStringLiteral("cut");
  }
  if (value == QStringLiteral("CP")) {
    return QStringLiteral("copy");
  }
  if (value == QStringLiteral("CM")) {
    return QStringLiteral("copy-merged");
  }
  if (value == QStringLiteral("TR")) {
    return QStringLiteral("transform");
  }
  if (value == QStringLiteral("SA")) {
    return QStringLiteral("select-all");
  }
  if (value == QStringLiteral("DS")) {
    return QStringLiteral("deselect");
  }
  if (value == QStringLiteral("RS")) {
    return QStringLiteral("reselect");
  }
  if (value == QStringLiteral("INV") || value == QStringLiteral("inv")) {
    return QStringLiteral("invert");
  }
  if (value == QStringLiteral("GR")) {
    return QStringLiteral("grow");
  }
  if (value == QStringLiteral("SIM")) {
    return QStringLiteral("similar");
  }
  if (value == QStringLiteral("EXP")) {
    return QStringLiteral("expand");
  }
  if (value == QStringLiteral("CTR")) {
    return QStringLiteral("contract");
  }
  if (value == QStringLiteral("BD")) {
    return QStringLiteral("border");
  }
  if (value == QStringLiteral("AL")) {
    return QStringLiteral("alpha");
  }
  if (value == QStringLiteral("ADJ")) {
    return QStringLiteral("adjustment");
  }
  if (value == QStringLiteral("fx")) {
    return QStringLiteral("effects");
  }
  if (value == QStringLiteral("RN")) {
    return QStringLiteral("rename");
  }
  if (value == QStringLiteral("LVL")) {
    return QStringLiteral("levels");
  }
  if (value == QStringLiteral("CRV")) {
    return QStringLiteral("curves");
  }
  if (value == QStringLiteral("HSL")) {
    return QStringLiteral("hsl");
  }
  if (value == QStringLiteral("CB")) {
    return QStringLiteral("color-balance");
  }
  if (value == QStringLiteral("IS")) {
    return QStringLiteral("image-size");
  }
  if (value == QStringLiteral("CS")) {
    return QStringLiteral("canvas-size");
  }
  if (value == QStringLiteral("SE")) {
    return QStringLiteral("selection-edges");
  }
  if (value == QStringLiteral("D")) {
    return QStringLiteral("default-colors");
  }
  if (value == QStringLiteral("X") || value == QStringLiteral("swap")) {
    return QStringLiteral("swap-colors");
  }
  if (value == QStringLiteral("N")) {
    return QStringLiteral("selection-new");
  }
  if (value == QStringLiteral("+")) {
    return QStringLiteral("selection-add");
  }
  if (value == QStringLiteral("-")) {
    return QStringLiteral("selection-subtract");
  }
  if (value == QStringLiteral("Ix")) {
    return QStringLiteral("selection-intersect");
  }
  if (value == QStringLiteral("8BF")) {
    return QStringLiteral("plugin");
  }
  if (value == QStringLiteral("FH")) {
    return QStringLiteral("flip-h");
  }
  if (value == QStringLiteral("FV")) {
    return QStringLiteral("flip-v");
  }
  if (value == QStringLiteral("zoomIn")) {
    return QStringLiteral("zoom-in");
  }
  if (value == QStringLiteral("zoomOut")) {
    return QStringLiteral("zoom-out");
  }
  if (value == QStringLiteral("1x")) {
    return QStringLiteral("zoom-reset");
  }
  if (value == QStringLiteral("ok")) {
    return QStringLiteral("apply");
  }
  return value;
}

}  // namespace

QIcon patchy_app_icon() {
  QPixmap pixmap(64, 64);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  QRectF tile(7.0, 7.0, 50.0, 50.0);
  QLinearGradient glow(tile.topLeft(), tile.bottomRight());
  glow.setColorAt(0.0, QColor(88, 170, 235));
  glow.setColorAt(0.55, QColor(132, 214, 169));
  glow.setColorAt(1.0, QColor(242, 177, 92));
  painter.setPen(Qt::NoPen);
  painter.setBrush(glow);
  painter.drawRoundedRect(tile, 13.0, 13.0);

  tile = tile.adjusted(4.0, 4.0, -4.0, -4.0);
  painter.setBrush(QColor(23, 30, 40));
  painter.drawRoundedRect(tile, 10.0, 10.0);

  const QRectF canvas(tile.left() + 8.0, tile.top() + 8.0, tile.width() - 16.0, tile.height() - 18.0);
  painter.setPen(QPen(QColor(238, 243, 248), 2.0));
  painter.setBrush(QColor(247, 249, 252));
  painter.drawRoundedRect(canvas, 6.0, 6.0);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(88, 170, 235));
  painter.drawRoundedRect(QRectF(canvas.left() + 5.0, canvas.top() + 5.0, 13.0, 12.0), 3.0, 3.0);
  painter.setBrush(QColor(132, 214, 169));
  painter.drawRoundedRect(QRectF(canvas.right() - 18.0, canvas.center().y() - 4.0, 13.0, 12.0), 3.0, 3.0);

  QPainterPath patch;
  patch.moveTo(canvas.left() + 8.0, canvas.bottom() - 8.0);
  patch.lineTo(canvas.left() + 17.0, canvas.bottom() - 15.0);
  patch.lineTo(canvas.left() + 30.0, canvas.bottom() - 7.0);
  patch.lineTo(canvas.left() + 20.0, canvas.bottom() - 2.0);
  patch.closeSubpath();
  painter.setBrush(QColor(242, 177, 92));
  painter.drawPath(patch);

  return QIcon(pixmap);
}

QIcon simple_icon(QString text, QColor accent) {
  ensure_icon_resources();
  const auto resource_path = QStringLiteral(":/patchy/icons/%1.svg").arg(resource_icon_key(text));
  if (QFile::exists(resource_path)) {
    return QIcon(resource_path);
  }

  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(accent, 2.2));
  painter.setBrush(Qt::NoBrush);

  if (text == QStringLiteral("new")) {
    painter.drawRect(QRect(9, 6, 14, 20));
    painter.drawLine(16, 11, 16, 21);
    painter.drawLine(11, 16, 21, 16);
  } else if (text == QStringLiteral("dir")) {
    QPainterPath folder_path(QPointF(6.0, 11.0));
    folder_path.lineTo(13.0, 11.0);
    folder_path.lineTo(15.5, 8.0);
    folder_path.lineTo(25.0, 8.0);
    folder_path.lineTo(25.0, 24.0);
    folder_path.lineTo(6.0, 24.0);
    folder_path.closeSubpath();
    painter.drawPath(folder_path);
    painter.drawLine(6, 13, 25, 13);
  } else if (text == QStringLiteral("dup")) {
    painter.drawRect(QRect(7, 10, 13, 15));
    painter.drawRect(QRect(12, 6, 13, 15));
  } else if (text == QStringLiteral("RN")) {
    painter.drawLine(QPointF(9.0, 23.0), QPointF(22.0, 10.0));
    painter.drawLine(QPointF(18.0, 8.0), QPointF(24.0, 14.0));
    painter.drawLine(QPointF(8.0, 24.0), QPointF(13.0, 22.5));
    painter.drawLine(QPointF(7.0, 25.0), QPointF(9.0, 20.0));
  } else if (text == QStringLiteral("trash")) {
    painter.drawLine(9, 10, 23, 10);
    painter.drawRect(QRect(11, 11, 10, 15));
    painter.drawLine(13, 15, 13, 23);
    painter.drawLine(19, 15, 19, 23);
  } else if (text == QStringLiteral("fill")) {
    painter.drawPolygon(QPolygon({QPoint(9, 10), QPoint(21, 15), QPoint(15, 25), QPoint(5, 18)}));
    painter.setBrush(accent);
    painter.drawEllipse(QPoint(25, 24), 3, 3);
  } else if (text == QStringLiteral("clear")) {
    painter.drawRect(QRect(8, 8, 16, 16));
    painter.drawLine(8, 24, 24, 8);
  } else if (text == QStringLiteral("warp")) {
    // Warp cage: a 3x3 grid of gently bowed curves (Photoshop's warp-toggle glyph).
    painter.setPen(QPen(accent, 1.8));
    for (const double y : {8.0, 16.0, 24.0}) {
      QPainterPath row(QPointF(6.0, y + 1.5));
      row.cubicTo(QPointF(12.0, y - 2.5), QPointF(20.0, y - 2.5), QPointF(26.0, y + 1.5));
      painter.drawPath(row);
    }
    for (const double x : {7.0, 16.0, 25.0}) {
      QPainterPath column(QPointF(x - 1.0, 8.0));
      column.cubicTo(QPointF(x + 2.0, 13.0), QPointF(x + 2.0, 19.0), QPointF(x - 1.0, 25.0));
      painter.drawPath(column);
    }
  } else if (text == QStringLiteral("link")) {
    painter.drawRoundedRect(QRectF(6.5, 11.0, 10.0, 10.0), 4.0, 4.0);
    painter.drawRoundedRect(QRectF(15.5, 11.0, 10.0, 10.0), 4.0, 4.0);
    painter.drawLine(QPointF(13.0, 16.0), QPointF(19.0, 16.0));
  } else if (text == QStringLiteral("swap")) {
    painter.drawLine(8, 11, 23, 11);
    painter.drawLine(23, 11, 19, 7);
    painter.drawLine(23, 11, 19, 15);
    painter.drawLine(24, 21, 9, 21);
    painter.drawLine(9, 21, 13, 17);
    painter.drawLine(9, 21, 13, 25);
  } else if (text == QStringLiteral("default")) {
    painter.setBrush(Qt::black);
    painter.drawRect(QRect(7, 7, 13, 13));
    painter.setBrush(Qt::white);
    painter.drawRect(QRect(13, 13, 13, 13));
  } else if (text == QStringLiteral("zoomIn") || text == QStringLiteral("zoomOut")) {
    painter.drawEllipse(QRect(7, 7, 14, 14));
    painter.drawLine(18, 18, 26, 26);
    painter.drawLine(11, 14, 17, 14);
    if (text == QStringLiteral("zoomIn")) {
      painter.drawLine(14, 11, 14, 17);
    }
  } else if (text == QStringLiteral("fit")) {
    painter.drawRect(QRect(7, 9, 18, 14));
    painter.drawLine(7, 9, 12, 9);
    painter.drawLine(7, 9, 7, 14);
    painter.drawLine(25, 23, 20, 23);
    painter.drawLine(25, 23, 25, 18);
  } else if (text == QStringLiteral("crop")) {
    painter.drawLine(10, 5, 10, 23);
    painter.drawLine(5, 20, 23, 20);
    painter.drawLine(15, 9, 27, 9);
    painter.drawLine(22, 9, 22, 27);
  } else if (text == QStringLiteral("rotate")) {
    painter.drawArc(QRect(7, 7, 18, 18), 30 * 16, 280 * 16);
    painter.drawLine(22, 6, 26, 7);
    painter.drawLine(22, 6, 23, 11);
  } else if (text == QStringLiteral("merge")) {
    painter.drawRect(QRect(8, 8, 15, 10));
    painter.drawRect(QRect(11, 14, 15, 10));
    painter.drawLine(10, 26, 24, 26);
  } else if (text == QStringLiteral("stroke")) {
    QPen dashed(accent, 2.0);
    dashed.setStyle(Qt::DashLine);
    painter.setPen(dashed);
    painter.drawRect(QRect(8, 8, 16, 16));
  } else if (text == QStringLiteral("eye") || text == QStringLiteral("eyeOff")) {
    QPainterPath eye;
    eye.moveTo(5.5, 16.0);
    eye.cubicTo(9.0, 9.5, 23.0, 9.5, 26.5, 16.0);
    eye.cubicTo(23.0, 22.5, 9.0, 22.5, 5.5, 16.0);
    painter.drawPath(eye);
    painter.drawEllipse(QPointF(16.0, 16.0), 3.8, 3.8);
    if (text == QStringLiteral("eyeOff")) {
      painter.drawLine(QPointF(7.0, 25.0), QPointF(25.0, 7.0));
    }
  } else if (text == QStringLiteral("lock") || text == QStringLiteral("unlock")) {
    painter.drawRoundedRect(QRectF(8.0, 14.0, 16.0, 11.0), 2.0, 2.0);
    QPainterPath shackle;
    shackle.moveTo(11.0, 14.0);
    shackle.lineTo(11.0, 11.5);
    shackle.cubicTo(11.0, 6.5, 21.0, 6.5, 21.0, 11.5);
    if (text == QStringLiteral("lock")) {
      shackle.lineTo(21.0, 14.0);
    } else {
      shackle.lineTo(24.0, 14.0);
    }
    painter.drawPath(shackle);
    painter.drawLine(QPointF(16.0, 18.0), QPointF(16.0, 22.0));
  } else {
    painter.setPen(accent);
    auto font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text.left(2).toUpper());
  }

  return QIcon(pixmap);
}

QIcon window_chrome_icon(QString role) {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(235, 238, 242), 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));

  if (role == QStringLiteral("minimize")) {
    painter.drawLine(QPointF(9.0, 21.0), QPointF(23.0, 21.0));
  } else if (role == QStringLiteral("maximize")) {
    painter.drawRect(QRectF(9.5, 9.5, 13.0, 13.0));
  } else if (role == QStringLiteral("close")) {
    painter.drawLine(QPointF(10.0, 10.0), QPointF(22.0, 22.0));
    painter.drawLine(QPointF(22.0, 10.0), QPointF(10.0, 22.0));
  }

  return QIcon(pixmap);
}

QIcon canvas_anchor_icon(CanvasAnchor anchor) {
  QPixmap pixmap(22, 22);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(244, 244, 244), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(QColor(244, 244, 244));

  const QPointF center(11.0, 11.0);
  if (anchor == CanvasAnchor::Center) {
    painter.drawEllipse(center, 2.3, 2.3);
    return QIcon(pixmap);
  }

  QPointF target(11.0, 11.0);
  switch (anchor) {
    case CanvasAnchor::TopLeft:
      target = QPointF(5.5, 5.5);
      break;
    case CanvasAnchor::Top:
      target = QPointF(11.0, 4.8);
      break;
    case CanvasAnchor::TopRight:
      target = QPointF(16.5, 5.5);
      break;
    case CanvasAnchor::Left:
      target = QPointF(4.8, 11.0);
      break;
    case CanvasAnchor::Center:
      break;
    case CanvasAnchor::Right:
      target = QPointF(17.2, 11.0);
      break;
    case CanvasAnchor::BottomLeft:
      target = QPointF(5.5, 16.5);
      break;
    case CanvasAnchor::Bottom:
      target = QPointF(11.0, 17.2);
      break;
    case CanvasAnchor::BottomRight:
      target = QPointF(16.5, 16.5);
      break;
  }

  painter.drawLine(center, target);
  const auto angle = std::atan2(target.y() - center.y(), target.x() - center.x());
  constexpr double kArrowSize = 4.0;
  constexpr double kArrowAngle = 0.72;
  const QPointF wing_a(target.x() - std::cos(angle - kArrowAngle) * kArrowSize,
                       target.y() - std::sin(angle - kArrowAngle) * kArrowSize);
  const QPointF wing_b(target.x() - std::cos(angle + kArrowAngle) * kArrowSize,
                       target.y() - std::sin(angle + kArrowAngle) * kArrowSize);
  painter.drawLine(target, wing_a);
  painter.drawLine(target, wing_b);
  return QIcon(pixmap);
}

}  // namespace patchy::ui
