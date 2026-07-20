#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;

namespace patchy::ui {

class MainWindow;
class ScriptEngineHost;
class JsSyntaxHighlighter;

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

// The Script Editor (File > Scripts > Script Editor...): script list (bundled +
// user folders), code editor with JS highlighting, console pane wired to the
// engine host's output, and Run/Stop. Non-modal; opened through
// run_non_modal_dialog by MainWindow::open_script_editor.
class ScriptEditorDialog : public QDialog {
  Q_OBJECT

public:
  ScriptEditorDialog(MainWindow& window, ScriptEngineHost& host);

private:
  void refresh_script_list();
  void load_script(const QString& path);
  void handle_list_activated(QListWidgetItem* item);
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
  [[nodiscard]] bool editor_modified() const;

  MainWindow& window_;
  ScriptEngineHost& host_;
  QListWidget* script_list_{nullptr};
  ScriptCodeEditor* editor_{nullptr};
  QPlainTextEdit* console_{nullptr};
  QPushButton* run_button_{nullptr};
  QPushButton* stop_button_{nullptr};
  QLabel* file_label_{nullptr};
  QString current_path_;
};

}  // namespace patchy::ui
