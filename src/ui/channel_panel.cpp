#include "ui/channel_panel.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

ChannelPanel::ChannelPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("channelPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(5);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("channelList"));
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
  action_bar->setObjectName(QStringLiteral("channelActionBar"));
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
  create_button_ = add_button(QStringLiteral("channelNewButton"));
  save_selection_button_ = add_button(QStringLiteral("channelSaveSelectionButton"));
  load_selection_button_ = add_button(QStringLiteral("channelLoadSelectionButton"));
  buttons->addSpacing(4);
  rename_button_ = add_button(QStringLiteral("channelRenameButton"));
  invert_button_ = add_button(QStringLiteral("channelInvertButton"));
  remove_button_ = add_button(QStringLiteral("channelDeleteButton"));
  buttons->addStretch(1);
  layout->addWidget(action_bar);

  connect(list_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current, QListWidgetItem*) { handle_current_item_changed(current); });
  connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) { handle_item_changed(item); });
  connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
          [this] { QTimer::singleShot(0, this, [this] { handle_rows_moved(); }); });
  connect(list_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_context_menu(position); });

  retranslate_ui();
  refresh_action_states();
}

void ChannelPanel::set_rows(std::vector<Row> rows, std::optional<Row> selected) {
  updating_ = true;
  committed_rows_ = rows;
  fixed_spot_positions_.clear();
  std::size_t saved_index = 0;
  for (const auto& row : rows) {
    if (row.kind == RowKind::Alpha || row.kind == RowKind::Spot) {
      if (row.kind == RowKind::Spot) {
        fixed_spot_positions_.emplace_back(saved_index, row.id);
      }
      ++saved_index;
    }
  }
  const QSignalBlocker blocker(list_);
  list_->clear();
  QListWidgetItem* selected_item = nullptr;
  for (auto& row : rows) {
    auto* item = new QListWidgetItem(row.thumbnail, row.name, list_);
    item->setData(kKindRole, static_cast<int>(row.kind));
    item->setData(kChannelIdRole, QVariant::fromValue<qulonglong>(row.id));
    auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (row.kind == RowKind::Alpha) {
      flags |= Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable;
      item->setCheckState(row.overlay ? Qt::Checked : Qt::Unchecked);
      item->setToolTip(tr("Select for a grayscale view. Check to show a colored overlay.") + QLatin1Char('\n') +
                       tr("Ctrl-click to load this channel as a selection."));
    } else if (row.kind == RowKind::Spot) {
      flags |= Qt::ItemIsUserCheckable;
      item->setCheckState(row.overlay ? Qt::Checked : Qt::Unchecked);
      item->setToolTip(tr("Spot channels can be previewed but not edited.") + QLatin1Char('\n') +
                       tr("Ctrl-click to load this channel as a selection."));
    } else if (row.kind == RowKind::QuickMask) {
      item->setToolTip(tr("Temporary selection mask. White selects, black masks, and gray creates partial selection."));
    } else {
      const auto preview_tip = row.kind == RowKind::Composite
                                   ? tr("Show the normal composite image.")
                                   : tr("Preview this component as grayscale. Component channels are read-only.");
      item->setToolTip(preview_tip + QLatin1Char('\n') +
                       tr("Ctrl-click to load this channel as a selection."));
    }
    item->setFlags(flags);
    if (selected.has_value() && selected->kind == row.kind && selected->id == row.id) {
      selected_item = item;
    }
  }
  if (selected_item == nullptr && list_->count() > 0) {
    selected_item = list_->item(0);
  }
  list_->setCurrentItem(selected_item);
  updating_ = false;
  refresh_action_states();
}

void ChannelPanel::set_document_available(bool available) {
  document_available_ = available;
  if (!available) {
    channel_creation_available_ = false;
  }
  list_->setEnabled(available);
  refresh_action_states();
}

void ChannelPanel::set_channel_creation_available(bool available) {
  channel_creation_available_ = available;
  refresh_action_states();
}

