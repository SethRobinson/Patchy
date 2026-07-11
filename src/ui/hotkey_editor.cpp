#include "ui/hotkey_editor.hpp"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <functional>
#include <utility>

namespace patchy::ui {

namespace {

bool is_modifier_only_key(int key) {
  return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
         key == Qt::Key_AltGr || key == Qt::Key_CapsLock || key == Qt::Key_NumLock || key == Qt::Key_ScrollLock;
}

// Keys the canvas (or the dialog itself) consumes positionally; binding them
// app-wide would break core interactions, so the capture field refuses them.
bool is_reserved_binding_key(int key, Qt::KeyboardModifiers modifiers) {
  if ((modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0) {
    return key == Qt::Key_Escape;
  }
  switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
      return true;
    default:
      break;
  }
  if ((modifiers & Qt::ShiftModifier) == 0 && key >= Qt::Key_0 && key <= Qt::Key_9) {
    return true;
  }
  return false;
}

// Menu texts carry an "opens a dialog" ellipsis that is noise in a command
// list ("used by Levels...." reads badly in the conflict banner).
QString strip_menu_ellipsis(QString label) {
  while (label.endsWith(QStringLiteral("..."))) {
    label.chop(3);
  }
  while (label.endsWith(QChar(0x2026))) {
    label.chop(1);
  }
  return label.trimmed();
}

}  // namespace

// Records exactly one chord. Esc cancels, plain Backspace clears the binding,
// reserved keys show a transient refusal instead of committing.
class HotkeyCaptureEdit final : public QLineEdit {
public:
  HotkeyCaptureEdit(QString reserved_message, QWidget* parent) : QLineEdit(parent),
                                                                 reserved_message_(std::move(reserved_message)) {
    setObjectName(QStringLiteral("hotkeyCaptureEdit"));
    setReadOnly(true);
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setMinimumWidth(190);
    setFixedHeight(24);
  }

  std::function<void(QKeySequence)> on_commit;
  std::function<void()> on_cancel;
  std::function<void()> on_clear;

protected:
  bool event(QEvent* event) override {
    // Keep every chord out of the shortcut system while recording.
    if (event->type() == QEvent::ShortcutOverride) {
      event->accept();
      return true;
    }
    return QLineEdit::event(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    event->accept();
    if (finished_) {
      return;
    }
    const int key = event->key();
    if (key == Qt::Key_unknown || is_modifier_only_key(key)) {
      return;
    }
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Escape && modifiers == Qt::NoModifier) {
      finish(on_cancel);
      return;
    }
    if (key == Qt::Key_Backspace && modifiers == Qt::NoModifier) {
      finish(on_clear);
      return;
    }
    if (is_reserved_binding_key(key, modifiers)) {
      setText(reserved_message_);
      QTimer::singleShot(1200, this, [this] {
        if (!finished_) {
          setText(QString());
        }
      });
      return;
    }
    const QKeySequence sequence(QKeyCombination(modifiers, static_cast<Qt::Key>(key)));
    if (sequence.isEmpty()) {
      return;
    }
    if (on_commit) {
      finished_ = true;
      on_commit(sequence);
    }
  }

  void keyReleaseEvent(QKeyEvent* event) override { event->accept(); }

  void focusOutEvent(QFocusEvent* event) override {
    QLineEdit::focusOutEvent(event);
    finish(on_cancel);
  }

private:
  void finish(const std::function<void()>& callback) {
    if (finished_) {
      return;
    }
    finished_ = true;
    if (callback) {
      callback();
    }
  }

