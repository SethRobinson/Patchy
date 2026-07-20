// The Script Manager dialog (docs/scripting.md): a folder tree over the
// bundled and user script roots, a JS editor pane, a console wired to the
// engine host, and a live run status (spinner + "Running... 13s", "Ready" when
// idle). Runs are one-at-a-time (the host enforces it); the stop-sign button
// interrupts stuck scripts and tears down timer/window-driven ones. Saving a
// bundled script writes the user-folder shadow copy (script_folders.hpp) so
// the shipped file stays pristine; Revert to Bundled deletes the copy.

#include "ui/script_editor_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/js_syntax_highlighter.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kScriptPathRole = Qt::UserRole;
constexpr int kScriptBundledPathRole = Qt::UserRole + 1;  // overrides: the shipped original
constexpr int kScriptFolderPathRole = Qt::UserRole + 2;   // folder rows (incl. the two roots)

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

// Small rotating-arc activity indicator, visible while a run is active; the
// dialog's status timer drives advance() (non-Q_OBJECT).
class RunSpinner : public QWidget {
public:
  explicit RunSpinner(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(16, 16); }

  void advance() {
    angle_ = (angle_ + 30) % 360;
    update();
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(0x6f, 0xb1, 0xe8), 2.0);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawArc(QRectF(2.5, 2.5, 11.0, 11.0), -angle_ * 16, 130 * 16);
  }

private:
  int angle_{0};
};