void ChannelPanel::set_actions(QAction* create, QAction* save_selection, QAction* load_selection,
                               QAction* rename, QAction* invert, QAction* remove) {
  const auto bind_action = [](QToolButton* button, QAction* action) {
    button->setDefaultAction(action);
    button->setAccessibleName(action->text());
    QObject::connect(action, &QAction::changed, button,
                     [button, action] { button->setAccessibleName(action->text()); });
  };
  bind_action(create_button_, create);
  bind_action(save_selection_button_, save_selection);
  bind_action(load_selection_button_, load_selection);
  bind_action(rename_button_, rename);
  bind_action(invert_button_, invert);
  bind_action(remove_button_, remove);
  retranslate_ui();
  refresh_action_states();
}

void ChannelPanel::set_target_callback(TargetCallback callback) {
  target_callback_ = std::move(callback);
}

void ChannelPanel::set_load_selection_callback(LoadSelectionCallback callback) {
  load_selection_callback_ = std::move(callback);
}

void ChannelPanel::set_reorder_callback(ReorderCallback callback) {
  reorder_callback_ = std::move(callback);
}

std::optional<ChannelPanel::Row> ChannelPanel::selected_row() const {
  const auto* item = list_->currentItem();
  if (item == nullptr) {
    return std::nullopt;
  }
  return row_from_item(item);
}

std::vector<ChannelId> ChannelPanel::saved_channel_order() const {
  std::vector<ChannelId> result;
  for (int row = 0; row < list_->count(); ++row) {
    const auto* item = list_->item(row);
    const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
    if (kind == RowKind::Alpha || kind == RowKind::Spot) {
      result.push_back(static_cast<ChannelId>(item->data(kChannelIdRole).toULongLong()));
    }
  }
  return result;
}

void ChannelPanel::changeEvent(QEvent* event) {
  if (event != nullptr && event->type() == QEvent::LanguageChange) {
    retranslate_ui();
  }
  QWidget::changeEvent(event);
}

