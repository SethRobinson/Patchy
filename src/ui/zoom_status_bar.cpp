#include "ui/zoom_status_bar.hpp"

#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

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

ZoomStatusBar::ZoomStatusBar(QWidget* parent) : QStatusBar(parent) {}

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
  painter.setPen(palette().windowText().color());
  painter.drawText(QRect(left, 0, right - left, height()),
                   Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, message);
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
