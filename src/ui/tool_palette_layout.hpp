#pragma once

// The tool palette's items as the customization lays them out (GitHub issues 213 and 316;
// docs/tool-palette.md, "Customizing the palette"): the gear, then each cluster's visible
// slots, one per toolbar item (one column) or two side by side per row widget (two columns),
// with a separator between clusters. Hiding a tool never touches its QAction, so its shortcut
// and its Shift+letter cycle membership are decided here, not by QAction visibility (a hidden
// QAction drops its shortcut). In wheel-scroll mode the leading items of an overflowing
// column scroll off the top and small arrows replace the overflow extension.

#include "ui/tool_palette_customization.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <vector>

class QAction;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QToolBar;
class QToolButton;
class QWidgetAction;

namespace patchy::ui {

// The tool hotkey id ("tools.hand") every tool action carries, the id a customization hides.
inline constexpr const char* kToolPaletteToolIdProperty = "toolPaletteToolId";

// One of the small chevrons that mark more tools above or below a scrolled column; a click
// scrolls one item.
class ToolPaletteScrollArrow final : public QWidget {
public:
  enum class Direction { Up, Down };
  ToolPaletteScrollArrow(Direction direction, QWidget* parent);

  [[nodiscard]] QSize sizeHint() const override;
  std::function<void()> on_click;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  Direction direction_;
};

class ToolPaletteLayout final : public QObject {
public:
  struct Slot {
    int cluster{0};
    QToolButton* button{nullptr};
    // Flyouts only: the menu, its members in menu order and the Shift+letter cycle command.
    QMenu* menu{nullptr};
    QList<QAction*> members;
    QAction* cycle{nullptr};
    // The slot's own toolbar item (one-column layout), created by add_slot.
    QWidgetAction* item{nullptr};
  };

  // `customize_widget` (the gear) becomes the palette's first item. `stash` (hidden) keeps
  // the buttons of slots that are not on the palette.
  ToolPaletteLayout(QToolBar* palette, QWidget* customize_widget, QWidget* stash);

  // Slots in palette order; `cluster` counts from 0 (select, paint, retouch, draw/type, view).
  void add_slot(int cluster, QToolButton* button, QMenu* menu, QList<QAction*> members, QAction* cycle);
  // Rebuilds the palette's items, the flyout menus and the buttons' shown tools.
  void apply(const ToolPaletteCustomization& customization);

  [[nodiscard]] const ToolPaletteCustomization& customization() const noexcept { return customization_; }
  [[nodiscard]] const std::vector<Slot>& entries() const noexcept { return slots_; }
  [[nodiscard]] bool tool_hidden(const QAction* tool) const;
  // What Shift+letter walks for a flyout: its visible members, or all of them when the
  // whole flyout is hidden (its tools keep their keys).
  [[nodiscard]] QList<QAction*> cycle_members(const QToolButton* button) const;

  // Wheel scrolling (GitHub issue 316): how many leading items sit above the top.
  [[nodiscard]] int scroll_offset() const noexcept { return scroll_offset_; }
  [[nodiscard]] bool can_scroll_up() const;
  [[nodiscard]] bool can_scroll_down() const;
  // Scrolls by whole items, positive toward the bottom; returns whether anything moved.
  bool scroll_by(int steps);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct Item {
    QAction* action{nullptr};
    bool separator{false};
    // A two-column row: its buttons go back to the stash before it is deleted.
    QWidget* row{nullptr};
  };

  [[nodiscard]] bool slot_visible(const Slot& slot) const;
  [[nodiscard]] QList<QAction*> visible_members(const Slot& slot) const;
  void refresh_flyout(const Slot& slot);
  void prepare_button(QToolButton* button);
  void clear_items();
  void build_items();
  void apply_scroll();
  void schedule_scroll_update();
  void update_scroll_state();
  void place_down_arrow();
  [[nodiscard]] bool scroll_mode() const;
  [[nodiscard]] int scrollable_count() const;

  QPointer<QToolBar> palette_;
  QPointer<QWidget> stash_;
  QPointer<QToolButton> extension_;
  QAction* customize_item_{nullptr};
  QAction* scroll_up_item_{nullptr};
  ToolPaletteScrollArrow* scroll_down_arrow_{nullptr};
  std::vector<Slot> slots_;
  std::vector<Item> items_;
  ToolPaletteCustomization customization_;
  int scroll_offset_{0};
  int wheel_remainder_{0};
  bool scroll_update_scheduled_{false};
};

}  // namespace patchy::ui
