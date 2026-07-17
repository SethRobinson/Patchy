#include "ui/paths_panel.hpp"

#include <QAction>
#include <QAbstractItemModel>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace patchy::ui {

PathsPanel::PathsPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("pathsPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(5);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("pathsList"));
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setDragEnabled(true);
  list_->setAcceptDrops(true);
  list_->setDropIndicatorShown(true);
  list_->setDragDropOverwriteMode(false);
  list_->setDefaultDropAction(Qt::MoveAction);
  list_->setDragDropMode(QAbstractItemView::InternalMove);
  list_->setIconSize(QSize(42, 30));
  list_->setSpacing(1);
  list_->viewport()->installEventFilter(this);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(list_, 1);

  auto* action_bar = new QWidget(this);
  action_bar->setObjectName(QStringLiteral("pathsActionBar"));
  auto* buttons = new QHBoxLayout(action_bar);
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(4);
  const auto add_button = [action_bar, buttons](const QString& object_name) {
    auto* button = new QToolButton(action_bar);
    button->setObjectName(object_name);
    button->setProperty("channelActionButton", true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(34, 30);
    button->setIconSize(QSize(20, 20));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    buttons->addWidget(button);
    return button;
  };
  auto* new_button = add_button(QStringLiteral("pathsNewButton"));
  auto* fill_button = add_button(QStringLiteral("pathsFillButton"));
  auto* stroke_button = add_button(QStringLiteral("pathsStrokeButton"));
  auto* selection_button = add_button(QStringLiteral("pathsMakeSelectionButton"));
  auto* from_selection_button = add_button(QStringLiteral("pathsFromSelectionButton"));
  buttons->addSpacing(4);
  auto* delete_button = add_button(QStringLiteral("pathsDeleteButton"));
  buttons->addStretch(1);
  layout->addWidget(action_bar);

  // The buttons adopt their QAction defaults in set_actions; stash pointers
  // via object names to avoid six extra members.
  connect(list_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current, QListWidgetItem*) { handle_current_item_changed(current); });
  // A real mouse click updates the CURRENT item first (currentItemChanged
  // fires while the item is not yet selected) and commits the selection
  // afterwards, so the action states must also track selectionChanged or a
  // click leaves Fill/Stroke/Make Selection/Delete stuck disabled.
  connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
    if (!updating_) {
      refresh_action_states();
    }
  });
  connect(list_, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem* item) { handle_item_double_clicked(item); });
  connect(list_, &QListWidget::itemChanged, this,
          [this](QListWidgetItem* item) { handle_item_changed(item); });
  connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
          [this] { QTimer::singleShot(0, this, [this] { handle_rows_moved(); }); });
  connect(list_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_context_menu(position); });
  Q_UNUSED(new_button);
  Q_UNUSED(fill_button);
  Q_UNUSED(stroke_button);
  Q_UNUSED(selection_button);
  Q_UNUSED(from_selection_button);
  Q_UNUSED(delete_button);
}

void PathsPanel::set_actions(QAction* new_path, QAction* fill_path, QAction* stroke_path,
                             QAction* make_selection, QAction* from_selection,
                             QAction* duplicate_path, QAction* delete_path) {
  new_path_action_ = new_path;
  fill_path_action_ = fill_path;
  stroke_path_action_ = stroke_path;
  make_selection_action_ = make_selection;
  from_selection_action_ = from_selection;
  duplicate_path_action_ = duplicate_path;
  delete_path_action_ = delete_path;
  const auto bind = [this](const QString& button_name, QAction* action) {
    auto* button = findChild<QToolButton*>(button_name);
    if (button != nullptr && action != nullptr) {
      button->setDefaultAction(action);
    }
  };
  bind(QStringLiteral("pathsNewButton"), new_path);
  bind(QStringLiteral("pathsFillButton"), fill_path);
  bind(QStringLiteral("pathsStrokeButton"), stroke_path);
  bind(QStringLiteral("pathsMakeSelectionButton"), make_selection);
  bind(QStringLiteral("pathsFromSelectionButton"), from_selection);
  bind(QStringLiteral("pathsDeleteButton"), delete_path);
  refresh_action_states();
}

