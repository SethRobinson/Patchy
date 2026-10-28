#pragma once

#include <QIcon>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

#include <functional>
#include <vector>

namespace patchy::ui {

// One entry row for PresetTreeWidget::reload. The id lands in Qt::UserRole
// (UI tests read it there); a non-empty folder groups the entry under a bold
// folder row keyed by the folder name.
struct PresetTreeEntry {
  QString id;
  QString text;
  QIcon icon;
  QString tool_tip;
  QString folder;
};

// Folder-grouped preset tree shared by the preset manager dialogs and
// StyleBrowserWidget: entry rows under bold single-level folder rows with
// per-folder counts, collapse preservation across reloads, selection helpers
// (a selected folder row stands for all of its children), and reload
// preserving the selected id. Deliberately not a Q_OBJECT: configuration and
// click dispatch use std::function callbacks so each user keeps its strings
// (and therefore its translation context) and its own signals local.
class PresetTreeWidget : public QTreeWidget {
public:
  static constexpr int kFolderMarkerRole = Qt::UserRole + 1;

  // What reload selects when select_id is absent from the rebuilt tree.
  enum class ReloadFallback {
    none,                       // leave nothing selected
    first_entry_expanding,      // first row; a leading folder is expanded and its first child selected
    first_entry_when_expanded,  // first row; descend into a folder only if it is already expanded
  };

  explicit PresetTreeWidget(QWidget* parent = nullptr);

  void set_entries_callback(std::function<std::vector<PresetTreeEntry>()> callback);
  // Formats the "<folder> (<count>)" label; the callback owns the translated string.
  void set_folder_label_callback(std::function<QString(const QString& folder, int count)> callback);
  // Inserts special leading rows (e.g. the Styles page's "No Style" row) before the entries.
  void set_prepend_rows_callback(std::function<void(QTreeWidget& tree)> callback);
  void set_entry_row_height(int height);
  void set_reload_fallback(ReloadFallback fallback);
  // StyleBrowserWidget records collapsed folders at every reload; the manager
  // dialogs instead call remember_collapsed_folders() explicitly before
  // mutating their libraries.
  void set_remember_collapse_on_reload(bool remember);
  void set_entry_clicked_callback(std::function<void(const QString& id)> callback);
  void set_entry_double_clicked_callback(std::function<void(const QString& id)> callback);
  // Clicks on prepended special rows (rows that are neither folders nor entries).
  void set_special_row_clicked_callback(std::function<void(QTreeWidgetItem* item)> callback);

  void remember_collapsed_folders();
  // The collapsed folder rows as the tree shows them now, and the set a reload starts from
  // (the Custom Shape picker keeps it in settings).
  [[nodiscard]] QStringList collapsed_folders() const;
  void set_collapsed_folders(const QStringList& folders);
  void reload(const QString& select_id = {});

  // Shows only the entries whose text contains `text` (case-insensitive), with their folders
  // expanded; an empty text shows everything again with the folders as they were.
  void set_filter_text(const QString& text);
  // The first entry row the filter leaves visible, in tree order; empty when none.
  [[nodiscard]] QString first_visible_entry_id() const;

  // Turns on dragging entries onto folder rows: `moved` gets the selected entry ids and the
  // target folder (a folder row, an entry's folder, or empty for the space below the rows,
  // which ungroups them). Called after the drop returns, so it may reload the tree.
  void set_drop_callback(std::function<void(const QStringList& ids, const QString& folder)> moved);
  // What a drop of the selected rows onto `target` does (null: the space below the rows);
  // dropEvent's whole effect, public so tests can drive it without a drag.
  void drop_selected_on(const QTreeWidgetItem* target);

  // Entry id of the current row; empty for folder and special rows.
  [[nodiscard]] QString current_id() const;
  // Selected entry ids with folder rows expanded to their children, deduped,
  // in tree order.
  [[nodiscard]] QStringList selected_ids() const;
  [[nodiscard]] static QString item_entry_id(const QTreeWidgetItem* item);

protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  std::function<void(const QStringList&, const QString&)> drop_moved_;
  QString filter_text_;
  std::function<std::vector<PresetTreeEntry>()> entries_;
  std::function<QString(const QString&, int)> folder_label_;
  std::function<void(QTreeWidget&)> prepend_rows_;
  std::function<void(const QString&)> entry_clicked_;
  std::function<void(const QString&)> entry_double_clicked_;
  std::function<void(QTreeWidgetItem*)> special_row_clicked_;
  QSet<QString> collapsed_folders_;
  int entry_row_height_{46};
  ReloadFallback reload_fallback_{ReloadFallback::none};
  bool remember_collapse_on_reload_{false};
};

}  // namespace patchy::ui
