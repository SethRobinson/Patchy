#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace patchy::ui {

class MainWindow;
class ScriptEngineHost;
class JsSyntaxHighlighter;
struct ScriptFolderEntry;

// The editor pane: QPlainTextEdit plus a line-number gutter (the standard Qt
// code-editor pattern).
class ScriptCodeEditor : public QPlainTextEdit {
  Q_OBJECT

public:
  explicit ScriptCodeEditor(QWidget* parent = nullptr);

  [[nodiscard]] int line_number_area_width() const;
  void line_number_area_paint_event(QPaintEvent* event);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  void update_line_number_area_width();
  void update_line_number_area(const QRect& rect, int dy);

  QWidget* line_number_area_{nullptr};
};

// The Script Editor (File > Scripts > Script Editor...): a folder tree over
// the bundled and user script roots (script_folders.hpp; "Bundled" expands the
// shipped folders, user shadow copies replace bundled entries in place tagged
// "(modified)"), a code editor with JS highlighting, a console pane wired to
// the engine host's output, and Run/Stop. Saving a bundled script writes the
// user-folder shadow copy instead of touching the shipped file; Revert to
// Bundled (context menu) deletes the copy. Non-modal; opened through
// run_non_modal_dialog by MainWindow::open_script_editor.
class ScriptEditorDialog : public QDialog {
  Q_OBJECT

public:
  ScriptEditorDialog(MainWindow& window, ScriptEngineHost& host);

private:
  void refresh_script_tree(const QString& select_path = QString());
  void load_script(const QString& path);
  void handle_tree_activated(QTreeWidgetItem* item);
  void show_tree_context_menu(const QPoint& position);
  void revert_override_to_bundled(const QString& user_copy_path, const QString& bundled_path);
  [[nodiscard]] bool confirm_discard_changes();
  void run_current();
  void stop_running();
  void new_script();
  bool save_script();
  bool save_script_as();
  void reload_script();
  void append_console(int kind, const QString& text);
  void update_run_state();
  void set_current_path(const QString& path);
  void update_file_label();
  [[nodiscard]] bool editor_modified() const;

  MainWindow& window_;
  ScriptEngineHost& host_;
  QTreeWidget* script_tree_{nullptr};
  ScriptCodeEditor* editor_{nullptr};
  QPlainTextEdit* console_{nullptr};
  QPushButton* run_button_{nullptr};
  QPushButton* stop_button_{nullptr};
  QLabel* file_label_{nullptr};
  QString current_path_;
};

}  // namespace patchy::ui
