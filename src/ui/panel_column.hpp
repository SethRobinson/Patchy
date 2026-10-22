#pragma once

// The right-hand panel column (docs/right-docks.md): Layers, Channels and Paths as
// one tab group, then History, Properties, Info and Palette, each a section with a
// title row (collapse toggle and title) over its panel. Sections sit in a vertical
// QSplitter, so the dividers between them drag; a collapsed section is exactly its
// title row. Dragging a title row (or the group's tab bar) up or down moves its
// section within the column; a click on a collapsed section's title row expands it and
// a double click on an expanded one collapses it (GitHub issue 335). Patchy owns this
// layout: nothing floats, nothing is a QDockWidget, and every size is set through plain
// layout and splitter calls.

#include <QByteArray>
#include <QPoint>
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
  // collapse, expand and size together; the first tab starts in front.
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
  // its first tab, "layersDock"), closed sections included.
  [[nodiscard]] QStringList section_order() const;
  // Reorders the sections to follow `ids` (any panel id names its section). Unknown ids
  // are skipped; sections left unnamed keep their relative order after the named ones.
  // Each section keeps its height.
  void set_section_order(const QStringList& ids);
  // Moves `id`'s section to sit just above the section now at `slot` (the section
  // count: the bottom), as a title-row drag does.
  void move_section(const QString& id, int slot);

signals:
  // A right-click on a title row or a tab: the panel under the pointer.
  void header_context_menu_requested(const QString& panel_id, const QPoint& global_position, bool on_tab_bar);
  void panel_visibility_changed(const QString& panel_id, bool visible);
  void panel_expanded_changed(const QString& panel_id, bool expanded);
  // A drag, move_section or set_section_order changed the order.
  void section_order_changed();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  struct Page;
  struct Section;

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
  [[nodiscard]] int drop_slot_at(const QPoint& column_position) const;
  void show_drop_indicator(int slot);
  void end_section_drag(bool apply);
  void move_section_index(int from, int slot);
  void reorder_sections(std::vector<std::unique_ptr<Section>> order);

  QSplitter* splitter_{nullptr};
  std::vector<std::unique_ptr<Section>> sections_;
  bool preferred_heights_pending_{true};
  // A title-row drag: the section pressed, where, and the slot it would drop into.
  Section* drag_section_{nullptr};
  QPoint drag_press_global_;
  bool dragging_{false};
  // The title row a left press landed on, for the click that expands it, and the section
  // that click expanded, so the double click it starts does not collapse it again.
  Page* click_page_{nullptr};
  Section* expanded_by_click_{nullptr};
  int drop_slot_{-1};
  QWidget* drop_indicator_{nullptr};
};

}  // namespace patchy::ui
