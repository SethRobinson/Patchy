#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
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
  // the whole Recent section hides when none survive the filter.
  void set_recent_files(const QStringList& paths);

  // Footer update-check line; an empty text hides it. MainWindow pushes the
  // startup check result here even while the panel is hidden, so the cached
  // status shows if the panel reappears later.
  void set_update_status(const QString& text);

 signals:
  void new_document_requested();
  void open_requested();
  void recent_file_requested(const QString& path);

 private:
  QLabel* recent_label_{nullptr};
  QListWidget* recent_list_{nullptr};
  QLabel* update_status_label_{nullptr};
};

}  // namespace patchy::ui
