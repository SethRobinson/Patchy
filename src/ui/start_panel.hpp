#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace patchy::ui {

class RecentFileList;
class RecentThumbnailCache;

// The empty-workspace start panel: shown (as an overlay filling the document tab
// area) whenever no document sessions exist. Offers New, Clipboard and Open buttons
// and the clickable recent files, as a list or as a grid of thumbnails, and carries
// the app branding (artwork, tagline, credit and links in the header, the version in
// the footer) plus the startup update-check status; MainWindow owns visibility and
// geometry.
class StartPanel final : public QWidget {
  Q_OBJECT

 public:
  explicit StartPanel(QWidget* parent = nullptr);

  // Replaces the recent list with the first files from paths (capped; the caller
  // has already dropped missing ones, and this never touches the disk). The
  // whole Recent section hides when the list is empty. The typed name filter,
  // if any, is reapplied to the new list. A list equal to the one shown changes
  // nothing (rows, hover and scroll position stay), so MainWindow pushes freely,
  // whether or not the panel is showing.
  void set_recent_files(const QStringList& paths);

  // The window just recorded an open or save: clears the filter and scrolls the
  // list to its top, so that file is the first row in view.
  void show_newest_recent_file();

  // Footer update-check line; an empty text hides it. MainWindow pushes the
  // startup check result here even while the panel is hidden, so the cached
  // status shows if the panel reappears later.
  void set_update_status(const QString& text);

  // Enables the Clipboard button; MainWindow mirrors File > Open from Clipboard's
  // enabled state here (an image on the clipboard). Always off in the browser build.
  void set_clipboard_available(bool available);

  // Recent Files as a grid of thumbnails instead of the list (GitHub issue 750). The two
  // buttons beside the filter switch it; the choice is stored (`startPanel/recentFilesGridView`,
  // app_settings.hpp) and the list is the default. The browser build keeps no recent files, so
  // it has the list only and no buttons.
  [[nodiscard]] bool recent_grid_view() const noexcept { return recent_grid_view_; }
  void set_recent_grid_view(bool grid);
  // The pictures behind the grid. MainWindow gives it the composite of a document it just
  // opened or saved and tells it which entries left the stored recent list.
  [[nodiscard]] RecentThumbnailCache& recent_thumbnails() { return *recent_thumbnails_; }

 signals:
  void new_document_requested();
  void open_clipboard_requested();
  void open_requested();
  void recent_file_requested(const QString& path);
  // Right-click on a recent row; MainWindow builds the menu so it shares the
  // Open Recent menu's actions, reveal helper, and status messages.
  void recent_file_context_menu_requested(const QString& path, const QPoint& global_position);
  // The remove (x) button on a hovered recent row (GitHub issue 157).
  void recent_file_remove_requested(const QString& path);
  // The user switched between the list and the grid.
  void recent_grid_view_changed(bool grid);

 protected:
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  // The stylesheet background, then a theme package's backdrop image when the
  // active theme has one (the empty workspace is pasteboard, theme_backdrop.hpp).
  void paintEvent(QPaintEvent* event) override;

 private:
  // Rebuilds the visible rows from recent_paths_ through the current filter text.
  void rebuild_recent_rows();
  // Opens the topmost row that is a real file; the no-match placeholder is skipped.
  void open_first_recent_match();
  // Puts the list widget and the two view buttons in step with recent_grid_view_.
  void apply_recent_view();

  // Every existing recent file, filter or no filter; the list widget holds only
  // the rows the filter currently keeps.
  std::vector<std::function<void()>> retranslation_callbacks_;
  QStringList recent_paths_;
  QLabel* recent_label_{nullptr};
  QLineEdit* recent_filter_edit_{nullptr};
  RecentFileList* recent_list_{nullptr};
  QToolButton* recent_list_view_button_{nullptr};
  QToolButton* recent_grid_view_button_{nullptr};
  RecentThumbnailCache* recent_thumbnails_{nullptr};
  bool recent_grid_view_{false};
  QLabel* update_status_label_{nullptr};
  QPushButton* clipboard_button_{nullptr};
};

}  // namespace patchy::ui