bool ChannelPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == list_->viewport() && event != nullptr) {
    if (event->type() == QEvent::MouseButtonPress) {
      ctrl_load_mouse_press_ = false;
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      const auto load_modifier =
          (mouse_event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;
      if (mouse_event->button() == Qt::LeftButton && load_modifier && document_available_ &&
          load_selection_callback_) {
        if (auto* item = list_->itemAt(mouse_event->position().toPoint()); item != nullptr) {
          const auto row = row_from_item(item);
          ctrl_load_mouse_press_ = true;
          load_selection_callback_(row.kind, row.id);
          event->accept();
          return true;
        }
      }
    } else if (event->type() == QEvent::MouseButtonRelease && ctrl_load_mouse_press_) {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() == Qt::LeftButton) {
        ctrl_load_mouse_press_ = false;
        event->accept();
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

ChannelPanel::Row ChannelPanel::row_from_item(const QListWidgetItem* item) const {
  Row row;
  if (item == nullptr) {
    return row;
  }
  row.kind = static_cast<RowKind>(item->data(kKindRole).toInt());
  row.id = static_cast<ChannelId>(item->data(kChannelIdRole).toULongLong());
  row.name = item->text();
  row.thumbnail = item->icon().pixmap(list_->iconSize());
  row.overlay = item->flags().testFlag(Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked;
  return row;
}

void ChannelPanel::handle_current_item_changed(QListWidgetItem* current) {
  refresh_action_states();
  if (updating_ || current == nullptr || !target_callback_) {
    return;
  }
  const auto selected = row_from_item(current);
  if (selected.kind != RowKind::Alpha && selected.kind != RowKind::Spot) {
    const QSignalBlocker blocker(list_);
    for (int row = 0; row < list_->count(); ++row) {
      auto* item = list_->item(row);
      if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
        item->setCheckState(Qt::Unchecked);
      }
    }
  }
  target_callback_(selected.kind, selected.id, selected.overlay);
}

void ChannelPanel::handle_item_changed(QListWidgetItem* item) {
  if (updating_ || item == nullptr || !item->flags().testFlag(Qt::ItemIsUserCheckable)) {
    return;
  }
  if (item->checkState() == Qt::Checked) {
    const QSignalBlocker blocker(list_);
    for (int row = 0; row < list_->count(); ++row) {
      auto* other = list_->item(row);
      if (other != item && other->flags().testFlag(Qt::ItemIsUserCheckable)) {
        other->setCheckState(Qt::Unchecked);
      }
    }
    list_->setCurrentItem(item);
  }
  refresh_action_states();
  if (item == list_->currentItem() && target_callback_) {
    const auto selected = row_from_item(item);
    target_callback_(selected.kind, selected.id, selected.overlay);
  }
}

void ChannelPanel::handle_rows_moved() {
  if (updating_) {
    return;
  }
  const bool expects_quick_mask =
      committed_rows_.size() >= 5 &&
      committed_rows_[4].kind == RowKind::QuickMask;
  const bool derived_rows_moved =
      list_->count() < 4 ||
      static_cast<RowKind>(list_->item(0)->data(kKindRole).toInt()) != RowKind::Composite ||
      static_cast<RowKind>(list_->item(1)->data(kKindRole).toInt()) != RowKind::Red ||
      static_cast<RowKind>(list_->item(2)->data(kKindRole).toInt()) != RowKind::Green ||
      static_cast<RowKind>(list_->item(3)->data(kKindRole).toInt()) != RowKind::Blue ||
      (expects_quick_mask &&
       (list_->count() < 5 ||
        static_cast<RowKind>(list_->item(4)->data(kKindRole).toInt()) !=
            RowKind::QuickMask));
  const auto order = saved_channel_order();
  const auto spot_moved = std::any_of(fixed_spot_positions_.begin(), fixed_spot_positions_.end(),
                                      [&order](const auto& fixed) {
                                        return fixed.first >= order.size() || order[fixed.first] != fixed.second;
                                      });
  if (derived_rows_moved || spot_moved) {
    const auto selected = selected_row();
    set_rows(committed_rows_, selected);
    return;
  }
  if (reorder_callback_) {
    reorder_callback_(order);
  }
}

void ChannelPanel::show_context_menu(const QPoint& position) {
  if (auto* item = list_->itemAt(position); item != nullptr) {
    list_->setCurrentItem(item);
  }
  refresh_action_states();

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("channelContextMenu"));
  menu.addAction(create_button_->defaultAction());
  menu.addAction(save_selection_button_->defaultAction());
  menu.addAction(load_selection_button_->defaultAction());
  menu.addSeparator();
  menu.addAction(rename_button_->defaultAction());
  menu.addAction(invert_button_->defaultAction());
  menu.addAction(remove_button_->defaultAction());
  menu.exec(list_->viewport()->mapToGlobal(position));
}

void ChannelPanel::refresh_action_states() {
  const auto selected = selected_row();
  const bool loadable = selected.has_value() &&
                        selected->kind != RowKind::QuickMask;
  const bool editable_alpha = selected.has_value() && selected->kind == RowKind::Alpha;
  if (create_button_->defaultAction() != nullptr) {
    create_button_->defaultAction()->setEnabled(document_available_ && channel_creation_available_);
  }
  if (save_selection_button_->defaultAction() != nullptr) {
    save_selection_button_->defaultAction()->setEnabled(document_available_ && channel_creation_available_);
  }
  if (load_selection_button_->defaultAction() != nullptr) {
    load_selection_button_->defaultAction()->setEnabled(document_available_ && loadable);
  }
  if (rename_button_->defaultAction() != nullptr) {
    rename_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
  if (invert_button_->defaultAction() != nullptr) {
    invert_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
  if (remove_button_->defaultAction() != nullptr) {
    remove_button_->defaultAction()->setEnabled(document_available_ && editable_alpha);
  }
}

void ChannelPanel::retranslate_ui() {
  const auto update_button = [](QToolButton* button, const QString& fallback_tooltip) {
    if (button->defaultAction() == nullptr) {
      button->setToolTip(fallback_tooltip);
      button->setAccessibleName(fallback_tooltip);
      return;
    }
    button->setToolTip(button->defaultAction()->toolTip());
    button->setAccessibleName(button->defaultAction()->text());
  };
  update_button(create_button_, tr("New alpha channel"));
  update_button(save_selection_button_, tr("Save selection as channel"));
  update_button(load_selection_button_, tr("Load channel as selection"));
  update_button(rename_button_, tr("Rename channel"));
  update_button(invert_button_, tr("Invert channel"));
  update_button(remove_button_, tr("Delete channel"));
}

}  // namespace patchy::ui
