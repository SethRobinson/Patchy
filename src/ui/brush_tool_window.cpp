#include "ui/brush_tool_window.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace patchy::ui {

BrushToolWindow::BrushToolWindow(const QString& object_name, const QString& title,
                                 const QString& settings_prefix, QWidget* owner)
    : QDialog(owner), settings_prefix_(settings_prefix) {
  setObjectName(object_name);
  // A tool window: above its owner, out of the taskbar, closed with it.
  setWindowFlag(Qt::Tool, true);
  setModal(false);
  setWindowModality(Qt::NonModal);
  auto* root = new QVBoxLayout(this);
  content_ = install_dark_dialog_chrome(*this, root, title);
  content_->setSpacing(4);
  unavailable_label_ = new QLabel(this);
  unavailable_label_->setObjectName(QStringLiteral("brushToolWindowUnavailableLabel"));
  unavailable_label_->setWordWrap(true);
  unavailable_label_->setVisible(false);
  content_->addWidget(unavailable_label_);
}

void BrushToolWindow::set_body(QWidget* body) {
  body_ = body;
  body->setParent(this);
  content_->addWidget(body, 1);
  // A frameless chrome window has no native border to drag: the grip resizes it.
  auto* grip_row = new QHBoxLayout();
  grip_row->setContentsMargins(0, 0, 0, 0);
  grip_row->addStretch(1);
  auto* grip = new VisibleSizeGrip(this);
  grip->setObjectName(QStringLiteral("brushToolWindowSizeGrip"));
  grip_row->addWidget(grip, 0, Qt::AlignBottom | Qt::AlignRight);
  content_->addLayout(grip_row);
  for (auto* button : findChildren<QPushButton*>()) {
    button->setAutoDefault(false);  // Enter belongs to the filter field and the canvas
    button->setDefault(false);
  }
  leave_keys_to_canvas(*this);
  install_scrub_labels_in(this);
}

void BrushToolWindow::set_unavailable_reason(const QString& reason) {
  if (reason == unavailable_reason_) {
    return;
  }
  unavailable_reason_ = reason;
  unavailable_label_->setText(reason);
  unavailable_label_->setVisible(!reason.isEmpty());
  if (body_ != nullptr) {
    body_->setEnabled(reason.isEmpty());
  }
}

void BrushToolWindow::open_window(std::optional<QPoint> global_top_left, bool activate) {
  if (!size_restored_) {
    size_restored_ = true;
    adjustSize();
    const auto saved = app_settings().value(settings_prefix_ + QStringLiteral("Size")).toSize();
    if (saved.isValid()) {
      resize(saved.expandedTo(minimumSizeHint()));
    }
    remember_dialog_position(*this);
    keep_dialog_above_parent_window(*this);
  }
  if (activate) {
    show();
  } else {
    // A restore at startup: the main window keeps the keyboard.
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    show();
    setAttribute(Qt::WA_ShowWithoutActivating, false);
  }
  if (global_top_left.has_value()) {
    move(*global_top_left);
  }
  raise();
  if (activate) {
    activateWindow();
  }
  record_open(true);
}

void BrushToolWindow::hide_for_shutdown() {
  shutting_down_ = true;
  hide();
  shutting_down_ = false;
}

bool BrushToolWindow::remembered_open(const QString& settings_prefix) {
  return app_settings().value(settings_prefix + QStringLiteral("Open"), false).toBool();
}

void BrushToolWindow::record_open(bool open) const {
  app_settings().setValue(settings_prefix_ + QStringLiteral("Open"), open);
}

void BrushToolWindow::done(int result) {
  if (!shutting_down_) {
    record_open(false);
  }
  QDialog::done(result);
}

void BrushToolWindow::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  if (isVisible() && size_restored_) {
    app_settings().setValue(settings_prefix_ + QStringLiteral("Size"), size());
  }
}

bool BrushToolWindow::forward_key(QKeyEvent* event) {
  auto* target = key_target_ ? key_target_() : nullptr;
  if (target == nullptr || !target->isEnabled()) {
    return false;
  }
  QKeyEvent copy(event->type(), event->key(), event->modifiers(), event->nativeScanCode(),
                 event->nativeVirtualKey(), event->nativeModifiers(), event->text(), event->isAutoRepeat(),
                 static_cast<quint16>(event->count()));
  QCoreApplication::sendEvent(target, &copy);
  event->accept();
  return true;
}

void BrushToolWindow::keyPressEvent(QKeyEvent* event) {
  // Escape too: it cancels the canvas gesture, as it would with the canvas focused. The
  // window closes from its title bar, like Photoshop's panels.
  if (!forward_key(event)) {
    QDialog::keyPressEvent(event);
  }
}

void BrushToolWindow::keyReleaseEvent(QKeyEvent* event) {
  if (!forward_key(event)) {
    QDialog::keyReleaseEvent(event);
  }
}

void leave_keys_to_canvas(QWidget& root) {
  for (auto* widget : root.findChildren<QWidget*>()) {
    if (widget->window() != root.window()) {
      continue;  // a combo box's drop-down list keeps its keyboard
    }
    if (qobject_cast<QAbstractButton*>(widget) != nullptr || qobject_cast<QSlider*>(widget) != nullptr ||
        qobject_cast<QComboBox*>(widget) != nullptr || qobject_cast<QAbstractItemView*>(widget) != nullptr) {
      widget->setFocusPolicy(Qt::NoFocus);
    }
  }
}

}  // namespace patchy::ui
