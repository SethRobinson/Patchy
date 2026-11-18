#include "ui/splash_artwork.hpp"

#include "ui/action_icons.hpp"

#include <QAbstractAnimation>
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
#include <QAccessibilityHints>
#include <QStyleHints>
#elif defined(Q_OS_WIN)
#include <QtCore/qt_windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {
namespace {

bool allow_logo_intro(const QWidget& widget) {
  const QString platform = QApplication::platformName();
  if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal") ||
      qEnvironmentVariableIsSet("PATCHY_UI_TEST_PLATFORM") || qEnvironmentVariableIsSet("PATCHY_HEADLESS")) {
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
  if (QApplication::styleHints()->accessibility()->motionPreference() == Qt::MotionPreference::ReducedMotion) {
    return false;
  }
#elif defined(Q_OS_WIN)
  // Older Windows styles read this switch internally. Their public animation
  // duration hint does not reflect it.
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) {
    return false;
  }
#endif
  return widget.style()->styleHint(QStyle::SH_Widget_Animation_Duration, nullptr, &widget) > 0;
}

double progress(int ms, int begin, int end) {
  return std::clamp(static_cast<double>(ms - begin) / (end - begin), 0.0, 1.0);
}

}  // namespace

class LogoIntro final : public QAbstractAnimation {
 public:
  explicit LogoIntro(SplashArtwork* widget) : QAbstractAnimation(widget), widget_(widget) {}
  int duration() const override { return 1240; }

  void paint(QPainter& painter, const QRect& area, const QIcon& logo) {
    const int ms = currentTime();
    if (ms >= 820) {
      // Work on an isolated icon-sized pixmap. SourceAtop preserves the SVG's
      // alpha, including the rounded edges and the cutaway above the patch.
      QPixmap frame = logo.pixmap(area.size(), painter.device()->devicePixelRatioF());
      if (ms >= 900 && !frame.isNull()) {
        QPainter overlay(&frame);
        const QSizeF size = frame.deviceIndependentSize();
        const double x = size.width() * (-0.45 + 1.75 * progress(ms, 900, duration()));
        QLinearGradient band(x, 0, x + size.width() * 0.25, size.height() * 0.06);
        band.setColorAt(0, Qt::transparent);
        band.setColorAt(0.5, QColor(255, 255, 255, 132));  // color-literal-ok: glint on the logo artwork
        band.setColorAt(1, Qt::transparent);
        overlay.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        overlay.fillRect(QRectF(QPointF(), size), band);
      }
      painter.drawPixmap(area, frame);
      return;
    }

    const QRectF view = svg_.viewBoxF();
    painter.translate(area.topLeft());
    painter.scale(area.width() / view.width(), area.height() / view.height());
    // Rendering an SVG element in isolation omits its ancestors' transforms.
    // Restore its authored bounds, including the letter group's offset.
    auto draw = [&](const QString& id) {
      const QRectF bounds = svg_.transformForElement(id).mapRect(svg_.boundsOnElement(id));
      svg_.render(&painter, id, bounds);
    };
    draw(QStringLiteral("tile"));
    draw(QStringLiteral("corner-fold"));
    if (ms < 160) {
      return;
    }
    painter.setOpacity(progress(ms, 160, 340));
    draw(QStringLiteral("p"));
    draw(QStringLiteral("folds"));
    if (ms < 380) {
      return;
    }

    const QString patch = QStringLiteral("patch");
    const QRectF bounds = svg_.transformForElement(patch).mapRect(svg_.boundsOnElement(patch));
    const double turn = progress(ms, 380, 820);
    painter.setOpacity(progress(ms, 640, 820));
    draw(QStringLiteral("patch-shadow"));
    // Flip around the paper's left edge. The free edge lifts as it passes
    // edge-on, then settles exactly onto the authored face.
    painter.setOpacity(progress(ms, 380, 475));
    painter.translate(bounds.left(), bounds.center().y());
    const double angle = std::numbers::pi * turn;
    painter.shear(0, -0.20 * std::sin(angle));
    painter.scale(-std::cos(angle), 1);
    painter.translate(-bounds.left(), -bounds.center().y());
    draw(patch);
  }

 protected:
  void updateCurrentTime(int) override { widget_->update(); }

 private:
  SplashArtwork* widget_;
  QSvgRenderer svg_{QStringLiteral(":/patchy/branding/logo.svg")};
};

SplashArtwork::SplashArtwork(QWidget* parent, Playback playback)
    : QWidget(parent), playback_(playback), logo_(patchy_app_icon()) {}

void SplashArtwork::start_intro() {
  if (!isVisible()) {
    state_ = IntroState::Finished;
    return;
  }
  state_ = IntroState::Playing;
  intro_ = new LogoIntro(this);
  connect(intro_, &QAbstractAnimation::finished, this, [this] {
    state_ = IntroState::Finished;
    update();
  });
  intro_->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashArtwork::hideEvent(QHideEvent* event) {
  if (state_ != IntroState::FirstPaint) {
    state_ = IntroState::Finished;
    if (intro_) {
      intro_->stop();
    }
  }
  QWidget::hideEvent(event);
}

void SplashArtwork::paintEvent(QPaintEvent*) {
  if (state_ == IntroState::FirstPaint) {
    if (isVisible() && (playback_ == Playback::Animate || allow_logo_intro(*this))) {
      state_ = IntroState::Queued;
      QTimer::singleShot(16, this, [this] {
        if (state_ == IntroState::Queued) {
          start_intro();
        }
      });
    } else {
      state_ = IntroState::Finished;
    }
  }
  if (state_ == IntroState::Queued) {
    return;
  }
  QPainter painter(this);
  const int side = std::min(width(), height()) * 824 / 1024;
  const QRect area((width() - side) / 2, (height() - side) / 2, side, side);
  if (state_ == IntroState::Playing && side > 0) {
    intro_->paint(painter, area, logo_);
  } else {
    logo_.paint(&painter, area);
  }
}

}  // namespace patchy::ui