// The Stop button's red octagon stop-sign, painted in code like the rest of
// the app's icons (action_icons.cpp style).
QIcon stop_sign_icon() {
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPolygonF octagon;
    constexpr double kPi = 3.14159265358979323846;
    const double center = 16.0;
    const double radius = 14.0;
    for (int i = 0; i < 8; ++i) {
      const double angle = (static_cast<double>(i) * 45.0 + 22.5) * kPi / 180.0;
      octagon << QPointF(center + radius * std::cos(angle),
                         center + radius * std::sin(angle));
    }
    painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0xb0), 1.5));
    painter.setBrush(QColor(0xc8, 0x32, 0x28));
    painter.drawPolygon(octagon);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRect(QRectF(10.5, 10.5, 11.0, 11.0));
  }
  return QIcon(pixmap);
}

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
  setWindowTitle(tr("Script Manager"));
  resize(900, 620);

  auto* root = new QVBoxLayout(this);

  auto* toolbar = new QHBoxLayout();
  run_button_ = new QPushButton(tr("Run"), this);
  run_button_->setObjectName(QStringLiteral("scriptEditorRunButton"));
  auto* spinner = new RunSpinner(this);
  run_spinner_ = spinner;
  status_label_ = new QLabel(tr("Ready"), this);
  status_label_->setObjectName(QStringLiteral("scriptEditorStatusLabel"));
  // Reserve the widest running text so the toolbar doesn't shift while the
  // seconds tick.
  status_label_->setMinimumWidth(
      status_label_->fontMetrics().horizontalAdvance(tr("Running... %1s").arg(888)) + 4);
  stop_button_ = new QPushButton(this);
  stop_button_->setObjectName(QStringLiteral("scriptEditorStopButton"));
  stop_button_->setIcon(stop_sign_icon());
  stop_button_->setIconSize(QSize(18, 18));
  stop_button_->setToolTip(tr("Stop the running script"));
  status_timer_ = new QTimer(this);
  status_timer_->setInterval(100);
  connect(status_timer_, &QTimer::timeout, this, [this, spinner] {
    spinner->advance();
    update_status_text();
  });
  auto* new_button = new QPushButton(tr("New"), this);
  new_button->setObjectName(QStringLiteral("scriptEditorNewButton"));
  auto* save_button = new QPushButton(tr("Save"), this);
  save_button->setObjectName(QStringLiteral("scriptEditorSaveButton"));
  auto* save_as_button = new QPushButton(tr("Save As..."), this);
  save_as_button->setObjectName(QStringLiteral("scriptEditorSaveAsButton"));
  auto* reload_button = new QPushButton(tr("Reload"), this);
  reload_button->setObjectName(QStringLiteral("scriptEditorReloadButton"));
  auto* refresh_button = new QPushButton(tr("Refresh"), this);
  refresh_button->setObjectName(QStringLiteral("scriptEditorRefreshButton"));
  refresh_button->setToolTip(tr("Rescan the script folders"));
  file_label_ = new QLabel(tr("untitled.js"), this);
  file_label_->setObjectName(QStringLiteral("scriptEditorFileLabel"));
  toolbar->addWidget(run_button_);
  toolbar->addSpacing(8);
  toolbar->addWidget(run_spinner_);
  toolbar->addWidget(status_label_);
  toolbar->addWidget(stop_button_);
  toolbar->addSpacing(12);
  toolbar->addWidget(new_button);
  toolbar->addWidget(save_button);
  toolbar->addWidget(save_as_button);
  toolbar->addWidget(reload_button);
  toolbar->addWidget(refresh_button);
  toolbar->addStretch(1);
  toolbar->addWidget(file_label_);
  root->addLayout(toolbar);

  auto* horizontal = new QSplitter(Qt::Horizontal, this);
  script_tree_ = new QTreeWidget(horizontal);
  script_tree_->setObjectName(QStringLiteral("scriptEditorTree"));
  script_tree_->setHeaderHidden(true);
  script_tree_->setColumnCount(1);
  script_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
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
  horizontal->addWidget(script_tree_);
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
  connect(refresh_button, &QPushButton::clicked, this,
          [this] { refresh_script_tree(current_path_); });
  connect(script_tree_, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem* item, int) { handle_tree_activated(item); });
  connect(script_tree_, &QTreeWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_tree_context_menu(position); });
  auto* run_shortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
  connect(run_shortcut, &QShortcut::activated, this, [this] { run_current(); });
  auto* save_shortcut = new QShortcut(QKeySequence::Save, this);
  connect(save_shortcut, &QShortcut::activated, this, [this] { (void)save_script(); });
  connect(editor_->document(), &QTextDocument::modificationChanged, this,
          [this](bool) { update_file_label(); });

  connect(&host_, &ScriptEngineHost::message_emitted, this,
          [this](int kind, const QString& text) { append_console(kind, text); });
  connect(&host_, &ScriptEngineHost::run_state_changed, this, [this] { update_run_state(); });

  // Output that happened while the dialog was closed (menu/CLI runs).
  for (const auto& line : host_.message_backlog()) {
    console_->appendPlainText(line);
  }

  refresh_script_tree();
  update_run_state();
}

void ScriptEditorDialog::refresh_script_tree(const QString& select_path) {
  script_tree_->clear();
  QTreeWidgetItem* to_select = nullptr;
  const std::function<void(QTreeWidgetItem*, const std::vector<ScriptFolderEntry>&, const QString&)>
      add_entries = [&](QTreeWidgetItem* parent, const std::vector<ScriptFolderEntry>& entries,
                        const QString& root_dir) {
        for (const auto& entry : entries) {
          auto* item = new QTreeWidgetItem(parent);
          if (entry.is_folder) {
            item->setText(0, script_folder_display_name(entry.name));
            item->setFlags(Qt::ItemIsEnabled);
            item->setData(0, kScriptFolderPathRole,
                          QDir(root_dir).absoluteFilePath(entry.relative_path));
            add_entries(item, entry.children, root_dir);
            continue;
          }
          item->setText(0, entry.is_override ? tr("%1 (modified)").arg(entry.name) : entry.name);
          item->setData(0, kScriptPathRole, entry.path);
          if (entry.is_override) {
            item->setData(0, kScriptBundledPathRole, entry.bundled_path);
          }
          if (!select_path.isEmpty() && entry.path == select_path) {
            to_select = item;
          }
        }
      };
  const auto bundled_dir = MainWindow::bundled_scripts_directory();
  const auto user_dir = MainWindow::user_scripts_directory();
  const auto scan = scan_scripts(bundled_dir, user_dir);
  if (!scan.bundled.empty()) {
    auto* bundled_root = new QTreeWidgetItem(script_tree_);
    bundled_root->setText(0, tr("Bundled"));
    bundled_root->setFlags(Qt::ItemIsEnabled);
    bundled_root->setData(0, kScriptFolderPathRole, bundled_dir);
    add_entries(bundled_root, scan.bundled, bundled_dir);
  }
  auto* user_root = new QTreeWidgetItem(script_tree_);
  user_root->setText(0, tr("My Scripts"));
  user_root->setFlags(Qt::ItemIsEnabled);
  user_root->setData(0, kScriptFolderPathRole, user_dir);
  add_entries(user_root, scan.user, user_dir);
  script_tree_->expandAll();
  if (to_select != nullptr) {
    script_tree_->setCurrentItem(to_select);
  }
}

