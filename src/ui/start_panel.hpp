#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace patchy::ui {

// The empty-workspace start panel: shown (as an overlay filling the document tab
// area) whenever no document sessions exist. Offers New Document / Open buttons
// and a clickable recent-files list; MainWindow owns visibility and geometry.
class StartPanel final : public QWidget {
  Q_OBJECT

 public:
  explicit StartPanel(QWidget* parent = nullptr);

  // Replaces the recent list with the first existing files from paths (capped);
  // the whole Recent section hides when none survive the filter.
  void set_recent_files(const QStringList& paths);

 signals:
  void new_document_requested();
  void open_requested();
  void recent_file_requested(const QString& path);

 private:
  QLabel* recent_label_{nullptr};
  QListWidget* recent_list_{nullptr};
};

}  // namespace patchy::ui
