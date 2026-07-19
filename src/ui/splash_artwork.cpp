#include "ui/splash_artwork.hpp"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>

namespace patchy::ui {

namespace {

// The artwork was designed at the About dialog's 210x270; painting happens in that
// logical space and scales uniformly so other sizes keep the same proportions.
constexpr int kLogicalWidth = 210;
constexpr int kLogicalHeight = 270;

}  // namespace

SplashArtwork::SplashArtwork(QWidget* parent) : QWidget(parent) {}

void SplashArtwork::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const qreal scale = std::min(width() / qreal(kLogicalWidth), height() / qreal(kLogicalHeight));
  painter.translate((width() - kLogicalWidth * scale) / 2.0, (height() - kLogicalHeight * scale) / 2.0);
  painter.scale(scale, scale);

  const QRectF bounds = QRectF(0, 0, kLogicalWidth, kLogicalHeight).adjusted(10, 14, -10, -14);
  QLinearGradient glow(bounds.topLeft(), bounds.bottomRight());
  glow.setColorAt(0.0, QColor(88, 170, 235));
  glow.setColorAt(0.58, QColor(132, 214, 169));
  glow.setColorAt(1.0, QColor(242, 177, 92));
  painter.setPen(Qt::NoPen);
  painter.setBrush(glow);
  painter.drawRoundedRect(bounds, 28, 28);

  painter.setBrush(QColor(22, 29, 39, 230));
  painter.drawRoundedRect(bounds.adjusted(9, 9, -9, -9), 22, 22);

  const QRectF canvas(bounds.left() + 34, bounds.top() + 30, bounds.width() - 68, bounds.height() - 74);
  painter.setPen(QPen(QColor(231, 237, 245), 3));
  painter.setBrush(QColor(247, 249, 252));
  painter.drawRoundedRect(canvas, 16, 16);

  const QRectF patch_a(canvas.left() + 20, canvas.top() + 20, 54, 46);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(88, 170, 235));
  painter.drawRoundedRect(patch_a, 11, 11);

  const QRectF patch_b(canvas.right() - 76, canvas.center().y() - 18, 58, 50);
  painter.setBrush(QColor(132, 214, 169));
  painter.drawRoundedRect(patch_b, 12, 12);

  QPainterPath cut;
  cut.moveTo(canvas.left() + 38, canvas.bottom() - 46);
  cut.lineTo(canvas.left() + 78, canvas.bottom() - 70);
  cut.lineTo(canvas.left() + 122, canvas.bottom() - 38);
  cut.lineTo(canvas.left() + 84, canvas.bottom() - 18);
  cut.closeSubpath();
  painter.setBrush(QColor(242, 177, 92));
  painter.drawPath(cut);

  painter.setPen(QPen(QColor(24, 31, 42), 5, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QPointF(canvas.left() + 36, canvas.bottom() + 14),
                   QPointF(canvas.right() - 18, canvas.bottom() + 14));
  painter.setPen(QPen(QColor(232, 238, 246), 3, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QPointF(canvas.left() + 44, canvas.bottom() + 14),
                   QPointF(canvas.right() - 26, canvas.bottom() + 14));
}

}  // namespace patchy::ui
