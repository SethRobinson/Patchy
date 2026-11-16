#pragma once

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPdfDocument;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTimer;
class QToolBar;
class QToolButton;

namespace patchy::ui {

class PdfPageView;
class PdfTextSearch;

// The PDF viewer (docs/pdf-viewer.md): what opening a PDF shows by default. A MainWindow-
// parented top-level window (like a floated document) with a toolbar for pages, zoom and
// layout, a find bar, a sidebar with Edit... (the Import PDF dialog, for editing pages in
// Patchy) and Print as Book..., and page thumbnails. Built only with Qt PDF
// (PATCHY_HAVE_QT_PDF).
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

  // Find: the bar under the toolbar. Opening it puts the keyboard in its field with the
  // last text selected and searches for that text again; closing it clears the highlights
  // and hands the keyboard back to the pages.
  void open_find_bar();
  void close_find_bar();
  [[nodiscard]] bool find_bar_open() const { return find_open_; }
  // The next or previous match, around the ends of the document. With the bar closed they
  // open it, which finds the first match from the page being read.
  void find_next();
  void find_previous();
  [[nodiscard]] PdfTextSearch* text_search() const { return search_; }

  // Files dragged onto the viewer open like a drop on the main window: `accept` answers the
  // drag, `drop` opens the files. Each returns whether it took the event.
  using FileDropHandler = std::function<bool(QDropEvent* event)>;
  void set_file_drop_handlers(FileDropHandler accept, FileDropHandler drop);

  // The viewer also closes on the shortcuts `action` has at the time a key is typed: the
  // main window's File > Close, whose key the user can change. The viewer's own keys win.
  void set_close_key_source(QAction* action);

protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
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
  void build_find_bar();
  void start_find();
  void update_find_status();
  void show_find_note(const QString& text);
  // This window's own command for a key typed in it, or nullptr.
  [[nodiscard]] QAction* window_key_action(const QKeyEvent& event) const;
  [[nodiscard]] bool is_close_source_key(const QKeyEvent& event) const;

  QString path_;
  QString password_;
  std::unique_ptr<QPdfDocument> document_;
  EditHandler edit_handler_;
  FileDropHandler accept_file_drag_;
  FileDropHandler drop_files_;
  PdfPageView* view_{nullptr};
  PdfTextSearch* search_{nullptr};
  QToolBar* toolbar_{nullptr};
  QWidget* find_bar_{nullptr};
  QLineEdit* find_edit_{nullptr};
  QToolButton* find_previous_button_{nullptr};
  QToolButton* find_next_button_{nullptr};
  QLabel* find_status_label_{nullptr};
  QLabel* find_note_label_{nullptr};
  QCheckBox* find_match_case_check_{nullptr};
  QCheckBox* find_whole_words_check_{nullptr};
  QTimer* find_note_timer_{nullptr};
  bool find_open_{false};
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
  QAction* close_action_{nullptr};
  QPointer<QAction> close_key_source_;
  bool full_screen_{false};
  bool was_maximized_{false};
  bool syncing_{false};
};

}  // namespace patchy::ui
