#pragma once

// The right-hand panel column (docs/right-docks.md): Layers, Channels and Paths as
// one tab group, then History, Properties, Info and Palette, each a section with a
// title row (collapse toggle and title) over its panel. Sections sit in a vertical
// QSplitter, so the dividers between them drag; a collapsed section is exactly its
// title row. Dragging a title row (or a group's tab bar) up or down moves its section
// within the column; dropped on another section's title row or tab bar, it joins that
// section as tabs. Dragging a tab sideways reorders its group's tabs, and dragged out
// between sections it becomes a section of its own. A click on a collapsed section's
// title row expands it and a double click on an expanded one collapses it (GitHub issue
// 335). Patchy owns this layout: nothing floats, nothing is a QDockWidget, no drag
// leaves the window, and every size is set through plain layout and splitter calls.

#include <QByteArray>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace patchy::ui {

// One panel. The id is the widget's object name and is what the saved panel layout
// stores, so it never changes ("layersDock", "historyDock", ...; the names predate the
// column). The prefix names the title row's widgets: <prefix>DockTitle,
// <prefix>DockTitleLabel and <prefix>DockCollapseButton.
struct PanelSpec {
  QString id;
  QString object_prefix;
  QString title;
  // The panel itself. Anything but a QScrollArea is wrapped so it scrolls sideways
  // when the column is narrower than the panel (GitHub issue 96).
  QWidget* content{nullptr};
  // The section's floor while expanded, title row included (0: the content's own).
  int expanded_minimum_height{0};
  int expanded_maximum_height{QWIDGETSIZE_MAX};
  // The height an expand asks for (0: the floor).
  int preferred_height{0};
};

class PanelColumn final : public QWidget {
  Q_OBJECT

public:
  explicit PanelColumn(QWidget* parent = nullptr);
  ~PanelColumn() override;
  PanelColumn(const PanelColumn&) = delete;
  PanelColumn& operator=(const PanelColumn&) = delete;

  // A section of its own at the bottom of the column.
  void add_panel(const PanelSpec& spec, bool initially_expanded);
  // A tab group at the bottom of the column. Its tabs share one section, so they
  // collapse, expand and size together; the first tab starts in front. Groups formed
  // later by merging panels take the same object names.
  void add_tab_group(const QString& group_name, const std::vector<PanelSpec>& tabs, bool initially_expanded);

  // Every panel id, in column order (tab groups in tab order).
  [[nodiscard]] QStringList panel_ids() const;
  [[nodiscard]] bool has_panel(const QString& id) const;
  // Whether the panel is open (not closed). A tab behind another is open.
  [[nodiscard]] bool panel_visible(const QString& id) const;
  void set_panel_visible(const QString& id, bool visible);
  [[nodiscard]] bool panel_expanded(const QString& id) const;
  void set_panel_expanded(const QString& id, bool expanded);
  // Whether the panel is its group's current tab (always true outside a group).
  [[nodiscard]] bool panel_in_front(const QString& id) const;
  void bring_to_front(const QString& id);
  // Every panel of `id`'s section (the whole tab group for a tab).
  [[nodiscard]] QStringList section_panel_ids(const QString& id) const;
  [[nodiscard]] bool in_tab_group(const QString& id) const;

  [[nodiscard]] QWidget* panel_page(const QString& id) const;
  [[nodiscard]] QLabel* title_label(const QString& id) const;
  [[nodiscard]] QToolButton* collapse_button(const QString& id) const;
  // The section widget holding `id` (the panel's own page outside a group).
  [[nodiscard]] QWidget* section_widget(const QString& id) const;
  [[nodiscard]] QTabBar* tab_bar(const QString& id) const;
  [[nodiscard]] QSplitter* splitter() const;

  // Tab texts follow the title labels; call after a language switch rebinds them.
  void refresh_tab_titles();

  // The sections' heights, for the saved layout; restore refuses a state from a
  // different section count.
  [[nodiscard]] QByteArray save_heights() const;
  bool restore_heights(const QByteArray& state);
  // The first-launch distribution: each expanded section at its preferred height,
  // the first one (Layers) taking what is left. Before the first show it is applied
  // when the column gets its height.
  void apply_preferred_heights();
  // Lowers expanded sections' floors by up to `excess` pixels (bottom up, the first
  // section added (Layers) last wherever it sits, never below its title row plus `keep`). Returns how much
  // it lowered. The window calls it when the column's minimum would not fit the screen.
  int lower_floors(int excess, int keep = 48);
  // Puts every section's floor back to its spec (Reset Panels, a taller screen).
  void restore_floors();

  // The sections top to bottom, each named by its first panel's id (the tab group by
  // its first tab, "layersDock" until its tabs move), closed sections included.
  [[nodiscard]] QStringList section_order() const;
  // Reorders the sections to follow `ids` (any panel id names its section). Unknown ids
  // are skipped; sections left unnamed keep their relative order after the named ones.
  // Each section keeps its height.
  void set_section_order(const QStringList& ids);
  // Moves `id`'s section to sit just above the section now at `slot` (the section
  // count: the bottom), as a title-row drag does.
  void move_section(const QString& id, int slot);

