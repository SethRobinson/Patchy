#include "ui/zoom_status_bar.hpp"
#include "ui/theme_palette.hpp"

#include <QAction>
#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyleOption>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {

namespace {

constexpr int kErrorFlashDurationMs = 1000;
constexpr double kErrorFlashPulses = 2.0;  // decaying red pulses over the flash
constexpr int kErrorFlashFrameMs = 33;     // ~30 fps, only while the flash runs
// These follow the color scheme; status_error_mark is the status-bar surface
// punched out of the warning triangle, so it tracks status_bar_bg by role
// rather than by a copied literal (see theme_palette.hpp).
[[nodiscard]] QColor error_text_color() { return theme().status_error_text; }
[[nodiscard]] QColor error_wash_color() { return theme().status_error_wash; }
[[nodiscard]] QColor warning_fill_color() { return theme().status_warning_fill; }
[[nodiscard]] QColor warning_mark_color() { return theme().status_error_mark; }

// QLineEdit's fixed inset between its contents rect and the text (QLineEditPrivate::horizontalMargin).
constexpr int kLineEditHorizontalMargin = 2;

}  // namespace

ZoomPercentEdit::ZoomPercentEdit(QWidget* parent) : QLineEdit(parent) {
  setObjectName(QStringLiteral("statusZoomEdit"));
  setFixedSize(80, 18);  // refit_to_font() sets the width once the font and chevron are known
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setFocusPolicy(Qt::ClickFocus);
  static const QRegularExpression zoom_pattern(QStringLiteral("^\\s*\\d{1,6}([.,]\\d{0,2})?\\s*%?\\s*$"));
  setValidator(new QRegularExpressionValidator(zoom_pattern, this));
  connect(this, &QLineEdit::editingFinished, this, [this] { commit_editor_text(); });
  update_scrub_cursor();
}

void ZoomPercentEdit::update_scrub_cursor() {
  // The unfocused box is a scrub handle, like an options-bar label; once
  // focused it is an ordinary text field.
  setCursor(hasFocus() ? Qt::IBeamCursor : Qt::SizeHorCursor);
}

void ZoomPercentEdit::wheelEvent(QWheelEvent* event) {
  if (!isEnabled()) {
    event->ignore();
    return;
  }
  // Whole notches step one rung each; a trackpad's small deltas add up to one.
  wheel_remainder_ += event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
  while (std::abs(wheel_remainder_) >= 120) {
    const bool zoom_in = wheel_remainder_ > 0;
    wheel_remainder_ += zoom_in ? -120 : 120;
    emit zoom_step_requested(zoom_in);
  }
  event->accept();
}

void ZoomPercentEdit::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && !hasFocus()) {
    // Held back until release or a drag decides between a scrub and a click.
    scrub_armed_ = true;
    scrubbing_ = false;
    scrub_press_x_ = scrub_last_x_ = qRound(event->position().x());
    event->accept();
    return;
  }
  QLineEdit::mousePressEvent(event);
}

void ZoomPercentEdit::mouseMoveEvent(QMouseEvent* event) {
  if (scrub_armed_) {
    const int x = qRound(event->position().x());
    if (!scrubbing_ && std::abs(x - scrub_press_x_) >= QApplication::startDragDistance()) {
      scrubbing_ = true;
      scrub_last_x_ = scrub_press_x_;
    }
    if (scrubbing_ && x != scrub_last_x_) {
      emit zoom_scrub_requested(std::pow(kScrubFactorPerPixel, x - scrub_last_x_));
      scrub_last_x_ = x;
    }
    event->accept();
    return;
  }
  QLineEdit::mouseMoveEvent(event);
}

void ZoomPercentEdit::mouseReleaseEvent(QMouseEvent* event) {
  if (scrub_armed_ && event->button() == Qt::LeftButton) {
    const bool scrubbed = scrubbing_;
    scrub_armed_ = false;
    scrubbing_ = false;
    event->accept();
    if (!scrubbed) {
      setFocus(Qt::MouseFocusReason);  // a plain click edits, as before
    }
    return;
  }
  QLineEdit::mouseReleaseEvent(event);
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
  // Fewer decimals as the value grows (GitHub issue 237): two below 100%, one below
  // 1000%, none from there up, trailing zeros dropped. Display only: the view keeps
  // its exact zoom, and typed decimals still apply.
  const auto percent = zoom * 100.0;
  const int decimals = percent < 100.0 ? 2 : (percent < 1000.0 ? 1 : 0);
  const auto scale = std::pow(10.0, decimals);
  auto text = QString::number(std::round(percent * scale) / scale, 'f', decimals);
  if (decimals > 0) {
    while (text.endsWith(QLatin1Char('0'))) {
      text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
    }
  }
  return text + QLatin1Char('%');
}

