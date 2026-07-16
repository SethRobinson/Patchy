#include "ui/zoom_status_bar.hpp"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

constexpr int kErrorFlashDurationMs = 1000;
constexpr double kErrorFlashPulses = 2.0;  // decaying red pulses over the flash
constexpr int kErrorFlashFrameMs = 33;     // ~30 fps, only while the flash runs
// Colors tuned for the #252525 status bar (QStatusBar rule in photoshop_style()).
const QColor kErrorTextColor(0xff, 0x6b, 0x68);
const QColor kErrorWashColor(0xa8, 0x32, 0x32);
const QColor kWarningFillColor(0xe0, 0x5c, 0x5c);
const QColor kWarningMarkColor(0x25, 0x25, 0x25);

}  // namespace

ZoomPercentEdit::ZoomPercentEdit(QWidget* parent) : QLineEdit(parent) {
  setObjectName(QStringLiteral("statusZoomEdit"));
  setFixedSize(64, 18);
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setFocusPolicy(Qt::ClickFocus);
  static const QRegularExpression zoom_pattern(QStringLiteral("^\\s*\\d{1,6}([.,]\\d{0,2})?\\s*%?\\s*$"));
  setValidator(new QRegularExpressionValidator(zoom_pattern, this));
  connect(this, &QLineEdit::editingFinished, this, [this] { commit_editor_text(); });
}

void ZoomPercentEdit::set_display_zoom(double zoom) {
  display_text_ = format_zoom_text(zoom);
  if (!isModified()) {
    setText(display_text_);
  }
}

void ZoomPercentEdit::clear_display() {
  display_text_.clear();
  setText(QString());
}

QString ZoomPercentEdit::format_zoom_text(double zoom) {
  const auto percent = zoom * 100.0;
  const auto rounded = std::round(percent * 100.0) / 100.0;
  QString text;
  if (std::abs(rounded - std::round(rounded)) < 0.005) {
    text = QString::number(static_cast<qlonglong>(std::llround(rounded)));
  } else {
    text = QString::number(rounded, 'f', 2);
    while (text.endsWith(QLatin1Char('0'))) {
      text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
    }
  }
  return text + QLatin1Char('%');
}

void ZoomPercentEdit::focusInEvent(QFocusEvent* event) {
  QLineEdit::focusInEvent(event);
  // Queued so the click that gave us focus does not immediately collapse the selection.
  QMetaObject::invokeMethod(this, [this] { selectAll(); }, Qt::QueuedConnection);
}

void ZoomPercentEdit::focusOutEvent(QFocusEvent* event) {
  QLineEdit::focusOutEvent(event);  // emits editingFinished (= commit) when input is acceptable
  if (!hasAcceptableInput()) {
    setText(display_text_);
  }
  setModified(false);
}

void ZoomPercentEdit::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    setText(display_text_);
    clearFocus();
    event->accept();
    return;
  }
  QLineEdit::keyPressEvent(event);
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    clearFocus();
  }
}

void ZoomPercentEdit::commit_editor_text() {
  auto value_text = text();
  value_text.remove(QLatin1Char('%'));
  value_text.replace(QLatin1Char(','), QLatin1Char('.'));
  bool ok = false;
  const auto percent = value_text.trimmed().toDouble(&ok);
  if (!ok || percent <= 0.0) {
    setText(display_text_);
    return;
  }
  setModified(false);
  emit zoom_percent_committed(percent);  // the host reacts by calling set_display_zoom
  setText(display_text_);                // normalize even when the zoom did not change
}

ZoomStatusBar::ZoomStatusBar(QWidget* parent) : QStatusBar(parent), error_flash_timer_(this) {
  error_flash_timer_.setInterval(kErrorFlashFrameMs);
  connect(&error_flash_timer_, &QTimer::timeout, this, [this] {
    if (error_flash_clock_.elapsed() >= kErrorFlashDurationMs) {
      error_flash_timer_.stop();  // settle into the persistent red state
    }
    update();
  });
  // Any different message (info showMessage, clearMessage, a new error) ends
  // the error presentation. showMessage with an IDENTICAL string never emits
  // messageChanged, which is why show_error_message restarts the flash itself.
  connect(this, &QStatusBar::messageChanged, this, [this](const QString& text) {
    if (!error_active_ || text == error_text_) {
      return;
    }
    error_active_ = false;
    error_text_.clear();
    error_flash_timer_.stop();
    update();
  });
}

