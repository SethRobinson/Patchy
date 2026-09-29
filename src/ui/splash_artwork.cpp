#include "ui/splash_artwork.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>

namespace patchy::ui {

SplashArtwork::SplashArtwork(QWidget* parent) : QWidget(parent) {}

void SplashArtwork::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  const QPixmap logo(QStringLiteral(":/patchy/icons/lienzo-app.png"));
  if (logo.isNull()) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const QSize target_size =
      logo.size().scaled(size(), Qt::KeepAspectRatio);

  const QRect target(
      (width() - target_size.width()) / 2,
      (height() - target_size.height()) / 2,
      target_size.width(),
      target_size.height());

  painter.drawPixmap(target, logo);
}

}  // namespace patchy::ui
