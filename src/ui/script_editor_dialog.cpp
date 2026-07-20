// The Script Editor dialog (docs/scripting.md): script browser over the bundled
// and user script folders, a JS editor pane, and a console wired to the engine
// host. Runs are one-at-a-time (the host enforces it); Stop interrupts stuck
// scripts and tears down timer/window-driven ones.

#include "ui/script_editor_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/js_syntax_highlighter.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>

namespace patchy::ui {

namespace {

constexpr int kScriptPathRole = Qt::UserRole;

// The gutter widget; forwards painting back to the editor (Qt code-editor
// example structure, non-Q_OBJECT).
class LineNumberArea : public QWidget {
public:
  explicit LineNumberArea(ScriptCodeEditor* editor) : QWidget(editor), editor_(editor) {}

  [[nodiscard]] QSize sizeHint() const override {
    return QSize(editor_->line_number_area_width(), 0);
  }

protected:
  void paintEvent(QPaintEvent* event) override { editor_->line_number_area_paint_event(event); }

private:
  ScriptCodeEditor* editor_;
};

}  // namespace

// ---------------------------------------------------------------------------
// ScriptCodeEditor

ScriptCodeEditor::ScriptCodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
  line_number_area_ = new LineNumberArea(this);
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 2);
  connect(this, &QPlainTextEdit::blockCountChanged, this,
          [this](int) { update_line_number_area_width(); });
  connect(this, &QPlainTextEdit::updateRequest, this,
          [this](const QRect& rect, int dy) { update_line_number_area(rect, dy); });
  update_line_number_area_width();
}

int ScriptCodeEditor::line_number_area_width() const {
  int digits = 1;
  int max = qMax(1, blockCount());
  while (max >= 10) {
    max /= 10;
    ++digits;
  }
  return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void ScriptCodeEditor::update_line_number_area_width() {
  setViewportMargins(line_number_area_width(), 0, 0, 0);
}

void ScriptCodeEditor::update_line_number_area(const QRect& rect, int dy) {
  if (dy != 0) {
    line_number_area_->scroll(0, dy);
  } else {
    line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
  }
  if (rect.contains(viewport()->rect())) {
    update_line_number_area_width();
  }
}

void ScriptCodeEditor::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);
  const QRect contents = contentsRect();
  line_number_area_->setGeometry(
      QRect(contents.left(), contents.top(), line_number_area_width(), contents.height()));
}

void ScriptCodeEditor::line_number_area_paint_event(QPaintEvent* event) {
  QPainter painter(line_number_area_);
  painter.fillRect(event->rect(), QColor(0x2a, 0x2a, 0x2a));
  painter.setPen(QColor(0x80, 0x80, 0x80));
  QTextBlock block = firstVisibleBlock();
  int block_number = block.blockNumber();
  int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + static_cast<int>(blockBoundingRect(block).height());
  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      painter.drawText(0, top, line_number_area_->width() - 6, fontMetrics().height(),
                       Qt::AlignRight, QString::number(block_number + 1));
    }
    block = block.next();
    top = bottom;
    bottom = top + static_cast<int>(blockBoundingRect(block).height());
    ++block_number;
  }
}

// ---------------------------------------------------------------------------
// ScriptEditorDialog