bool ScriptEditorDialog::editor_modified() const { return editor_->document()->isModified(); }

bool ScriptEditorDialog::confirm_discard_changes() {
  if (!editor_modified()) {
    return true;
  }
  const auto name =
      current_path_.isEmpty() ? tr("untitled.js") : QFileInfo(current_path_).fileName();
  const auto answer = show_warning_message(
      this, tr("Script Manager"), tr("Discard unsaved changes to %1?").arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
      QStringLiteral("scriptEditorDiscardMessageBox"));
  return answer == QMessageBox::Yes;
}

void ScriptEditorDialog::handle_tree_activated(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const auto path = item->data(0, kScriptPathRole).toString();
  if (path.isEmpty()) {
    return;  // folder / root rows
  }
  if (!confirm_discard_changes()) {
    return;
  }
  load_script(path);
}

void ScriptEditorDialog::show_tree_context_menu(const QPoint& position) {
  auto* item = script_tree_->itemAt(position);
  if (item == nullptr) {
    return;
  }
  const auto path = item->data(0, kScriptPathRole).toString();
  if (path.isEmpty()) {
    // Folder rows (including the Bundled / My Scripts roots) offer opening the
    // folder itself.
    const auto folder_path = item->data(0, kScriptFolderPathRole).toString();
    if (folder_path.isEmpty()) {
      return;
    }
    QMenu folder_menu(this);
    folder_menu.setObjectName(QStringLiteral("scriptEditorTreeMenu"));
    auto* open_folder_action = folder_menu.addAction(tr("Show in Folder"));
    if (folder_menu.exec(script_tree_->viewport()->mapToGlobal(position)) == open_folder_action) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(folder_path));
    }
    return;
  }
  const auto bundled_path = item->data(0, kScriptBundledPathRole).toString();
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("scriptEditorTreeMenu"));
  auto* run_action = menu.addAction(tr("Run"));
  auto* reveal_action = menu.addAction(tr("Show in Folder"));
  QAction* revert_action = nullptr;
  if (!bundled_path.isEmpty()) {
    menu.addSeparator();
    revert_action = menu.addAction(tr("Revert to Bundled"));
  }
  auto* chosen = menu.exec(script_tree_->viewport()->mapToGlobal(position));
  if (chosen == nullptr) {
    return;
  }
  if (chosen == run_action) {
    if (host_.run_active()) {
      append_console(2, tr("A script is already running: %1").arg(host_.active_run_name()));
      return;
    }
    console_->appendPlainText(QStringLiteral("--- %1 ---").arg(QFileInfo(path).fileName()));
    (void)host_.run_file(path);
  } else if (chosen == reveal_action) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
  } else if (chosen == revert_action) {
    revert_override_to_bundled(path, bundled_path);
  }
}