  QString reserved_message_;
  bool finished_{false};
};

HotkeyEditorPanel::HotkeyEditorPanel(HotkeyRegistry& registry, QMenuBar* menu_bar, QWidget* parent)
    : QWidget(parent), registry_(registry), staged_(registry.overrides()) {
  setObjectName(QStringLiteral("hotkeyEditorPanel"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* top_row = new QWidget(this);
  auto* top_layout = new QHBoxLayout(top_row);
  top_layout->setContentsMargins(0, 0, 0, 0);
  top_layout->setSpacing(8);
  search_edit_ = new QLineEdit(top_row);
  search_edit_->setObjectName(QStringLiteral("hotkeySearchEdit"));
  search_edit_->setPlaceholderText(tr("Search commands or shortcuts..."));
  search_edit_->setClearButtonEnabled(true);
  top_layout->addWidget(search_edit_, 1);
  auto* reset_all_button = new QPushButton(tr("Reset All"), top_row);
  reset_all_button->setObjectName(QStringLiteral("hotkeyResetAllButton"));
  reset_all_button->setToolTip(tr("Restore every hotkey to its default"));
  top_layout->addWidget(reset_all_button);
  layout->addWidget(top_row);

  build_rows(menu_bar);

  auto* hint = new QLabel(
      tr("Click a shortcut to change it. Backspace clears it. Esc cancels. Changes apply when you click OK."), this);
  hint->setProperty("hotkeyDim", true);
  hint->setWordWrap(true);
  layout->addWidget(hint);

  connect(search_edit_, &QLineEdit::textChanged, this, [this] { filter_rows(); });
  connect(reset_all_button, &QPushButton::clicked, this, [this] {
    QTimer::singleShot(0, this, [this] {
      staged_.clear();
      refresh_rows();
    });
  });

  setStyleSheet(QStringLiteral(R"(
    QWidget#hotkeyEditorPanel QLabel[hotkeyCategoryHeader="true"] {
      color: #a8b4be;
      font-size: 11px;
      font-weight: 600;
    }
    QWidget#hotkeyEditorPanel QLabel[hotkeyCategoryGap="true"] {
      margin-bottom: 9px;
    }
    QWidget#hotkeyEditorPanel QWidget[hotkeyRow="true"] {
      border-bottom: 1px solid #383838;
    }
    QWidget#hotkeyEditorPanel QLabel[hotkeyDim="true"] {
      color: #9a9a9a;
      font-size: 11px;
    }
    QWidget#hotkeyEditorPanel QLabel[hotkeyNote="true"] {
      color: #e8b04a;
      font-size: 11px;
    }
    QWidget#hotkeyEditorPanel QLabel[hotkeyFixedChip="true"] {
      background: #343434;
      border: 1px solid #4a4a4a;
      border-radius: 3px;
      color: #b5b5b5;
      font-size: 12px;
      padding: 2px 9px;
    }
    QWidget#hotkeyEditorPanel QPushButton[hotkeyChip="true"] {
      background: #3a3a3a;
      border: 1px solid #171717;
      border-top-color: #5d5d5d;
      border-radius: 3px;
      color: #f0f0f0;
      font-size: 12px;
      min-height: 18px;
      padding: 2px 10px;
    }
    QWidget#hotkeyEditorPanel QPushButton[hotkeyChip="true"]:hover {
      background: #404040;
      border-color: #80bfff;
    }
    QWidget#hotkeyEditorPanel QPushButton[hotkeyChipUnbound="true"] {
      background: transparent;
      border: 1px dashed #5a5a5a;
      color: #9a9a9a;
    }
    QWidget#hotkeyEditorPanel QToolButton[hotkeyReset="true"] {
      background: transparent;
      border: none;
      color: #9ab8d6;
      font-size: 14px;
    }
    QWidget#hotkeyEditorPanel QToolButton[hotkeyReset="true"]:hover {
      color: #cfe5ff;
    }
    QLineEdit#hotkeyCaptureEdit {
      background: #252525;
      border: 1px solid #2f75bd;
      border-radius: 3px;
      color: #9fc1e4;
      font-size: 12px;
      padding: 1px 8px;
    }
    QFrame#hotkeyConflictBanner {
      background: #322d20;
      border: 1px solid #6a5a2c;
      border-radius: 3px;
    }
    QFrame#hotkeyConflictBanner QLabel {
      background: transparent;
      border: none;
      color: #e8c87a;
      font-size: 11px;
    }
    QFrame#hotkeyConflictBanner QPushButton {
      background: #3a3a3a;
      border: 1px solid #626262;
      border-radius: 3px;
      color: #f0f0f0;
      font-size: 11px;
      padding: 2px 10px;
    }
    QFrame#hotkeyConflictBanner QPushButton:hover {
      background: #404040;
      border-color: #80bfff;
    }
  )"));

  refresh_rows();
}

void HotkeyEditorPanel::commit() {
  registry_.apply_overrides(staged_);
}

QFrame* HotkeyEditorPanel::add_category_panel(const QString& title, bool dimmed) {
  auto* panel = new QFrame(this);
  panel->setProperty("preferencesPanel", true);
  auto* panel_layout = new QVBoxLayout(panel);
  panel_layout->setContentsMargins(12, 8, 12, 6);
  panel_layout->setSpacing(0);
  auto* header = new QLabel(title, panel);
  header->setProperty(dimmed ? "hotkeyDim" : "hotkeyCategoryHeader", true);
  // Breathing room between the section title and its first command row. Must be
  // stylesheet margin, not widget contentsMargins: stylesheet polish overwrites a
  // styled label's contents margins with the rule's box values.
  header->setProperty("hotkeyCategoryGap", true);
  panel_layout->addWidget(header);
  layout()->addWidget(panel);
  panel_titles_.insert(panel, title);
  return panel;
}