ScriptEditorDialog::ScriptEditorDialog(MainWindow& window, ScriptEngineHost& host)
    : QDialog(&window), window_(window), host_(host) {
  setObjectName(QStringLiteral("scriptEditorDialog"));
  setWindowTitle(tr("Script Editor"));
  resize(900, 620);

  auto* root = new QVBoxLayout(this);

  auto* toolbar = new QHBoxLayout();
  run_button_ = new QPushButton(tr("Run"), this);
  run_button_->setObjectName(QStringLiteral("scriptEditorRunButton"));
  stop_button_ = new QPushButton(tr("Stop"), this);
  stop_button_->setObjectName(QStringLiteral("scriptEditorStopButton"));
  auto* new_button = new QPushButton(tr("New"), this);
  new_button->setObjectName(QStringLiteral("scriptEditorNewButton"));
  auto* save_button = new QPushButton(tr("Save"), this);
  save_button->setObjectName(QStringLiteral("scriptEditorSaveButton"));
  auto* save_as_button = new QPushButton(tr("Save As..."), this);
  save_as_button->setObjectName(QStringLiteral("scriptEditorSaveAsButton"));
  auto* reload_button = new QPushButton(tr("Reload"), this);
  reload_button->setObjectName(QStringLiteral("scriptEditorReloadButton"));
  file_label_ = new QLabel(tr("untitled.js"), this);
  file_label_->setObjectName(QStringLiteral("scriptEditorFileLabel"));
  toolbar->addWidget(run_button_);
  toolbar->addWidget(stop_button_);
  toolbar->addSpacing(12);
  toolbar->addWidget(new_button);
  toolbar->addWidget(save_button);
  toolbar->addWidget(save_as_button);
  toolbar->addWidget(reload_button);
  toolbar->addStretch(1);
  toolbar->addWidget(file_label_);
  root->addLayout(toolbar);

  auto* horizontal = new QSplitter(Qt::Horizontal, this);
  script_list_ = new QListWidget(horizontal);
  script_list_->setObjectName(QStringLiteral("scriptEditorList"));
  auto* right = new QSplitter(Qt::Vertical, horizontal);
  editor_ = new ScriptCodeEditor(right);
  editor_->setObjectName(QStringLiteral("scriptEditorCode"));
  new JsSyntaxHighlighter(editor_->document());
  console_ = new QPlainTextEdit(right);
  console_->setObjectName(QStringLiteral("scriptEditorConsole"));
  console_->setReadOnly(true);
  console_->setMaximumBlockCount(2000);
  console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  right->addWidget(editor_);
  right->addWidget(console_);
  right->setStretchFactor(0, 3);
  right->setStretchFactor(1, 1);
  horizontal->addWidget(script_list_);
  horizontal->addWidget(right);
  horizontal->setStretchFactor(0, 1);
  horizontal->setStretchFactor(1, 4);
  root->addWidget(horizontal, 1);

  connect(run_button_, &QPushButton::clicked, this, [this] { run_current(); });
  connect(stop_button_, &QPushButton::clicked, this, [this] { stop_running(); });
  connect(new_button, &QPushButton::clicked, this, [this] { new_script(); });
  connect(save_button, &QPushButton::clicked, this, [this] { (void)save_script(); });
  connect(save_as_button, &QPushButton::clicked, this, [this] { (void)save_script_as(); });
  connect(reload_button, &QPushButton::clicked, this, [this] { reload_script(); });
  connect(script_list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem* item) { handle_list_activated(item); });
  auto* run_shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
  connect(run_shortcut, &QShortcut::activated, this, [this] { run_current(); });

  connect(&host_, &ScriptEngineHost::message_emitted, this,
          [this](int kind, const QString& text) { append_console(kind, text); });
  connect(&host_, &ScriptEngineHost::run_state_changed, this, [this] { update_run_state(); });

  // Output that happened while the dialog was closed (menu/CLI runs).
  for (const auto& line : host_.message_backlog()) {
    console_->appendPlainText(line);
  }

  refresh_script_list();
  update_run_state();
}

void ScriptEditorDialog::refresh_script_list() {
  script_list_->clear();
  const auto add_directory = [this](const QString& directory, const QString& header) {
    if (directory.isEmpty() || !QDir(directory).exists()) {
      return;
    }
    const auto entries = QDir(directory).entryInfoList({QStringLiteral("*.js")},
                                                       QDir::Files | QDir::Readable, QDir::Name);
    if (entries.isEmpty()) {
      return;
    }
    auto* header_item = new QListWidgetItem(header, script_list_);
    header_item->setFlags(Qt::NoItemFlags);
    for (const auto& entry : entries) {
      auto* item = new QListWidgetItem(entry.fileName(), script_list_);
      item->setData(kScriptPathRole, entry.absoluteFilePath());
    }
  };
  add_directory(MainWindow::bundled_scripts_directory(), tr("Bundled"));
  add_directory(MainWindow::user_scripts_directory(), tr("My Scripts"));
}

bool ScriptEditorDialog::editor_modified() const { return editor_->document()->isModified(); }

