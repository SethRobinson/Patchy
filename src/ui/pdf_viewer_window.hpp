#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QAction;
class QComboBox;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPdfDocument;
class QPushButton;
class QSpinBox;
class QSplitter;
class QToolBar;
class QToolButton;

namespace patchy::ui {

class PdfPageView;

// The PDF viewer (docs/pdf-viewer.md): what opening a PDF shows by default. A MainWindow-
// parented top-level window (like a floated document) with a toolbar for pages, zoom and
// layout, a sidebar with Edit... (the Import PDF dialog, for editing pages in Patchy) and
// Print as Book..., and page thumbnails. Built only with Qt PDF (PATCHY_HAVE_QT_PDF).
class PdfViewerWindow final : public QWidget {
  Q_OBJECT

public:
  // Called by Edit... with the pages to preselect (the current page or spread) and the
  // password that opened the file. Returns true when pages opened for editing; the viewer
  // then closes.
  using EditHandler = std::function<bool(PdfViewerWindow& viewer, const std::vector<int>& pages,
                                         const QString& password)>;

  // Opens `path`, asking for a password when the file needs one. Returns nullptr after
  // telling the user why the file could not be opened (or when the password was refused).
  [[nodiscard]] static PdfViewerWindow* open(QWidget* owner, const QString& path, EditHandler edit_handler);
  ~PdfViewerWindow() override;

  [[nodiscard]] const QString& path() const { return path_; }
  [[nodiscard]] PdfPageView* page_view() const { return view_; }
  [[nodiscard]] QPdfDocument* document() const { return document_.get(); }
  [[nodiscard]] bool full_screen_mode() const { return full_screen_; }

  void edit_pages();
  void print_as_book();
  void set_full_screen(bool full_screen);

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  PdfViewerWindow(QWidget* owner, QString path, std::unique_ptr<QPdfDocument> document, QString password,
                  EditHandler edit_handler);
  void build_ui();
  void restore_settings();
  void save_settings() const;
  void sync_controls();
  void on_current_page_changed(int page);
  void on_zoom_changed(double percent);
  void apply_zoom_text();
  void request_visible_thumbnails();
  void reset_thumbnails();
  void size_thumbnail_cells();
  void update_title();
  // This window's own command for a key typed in it, or nullptr.
  [[nodiscard]] QAction* window_key_action(const QKeyEvent& event) const;

  QString path_;
  QString password_;
  std::unique_ptr<QPdfDocument> document_;
  EditHandler edit_handler_;
  PdfPageView* view_{nullptr};
  QToolBar* toolbar_{nullptr};
  QSplitter* splitter_{nullptr};
  QWidget* sidebar_{nullptr};
  QListWidget* thumbnails_{nullptr};
  QPushButton* edit_button_{nullptr};
  QPushButton* print_button_{nullptr};
  QSpinBox* page_spin_{nullptr};
  QLabel* page_count_label_{nullptr};
  QComboBox* zoom_combo_{nullptr};
  QComboBox* layout_combo_{nullptr};
  QAction* sidebar_action_{nullptr};
  QAction* continuous_action_{nullptr};
  QAction* right_to_left_action_{nullptr};
  QAction* full_screen_action_{nullptr};
  bool full_screen_{false};
  bool was_maximized_{false};
  bool syncing_{false};
};

}  // namespace patchy::ui