HotkeyEditorPanel::Row& HotkeyEditorPanel::add_command_row(QFrame* panel, const HotkeyCommand* command,
                                                           const QString& label) {
  auto* container = new QWidget(panel);
  container->setObjectName(QStringLiteral("hotkeyRow.") + command->id);
  container->setProperty("hotkeyRow", true);
  container->setAttribute(Qt::WA_StyledBackground, true);
  auto* grid = new QGridLayout(container);
  // The 8px left inset keeps command names off the row's left edge (the chips
  // carry their own padding on the right).
  grid->setContentsMargins(8, 5, 0, 5);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(3);

  auto* name_label = new QLabel(container);
  name_label->setTextFormat(Qt::RichText);
  grid->addWidget(name_label, 0, 0);
  grid->setColumnStretch(0, 1);

  auto* chips_host = new QWidget(container);
  auto* chips_layout = new QHBoxLayout(chips_host);
  chips_layout->setContentsMargins(0, 0, 0, 0);
  chips_layout->setSpacing(6);
  grid->addWidget(chips_host, 0, 1, Qt::AlignRight | Qt::AlignVCenter);

  auto* reset_button = new QToolButton(container);
  reset_button->setObjectName(QStringLiteral("hotkeyRowReset.") + command->id);
  reset_button->setProperty("hotkeyReset", true);
  reset_button->setText(QStringLiteral("↺"));
  reset_button->setCursor(Qt::PointingHandCursor);
  grid->addWidget(reset_button, 0, 2);

  auto* note_label = new QLabel(container);
  note_label->setObjectName(QStringLiteral("hotkeyRowNote.") + command->id);
  note_label->setProperty("hotkeyNote", true);
  note_label->setWordWrap(true);
  note_label->hide();
  grid->addWidget(note_label, 1, 0, 1, 3);

  qobject_cast<QVBoxLayout*>(panel->layout())->addWidget(container);

  const auto id = command->id;
  connect(reset_button, &QToolButton::clicked, this, [this, id] {
    QTimer::singleShot(0, this, [this, id] {
      staged_.remove(id);
      refresh_rows();
    });
  });

  rows_.push_back({command, container, name_label, chips_host, chips_layout, reset_button, note_label, panel, label,
                   QString()});
  return rows_.back();
}

void HotkeyEditorPanel::add_built_in_row(QFrame* panel, const QString& keys, const QString& description) {
  auto* container = new QWidget(panel);
  container->setProperty("hotkeyRow", true);
  container->setAttribute(Qt::WA_StyledBackground, true);
  auto* row_layout = new QHBoxLayout(container);
  row_layout->setContentsMargins(8, 5, 0, 5);
  row_layout->setSpacing(8);
  auto* description_label = new QLabel(description, container);
  description_label->setProperty("hotkeyDim", true);
  row_layout->addWidget(description_label, 1);
  auto* keys_label = new QLabel(keys, container);
  keys_label->setProperty("hotkeyFixedChip", true);
  row_layout->addWidget(keys_label, 0, Qt::AlignRight);
  qobject_cast<QVBoxLayout*>(panel->layout())->addWidget(container);

  rows_.push_back({nullptr, container, description_label, nullptr, nullptr, nullptr, nullptr, panel, description,
                   (description + QLatin1Char(' ') + keys).toLower()});
}

