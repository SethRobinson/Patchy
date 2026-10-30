#include "ui/selection_load_cursor.hpp"

#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>

#include <utility>

namespace patchy::ui {
namespace {

// Forwards Ctrl, Shift and Alt presses and releases anywhere in the application.
class ModifierKeyWatcher final : public QObject {
public:
  explicit ModifierKeyWatcher(std::function<void(const QKeyEvent&)> on_key) : on_key_(std::move(on_key)) {}

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
      const auto key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt) {
        on_key_(*static_cast<QKeyEvent*>(event));
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void(const QKeyEvent&)> on_key_;
};

}  // namespace

int thumbnail_click_selection_operation(Qt::KeyboardModifiers modifiers) {
  const bool shift = (modifiers & Qt::ShiftModifier) != 0;
  const bool alt = (modifiers & Qt::AltModifier) != 0;
  if (shift && alt) {
    return 3;
  }
  if (alt) {
    return 2;
  }
  return shift ? 1 : 0;
}

SelectionLoadCursor::SelectionLoadCursor(HitTest hit, ForeignCursor foreign, KeyHook key_hook)
    : hit_(std::move(hit)), foreign_(std::move(foreign)), key_hook_(std::move(key_hook)) {}

SelectionLoadCursor::~SelectionLoadCursor() {
  delete watcher_;
}

void SelectionLoadCursor::update(QPoint global_position, Qt::KeyboardModifiers modifiers) {
  QWidget* target = nullptr;
  if ((modifiers & Qt::ControlModifier) != 0 && QApplication::mouseButtons() == Qt::NoButton && hit_) {
    target = hit_(global_position);
  }
  const auto operation = target != nullptr ? thumbnail_click_selection_operation(modifiers) : -1;
  if (target == widget_.data() && operation == operation_) {
    return;
  }
  if (target != widget_.data()) {
    clear();
  }
  if (target != nullptr) {
    target->setCursor(load_selection_pointer_cursor(operation));
    widget_ = target;
    operation_ = operation;
  }
}

void SelectionLoadCursor::clear() {
  // Another hint may have taken over the same widget; leave it in place.
  if (!widget_.isNull() && (!foreign_ || foreign_() != widget_.data())) {
    widget_->unsetCursor();
  }
  widget_.clear();
  operation_ = -1;
}

void SelectionLoadCursor::watch_modifier_keys(bool watch) {
  if (watch == (watcher_ != nullptr)) {
    return;
  }
  if (!watch) {
    delete watcher_;
    watcher_ = nullptr;
    return;
  }
  watcher_ = new ModifierKeyWatcher([this](const QKeyEvent& event) {
    // Fold the key itself into the modifiers: some platforms report a modifier key's own
    // press or release with the state from before it.
    auto modifiers = event.modifiers();
    const auto flag = event.key() == Qt::Key_Control ? Qt::ControlModifier
                      : event.key() == Qt::Key_Shift ? Qt::ShiftModifier
                                                     : Qt::AltModifier;
    modifiers.setFlag(flag, event.type() == QEvent::KeyPress);
    if (key_hook_) {
      key_hook_(modifiers);
    }
    update(QCursor::pos(), modifiers);
  });
  qApp->installEventFilter(watcher_);
}

}  // namespace patchy::ui
