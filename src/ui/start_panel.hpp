#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace patchy::ui {

// The empty-workspace start panel: shown (as an overlay filling the document tab
// area) whenever no document sessions exist. Offers New Document / Open buttons
// and a clickable recent-files list, and carries the app branding (artwork,
// tagline, version, links) plus the startup update-check status; MainWindow owns
// visibility and geometry.
class StartPanel final : public QWidget {
  Q_OBJECT

 public:
  explicit StartPanel(QWidget* parent = nullptr);

  // Replaces the recent list with the first existing files from paths (capped);
  // the whole Recent section hides when none survive the filter. The typed name
  // filter, if any, is reapplied to the new list.
  void set_recent_files(const QStringList& paths);

  // Footer update-check line; an empty text hides it. MainWindow pushes the
  // startup check result here even while the panel is hidden, so the cached
  // status shows if the panel reappears later.
  void set_update_status(const QString& text);

 signals:
  void new_document_requested();
  void open_requested();
  void recent_file_requested(const QString& path);
  // Right-click on a recent row; MainWindow builds the menu so it shares the
  // Open Recent menu's actions, reveal helper, and status messages.
  void recent_file_context_menu_requested(const QString& path, const QPoint& global_position);

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  // Rebuilds the visible rows from recent_paths_ through the current filter text.
  void rebuild_recent_rows();
  // Opens the topmost row that is a real file; the no-match placeholder is skipped.
  void open_first_recent_match();

  // Every existing recent file, filter or no filter; the list widget holds only
  // the rows the filter currently keeps.
  QStringList recent_paths_;
  QLabel* recent_label_{nullptr};
  QLineEdit* recent_filter_edit_{nullptr};
  QListWidget* recent_list_{nullptr};
  QLabel* update_status_label_{nullptr};
};

}  // namespace patchy::ui