void HotkeyEditorPanel::build_rows(QMenuBar* menu_bar) {
  QHash<QAction*, const HotkeyCommand*> command_by_action;
  for (const auto& command : registry_.commands()) {
    if (command.action != nullptr) {
      command_by_action.insert(command.action.data(), &command);
    }
  }

  struct PendingRow {
    const HotkeyCommand* command;
    QString label;
    QString category;
  };
  std::vector<PendingRow> pending;
  QSet<QString> added;

  const QString path_separator = QStringLiteral(" › ");
  std::function<void(QMenu*, const QString&, const QString&)> walk = [&](QMenu* menu, const QString& top,
                                                                         const QString& path) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        const auto submenu_label = clean_action_text(action);
        walk(submenu, top, path.isEmpty() ? submenu_label : path + path_separator + submenu_label);
        continue;
      }
      const auto* command = command_by_action.value(action);
      if (command == nullptr || added.contains(command->id)) {
        continue;
      }
      added.insert(command->id);
      auto label = strip_menu_ellipsis(clean_action_text(action));
      if (!path.isEmpty()) {
        label = path + path_separator + label;
      }
      pending.push_back({command, label, top});
    }
  };
  if (menu_bar != nullptr) {
    for (auto* top_action : menu_bar->actions()) {
      if (auto* menu = top_action->menu(); menu != nullptr) {
        walk(menu, clean_action_text(top_action), QString());
      }
    }
  }
  for (const auto& command : registry_.commands()) {
    if (added.contains(command.id) || command.action == nullptr) {
      continue;
    }
    added.insert(command.id);
    pending.push_back({&command, strip_menu_ellipsis(clean_action_text(command.action.data())),
                       category_display_name(command.category)});
  }

  QFrame* current_panel = nullptr;
  QString current_category;
  for (const auto& row : pending) {
    if (current_panel == nullptr || row.category != current_category) {
      current_panel = add_category_panel(row.category, false);
      current_category = row.category;
    }
    add_command_row(current_panel, row.command, row.label);
  }

  auto* built_in_panel = add_category_panel(tr("Built-in canvas keys (not editable)"), true);
  const std::array<std::pair<QString, QString>, 5> built_ins{{
      {QStringLiteral("Space"), tr("Pan canvas (hold); while dragging, moves the selection or shape")},
      {QStringLiteral("0-9"), tr("Set tool opacity (10%-100%)")},
      {QStringLiteral("Arrows / Shift+Arrows"), tr("Nudge layer by 1 px / 10 px")},
      {QStringLiteral("Enter / Esc"), tr("Commit / cancel a transform or text edit")},
      {QStringLiteral("Delete / Backspace"), tr("Delete selected guides")},
  }};
  for (const auto& [keys, description] : built_ins) {
    add_built_in_row(built_in_panel, keys, description);
  }
}

void HotkeyEditorPanel::refresh_rows() {
  end_capture_widget();
  remove_conflict_banner();
  resolution_ = resolve_hotkey_assignments(registry_.commands(), staged_);

  QHash<QString, QStringList> suppression_notes;
  for (const auto& suppression : resolution_.suppressions) {
    suppression_notes[suppression.id] << tr("%1 is assigned to %2.")
                                             .arg(suppression.sequence.toString(QKeySequence::NativeText),
                                                  display_label(suppression.winner_id));
  }

  for (auto& row : rows_) {
    if (row.command == nullptr) {
      continue;
    }
    const auto& id = row.command->id;
    const bool modified = staged_.contains(id);
    const auto escaped_label = row.label.toHtmlEscaped();
    row.name_label->setText(modified
                                ? QStringLiteral("<span style=\"color:#6bb3ff;\">&#9679;</span> ") + escaped_label
                                : escaped_label);
    row.reset_button->setVisible(modified);
    const auto default_text = hotkey_shortcuts_to_storage(row.command->default_shortcuts)
                                  .replace(QLatin1Char('\n'), QStringLiteral(", "));
    row.reset_button->setToolTip(default_text.isEmpty() ? tr("Reset to default (no shortcut)")
                                                        : tr("Reset to default (%1)").arg(default_text));

    while (auto* item = row.chips_layout->takeAt(0)) {
      if (auto* widget = item->widget(); widget != nullptr) {
        widget->hide();
        widget->setParent(nullptr);
        widget->deleteLater();
      }
      delete item;
    }

    const auto effective = resolution_.effective.value(id);
    QStringList chip_texts;
    for (int slot = 0; slot < effective.size(); ++slot) {
      const auto& sequence = effective.at(slot);
      const auto native = sequence.toString(QKeySequence::NativeText);
      chip_texts << native << sequence.toString(QKeySequence::PortableText);
      auto* chip = new QPushButton(native, row.chips_host);
      chip->setObjectName(QStringLiteral("hotkeyChip.") + id + QLatin1Char('.') + QString::number(slot));
      chip->setProperty("hotkeyChip", true);
      chip->setCursor(Qt::PointingHandCursor);
      chip->setFocusPolicy(Qt::NoFocus);
      chip->setToolTip(tr("Click to change this shortcut"));
      row.chips_layout->addWidget(chip);
      connect(chip, &QPushButton::clicked, this, [this, id, slot] { begin_capture(id, slot); });
    }
    if (effective.isEmpty()) {
      auto* assign_chip = new QPushButton(tr("Click to assign"), row.chips_host);
      assign_chip->setObjectName(QStringLiteral("hotkeyAssignChip.") + id);
      assign_chip->setProperty("hotkeyChip", true);
      assign_chip->setProperty("hotkeyChipUnbound", true);
      assign_chip->setCursor(Qt::PointingHandCursor);
      assign_chip->setFocusPolicy(Qt::NoFocus);
      row.chips_layout->addWidget(assign_chip);
      connect(assign_chip, &QPushButton::clicked, this, [this, id] { begin_capture(id, -1); });
    }

    const auto notes = suppression_notes.value(id);
    row.note_label->setText(notes.join(QLatin1Char(' ')));
    row.note_label->setVisible(!notes.isEmpty());

    row.search_haystack = (row.label + QLatin1Char(' ') + panel_titles_.value(row.panel) + QLatin1Char(' ') +
                           chip_texts.join(QLatin1Char(' ')))
                              .toLower();
  }

  filter_rows();
}

