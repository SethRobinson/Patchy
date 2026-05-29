#include "ui/layer_list_widget.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QItemSelection>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QTimerEvent>
#include <QToolButton>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace patchy::ui {

namespace {

#ifdef Q_OS_WIN
QPointer<LayerListWidget> active_drag_wheel_target;
HHOOK active_drag_wheel_hook = nullptr;

LRESULT CALLBACK layer_drag_mouse_hook(int code, WPARAM w_param, LPARAM l_param) {
  if (code == HC_ACTION && active_drag_wheel_target != nullptr && l_param != 0 &&
      (w_param == WM_MOUSEWHEEL || w_param == WM_MOUSEHWHEEL)) {
    const auto* hook_info = reinterpret_cast<MSLLHOOKSTRUCT*>(l_param);
    const auto wheel_delta = static_cast<int>(static_cast<short>(HIWORD(hook_info->mouseData)));
    if (wheel_delta != 0 &&
        active_drag_wheel_target->handle_drag_wheel_at_global_position(QPoint(hook_info->pt.x, hook_info->pt.y),
                                                                       wheel_delta)) {
      return 1;
    }
  }
  return CallNextHookEx(active_drag_wheel_hook, code, w_param, l_param);
}
#endif

}  // namespace

QByteArray layer_ids_to_mime_data(const std::vector<LayerId>& ids) {
  QByteArray payload;
  for (const auto id : ids) {
    if (id == 0) {
      continue;
    }
    if (!payload.isEmpty()) {
      payload.append('\n');
    }
    payload.append(QByteArray::number(static_cast<qulonglong>(id)));
  }
  return payload;
}

std::vector<LayerId> layer_ids_from_mime_data(const QMimeData* mime_data) {
  std::vector<LayerId> ids;
  if (mime_data == nullptr || !mime_data->hasFormat(QString::fromLatin1(kLayerDragMimeType))) {
    return ids;
  }
  for (const auto& part : mime_data->data(QString::fromLatin1(kLayerDragMimeType)).split('\n')) {
    bool ok = false;
    const auto value = part.toULongLong(&ok);
    if (ok && value != 0) {
      ids.push_back(static_cast<LayerId>(value));
    }
  }
  return ids;
}

void LayerListWidget::set_drop_finished_callback(std::function<void()> callback) {
  drop_finished_callback_ = std::move(callback);
}

void LayerListWidget::set_ctrl_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback) {
  ctrl_click_callback_ = std::move(callback);
}

