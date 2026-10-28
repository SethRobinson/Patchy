#pragma once

// The tool palette's items as the customization lays them out (GitHub issues 213 and 316;
// docs/tool-palette.md, "Customizing the palette"): each cluster's visible slots, one per toolbar item (one column) or two side by side per row widget (two columns),
// with a separator between clusters. Hiding a tool never touches its QAction, so its shortcut
// and its Shift+letter cycle membership are decided here, not by QAction visibility (a hidden
// QAction drops its shortcut). In wheel-scroll mode the leading items of an overflowing
// column scroll off the top, small arrows scroll it, and an expand row of the palette's own
// stands in for the overflow extension (GitHub issue 488).

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

// One of the full-width arrow rows of a scrolled column: Up and Down mark more tools above or
// below and scroll one item per click; Expand opens the overflow columns.
class ToolPaletteScrollArrow final : public QWidget {
public:
  enum class Direction { Up, Down, Expand };
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

  // `stash` (hidden) keeps the buttons of slots that are not on the palette.
  ToolPaletteLayout(QToolBar* palette, QWidget* stash);

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
  // Wheel-scroll mode (GitHub issue 488): the height of the expand row kept clear directly
  // above the footer, 0 when nothing is hidden or the columns are open. The footer placer
  // adds it to the bar's bottom margin and is told through on_bottom_reserve_changed.
  [[nodiscard]] int bottom_reserve() const noexcept { return bottom_reserve_; }
  std::function<void()> on_bottom_reserve_changed;
  // Scrolls back to the top and opens the overflow columns with every tool.
  void expand();

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
  void update_bottom_reserve();
  void place_arrows();
  [[nodiscard]] bool expanded() const;
  [[nodiscard]] bool scroll_mode() const;
  [[nodiscard]] int scrollable_count() const;

  QPointer<QToolBar> palette_;
  QPointer<QWidget> stash_;
  QPointer<QToolButton> extension_;
  QAction* scroll_up_item_{nullptr};
  ToolPaletteScrollArrow* scroll_down_arrow_{nullptr};
  ToolPaletteScrollArrow* expand_arrow_{nullptr};
  std::vector<Slot> slots_;
  std::vector<Item> items_;
  ToolPaletteCustomization customization_;
  int scroll_offset_{0};
  int wheel_remainder_{0};
  int bottom_reserve_{0};
  bool scroll_update_scheduled_{false};
};

}  // namespace patchy::ui