  // Each tab group's panel ids in tab order, top to bottom.
  [[nodiscard]] QList<QStringList> tab_groups() const;
  // Regroups the column so its tab groups are `groups` (panels in tab order, unknown ids
  // skipped, a group of fewer than two panels ignored); every other panel gets a section
  // of its own. A formed group sits where its first panel was, with that panel in front;
  // a group that already holds the same panels only changes its tab order.
  void set_tab_groups(const QList<QStringList>& groups);
  // Reorders the tabs of the group holding `ids`' first tab to follow `ids`. Ids of other
  // sections are skipped; tabs left unnamed keep their relative order after the named ones.
  void set_tab_order(const QStringList& ids);
  // Moves tab `id` to sit just before the tab now at `slot` (the tab count: the end), as a
  // sideways drag in the tab bar does.
  void move_tab(const QString& id, int slot);
  // Moves panel `id` into `target_id`'s section as a tab just before its tab at `tab_slot`
  // (-1: after the last) and brings it to the front. A section left with one panel stops
  // being a group; one left empty goes. The section joined keeps its collapse state.
  void merge_panel(const QString& id, const QString& target_id, int tab_slot = -1);
  // The same for every panel of `id`'s section, in tab order, its front panel in front.
  void merge_section(const QString& id, const QString& target_id, int tab_slot = -1);
  // Takes panel `id` out of its group into a section of its own just above the section now
  // at `slot` (the section count: the bottom), with the group's collapse state. A panel
  // with a section of its own just moves there.
  void separate_panel(const QString& id, int slot);

signals:
  // A right-click on a title row or a tab: the panel under the pointer.
  void header_context_menu_requested(const QString& panel_id, const QPoint& global_position, bool on_tab_bar);
  void panel_visibility_changed(const QString& panel_id, bool visible);
  void panel_expanded_changed(const QString& panel_id, bool expanded);
  // A drag, move_section or set_section_order changed the order.
  void section_order_changed();
  // A group's tabs changed: their order, or panels joined or left it (a drag, move_tab,
  // set_tab_order, merge_panel, merge_section, separate_panel or set_tab_groups).
  void tab_groups_changed();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  struct Page;
  struct Section;
  // Where a drag would land.
  struct Drop {
    enum class Kind { None, Slot, Merge, TabOrder };
    Kind kind{Kind::None};
    // Slot: the section slot (a section drag moves there, a tab drag separates there).
    // TabOrder: the tab slot in the dragged tab's own bar.
    int slot{-1};
    // Merge: the section joined, and the tab slot there (-1: after its tabs).
    Section* target{nullptr};
    int tab_slot{-1};
  };

  [[nodiscard]] Page* page(const QString& id) const;
  [[nodiscard]] Section* section_of(const QString& id) const;
  [[nodiscard]] int section_index(const Section* section) const;
  Page* build_page(Section& section, const PanelSpec& spec);
  void apply_section_state(Section& section);
  void update_section_visibility(Section& section);
  void grant_preferred_height(Section& section);
  void settle_sizes(const Section& changed);
  [[nodiscard]] int collapsed_height(const Section& section) const;
  [[nodiscard]] int section_floor(const Section& section) const;
  [[nodiscard]] int section_preferred(const Section& section) const;
  [[nodiscard]] Section* section_for_drag_handle(const QObject* handle) const;
  [[nodiscard]] Page* page_for_header(const QObject* header) const;
  [[nodiscard]] Drop drop_at(const QPoint& column_position) const;
  [[nodiscard]] int tab_drop_slot_at(const Section& section, const QPoint& bar_position) const;
  void show_drop(const Drop& drop);
  void show_slot_line(int slot);
  void show_tab_line(const Section& section, int slot);
  void show_merge_frame(const Section& section);
  void start_drag_ghost();
  void move_drag_ghost(const QPoint& global_position);
  void end_section_drag(bool apply);
  void apply_drop(Section& source, int tab, const Drop& drop);
  void move_section_index(int from, int slot);
  void reorder_sections(std::vector<std::unique_ptr<Section>> order);
  bool move_tab_index(Section& section, int from, int slot);
  bool order_tabs(Section& section, const QStringList& ids);
  // Regrouping: a group's widgets, a single section turned into a group and back, a panel
  // taken out of its section or put into one, and the fix-ups after.
  void build_group_widgets(Section& section);
  void make_group(Section& section);
  void dissolve_group(Section& section);
  std::unique_ptr<Page> detach_page(const QString& id);
  void insert_page(Section& target, std::unique_ptr<Page> page, int tab_slot);
  Section& insert_section(std::unique_ptr<Page> page, int slot, bool expanded);
  void update_fill_section();
  void finish_regroup(const Section& changed);

  QSplitter* splitter_{nullptr};
  std::vector<std::unique_ptr<Section>> sections_;
  bool preferred_heights_pending_{true};
  // A title-row drag: the section pressed, the title row or tab bar pressed, where, and
  // whether the drag has started.
  Section* drag_section_{nullptr};
  QWidget* drag_handle_{nullptr};
  QPoint drag_press_global_;
  bool dragging_{false};
  // On a tab bar: the tab the press landed on (-1: none). A drag from it carries that tab
  // (drag_tab_), in any direction, instead of the section.
  int press_tab_{-1};
  int drag_tab_{-1};
  Drop drop_;
  // The translucent copy of the dragged title row or tab that follows the pointer, a child
  // of the window, and where in it the press landed.
  QPointer<QWidget> drag_ghost_;
  QPoint ghost_hotspot_;
  // The title row a left press landed on, for the click that expands it, and the section
  // that click expanded, so the double click it starts does not collapse it again.
  Page* click_page_{nullptr};
  Section* expanded_by_click_{nullptr};
  QWidget* drop_indicator_{nullptr};
  QWidget* merge_indicator_{nullptr};
  // The first panel added (Layers): its section fills the column's spare height.
  QString fill_panel_id_;
  // add_tab_group's name, which every group takes.
  QString group_name_{QStringLiteral("panelTabGroup")};
};

}  // namespace patchy::ui