bool ZoomPercentEdit::event(QEvent* event) {
  const auto handled = QLineEdit::event(event);
  switch (event->type()) {
    case QEvent::Polish:
    case QEvent::FontChange:
    case QEvent::StyleChange:
    case QEvent::ActionAdded:
    case QEvent::ActionChanged:
    case QEvent::ActionRemoved:
      refit_to_font();
      break;
    default:
      break;
  }
  return handled;
}

int ZoomPercentEdit::trailing_reserve() const {
  // Each visible action button takes this much of the right edge, the same sum
  // QLineEditPrivate::sideWidgetParameters() makes (icon size + 6, plus its margin).
  const auto visible = std::count_if(actions().cbegin(), actions().cend(),
                                     [](const QAction* action) { return action->isVisible(); });
  const auto icon_size = style()->pixelMetric(QStyle::PM_LineEditIconSize, nullptr, this);
  const auto icon_margin = style()->pixelMetric(QStyle::PM_LineEditIconMargin, nullptr, this);
  return static_cast<int>(visible) * (icon_size + 6 + icon_margin);
}

int ZoomPercentEdit::widest_display_width() const {
  // Every value the box shows has at most five digits ("12800%", the zoom limit) or
  // the shape "99.99%" / "999.9%" (format_zoom_text). Spelled with the font's widest
  // digit, the widest of the three covers any value in any language's UI font.
  const QFontMetricsF metrics(font());
  auto digit = QLatin1Char('0');
  for (char c = '1'; c <= '9'; ++c) {
    if (metrics.horizontalAdvance(QLatin1Char(c)) > metrics.horizontalAdvance(digit)) {
      digit = QLatin1Char(c);
    }
  }
  qreal widest = 0.0;
  for (const auto* shape : {"#####%", "##.##%", "###.#%"}) {
    auto text = QString::fromLatin1(shape);
    text.replace(QLatin1Char('#'), digit);
    widest = std::max(widest, metrics.horizontalAdvance(text));
  }
  // QLineEdit scrolls its text once the text plus the one-pixel cursor exceeds the
  // room; one more pixel absorbs rounding.
  return static_cast<int>(std::ceil(widest)) + 2;
}

void ZoomPercentEdit::refit_to_font() {
  if (refitting_) {
    return;
  }
  refitting_ = true;
  QStyleOptionFrame option;
  initStyleOption(&option);
  const auto contents = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);
  // The frame's right border and padding sit beyond the presets chevron, so the text
  // may use them: it then runs to the chevron's button instead of stopping short of it.
  const auto right_chrome = width() - (contents.right() + 1);
  setTextMargins(0, 0, -(right_chrome + kLineEditHorizontalMargin), 0);
  const auto left_chrome = contents.left() + kLineEditHorizontalMargin;
  setFixedWidth(left_chrome + widest_display_width() + trailing_reserve());
  refitting_ = false;
}

void ZoomPercentEdit::focusInEvent(QFocusEvent* event) {
  QLineEdit::focusInEvent(event);
  update_scrub_cursor();
  // Queued so the click that gave us focus does not immediately collapse the selection.
  QMetaObject::invokeMethod(this, [this] { selectAll(); }, Qt::QueuedConnection);
}

void ZoomPercentEdit::focusOutEvent(QFocusEvent* event) {
  QLineEdit::focusOutEvent(event);  // emits editingFinished (= commit) when input is acceptable
  update_scrub_cursor();
  if (!hasAcceptableInput()) {
    setText(display_text_);
  }
  setModified(false);
}

void ZoomPercentEdit::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    // A spin box's arrows: one rung, shown at once, typing abandoned.
    setModified(false);
    emit zoom_step_requested(event->key() == Qt::Key_Up);
    setText(display_text_);
    selectAll();
    event->accept();
    return;
  }
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
  if (text() == display_text_) {
    // Enter or focus loss on the shown value keeps the exact zoom it rounds.
    setModified(false);
    return;
  }
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
      auto wash = error_wash_color();
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
  painter.setPen(error_active_ ? error_text_color() : palette().windowText().color());
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
  painter.setBrush(warning_fill_color());
  painter.drawPath(triangle);
  const auto stem_width = std::max(1.0, r.width() / 8.0);
  const auto stem_x = r.center().x() - stem_width / 2.0;
  painter.setBrush(warning_mark_color());
  painter.drawRect(QRectF(stem_x, r.top() + r.height() * 0.34, stem_width, r.height() * 0.32));
  painter.drawRect(QRectF(stem_x, r.bottom() - r.height() * 0.20, stem_width, stem_width));
  painter.restore();
}

void ZoomStatusBar::resizeEvent(QResizeEvent* event) {
  QStatusBar::resizeEvent(event);
  position_left_widget();
  emit resized();
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
