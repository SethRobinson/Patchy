#include "ui/activity_spinner.hpp"

#include "ui/theme_palette.hpp"

#include <QPainter>
#include <QPen>

namespace patchy::ui {

namespace {

constexpr int kSpinnerIntervalMs = 100;
constexpr int kSpinnerStepDegrees = 30;

}  // namespace

ActivitySpinner::ActivitySpinner(QWidget* parent) : QWidget(parent) {
  setFixedSize(16, 16);
  timer_.setInterval(kSpinnerIntervalMs);
  QObject::connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
  hide();
}

void ActivitySpinner::set_active(bool active) {
  if (active == timer_.isActive()) {
    setVisible(active);
    return;
  }
  if (active) {
    angle_ = 0;
    timer_.start();
  } else {
    timer_.stop();
  }
  setVisible(active);
}

bool ActivitySpinner::active() const noexcept {
  return timer_.isActive();
}

void ActivitySpinner::advance() {
  angle_ = (angle_ + kSpinnerStepDegrees) % 360;
  update();
}

void ActivitySpinner::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(theme().dialog_busy_spinner, 2.0);
  pen.setCapStyle(Qt::RoundCap);
  painter.setPen(pen);
  painter.drawArc(QRectF(2.5, 2.5, 11.0, 11.0), -angle_ * 16, 130 * 16);
}

}  // namespace patchy::ui
