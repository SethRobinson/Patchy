#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QString>

class QLabel;
class QPushButton;
class QTimer;
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

// The Script Manager (File > Scripts > Script Manager...): a folder tree over
// the bundled and user script roots (script_folders.hpp; two-line rows with
// per-script icons, @name display names, and @window badges; user shadow
// copies replace bundled entries in place with an amber "modified" tag), a
// code editor with JS highlighting, a console pane wired to the engine host's
// output, Run/Stop, and a live run status ("Ready" / "Running... 13s" with a
// spinner). Saving a bundled script writes the user-folder shadow copy
// instead of touching the shipped file; Revert to Bundled (context menu)
// deletes the copy, and Set Icon from Current Window writes the script's icon
// PNG through the same shadow rule. The C:\ toolbar button (and the script
// context menu) pops a copyable command-line example for the selected script
// (script_cli_example_command), and Help opens the bundled scripting guide
// (MainWindow::open_scripting_guide). Non-modal; opened through
// run_non_modal_dialog by MainWindow::open_script_editor. Object names and the
// hotkey command id keep the historical scriptEditor/file.scripts.editor
// spelling (persisted identifiers; only the display text says Manager).
class ScriptEditorDialog : public QDialog {
  Q_OBJECT

public:
  ScriptEditorDialog(MainWindow& window, ScriptEngineHost& host);

private slots:
  // Slots so tests can drive them (context menus and real hover timing cannot
  // be exercised offscreen).
  void set_script_icon_from_window(QTreeWidgetItem* item);
  void show_script_hover_card(QTreeWidgetItem* item);
  void show_cli_example_for(const QString& script_path);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void hide_script_hover_card();
  void refresh_script_tree(const QString& select_path = QString());
  void load_script(const QString& path);
  void handle_tree_activated(QTreeWidgetItem* item);
  void show_tree_context_menu(const QPoint& position);
  void revert_override_to_bundled(const QString& user_copy_path, const QString& bundled_path);
  [[nodiscard]] bool confirm_discard_changes();
  void run_current();
  void stop_running();
  void show_cli_example();
  [[nodiscard]] QString cli_example_target_path() const;
  void update_cli_button_enabled();
  void new_script();
  bool save_script();
  bool save_script_as();
  void reload_script();
  void append_console(int kind, const QString& text);
  void update_run_state();
  void update_status_text();
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
  QPushButton* cli_button_{nullptr};
  QLabel* status_label_{nullptr};
  QWidget* run_spinner_{nullptr};  // RunSpinner (cpp-local type)
  QTimer* status_timer_{nullptr};
  QElapsedTimer run_elapsed_;
  QLabel* file_label_{nullptr};
  QString current_path_;
  QWidget* hover_card_{nullptr};  // ScriptHoverCard (cpp-local type)
  QTimer* hover_timer_{nullptr};
  QTreeWidgetItem* hover_item_{nullptr};  // cleared on every tree refresh
};

}  // namespace patchy::ui