void PathsPanel::set_rows(std::vector<Row> rows, std::optional<Row> selected) {
  updating_ = true;
  committed_rows_ = rows;
  const QSignalBlocker blocker(list_);
  list_->clear();
  QListWidgetItem* selected_item = nullptr;
  for (auto& row : rows) {
    auto* item = new QListWidgetItem(row.thumbnail, row.name, list_);
    item->setData(kKindRole, static_cast<int>(row.kind));
    item->setData(kPathIdRole, QVariant::fromValue<qulonglong>(row.id));
    auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (row.kind == RowKind::SavedPath) {
      flags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
      item->setToolTip(tr("Saved path. Double-click to rename; select to edit with the pen and "
                          "path tools.") + QLatin1Char('\n') +
                       tr("Ctrl-click to load the path as a selection; drag to reorder."));
    } else if (row.kind == RowKind::WorkPath) {
      QFont italic = item->font();
      italic.setItalic(true);
      item->setFont(italic);
      item->setToolTip(tr("The temporary work path. Double-click to save it as a named path."));
    } else {
      item->setToolTip(tr("The active layer's path (shape or vector mask)."));
    }
    item->setFlags(flags);
    const bool matches_selection =
        selected.has_value() && selected->kind == row.kind &&
        (row.kind == RowKind::LayerPath || selected->id == row.id);
    if (matches_selection) {
      selected_item = item;
    }
  }
  if (selected_item != nullptr) {
    list_->setCurrentItem(selected_item);
  } else {
    list_->setCurrentItem(nullptr);
  }
  updating_ = false;
  refresh_action_states();
}

void PathsPanel::set_document_available(bool available) {
  document_available_ = available;
  setEnabled(available);
  refresh_action_states();
}

void PathsPanel::set_target_callback(TargetCallback callback) {
  target_callback_ = std::move(callback);
}

void PathsPanel::set_deselect_callback(DeselectCallback callback) {
  deselect_callback_ = std::move(callback);
}

void PathsPanel::set_rename_callback(RenameCallback callback) {
  rename_callback_ = std::move(callback);
}

void PathsPanel::set_save_work_path_callback(SaveWorkPathCallback callback) {
  save_work_path_callback_ = std::move(callback);
}

void PathsPanel::set_load_selection_callback(LoadSelectionCallback callback) {
  load_selection_callback_ = std::move(callback);
}

void PathsPanel::set_reorder_callback(ReorderCallback callback) {
  reorder_callback_ = std::move(callback);
}

std::vector<DocumentPathId> PathsPanel::saved_path_order() const {
  std::vector<DocumentPathId> order;
  for (int row = 0; row < list_->count(); ++row) {
    const auto* item = list_->item(row);
    if (item != nullptr && static_cast<RowKind>(item->data(kKindRole).toInt()) == RowKind::SavedPath) {
      order.push_back(static_cast<DocumentPathId>(item->data(kPathIdRole).toULongLong()));
    }
  }
  return order;
}

void PathsPanel::handle_rows_moved() {
  if (updating_) {
    return;
  }
  // Only saved paths may move, and only among themselves: the transient layer
  // row stays first and the work path stays last. Revert any drop that broke
  // that frame.
  const bool expects_layer_row =
      !committed_rows_.empty() && committed_rows_.front().kind == RowKind::LayerPath;
  const bool expects_work_row =
      !committed_rows_.empty() && committed_rows_.back().kind == RowKind::WorkPath;
  bool frame_broken = list_->count() != static_cast<int>(committed_rows_.size());
  if (!frame_broken && expects_layer_row) {
    frame_broken =
        static_cast<RowKind>(list_->item(0)->data(kKindRole).toInt()) != RowKind::LayerPath;
  }
  if (!frame_broken && expects_work_row) {
    frame_broken = static_cast<RowKind>(list_->item(list_->count() - 1)->data(kKindRole).toInt()) !=
                   RowKind::WorkPath;
  }
  if (frame_broken) {
    set_rows(committed_rows_, selected_row());
    return;
  }
  if (reorder_callback_) {
    reorder_callback_(saved_path_order());
  }
}

void PathsPanel::begin_rename(DocumentPathId id) {
  for (int row = 0; row < list_->count(); ++row) {
    auto* item = list_->item(row);
    if (item != nullptr && static_cast<RowKind>(item->data(kKindRole).toInt()) == RowKind::SavedPath &&
        static_cast<DocumentPathId>(item->data(kPathIdRole).toULongLong()) == id) {
      list_->editItem(item);
      return;
    }
  }
}