bool ScriptEditorDialog::confirm_discard_changes() {
  if (!editor_modified()) {
    return true;
  }
  const auto answer = show_warning_message(
      this, tr("Script Editor"), tr("Discard unsaved changes to %1?").arg(file_label_->text()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
      QStringLiteral("scriptEditorDiscardMessageBox"));
  return answer == QMessageBox::Yes;
}

void ScriptEditorDialog::handle_list_activated(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const auto path = item->data(kScriptPathRole).toString();
  if (path.isEmpty()) {
    return;
  }
  if (!confirm_discard_changes()) {
    return;
  }
  load_script(path);
}

void ScriptEditorDialog::load_script(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    append_console(2, tr("Could not read %1").arg(QDir::toNativeSeparators(path)));
    return;
  }
  editor_->setPlainText(QString::fromUtf8(file.readAll()));
  editor_->document()->setModified(false);
  set_current_path(path);
}

void ScriptEditorDialog::set_current_path(const QString& path) {
  current_path_ = path;
  file_label_->setText(path.isEmpty() ? tr("untitled.js") : QFileInfo(path).fileName());
}

void ScriptEditorDialog::run_current() {
  if (host_.run_active()) {
    return;
  }
  console_->appendPlainText(QStringLiteral("--- %1 ---").arg(file_label_->text()));
  ScriptEngineHost::RunOptions options;
  options.name = file_label_->text();
  options.path = current_path_;
  (void)host_.run_source(editor_->toPlainText(), std::move(options));
}

void ScriptEditorDialog::stop_running() { host_.stop_active_run(); }

void ScriptEditorDialog::new_script() {
  if (!confirm_discard_changes()) {
    return;
  }
  editor_->clear();
  editor_->document()->setModified(false);
  set_current_path(QString());
}

bool ScriptEditorDialog::save_script() {
  const auto bundled_dir = MainWindow::bundled_scripts_directory();
  const bool bundled = !current_path_.isEmpty() && !bundled_dir.isEmpty() &&
                       QFileInfo(current_path_).absolutePath() == bundled_dir;
  if (current_path_.isEmpty() || bundled) {
    // Bundled scripts live next to the binary (possibly read-only); edits go to
    // the user's scripts folder instead.
    return save_script_as();
  }
  QFile file(current_path_);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    append_console(2, tr("Could not write %1").arg(QDir::toNativeSeparators(current_path_)));
    return false;
  }
  file.write(editor_->toPlainText().toUtf8());
  editor_->document()->setModified(false);
  return true;
}

bool ScriptEditorDialog::save_script_as() {
  const auto suggested_name =
      current_path_.isEmpty() ? QStringLiteral("untitled.js") : QFileInfo(current_path_).fileName();
  const auto suggested =
      QDir(MainWindow::user_scripts_directory()).absoluteFilePath(suggested_name);
  const auto path = get_save_file_name(this, tr("Save Script"), suggested,
                                       tr("JavaScript files (*.js)"), nullptr,
                                       QStringLiteral("scriptEditorSaveDialog"));
  if (path.isEmpty()) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    append_console(2, tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
    return false;
  }
  file.write(editor_->toPlainText().toUtf8());
  editor_->document()->setModified(false);
  set_current_path(path);
  refresh_script_list();
  return true;
}

void ScriptEditorDialog::reload_script() {
  if (current_path_.isEmpty()) {
    return;
  }
  if (!confirm_discard_changes()) {
    return;
  }
  load_script(current_path_);
}

void ScriptEditorDialog::append_console(int kind, const QString& text) {
  switch (kind) {
    case 1:
      console_->appendHtml(QStringLiteral("<span style=\"color:#e0a030\">%1</span>")
                               .arg(text.toHtmlEscaped()));
      break;
    case 2:
      console_->appendHtml(QStringLiteral("<span style=\"color:#e05050\">%1</span>")
                               .arg(text.toHtmlEscaped()));
      break;
    default:
      console_->appendPlainText(text);
      break;
  }
}

void ScriptEditorDialog::update_run_state() {
  const bool running = host_.run_active();
  run_button_->setEnabled(!running);
  stop_button_->setEnabled(running);
}

}  // namespace patchy::ui
