#pragma once

#include <QCursor>
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QStringLiteral>

namespace patchy::ui {

// The pointer shape the brush settings menu shows off the menu, over the canvas (GitHub issue
// 734 follow-up). The menu's popup grab owns the pointer, so a widget cursor set on the canvas
// or on the menu never reaches the screen there; only an application override cursor does.
// This child of the menu holds at most one override on QGuiApplication's stack: show() pushes
// it once and then changes it in place, clear() pops it once, and the destructor pops it if
// the menu goes away still holding it, so no close path can leave the override behind.
class BrushMenuCursorGuard final : public QObject {
public:
  explicit BrushMenuCursorGuard(QObject* menu) : QObject(menu) {
    setObjectName(object_name());
  }
  ~BrushMenuCursorGuard() override { clear(); }
  BrushMenuCursorGuard(const BrushMenuCursorGuard&) = delete;
  BrushMenuCursorGuard& operator=(const BrushMenuCursorGuard&) = delete;

  [[nodiscard]] static QString object_name() { return QStringLiteral("patchyBrushMenuCursorGuard"); }
  [[nodiscard]] static BrushMenuCursorGuard* of(const QObject& menu) {
    return dynamic_cast<BrushMenuCursorGuard*>(menu.findChild<QObject*>(object_name(), Qt::FindDirectChildrenOnly));
  }

  void show(const QCursor& cursor) {
    if (active_) {
      QGuiApplication::changeOverrideCursor(cursor);
    } else {
      QGuiApplication::setOverrideCursor(cursor);
      active_ = true;
    }
  }
  void clear() {
    if (active_) {
      active_ = false;
      QGuiApplication::restoreOverrideCursor();
    }
  }
  [[nodiscard]] bool active() const noexcept { return active_; }

private:
  bool active_{false};
};

}  // namespace patchy::ui