void HotkeyEditorPanel::filter_rows() {
  const auto needle = search_edit_ != nullptr ? search_edit_->text().trimmed().toLower() : QString();
  QHash<QFrame*, bool> panel_visible;
  for (auto& row : rows_) {
    const bool visible = needle.isEmpty() || row.search_haystack.contains(needle);
    row.container->setVisible(visible);
    panel_visible[row.panel] = panel_visible.value(row.panel, false) || visible;
  }
  for (auto it = panel_visible.constBegin(); it != panel_visible.constEnd(); ++it) {
    it.key()->setVisible(it.value());
  }
}

void HotkeyEditorPanel::begin_capture(const QString& id, int slot) {
  end_capture_widget();
  remove_conflict_banner();
  auto* row = find_row(id);
  if (row == nullptr) {
    return;
  }
  for (int i = 0; i < row->chips_layout->count(); ++i) {
    if (auto* widget = row->chips_layout->itemAt(i)->widget(); widget != nullptr) {
      widget->hide();
    }
  }
  capture_ = PendingCapture{id, slot};
  capture_edit_ = new HotkeyCaptureEdit(tr("Reserved for canvas and dialog use"), row->chips_host);
  capture_edit_->setPlaceholderText(tr("Press a shortcut..."));
  capture_edit_->on_commit = [this](QKeySequence sequence) { handle_capture_commit(sequence); };
  capture_edit_->on_cancel = [this] { handle_capture_cancel(); };
  capture_edit_->on_clear = [this] { handle_capture_clear(); };
  row->chips_layout->addWidget(capture_edit_);
  capture_edit_->show();
  capture_edit_->setFocus(Qt::MouseFocusReason);
}

void HotkeyEditorPanel::end_capture_widget() {
  if (capture_) {
    if (auto* row = find_row(capture_->id); row != nullptr && row->chips_layout != nullptr) {
      for (int i = 0; i < row->chips_layout->count(); ++i) {
        if (auto* widget = row->chips_layout->itemAt(i)->widget(); widget != nullptr && widget != capture_edit_) {
          widget->show();
        }
      }
    }
  }
  capture_.reset();
  if (capture_edit_ != nullptr) {
    capture_edit_->hide();
    capture_edit_->setParent(nullptr);
    capture_edit_->deleteLater();
    capture_edit_ = nullptr;
  }
}

void HotkeyEditorPanel::handle_capture_commit(QKeySequence sequence) {
  QTimer::singleShot(0, this, [this, sequence] {
    if (!capture_) {
      return;
    }
    const auto pending = *capture_;
    end_capture_widget();
    QString owner_id;
    for (auto it = resolution_.effective.constBegin(); it != resolution_.effective.constEnd(); ++it) {
      if (it.key() != pending.id && it.value().contains(sequence)) {
        owner_id = it.key();
        break;
      }
    }
    if (owner_id.isEmpty()) {
      assign_sequence(pending.id, pending.slot, sequence);
      refresh_rows();
      return;
    }
    if (auto* row = find_row(pending.id); row != nullptr) {
      show_conflict_banner(*row, pending.id, pending.slot, sequence, owner_id);
    }
  });
}

void HotkeyEditorPanel::handle_capture_cancel() {
  QTimer::singleShot(0, this, [this] {
    if (!capture_) {
      return;
    }
    end_capture_widget();
  });
}