void ZoomStatusBar::show_error_message(const QString& text) {
  // Error state must be set before showMessage: the messageChanged emission
  // compares against error_text_ and must see the new error as its own.
  error_text_ = text;
  error_active_ = true;
  error_flash_clock_.start();
  error_flash_timer_.start();
  showMessage(text);  // no-op (and no signal) when text is already current
  update();
}

bool ZoomStatusBar::error_message_active() const { return error_active_; }

bool ZoomStatusBar::error_flash_running() const {
  return error_active_ && error_flash_timer_.isActive();
}

void ZoomStatusBar::set_left_widget(QWidget* widget) {
  left_widget_ = widget;
  if (left_widget_ != nullptr) {
    if (left_widget_->parentWidget() != this) {
      left_widget_->setParent(this);
    }
    left_widget_->show();
    position_left_widget();
  }
  update();
}

void ZoomStatusBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  QStyleOption option;
  option.initFrom(this);
  style()->drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, this);
  const auto message = currentMessage();
  if (message.isEmpty()) {
    return;
  }
  auto left = 6;
  if (left_widget_ != nullptr && left_widget_->isVisible()) {
    left = left_widget_->geometry().right() + 9;
  }
  auto right = width() - 12;
  // Any other visible direct child while a message shows is a permanent widget
  // (or the size grip); keep the message text clear of them, like QStatusBar does.
  for (auto* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (child == left_widget_ || !child->isVisible()) {
      continue;
    }
    if (child->x() > left) {
      right = std::min(right, child->x() - 2);
    }
  }
  if (right <= left) {
    return;
  }
  if (error_active_ && error_flash_timer_.isActive()) {
    const auto phase = std::clamp(
        static_cast<double>(error_flash_clock_.elapsed()) / kErrorFlashDurationMs, 0.0, 1.0);
    const auto wave =
        0.5 * (1.0 + std::cos(phase * 2.0 * std::numbers::pi * kErrorFlashPulses));
    const auto alpha = static_cast<int>(std::lround(170.0 * wave * (1.0 - phase)));
    if (alpha > 0) {
      auto wash = kErrorWashColor;
      wash.setAlpha(alpha);
      painter.save();
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setPen(Qt::NoPen);
      painter.setBrush(wash);
      painter.drawRoundedRect(QRect(left - 4, 1, right - (left - 4), height() - 2), 3, 3);
      painter.restore();
    }
  }
  auto text_left = left;
  if (error_active_) {
    const auto icon_side = fontMetrics().ascent() + 2;
    const QRect icon_rect(left, (height() - icon_side) / 2, icon_side, icon_side);
    paint_warning_icon(painter, icon_rect);
    text_left = icon_rect.right() + 6;
  }
  if (right <= text_left) {
    return;
  }
  painter.setPen(error_active_ ? kErrorTextColor : palette().windowText().color());
  painter.drawText(QRect(text_left, 0, right - text_left, height()),
                   Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, message);
}

void ZoomStatusBar::paint_warning_icon(QPainter& painter, const QRect& icon_rect) const {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF r = QRectF(icon_rect).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath triangle;
  triangle.moveTo(r.center().x(), r.top());
  triangle.lineTo(r.right(), r.bottom());
  triangle.lineTo(r.left(), r.bottom());
  triangle.closeSubpath();
  painter.setPen(Qt::NoPen);
  painter.setBrush(kWarningFillColor);
  painter.drawPath(triangle);
  const auto stem_width = std::max(1.0, r.width() / 8.0);
  const auto stem_x = r.center().x() - stem_width / 2.0;
  painter.setBrush(kWarningMarkColor);
  painter.drawRect(QRectF(stem_x, r.top() + r.height() * 0.34, stem_width, r.height() * 0.32));
  painter.drawRect(QRectF(stem_x, r.bottom() - r.height() * 0.20, stem_width, stem_width));
  painter.restore();
}

void ZoomStatusBar::resizeEvent(QResizeEvent* event) {
  QStatusBar::resizeEvent(event);
  position_left_widget();
}

QSize ZoomStatusBar::minimumSizeHint() const {
  auto hint = QStatusBar::minimumSizeHint();
  if (left_widget_ != nullptr) {
    hint.setHeight(std::max(hint.height(), left_widget_->height() + 4));
  }
  return hint;
}

void ZoomStatusBar::position_left_widget() {
  if (left_widget_ == nullptr) {
    return;
  }
  left_widget_->move(6, std::max(0, (height() - left_widget_->height()) / 2));
}

}  // namespace patchy::ui