void LayerListWidget::set_thumbnail_click_callback(
    std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback) {
  thumbnail_click_callback_ = std::move(callback);
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
  if (event->type() == QEvent::Wheel) {
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    if (wheel_event_targets_list(watched, *wheel_event) && handle_wheel_event(wheel_event)) {
      return true;
    }
  }
  if (event->type() == QEvent::MouseButtonPress) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
        (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      auto* item = itemAt(viewport_pos);
      const auto target = item != nullptr ? ctrl_click_target(item, viewport_pos) : std::nullopt;
      if (item != nullptr && target.has_value() && ctrl_click_callback_) {
        ctrl_click_callback_(item, *target);
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
        begin_single_drag_item(item);
        if (const auto target = ctrl_click_target(item, viewport_pos); target.has_value() && thumbnail_click_callback_) {
          thumbnail_click_callback_(item, *target);
        }
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
    finish_pending_single_select();
    drag_anchor_layer_id_.reset();
  }
  return QListWidget::eventFilter(watched, event);
}

bool LayerListWidget::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) {
  Q_UNUSED(event_type);

#ifdef Q_OS_WIN
  if (message == nullptr || dragged_layer_ids_.empty()) {
    return false;
  }

  const auto* native_message = static_cast<MSG*>(message);
  if (native_message->message != WM_MOUSEWHEEL && native_message->message != WM_MOUSEHWHEEL) {
    return false;
  }
  if (!global_position_targets_list(QCursor::pos())) {
    return false;
  }

  const auto primary_delta = GET_WHEEL_DELTA_WPARAM(native_message->wParam);
  if (primary_delta == 0 || !scroll_by_wheel_delta(primary_delta, false)) {
    return false;
  }
  if (result != nullptr) {
    *result = 0;
  }
  return true;
#else
  Q_UNUSED(message);
  Q_UNUSED(result);
  return false;
#endif
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
  if (event->type() == QEvent::Wheel) {
    if (handle_wheel_event(static_cast<QWheelEvent*>(event))) {
      return true;
    }
  }
  if (event->type() == QEvent::MouseButtonPress) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (mouse_event->button() == Qt::LeftButton && (mouse_event->modifiers() & Qt::ControlModifier) != 0) {
      auto* item = itemAt(mouse_event->pos());
      const auto target = item != nullptr ? ctrl_click_target(item, mouse_event->pos()) : std::nullopt;
      if (item != nullptr && target.has_value() && ctrl_click_callback_) {
        ctrl_click_callback_(item, *target);
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
        begin_single_drag_item(item);
        if (const auto target = ctrl_click_target(item, mouse_event->pos()); target.has_value() &&
            thumbnail_click_callback_) {
          thumbnail_click_callback_(item, *target);
        }
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
    finish_pending_single_select();
    drag_anchor_layer_id_.reset();
  } else if (event->type() == QEvent::Leave) {
    if ((QApplication::mouseButtons() & Qt::LeftButton) == 0) {
      row_widget_drag_candidate_ = false;
      pending_single_select_on_release_ = false;
      drag_anchor_layer_id_.reset();
    }
  }
  return QListWidget::viewportEvent(event);
}

std::optional<LayerCtrlClickTarget> LayerListWidget::ctrl_click_target(QListWidgetItem* item,
                                                                       QPoint viewport_pos) const {
  auto* row = itemWidget(item);
  if (row == nullptr) {
    return std::nullopt;
  }
  const auto row_pos = row->mapFrom(viewport(), viewport_pos);
  if (auto* content_thumbnail = row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail"));
      content_thumbnail != nullptr && content_thumbnail->geometry().contains(row_pos)) {
    return LayerCtrlClickTarget::ContentThumbnail;
  }
  if (auto* mask_thumbnail = row->findChild<QWidget*>(QStringLiteral("layerMaskThumbnail"));
      mask_thumbnail != nullptr && mask_thumbnail->geometry().contains(row_pos)) {
    return LayerCtrlClickTarget::MaskThumbnail;
  }
  return std::nullopt;
}

QMimeData* LayerListWidget::mimeData(const QList<QListWidgetItem*>& items) const {
  auto* mime_data = QListWidget::mimeData(items);
  auto ids = selected_layer_ids_top_to_bottom();
  if (ids.empty()) {
    ids.reserve(static_cast<std::size_t>(items.size()));
    for (int row = 0; row < count(); ++row) {
      auto* layer_item = item(row);
      if (layer_item != nullptr && items.contains(layer_item)) {
        ids.push_back(static_cast<LayerId>(layer_item->data(kLayerIdRole).toULongLong()));
      }
    }
  }
  mime_data->setData(QString::fromLatin1(kLayerDragMimeType), layer_ids_to_mime_data(ids));
  return mime_data;
}

void LayerListWidget::toggle_ctrl_selection(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  row_widget_drag_candidate_ = false;
  pending_single_select_on_release_ = false;
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
  pending_single_select_on_release_ = false;
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

void LayerListWidget::begin_single_drag_item(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }

  drag_anchor_layer_id_ = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
  pending_single_select_on_release_ = item->isSelected();
  if (!pending_single_select_on_release_) {
    set_single_drag_item(item);
    return;
  }

  const auto list_updates_enabled = updatesEnabled();
  const auto viewport_updates_enabled = viewport()->updatesEnabled();
  setUpdatesEnabled(false);
  viewport()->setUpdatesEnabled(false);
  setCurrentItem(item, QItemSelectionModel::NoUpdate);
  viewport()->setUpdatesEnabled(viewport_updates_enabled);
  setUpdatesEnabled(list_updates_enabled);
  viewport()->update();
}

void LayerListWidget::set_single_drag_item(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  pending_single_select_on_release_ = false;
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

void LayerListWidget::finish_pending_single_select() {
  if (!pending_single_select_on_release_) {
    return;
  }
  pending_single_select_on_release_ = false;
  if (!drag_anchor_layer_id_.has_value()) {
    return;
  }
  if (auto* anchor = item_for_layer_id(*drag_anchor_layer_id_); anchor != nullptr) {
    set_single_drag_item(anchor);
  }
}

void LayerListWidget::startDrag(Qt::DropActions supported_actions) {
  if (drag_anchor_layer_id_.has_value()) {
    keep_drag_anchor_selected();
  }
  pending_single_select_on_release_ = false;
  dragged_layer_ids_ = selected_layer_ids_top_to_bottom();
  if (dragged_layer_ids_.empty()) {
    drag_anchor_layer_id_.reset();
    return;
  }

  set_layer_row_buttons_drag_active(true);
  QDrag drag(this);
  drag.setMimeData(mimeData(selectedItems()));
  qApp->installEventFilter(this);
#ifdef Q_OS_WIN
  qApp->installNativeEventFilter(this);
#endif
  install_drag_wheel_hook();
  drag.exec(supported_actions, Qt::MoveAction);
  remove_drag_wheel_hook();
#ifdef Q_OS_WIN
  qApp->removeNativeEventFilter(this);
#endif
  qApp->removeEventFilter(this);
  stop_auto_scroll();
  clear_drop_preview();
  set_layer_row_buttons_drag_active(false);
  dragged_layer_ids_.clear();
  drag_anchor_layer_id_.reset();
}

void LayerListWidget::dragEnterEvent(QDragEnterEvent* event) {
  keep_drag_anchor_selected();
  update_drop_preview(event->position().toPoint());
  update_auto_scroll(event->position().toPoint());
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void LayerListWidget::dragMoveEvent(QDragMoveEvent* event) {
  keep_drag_anchor_selected();
  update_drop_preview(event->position().toPoint());
  update_auto_scroll(event->position().toPoint());
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void LayerListWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  keep_drag_anchor_selected();
  stop_auto_scroll();
  clear_drop_preview();
  event->accept();
}

void LayerListWidget::dropEvent(QDropEvent* event) {
  stop_auto_scroll();
  clear_drop_preview();
  auto ids = !dragged_layer_ids_.empty() ? dragged_layer_ids_ : layer_ids_from_mime_data(event->mimeData());
  if (ids.empty()) {
    ids = selected_layer_ids_top_to_bottom();
  }
  if (!ids.empty()) {
    const auto event_position = event->position().toPoint();
    auto position = drop_event_uses_viewport_coordinates_ ? event_position : viewport()->mapFrom(this, event_position);
    if (!viewport()->rect().contains(position)) {
      const auto viewport_position = viewport()->mapFrom(this, event_position);
      if (viewport()->rect().contains(viewport_position)) {
        position = viewport_position;
      }
    }
    const auto target = drop_target_at(position);
    pending_drop_request_ = LayerDropRequest{
        std::move(ids),
        target.layer_id,
        target.position};
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

QListWidgetItem* LayerListWidget::parent_item_for(QListWidgetItem* item) const {
  if (item == nullptr) {
    return nullptr;
  }
  const auto item_depth = item->data(kLayerDepthRole).toInt();
  if (item_depth <= 0) {
    return nullptr;
  }
  for (int row_index = row(item) - 1; row_index >= 0; --row_index) {
    auto* candidate = this->item(row_index);
    if (candidate != nullptr && candidate->data(kLayerDepthRole).toInt() == item_depth - 1) {
      return candidate;
    }
  }
  return nullptr;
}

LayerListWidget::DropTarget LayerListWidget::drop_target_at(QPoint viewport_position) const {
  if (count() <= 0) {
    return DropTarget{std::nullopt, LayerDropPosition::OnViewport};
  }

  auto* target_item = itemAt(viewport_position);
  if (target_item == nullptr) {
    auto* first = item(0);
    auto* last = item(count() - 1);
    if (first != nullptr && viewport_position.y() < visualItemRect(first).top()) {
      return DropTarget{static_cast<LayerId>(first->data(kLayerIdRole).toULongLong()),
                        LayerDropPosition::AboveItem};
    }
    if (last != nullptr && viewport_position.y() > visualItemRect(last).bottom()) {
      return DropTarget{static_cast<LayerId>(last->data(kLayerIdRole).toULongLong()),
                        LayerDropPosition::BelowItem};
    }
    return DropTarget{std::nullopt, LayerDropPosition::OnViewport};
  }

  const auto depth = target_item->data(kLayerDepthRole).toInt();
  const auto target_position = inferred_drop_position(target_item, viewport_position);
  if (target_item->data(kLayerIsGroupRole).toBool() && target_position == LayerDropPosition::BelowItem) {
    const auto target_row = row(target_item);
    auto* next_item = target_row + 1 < count() ? item(target_row + 1) : nullptr;
    if (next_item != nullptr && next_item->data(kLayerDepthRole).toInt() > depth) {
      return DropTarget{static_cast<LayerId>(next_item->data(kLayerIdRole).toULongLong()),
                        LayerDropPosition::AboveItem};
    }
  }
  if (depth > 0 && viewport_position.x() < row_content_left(target_item) - 4) {
    if (auto* parent = parent_item_for(target_item); parent != nullptr) {
      const auto rect = visualItemRect(target_item);
      return DropTarget{static_cast<LayerId>(parent->data(kLayerIdRole).toULongLong()),
                        viewport_position.y() < rect.center().y() ? LayerDropPosition::AboveItem
                                                                  : LayerDropPosition::BelowItem};
    }
  }

  return DropTarget{static_cast<LayerId>(target_item->data(kLayerIdRole).toULongLong()),
                    target_position};
}

LayerDropPosition LayerListWidget::inferred_drop_position(QListWidgetItem* target_item,
                                                          QPoint viewport_position) const {
  if (target_item == nullptr) {
    return LayerDropPosition::OnViewport;
  }

  const auto rect = visualItemRect(target_item);
  if (!item_accepts_on_drop(target_item)) {
    return viewport_position.y() < rect.center().y() ? LayerDropPosition::AboveItem : LayerDropPosition::BelowItem;
  }

  const auto edge_band = std::max(8, rect.height() / 3);
  if (viewport_position.y() < rect.top() + edge_band) {
    return LayerDropPosition::AboveItem;
  }
  if (viewport_position.y() >= rect.bottom() - edge_band) {
    return LayerDropPosition::BelowItem;
  }
  return LayerDropPosition::OnItem;
}

int LayerListWidget::row_content_left(QListWidgetItem* item) const {
  auto* row_widget = itemWidget(item);
  if (row_widget == nullptr) {
    return visualItemRect(item).left();
  }
  if (auto* thumbnail = row_widget->findChild<QWidget*>(QStringLiteral("layerContentThumbnail"));
      thumbnail != nullptr) {
    return row_widget->mapTo(viewport(), thumbnail->pos()).x();
  }
  return row_widget->mapTo(viewport(), QPoint()).x();
}

bool LayerListWidget::item_accepts_on_drop(QListWidgetItem* item) const {
  return item != nullptr && item->data(kLayerIsGroupRole).toBool() &&
         item->data(kLayerGroupExpandedRole).toBool();
}

void LayerListWidget::update_drop_preview(QPoint viewport_position) {
  last_drag_viewport_position_ = viewport_position;
  const auto target = drop_target_at(viewport_position);
  if (drop_preview_.has_value() && drop_preview_->layer_id == target.layer_id &&
      drop_preview_->position == target.position) {
    return;
  }
  drop_preview_ = target;
  update_drop_preview_widgets();
  viewport()->update();
}

void LayerListWidget::clear_drop_preview() {
  if (!drop_preview_.has_value()) {
    return;
  }
  drop_preview_.reset();
  update_drop_preview_widgets();
  viewport()->update();
}

QWidget* LayerListWidget::ensure_insertion_indicator() {
  if (insertion_indicator_ == nullptr) {
    insertion_indicator_ = new QWidget(viewport());
    insertion_indicator_->setObjectName(QStringLiteral("layerDropInsertionIndicator"));
    insertion_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    insertion_indicator_->setStyleSheet(QStringLiteral(
        "QWidget#layerDropInsertionIndicator { background: #31a8ff; border: 1px solid #07121e; border-radius: 2px; }"));
    insertion_indicator_->hide();
  }
  return insertion_indicator_;
}

QWidget* LayerListWidget::ensure_folder_highlight_indicator() {
  if (folder_highlight_indicator_ == nullptr) {
    folder_highlight_indicator_ = new QWidget(viewport());
    folder_highlight_indicator_->setObjectName(QStringLiteral("layerDropFolderHighlightIndicator"));
    folder_highlight_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    folder_highlight_indicator_->setStyleSheet(QStringLiteral(
        "QWidget#layerDropFolderHighlightIndicator { background: rgba(49, 168, 255, 36); "
        "border: 2px solid #31a8ff; border-radius: 3px; }"));
    folder_highlight_indicator_->hide();
  }
  return folder_highlight_indicator_;
}

void LayerListWidget::update_drop_preview_widgets() {
  auto* insertion = ensure_insertion_indicator();
  auto* folder_highlight = ensure_folder_highlight_indicator();
  insertion->hide();
  folder_highlight->hide();

  if (!drop_preview_.has_value() || !drop_preview_->layer_id.has_value()) {
    return;
  }
  auto* target_item = item_for_layer_id(*drop_preview_->layer_id);
  if (target_item == nullptr) {
    return;
  }
  const auto rect = visualItemRect(target_item);
  if (!rect.isValid()) {
    return;
  }

  if (drop_preview_->position == LayerDropPosition::OnItem && item_accepts_on_drop(target_item)) {
    folder_highlight->setGeometry(rect.adjusted(2, 2, -3, -3));
    folder_highlight->raise();
    folder_highlight->show();
    return;
  }

  if (drop_preview_->position != LayerDropPosition::AboveItem &&
      drop_preview_->position != LayerDropPosition::BelowItem) {
    return;
  }
  const auto y = drop_preview_->position == LayerDropPosition::AboveItem ? rect.top() : rect.bottom() + 1;
  insertion->setGeometry(QRect(6, y - 2, std::max(1, viewport()->width() - 12), 5));
  insertion->raise();
  insertion->show();
}

void LayerListWidget::update_auto_scroll(QPoint viewport_position) {
  last_drag_viewport_position_ = viewport_position;
  auto* scroll_bar = verticalScrollBar();
  if (scroll_bar == nullptr || scroll_bar->maximum() <= scroll_bar->minimum()) {
    stop_auto_scroll();
    return;
  }

  constexpr int kAutoScrollMargin = 24;
  int direction = 0;
  if (viewport_position.y() < kAutoScrollMargin && scroll_bar->value() > scroll_bar->minimum()) {
    direction = -1;
  } else if (viewport_position.y() > viewport()->height() - kAutoScrollMargin &&
             scroll_bar->value() < scroll_bar->maximum()) {
    direction = 1;
  }

  if (direction == 0) {
    stop_auto_scroll();
    return;
  }
  auto_scroll_direction_ = direction;
  if (!auto_scroll_timer_.isActive()) {
    auto_scroll_timer_.start(90, this);
  }
}

void LayerListWidget::stop_auto_scroll() {
  auto_scroll_direction_ = 0;
  if (auto_scroll_timer_.isActive()) {
    auto_scroll_timer_.stop();
  }
}

void LayerListWidget::apply_auto_scroll_step() {
  auto* scroll_bar = verticalScrollBar();
  if (scroll_bar == nullptr || auto_scroll_direction_ == 0) {
    stop_auto_scroll();
    return;
  }
  const auto before = scroll_bar->value();
  const auto step = std::max(1, scroll_bar->singleStep());
  scroll_bar->setValue(std::clamp(before + auto_scroll_direction_ * step, scroll_bar->minimum(),
                                  scroll_bar->maximum()));
  if (scroll_bar->value() == before) {
    stop_auto_scroll();
    return;
  }
  update_drop_preview(last_drag_viewport_position_);
  update_drop_preview_widgets();
}

void LayerListWidget::install_drag_wheel_hook() {
#ifdef Q_OS_WIN
  active_drag_wheel_target = this;
  if (active_drag_wheel_hook == nullptr) {
    active_drag_wheel_hook = SetWindowsHookExW(WH_MOUSE_LL, layer_drag_mouse_hook, GetModuleHandleW(nullptr), 0);
  }
#endif
}

void LayerListWidget::remove_drag_wheel_hook() {
#ifdef Q_OS_WIN
  if (active_drag_wheel_hook != nullptr) {
    UnhookWindowsHookEx(active_drag_wheel_hook);
    active_drag_wheel_hook = nullptr;
  }
  if (active_drag_wheel_target == this) {
    active_drag_wheel_target.clear();
  }
#endif
}

bool LayerListWidget::handle_drag_wheel_at_global_position(QPoint global_position, int primary_delta) {
  if (dragged_layer_ids_.empty() || !global_position_targets_list(global_position)) {
    return false;
  }
  return scroll_by_wheel_delta(primary_delta, false);
}

bool LayerListWidget::handle_wheel_event(QWheelEvent* event) {
  if (event == nullptr) {
    return false;
  }
  const auto delta = !event->pixelDelta().isNull() ? event->pixelDelta() : event->angleDelta();
  const auto primary_delta = delta.y() != 0 ? delta.y() : delta.x();
  if (primary_delta == 0) {
    return false;
  }
  if (!scroll_by_wheel_delta(primary_delta, !event->pixelDelta().isNull())) {
    return false;
  }
  event->accept();
  return true;
}

bool LayerListWidget::wheel_event_targets_list(QObject* watched, const QWheelEvent& event) const {
  if (global_position_targets_list(event.globalPosition().toPoint())) {
    return true;
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  return widget != nullptr && (widget == this || widget == viewport() || isAncestorOf(widget));
}

bool LayerListWidget::global_position_targets_list(QPoint global_position) const {
  if (!isVisible() || viewport() == nullptr) {
    return false;
  }
  return rect().contains(mapFromGlobal(global_position)) ||
         viewport()->rect().contains(viewport()->mapFromGlobal(global_position));
}

bool LayerListWidget::scroll_by_wheel_delta(int primary_delta, bool pixel_delta) {
  auto* scroll_bar = verticalScrollBar();
  if (scroll_bar == nullptr || scroll_bar->maximum() <= scroll_bar->minimum()) {
    return false;
  }

  const auto wheel_units = pixel_delta ? primary_delta : primary_delta * scroll_bar->singleStep() / 120;
  if (wheel_units == 0) {
    return false;
  }
  scroll_bar->setValue(std::clamp(scroll_bar->value() - wheel_units, scroll_bar->minimum(), scroll_bar->maximum()));
  if (drop_preview_.has_value()) {
    update_drop_preview(last_drag_viewport_position_);
  }
  return true;
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
  if (anchor->isSelected() && currentItem() == anchor) {
    return;
  }
  if (currentItem() != anchor) {
    setCurrentItem(anchor, QItemSelectionModel::NoUpdate);
  }
  if (!anchor->isSelected()) {
    anchor->setSelected(true);
  }
  viewport()->update();
}

bool LayerListWidget::drag_selection_locked() const noexcept {
  return drag_anchor_layer_id_.has_value() && (row_widget_drag_candidate_ || !dragged_layer_ids_.empty());
}

void LayerListWidget::paintEvent(QPaintEvent* event) {
  QListWidget::paintEvent(event);
  if (!drop_preview_.has_value() || !drop_preview_->layer_id.has_value()) {
    return;
  }

  auto* target_item = item_for_layer_id(*drop_preview_->layer_id);
  if (target_item == nullptr) {
    return;
  }
  const auto rect = visualItemRect(target_item);
  if (!rect.isValid()) {
    return;
  }

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor indicator(49, 168, 255);
  if (drop_preview_->position == LayerDropPosition::OnItem && item_accepts_on_drop(target_item)) {
    painter.setPen(QPen(indicator, 2));
    painter.setBrush(QColor(49, 168, 255, 36));
    painter.drawRoundedRect(rect.adjusted(2, 2, -3, -3), 3, 3);
    return;
  }

  if (drop_preview_->position != LayerDropPosition::AboveItem &&
      drop_preview_->position != LayerDropPosition::BelowItem) {
    return;
  }
  const auto y = drop_preview_->position == LayerDropPosition::AboveItem ? rect.top() : rect.bottom() + 1;
  const auto left = rect.left() + 5;
  const auto right = rect.right() - 5;
  painter.setPen(QPen(QColor(7, 18, 30, 180), 4, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QPoint(left, y), QPoint(right, y));
  painter.setPen(QPen(indicator, 2, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(QPoint(left, y), QPoint(right, y));
  painter.setBrush(indicator);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPoint(left, y), 3, 3);
  painter.drawEllipse(QPoint(right, y), 3, 3);
}

void LayerListWidget::timerEvent(QTimerEvent* event) {
  if (event != nullptr && event->timerId() == auto_scroll_timer_.timerId()) {
    apply_auto_scroll_step();
    return;
  }
  QListWidget::timerEvent(event);
}

}  // namespace patchy::ui
