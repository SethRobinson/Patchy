#include "ui/layer_list_widget.hpp"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QItemSelection>
#include <QMouseEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace photoslop::ui {

void LayerListWidget::set_drop_finished_callback(std::function<void()> callback) {
  drop_finished_callback_ = std::move(callback);
}

void LayerListWidget::set_ctrl_click_callback(std::function<void(QListWidgetItem*)> callback) {
  ctrl_click_callback_ = std::move(callback);
}

bool LayerListWidget::drop_in_progress() const noexcept {
  return drop_in_progress_;
}

std::optional<LayerDropRequest> LayerListWidget::take_drop_request() {
  auto request = std::move(pending_drop_request_);
  pending_drop_request_.reset();
  return request;
}

bool LayerListWidget::event(QEvent* event) {
  if (event->type() == QEvent::Drop) {
    drop_event_uses_viewport_coordinates_ = false;
    dropEvent(static_cast<QDropEvent*>(event));
    drop_event_uses_viewport_coordinates_ = true;
    return event->isAccepted();
  }
  return QListWidget::event(event);
}

bool LayerListWidget::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
        (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      auto* item = itemAt(viewport_pos);
      if (item != nullptr && widget->objectName() == QStringLiteral("layerVisibilityCheck") && ctrl_click_callback_) {
        ctrl_click_callback_(item);
        event->accept();
        return true;
      }
      if (item != nullptr) {
        toggle_ctrl_selection(item);
        event->accept();
        return true;
      }
    } else if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
               (mouse_event->modifiers() & Qt::ShiftModifier) != 0 &&
               widget->objectName() != QStringLiteral("layerVisibilityCheck")) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      if (auto* item = itemAt(viewport_pos); item != nullptr) {
        select_range_to_item(item);
        event->accept();
        return true;
      }
    } else if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
               (mouse_event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0 &&
               widget->objectName() != QStringLiteral("layerVisibilityCheck")) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      if (auto* item = itemAt(viewport_pos); item != nullptr) {
        set_single_drag_item(item);
        drag_start_position_ = viewport_pos;
        row_widget_drag_candidate_ = true;
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseMove) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (row_widget_drag_candidate_ && widget != nullptr && (mouse_event->buttons() & Qt::LeftButton) != 0) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      if ((viewport_pos - drag_start_position_).manhattanLength() >= QApplication::startDragDistance()) {
        row_widget_drag_candidate_ = false;
        startDrag(Qt::MoveAction);
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    row_widget_drag_candidate_ = false;
    drag_anchor_layer_id_.reset();
  }
  return QListWidget::eventFilter(watched, event);
}

void LayerListWidget::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) {
  if (drag_selection_locked()) {
    keep_drag_anchor_selected();
    return;
  }
  QListWidget::setSelection(rect, command);
}