void HotkeyEditorPanel::handle_capture_clear() {
  QTimer::singleShot(0, this, [this] {
    if (!capture_) {
      return;
    }
    const auto pending = *capture_;
    end_capture_widget();
    auto shortcuts = resolution_.effective.value(pending.id);
    if (pending.slot >= 0 && pending.slot < shortcuts.size()) {
      shortcuts.removeAt(pending.slot);
    }
    stage_override(pending.id, shortcuts);
    refresh_rows();
  });
}

void HotkeyEditorPanel::assign_sequence(const QString& id, int slot, const QKeySequence& sequence) {
  const auto current = resolution_.effective.value(id);
  QList<QKeySequence> result;
  const int replace_at = (slot >= 0 && slot < current.size()) ? slot : current.size();
  for (int i = 0; i < current.size(); ++i) {
    if (i == replace_at) {
      result << sequence;
    } else if (current.at(i) != sequence) {
      result << current.at(i);
    }
  }
  if (replace_at == current.size()) {
    result << sequence;
  }
  stage_override(id, result);
}

void HotkeyEditorPanel::show_conflict_banner(Row& row, const QString& id, int slot, const QKeySequence& sequence,
                                             const QString& owner_id) {
  remove_conflict_banner();
  conflict_banner_ = new QFrame(row.container);
  conflict_banner_->setObjectName(QStringLiteral("hotkeyConflictBanner"));
  auto* banner_layout = new QHBoxLayout(conflict_banner_);
  banner_layout->setContentsMargins(8, 5, 8, 5);
  banner_layout->setSpacing(8);
  auto* message = new QLabel(tr("%1 is already used by %2.")
                                 .arg(sequence.toString(QKeySequence::NativeText), display_label(owner_id)),
                             conflict_banner_);
  message->setWordWrap(true);
  banner_layout->addWidget(message, 1);
  auto* assign_button = new QPushButton(tr("Use here instead"), conflict_banner_);
  assign_button->setObjectName(QStringLiteral("hotkeyConflictAssignButton"));
  banner_layout->addWidget(assign_button);
  auto* cancel_button = new QPushButton(tr("Cancel"), conflict_banner_);
  cancel_button->setObjectName(QStringLiteral("hotkeyConflictCancelButton"));
  banner_layout->addWidget(cancel_button);
  if (auto* grid = qobject_cast<QGridLayout*>(row.container->layout()); grid != nullptr) {
    grid->addWidget(conflict_banner_, 2, 0, 1, 3);
  }

  connect(assign_button, &QPushButton::clicked, this, [this, id, slot, sequence, owner_id] {
    QTimer::singleShot(0, this, [this, id, slot, sequence, owner_id] {
      if (auto it = staged_.find(owner_id); it != staged_.end()) {
        it->removeAll(sequence);
        stage_override(owner_id, *it);
      }
      assign_sequence(id, slot, sequence);
      refresh_rows();
    });
  });
  connect(cancel_button, &QPushButton::clicked, this, [this] {
    QTimer::singleShot(0, this, [this] { remove_conflict_banner(); });
  });
}

void HotkeyEditorPanel::remove_conflict_banner() {
  if (conflict_banner_ != nullptr) {
    conflict_banner_->hide();
    conflict_banner_->setParent(nullptr);
    conflict_banner_->deleteLater();
    conflict_banner_ = nullptr;
  }
}

void HotkeyEditorPanel::stage_override(const QString& id, QList<QKeySequence> shortcuts) {
  const auto* command = registry_.find_command(id);
  if (command != nullptr && hotkey_shortcut_lists_equivalent(shortcuts, command->default_shortcuts)) {
    staged_.remove(id);
  } else {
    staged_.insert(id, std::move(shortcuts));
  }
}

HotkeyEditorPanel::Row* HotkeyEditorPanel::find_row(const QString& id) {
  for (auto& row : rows_) {
    if (row.command != nullptr && row.command->id == id) {
      return &row;
    }
  }
  return nullptr;
}

QString HotkeyEditorPanel::display_label(const QString& id) const {
  for (const auto& row : rows_) {
    if (row.command != nullptr && row.command->id == id) {
      return row.label;
    }
  }
  return id;
}

QString HotkeyEditorPanel::category_display_name(const QString& category_key) const {
  if (category_key == QStringLiteral("tools")) {
    return tr("Tools");
  }
  if (category_key == QStringLiteral("color")) {
    return tr("Color");
  }
  if (category_key == QStringLiteral("brush")) {
    return tr("Brush");
  }
  if (category_key == QStringLiteral("channels")) {
    return tr("Channels");
  }
  return tr("Other");
}

}  // namespace patchy::ui
