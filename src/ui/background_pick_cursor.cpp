#include "ui/background_pick_cursor.hpp"

#include "ui/tool_cursors.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QInputEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>

namespace patchy::ui {

BackgroundPickCursor::BackgroundPickCursor(QWidget* widget)
    : QObject(widget), widget_(widget), normal_cursor_(widget->cursor()) {
  // Hover moves carry the modifiers too, so Alt held before the pointer arrived counts.
  widget_->setMouseTracking(true);
  widget_->installEventFilter(this);
}

BackgroundPickCursor::~BackgroundPickCursor() {
  if (hovered_ && QCoreApplication::instance() != nullptr) {
    QCoreApplication::instance()->removeEventFilter(this);
  }
}

void BackgroundPickCursor::set_hovered(bool hovered) {
  if (hovered_ == hovered) {
    return;
  }
  hovered_ = hovered;
  // Alt goes to the focus widget, which is rarely this one (the panels never take focus):
  // watch the application's key events only while the pointer is over the widget.
  if (hovered_) {
    QCoreApplication::instance()->installEventFilter(this);
  } else {
    QCoreApplication::instance()->removeEventFilter(this);
    pressed_ = false;
    show_background_cursor(false);
  }
}

void BackgroundPickCursor::show_background_cursor(bool background) {
  if (showing_background_ == background) {
    return;
  }
  showing_background_ = background;
  widget_->setCursor(background ? eyedropper_target_cursor(true) : normal_cursor_);
}

bool BackgroundPickCursor::eventFilter(QObject* watched, QEvent* event) {
  if (watched == widget_) {
    switch (event->type()) {
      case QEvent::Enter:
        set_hovered(true);
        show_background_cursor((static_cast<QInputEvent*>(event)->modifiers() & Qt::AltModifier) != 0);
        break;
      case QEvent::Leave:
      case QEvent::Hide:
        set_hovered(false);
        break;
      case QEvent::MouseMove:
        set_hovered(true);
        if (!pressed_) {
          show_background_cursor((static_cast<QMouseEvent*>(event)->modifiers() & Qt::AltModifier) != 0);
        }
        break;
      case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
          set_hovered(true);
          pressed_ = true;
          show_background_cursor((mouse->modifiers() & Qt::AltModifier) != 0);
        }
        break;
      }
      case QEvent::MouseButtonRelease: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
          pressed_ = false;
          show_background_cursor(hovered_ && (mouse->modifiers() & Qt::AltModifier) != 0);
        }
        break;
      }
      default:
        break;
    }
    return false;
  }
  if (hovered_ && !pressed_ && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
    const auto* key = static_cast<QKeyEvent*>(event);
    // The Alt key's own event: platforms differ on whether its modifiers already
    // include Alt, so the key decides; any other key carries the current state.
    const bool alt = key->key() == Qt::Key_Alt ? event->type() == QEvent::KeyPress
                                                : (key->modifiers() & Qt::AltModifier) != 0;
    show_background_cursor(alt);
  }
  return false;
}

}  // namespace patchy::ui