bool LayerListWidget::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::Drop) {
    drop_event_uses_viewport_coordinates_ = true;
    dropEvent(static_cast<QDropEvent*>(event));
    return event->isAccepted();
  }
  if (event->type() == QEvent::MouseButtonPress) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (mouse_event->button() == Qt::LeftButton && (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
      auto* item = itemAt(mouse_event->pos());
      if (item != nullptr && ctrl_click_callback_ && visibility_hit(item, mouse_event->pos())) {
        ctrl_click_callback_(item);
        event->accept();
        return true;
      }
    } else if (mouse_event->button() == Qt::LeftButton && (mouse_event->modifiers() & Qt::ShiftModifier) != 0) {
      if (auto* item = itemAt(mouse_event->pos()); item != nullptr) {
        select_range_to_item(item);
        event->accept();
        return true;
      }
    } else if (mouse_event->button() == Qt::LeftButton &&
               (mouse_event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0) {
      if (auto* item = itemAt(mouse_event->pos()); item != nullptr) {
        set_single_drag_item(item);
        drag_start_position_ = mouse_event->pos();
        row_widget_drag_candidate_ = true;
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseMove) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (row_widget_drag_candidate_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
      if ((mouse_event->pos() - drag_start_position_).manhattanLength() >= QApplication::startDragDistance()) {
        row_widget_drag_candidate_ = false;
        startDrag(Qt::MoveAction);
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    row_widget_drag_candidate_ = false;
    drag_anchor_layer_id_.reset();
  } else if (event->type() == QEvent::Leave) {
    if ((QApplication::mouseButtons() & Qt::LeftButton) == 0) {
      row_widget_drag_candidate_ = false;
      drag_anchor_layer_id_.reset();
    }
  }
  return QListWidget::viewportEvent(event);
}

bool LayerListWidget::visibility_hit(QListWidgetItem* item, QPoint viewport_pos) const {
  auto* row = itemWidget(item);
  if (row == nullptr) {
    return false;
  }
  auto* visibility = row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  if (visibility == nullptr) {
    return false;
  }
  return visibility->geometry().contains(row->mapFrom(viewport(), viewport_pos));
}

void LayerListWidget::toggle_ctrl_selection(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  row_widget_drag_candidate_ = false;
  drag_anchor_layer_id_.reset();
  const auto selected = !item->isSelected();
  item->setSelected(selected);
  if (selected) {
    setCurrentItem(item);
  }
}

void LayerListWidget::select_range_to_item(QListWidgetItem* target_item) {
  if (target_item == nullptr || selectionModel() == nullptr || model() == nullptr) {
    return;
  }
  row_widget_drag_candidate_ = false;
  drag_anchor_layer_id_.reset();

  const auto target_row = row(target_item);
  const auto anchor_row = currentRow() >= 0 ? currentRow() : target_row;
  const auto first_row = std::min(anchor_row, target_row);
  const auto last_row = std::max(anchor_row, target_row);
  const auto target_index = model()->index(target_row, 0);
  const QItemSelection range(model()->index(first_row, 0), model()->index(last_row, 0));
  selectionModel()->setCurrentIndex(target_index, QItemSelectionModel::NoUpdate);
  selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
  viewport()->update();
}

void LayerListWidget::set_single_drag_item(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  drag_anchor_layer_id_ = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  const auto list_updates_enabled = updatesEnabled();
  const auto viewport_updates_enabled = viewport()->updatesEnabled();
  setUpdatesEnabled(false);
  viewport()->setUpdatesEnabled(false);
  setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
  viewport()->setUpdatesEnabled(viewport_updates_enabled);
  setUpdatesEnabled(list_updates_enabled);
  viewport()->update();
}

void LayerListWidget::startDrag(Qt::DropActions supported_actions) {
  if (drag_anchor_layer_id_.has_value()) {
    if (auto* anchor = item_for_layer_id(*drag_anchor_layer_id_); anchor != nullptr) {
      set_single_drag_item(anchor);
    }
  }
  dragged_layer_ids_ = selected_layer_ids_top_to_bottom();
  if (dragged_layer_ids_.empty()) {
    drag_anchor_layer_id_.reset();
    return;
  }

  set_layer_row_buttons_drag_active(true);
  QDrag drag(this);
  drag.setMimeData(mimeData(selectedItems()));
  drag.exec(supported_actions, Qt::MoveAction);
  set_layer_row_buttons_drag_active(false);
  dragged_layer_ids_.clear();
  drag_anchor_layer_id_.reset();
}

void LayerListWidget::dragEnterEvent(QDragEnterEvent* event) {
  keep_drag_anchor_selected();
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void LayerListWidget::dragMoveEvent(QDragMoveEvent* event) {
  keep_drag_anchor_selected();
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void LayerListWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  keep_drag_anchor_selected();
  event->accept();
}

void LayerListWidget::dropEvent(QDropEvent* event) {
  auto ids = dragged_layer_ids_.empty() ? selected_layer_ids_top_to_bottom() : dragged_layer_ids_;
  if (!ids.empty()) {
    const auto event_position = event->position().toPoint();
    auto position = drop_event_uses_viewport_coordinates_ ? event_position : viewport()->mapFrom(this, event_position);
    auto* target_item = itemAt(position);
    if (target_item == nullptr) {
      const auto viewport_position = viewport()->mapFrom(this, event_position);
      if (viewport()->rect().contains(viewport_position)) {
        position = viewport_position;
        target_item = itemAt(position);
      }
    }
    pending_drop_request_ = LayerDropRequest{
        std::move(ids),
        target_item != nullptr
            ? std::optional<LayerId>{static_cast<LayerId>(target_item->data(kLayerIdRole).toULongLong())}
            : std::nullopt,
        inferred_drop_position(target_item, position)};
    drop_in_progress_ = true;
    event->setDropAction(Qt::MoveAction);
    event->accept();
    drop_in_progress_ = false;
  } else {
    drop_in_progress_ = true;
    QListWidget::dropEvent(event);
    drop_in_progress_ = false;
  }
  if (drop_finished_callback_) {
    QTimer::singleShot(0, this, [this] {
      if (drop_finished_callback_) {
        drop_finished_callback_();
      }
    });
  }
}

std::vector<LayerId> LayerListWidget::selected_layer_ids_top_to_bottom() const {
  std::vector<LayerId> ids;
  ids.reserve(static_cast<std::size_t>(selectedItems().size()));
  for (int row = 0; row < count(); ++row) {
    const auto* layer_item = item(row);
    if (layer_item != nullptr && layer_item->isSelected()) {
      ids.push_back(static_cast<LayerId>(layer_item->data(kLayerIdRole).toULongLong()));
    }
  }
  return ids;
}

QListWidgetItem* LayerListWidget::item_for_layer_id(LayerId id) const {
  for (int row = 0; row < count(); ++row) {
    auto* layer_item = item(row);
    if (layer_item != nullptr && static_cast<LayerId>(layer_item->data(kLayerIdRole).toULongLong()) == id) {
      return layer_item;
    }
  }
  return nullptr;
}

LayerDropPosition LayerListWidget::inferred_drop_position(QListWidgetItem* target_item,
                                                          QPoint viewport_position) const {
  if (target_item == nullptr) {
    return LayerDropPosition::OnViewport;
  }

  const auto rect = visualItemRect(target_item);
  const auto edge_band = target_item->data(kLayerIsGroupRole).toBool() ? 5 : std::max(4, rect.height() / 4);
  if (viewport_position.y() < rect.top() + edge_band) {
    return LayerDropPosition::AboveItem;
  }
  if (viewport_position.y() >= rect.bottom() - edge_band) {
    return LayerDropPosition::BelowItem;
  }
  return LayerDropPosition::OnItem;
}

void LayerListWidget::set_layer_row_buttons_drag_active(bool active) {
  for (auto* button : findChildren<QToolButton*>()) {
    if (button->objectName() != QStringLiteral("layerFolderDisclosureButton") &&
        button->objectName() != QStringLiteral("layerVisibilityCheck")) {
      continue;
    }
    button->setProperty("layerDragActive", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
  viewport()->update();
}

void LayerListWidget::keep_drag_anchor_selected() {
  if (!drag_anchor_layer_id_.has_value()) {
    return;
  }
  auto* anchor = item_for_layer_id(*drag_anchor_layer_id_);
  if (anchor == nullptr) {
    return;
  }
  const auto selected = selectedItems();
  if (selected.size() == 1 && selected.front() == anchor && currentItem() == anchor) {
    return;
  }
  set_single_drag_item(anchor);
}

bool LayerListWidget::drag_selection_locked() const noexcept {
  return drag_anchor_layer_id_.has_value() && (row_widget_drag_candidate_ || !dragged_layer_ids_.empty());
}

}  // namespace photoslop::ui