PathsPanel::Row PathsPanel::row_from_item(const QListWidgetItem* item) const {
  Row row;
  if (item == nullptr) {
    return row;
  }
  row.kind = static_cast<RowKind>(item->data(kKindRole).toInt());
  row.id = static_cast<DocumentPathId>(item->data(kPathIdRole).toULongLong());
  row.name = item->text();
  return row;
}

std::optional<PathsPanel::Row> PathsPanel::selected_row() const {
  auto* item = list_->currentItem();
  if (item == nullptr || !item->isSelected()) {
    return std::nullopt;
  }
  return row_from_item(item);
}

void PathsPanel::handle_current_item_changed(QListWidgetItem* current) {
  if (updating_) {
    return;
  }
  refresh_action_states();
  if (current == nullptr) {
    if (deselect_callback_) {
      deselect_callback_();
    }
    return;
  }
  const auto row = row_from_item(current);
  if (target_callback_) {
    target_callback_(row.kind, row.id);
  }
}

void PathsPanel::handle_item_double_clicked(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const auto row = row_from_item(item);
  if (row.kind == RowKind::WorkPath) {
    if (save_work_path_callback_) {
      save_work_path_callback_();
    }
  } else if (row.kind == RowKind::SavedPath) {
    list_->editItem(item);
  }
}

void PathsPanel::handle_item_changed(QListWidgetItem* item) {
  if (updating_ || item == nullptr) {
    return;
  }
  const auto row = row_from_item(item);
  if (row.kind == RowKind::SavedPath && rename_callback_) {
    rename_callback_(row.id, item->text());
  }
}

void PathsPanel::show_context_menu(const QPoint& position) {
  QMenu menu(this);
  for (auto* action : {new_path_action_, duplicate_path_action_, fill_path_action_,
                       stroke_path_action_, make_selection_action_, from_selection_action_,
                       delete_path_action_}) {
    if (action != nullptr) {
      menu.addAction(action);
    }
  }
  if (!menu.isEmpty()) {
    menu.exec(list_->viewport()->mapToGlobal(position));
  }
}

void PathsPanel::refresh_action_states() {
  const auto row = selected_row();
  const bool has_path = row.has_value();
  const bool deletable = has_path && row->kind != RowKind::LayerPath;
  if (fill_path_action_ != nullptr) {
    fill_path_action_->setEnabled(document_available_ && has_path);
  }
  if (stroke_path_action_ != nullptr) {
    stroke_path_action_->setEnabled(document_available_ && has_path);
  }
  if (make_selection_action_ != nullptr) {
    make_selection_action_->setEnabled(document_available_ && has_path);
  }
  if (duplicate_path_action_ != nullptr) {
    duplicate_path_action_->setEnabled(document_available_ && deletable);
  }
  if (delete_path_action_ != nullptr) {
    delete_path_action_->setEnabled(document_available_ && deletable);
  }
  if (new_path_action_ != nullptr) {
    new_path_action_->setEnabled(document_available_);
  }
  if (from_selection_action_ != nullptr) {
    from_selection_action_->setEnabled(document_available_);
  }
}

void PathsPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
}

bool PathsPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == list_->viewport() && event->type() == QEvent::MouseButtonPress) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    ctrl_load_mouse_press_ = false;
    auto* item = list_->itemAt(mouse_event->pos());
    // Photoshop: Ctrl+click (Cmd on macOS) loads the row's path as a
    // selection without changing the panel targeting (the channel-panel
    // convention).
    const auto load_modifier =
        (mouse_event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;
    if (item != nullptr && mouse_event->button() == Qt::LeftButton && load_modifier &&
        document_available_ && load_selection_callback_) {
      const auto row = row_from_item(item);
      ctrl_load_mouse_press_ = true;
      load_selection_callback_(row.kind, row.id);
      event->accept();
      return true;
    }
    if (item == nullptr) {
      // Photoshop: clicking the empty area deselects the path (hiding its
      // canvas overlay).
      list_->setCurrentItem(nullptr);
      if (auto* selection_model = list_->selectionModel(); selection_model != nullptr) {
        selection_model->clear();
      }
    }
  } else if (watched == list_->viewport() && event->type() == QEvent::MouseButtonRelease &&
             ctrl_load_mouse_press_) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (mouse_event->button() == Qt::LeftButton) {
      ctrl_load_mouse_press_ = false;
      event->accept();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace patchy::ui
