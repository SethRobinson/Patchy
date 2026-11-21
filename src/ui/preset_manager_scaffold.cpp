#include "ui/preset_manager_scaffold.hpp"

#include "ui/dialog_utils.hpp"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace patchy::ui {

PresetManagerScaffold::PresetManagerScaffold(QDialog& dialog, const QString& object_name,
                                             const QString& title)
    : dialog_(dialog) {
  dialog_.setObjectName(object_name);
  dialog_.setWindowTitle(title);
  dialog_.setModal(true);
  main_layout_ = new QHBoxLayout(&dialog_);
}

void PresetManagerScaffold::add_tree(QWidget* tree, int minimum_width) {
  tree_ = tree;
  tree_->setMinimumWidth(minimum_width);
  main_layout_->addWidget(tree_, 1);
  right_ = new QVBoxLayout();
  main_layout_->addLayout(right_, 2);
}

void PresetManagerScaffold::add_tree(QWidget* tree, int minimum_width, QWidget* above) {
  tree_ = tree;
  tree_->setMinimumWidth(minimum_width);
  auto* left = new QVBoxLayout();
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(4);
  left->addWidget(above);
  left->addWidget(tree_, 1);
  main_layout_->addLayout(left, 1);
  right_ = new QVBoxLayout();
  main_layout_->addLayout(right_, 2);
}

QHBoxLayout* PresetManagerScaffold::button_row(std::initializer_list<QPushButton*> leading,
                                               std::initializer_list<QPushButton*> trailing) {
  auto* row = new QHBoxLayout();
  for (auto* button : leading) {
    row->addWidget(button);
  }
  row->addStretch(1);
  for (auto* button : trailing) {
    row->addWidget(button);
  }
  return row;
}

void PresetManagerScaffold::add_action_row(std::initializer_list<QPushButton*> leading,
                                           std::initializer_list<QPushButton*> trailing) {
  right_->addLayout(button_row(leading, trailing));
}

void PresetManagerScaffold::add_restore_button_left(QPushButton* button) {
  right_->addWidget(button, 0, Qt::AlignLeft);
}

QPushButton* PresetManagerScaffold::add_dialog_buttons(const QString& use_label,
                                                       const QString& use_object_name,
                                                       std::function<void()> use) {
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
  auto* use_button = buttons->addButton(use_label, QDialogButtonBox::AcceptRole);
  use_button->setObjectName(use_object_name);
  right_->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog_, std::move(use));
  return use_button;
}

void PresetManagerScaffold::connect_selection_changed(std::function<void()> refresh) {
  if (auto* tree = qobject_cast<QTreeWidget*>(tree_); tree != nullptr) {
    QObject::connect(tree, &QTreeWidget::itemSelectionChanged, &dialog_, std::move(refresh));
  }
}

void PresetManagerScaffold::add_delete_plumbing(QPushButton* delete_button,
                                                std::function<void()> delete_selected) {
  auto* delete_shortcut = new QShortcut(QKeySequence::Delete, tree_);
  delete_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  QObject::connect(delete_shortcut, &QShortcut::activated, &dialog_, delete_selected);
  QObject::connect(delete_button, &QPushButton::clicked, &dialog_, std::move(delete_selected));
}

std::function<void()> PresetManagerScaffold::single_selection_accept(
    std::function<QStringList()> selected_ids,
    std::function<bool(const QString&)> library_has_entry, QString& chosen_id) const {
  auto* dialog = &dialog_;
  return [dialog, selected_ids = std::move(selected_ids),
          library_has_entry = std::move(library_has_entry), &chosen_id] {
    const auto ids = selected_ids();
    if (ids.size() != 1 || !library_has_entry(ids.front())) {
      return;
    }
    chosen_id = ids.front();
    dialog->accept();
  };
}

PresetImportProgressDialog::PresetImportProgressDialog(
    QWidget* parent, const QString& object_name, const QString& title, const QString& reading_label,
    const QString& cancel_label, std::function<QString(int item, int total)> step_label,
    qint64 file_bytes)
    : dialog_(std::make_unique<QProgressDialog>(reading_label, cancel_label, 0, 0, parent)),
      step_label_(std::move(step_label)),
      file_bytes_(file_bytes) {
  dialog_->setObjectName(object_name);
  dialog_->setWindowTitle(title);
  dialog_->setWindowModality(Qt::WindowModal);
  // Shown by show_now() alone: Qt's own timer would need the event loop this import blocks.
  dialog_->setMinimumDuration(std::numeric_limits<int>::max());
  dialog_->setAutoClose(false);
  dialog_->setAutoReset(false);
  running_.start();
}

PresetImportProgressDialog::~PresetImportProgressDialog() {
  dialog_->close();
}

void PresetImportProgressDialog::show_now() {
  if (dialog_->isVisible()) {
    return;
  }
  remember_dialog_position(*dialog_);
  dialog_->show();
}

PresetImportStep PresetImportProgressDialog::step() {
  return [this](int done, int total) {
    if (dialog_->wasCanceled()) {
      return false;  // Cancel hid the dialog; do not bring it back for the last step
    }
    if (total > 0) {
      const auto label = step_label_(std::min(done + 1, total), total);
      if (dialog_->labelText() != label) {
        dialog_->setLabelText(label);
      }
      dialog_->setMaximum(total);
      dialog_->setValue(done);
    }
    if (file_bytes_ >= kShowAtOnceBytes || running_.elapsed() >= kShowAfterMs) {
      show_now();
    }
    if (dialog_->isVisible()) {
      // Window-modal, so the manager under it takes no input while its import runs.
      QApplication::processEvents(QEventLoop::AllEvents);
    }
    return !dialog_->wasCanceled();
  };
}

bool PresetImportProgressDialog::cancelled() const {
  return dialog_->wasCanceled();
}

}  // namespace patchy::ui