void ScriptEditorDialog::revert_override_to_bundled(const QString& user_copy_path,
                                                    const QString& bundled_path) {
  const auto answer = show_warning_message(
      this, tr("Script Manager"),
      tr("Delete your modified copy of %1 and restore the bundled script?")
          .arg(QFileInfo(bundled_path).fileName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
      QStringLiteral("scriptEditorRevertMessageBox"));
  if (answer != QMessageBox::Yes) {
    return;
  }
  if (!QFile::remove(user_copy_path)) {
    append_console(2, tr("Could not delete %1").arg(QDir::toNativeSeparators(user_copy_path)));
    return;
  }
  if (current_path_ == user_copy_path) {
    load_script(bundled_path);
  }
  refresh_script_tree(current_path_);
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
  update_file_label();
}

void ScriptEditorDialog::update_file_label() {
  auto name = current_path_.isEmpty() ? tr("untitled.js") : QFileInfo(current_path_).fileName();
  if (editor_modified()) {
    name += QStringLiteral(" *");
  }
  file_label_->setText(name);
}

void ScriptEditorDialog::run_current() {
  if (host_.run_active()) {
    return;
  }
  console_->appendPlainText(QStringLiteral("--- %1 ---").arg(file_label_->text()));
  ScriptEngineHost::RunOptions options;
  options.name =
      current_path_.isEmpty() ? tr("untitled.js") : QFileInfo(current_path_).fileName();
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
  if (current_path_.isEmpty()) {
    return save_script_as();
  }
  auto target = current_path_;
  const auto bundled_relative =
      relative_path_under(MainWindow::bundled_scripts_directory(), current_path_);
  if (!bundled_relative.isEmpty()) {
    // Bundled script: Save writes the user-folder shadow copy that overrides
    // it (the shipped file stays pristine, so app updates never clobber the
    // edit; Revert to Bundled deletes the copy).
    target = QDir(MainWindow::user_scripts_directory()).absoluteFilePath(bundled_relative);
    QDir().mkpath(QFileInfo(target).absolutePath());
  }
  QFile file(target);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    append_console(2, tr("Could not write %1").arg(QDir::toNativeSeparators(target)));
    return false;
  }
  file.write(editor_->toPlainText().toUtf8());
  file.close();
  editor_->document()->setModified(false);
  if (target != current_path_) {
    append_console(0, tr("Saved your copy to %1; it now runs instead of the bundled script "
                         "(right-click it for Revert to Bundled).")
                          .arg(QDir::toNativeSeparators(target)));
    set_current_path(target);
  } else {
    update_file_label();
  }
  refresh_script_tree(target);
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
  file.close();
  editor_->document()->setModified(false);
  set_current_path(path);
  refresh_script_tree(path);
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
  // The elapsed clock restarts whenever a run becomes active - including runs
  // started from the menu or CLI while this dialog is open (and a run already
  // active when the dialog constructs counts from now, the best we can see).
  if (running && !status_timer_->isActive()) {
    run_elapsed_.start();
    status_timer_->start();
  } else if (!running && status_timer_->isActive()) {
    status_timer_->stop();
  }
  run_spinner_->setVisible(running);
  update_status_text();
}

void ScriptEditorDialog::update_status_text() {
  if (!host_.run_active()) {
    status_label_->setText(tr("Ready"));
    status_label_->setToolTip(QString());
    return;
  }
  const auto seconds = run_elapsed_.isValid() ? run_elapsed_.elapsed() / 1000 : 0;
  const auto text =
      seconds < 60 ? tr("Running... %1s").arg(seconds)
                   : tr("Running... %1m %2s")
                         .arg(seconds / 60)
                         .arg(seconds % 60, 2, 10, QLatin1Char('0'));
  status_label_->setText(text);
  status_label_->setToolTip(host_.active_run_name());
}

}  // namespace patchy::ui
