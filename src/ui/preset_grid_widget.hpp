#pragma once

#include "ui/preset_tree_widget.hpp"

#include <QListWidget>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QToolButton;

namespace patchy::ui {

// The thumbnail-grid twin of PresetTreeWidget (the Custom Shapes manager and picker, GitHub
// issue 486): entries are icon-only cells (name in the tooltip and accessible name) that wrap
// under full-width folder header rows, which open and close like the tree's folder rows. Same
// entries callback, selection rules (a selected header stands for its folder's entries),
// filter, collapse memory and drop-onto-folder callback as the tree, so a dialog can switch
// between the two (PresetBrowserView). Headers open and close from their arrow, a double-click,
// Enter or Left/Right; set_folder_click_toggles makes any click on a header do it.
class PresetGridWidget : public QListWidget {
public:
  static constexpr int kFolderMarkerRole = PresetTreeWidget::kFolderMarkerRole;
  // The entry's name (header rows: the folder's label) for the filter.
  static constexpr int kNameRole = Qt::UserRole + 2;
  // Header rows: whether the folder is open. Entry rows: the folder they sit in.
  static constexpr int kExpandedRole = Qt::UserRole + 3;
  static constexpr int kFolderRole = Qt::UserRole + 4;

  explicit PresetGridWidget(QWidget* parent = nullptr);

  void set_entries_callback(std::function<std::vector<PresetTreeEntry>()> callback);
  void set_folder_label_callback(std::function<QString(const QString& folder, int count)> callback);
  // Cell edge in pixels; the icon size stays whatever setIconSize set.
  void set_cell_extent(int extent);
  void set_reload_fallback(PresetTreeWidget::ReloadFallback fallback);
  void set_entry_clicked_callback(std::function<void(const QString& id)> callback);
  void set_entry_double_clicked_callback(std::function<void(const QString& id)> callback);
  void set_folder_click_toggles(bool toggles);
  // After a header opened or closed from the user's side (not reload or the filter).
  void set_folder_toggled_callback(std::function<void()> callback);

  void remember_collapsed_folders();
  [[nodiscard]] QStringList collapsed_folders() const;
  [[nodiscard]] QStringList remembered_collapsed_folders() const;
  void set_collapsed_folders(const QStringList& folders);
  void reload(const QString& select_id = {});

  void set_filter_text(const QString& text);
  [[nodiscard]] QString first_visible_entry_id() const;

  void set_drop_callback(std::function<void(const QStringList& ids, const QString& folder)> moved);
  // A drop of the selected cells onto `target` (a header or a cell: its folder; null: no folder).
  void drop_selected_on(const QListWidgetItem* target);

  [[nodiscard]] QString current_id() const;
  // The folder of the current header row; empty when the current row is an entry.
  [[nodiscard]] QString current_folder() const;
  [[nodiscard]] QStringList selected_ids() const;
  [[nodiscard]] QListWidgetItem* folder_item(const QString& folder) const;
  [[nodiscard]] QListWidgetItem* entry_item(const QString& id) const;
  [[nodiscard]] static bool is_folder_item(const QListWidgetItem* item);
  [[nodiscard]] static bool is_folder_expanded(const QListWidgetItem* item);
  [[nodiscard]] static QString item_entry_id(const QListWidgetItem* item);
  // Opens or closes a header row (hiding its cells, which also leave the selection).
  void set_folder_expanded(QListWidgetItem* header, bool expanded);
  // The row a drag is over, for the delegate's outline; -1 when none.
  [[nodiscard]] int drop_highlight_row() const { return drop_highlight_row_; }

protected:
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void startDrag(Qt::DropActions supported_actions) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  void apply_visibility();
  void update_header_widths();
  [[nodiscard]] QListWidgetItem* drop_folder_row(const QListWidgetItem* target) const;
  void set_drop_highlight(int row);

  std::function<std::vector<PresetTreeEntry>()> entries_;
  std::function<QString(const QString&, int)> folder_label_;
  std::function<void(const QString&)> entry_clicked_;
  std::function<void(const QString&)> entry_double_clicked_;
  std::function<void()> folder_toggled_;
  std::function<void(const QStringList&, const QString&)> drop_moved_;
  QSet<QString> collapsed_folders_;
  QString filter_text_;
  int cell_extent_{48};
  int drop_highlight_row_{-1};
  PresetTreeWidget::ReloadFallback reload_fallback_{PresetTreeWidget::ReloadFallback::none};
  bool folder_click_toggles_{false};
};

// A PresetTreeWidget (List) and a PresetGridWidget (Grid) over the same entries, one shown at a
// time. Configuration reaches both; queries and actions go to the shown one. Switching keeps the
// current entry, the open and closed folders and the filter.
class PresetBrowserView : public QStackedWidget {
public:
  explicit PresetBrowserView(QWidget* parent = nullptr);

  [[nodiscard]] PresetTreeWidget* tree() const { return tree_; }
  [[nodiscard]] PresetGridWidget* grid() const { return grid_; }
  [[nodiscard]] QAbstractItemView* active_view() const;
  [[nodiscard]] bool grid_view() const;
  void set_grid_view(bool grid);
  // Makes the pair checkable and exclusive, shows the current view on them and switches views
  // (then calls `changed`) when one is pressed. The caller owns the buttons and their text.
  void connect_view_buttons(QToolButton* list_button, QToolButton* grid_button,
                            std::function<void(bool grid)> changed = {});

  void set_entries_callback(const std::function<std::vector<PresetTreeEntry>()>& callback);
  void set_folder_label_callback(const std::function<QString(const QString&, int)>& callback);
  void set_reload_fallback(PresetTreeWidget::ReloadFallback fallback);
  void set_entry_clicked_callback(const std::function<void(const QString&)>& callback);
  void set_entry_double_clicked_callback(const std::function<void(const QString&)>& callback);
  void set_drop_callback(const std::function<void(const QStringList&, const QString&)>& moved);
  void set_selection_changed_callback(const std::function<void()>& callback);
  void set_folder_toggled_callback(const std::function<void()>& callback);

  void remember_collapsed_folders();
  [[nodiscard]] QStringList collapsed_folders() const;
  void set_collapsed_folders(const QStringList& folders);
  void reload(const QString& select_id = {});
  void set_filter_text(const QString& text);
  [[nodiscard]] QString first_visible_entry_id() const;
  // The current entry when the filter shows it; otherwise the first one the filter shows.
  [[nodiscard]] QString current_or_first_visible_entry_id() const;
  // Focuses the shown view, making the first visible row current when none is.
  void focus_first_visible();
  [[nodiscard]] QString current_id() const;
  [[nodiscard]] QString current_folder() const;
  [[nodiscard]] QStringList selected_ids() const;

private:
  PresetTreeWidget* tree_;
  PresetGridWidget* grid_;
  std::function<void()> selection_changed_;
  QString filter_text_;
};

}  // namespace patchy::ui
