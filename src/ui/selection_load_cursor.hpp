#pragma once

#include <QPoint>
#include <QPointer>
#include <QWidget>
#include <Qt>

#include <functional>

class QObject;

namespace patchy::ui {

// The selection combine a Ctrl-click (Cmd on macOS) on a Layers thumbnail or a Channels row
// asks for (#433, #525): plain replaces (0), Shift adds (1), Alt subtracts (2), Shift+Alt
// intersects (3), combine_selection_coverage's operations.
[[nodiscard]] int thumbnail_click_selection_operation(Qt::KeyboardModifiers modifiers);

// The selection-load hint shared by the Layers panel thumbnails and the Channels rows (GitHub
// issues 480 and 525; docs/layer-panel.md): while Ctrl (Cmd) is held over a place a click loads
// as a selection, that widget shows load_selection_pointer_cursor with the combine badge the
// held Shift and Alt pick. Updated from mouse moves and from Ctrl, Shift and Alt presses and
// releases (the key folded into the event's modifiers, QCursor::pos() for the place), so the
// badge follows the keys without a mouse move.
class SelectionLoadCursor final {
public:
  // The widget under `global_position` that a Ctrl-click there loads, or null. Called only
  // while Ctrl is held and no mouse button is down.
  using HitTest = std::function<QWidget*(QPoint global_position)>;
  // A widget whose cursor another hint has taken over (the Layers panel's Alt clip cursor), or
  // null: clearing leaves that widget's cursor alone.
  using ForeignCursor = std::function<const QWidget*()>;
  // Runs before each key-driven update, for an owner whose own hints follow the same keys.
  using KeyHook = std::function<void(Qt::KeyboardModifiers modifiers)>;

  explicit SelectionLoadCursor(HitTest hit, ForeignCursor foreign = {}, KeyHook key_hook = {});
  ~SelectionLoadCursor();
  SelectionLoadCursor(const SelectionLoadCursor&) = delete;
  SelectionLoadCursor& operator=(const SelectionLoadCursor&) = delete;

  void update(QPoint global_position, Qt::KeyboardModifiers modifiers);
  void clear();
  // Installs (or removes) the application-wide modifier key watcher; on while the pointer is
  // over the owner's rows, so keys aimed at the focused canvas still update the cursor.
  void watch_modifier_keys(bool watch);
  [[nodiscard]] QWidget* widget() const { return widget_.data(); }

private:
  HitTest hit_;
  ForeignCursor foreign_;
  KeyHook key_hook_;
  QPointer<QWidget> widget_;
  int operation_{-1};
  QObject* watcher_{nullptr};
};

}  // namespace patchy::ui
