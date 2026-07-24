#include "ui/layer_list_widget.hpp"

#include "ui/action_icons.hpp"

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
#include <QRegion>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QTimerEvent>
#include <QToolButton>
#include <QWidget>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
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

constexpr auto kLayerScrollBarContainerFilterProperty = "patchy.layerScrollBarContainerFilter";

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

QStyleOptionSlider scroll_bar_style_option(const QScrollBar& scroll_bar) {
  QStyleOptionSlider option;
  option.initFrom(&scroll_bar);
  option.orientation = scroll_bar.orientation();
  option.minimum = scroll_bar.minimum();
  option.maximum = scroll_bar.maximum();
  option.singleStep = scroll_bar.singleStep();
  option.pageStep = scroll_bar.pageStep();
  option.sliderPosition = scroll_bar.sliderPosition();
  option.sliderValue = scroll_bar.value();
  option.upsideDown = scroll_bar.invertedAppearance();
  return option;
}

int scroll_bar_drag_travel(QScrollBar& scroll_bar, const QStyleOptionSlider& option) {
  const auto slider =
      scroll_bar.style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, &scroll_bar);
  auto groove =
      scroll_bar.style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarGroove, &scroll_bar);
  if (!groove.isValid()) {
    groove = scroll_bar.rect();
  }
  return scroll_bar.orientation() == Qt::Vertical ? groove.height() - slider.height() : groove.width() - slider.width();
}

// Row child buttons that must receive mouse clicks themselves instead of the
// list-level select/drag handling.
bool layer_row_button_owns_clicks(const QString& object_name) {
  return object_name == QLatin1String("layerVisibilityCheck") ||
         object_name == QLatin1String("layerMaskLinkButton") ||
         object_name == QLatin1String("layerFxBadgeButton") ||
         object_name == QLatin1String("layerSmartObjectBadgeButton") ||
         object_name == QLatin1String("layerClippingBadgeButton") ||
         object_name == QLatin1String("layerSmartFiltersVisibilityButton") ||
         object_name == QLatin1String("layerSmartFilterVisibilityButton") ||
         object_name == QLatin1String("layerSmartFilterEditButton") ||
         object_name == QLatin1String("layerSmartFilterMoreButton");
}

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

LayerListWidget::LayerListWidget(QWidget* parent) : QListWidget(parent) {
  const auto connect_scroll_bar = [this](QScrollBar* scroll_bar) {
    if (scroll_bar == nullptr) {
      return;
    }
    connect(scroll_bar, &QScrollBar::rangeChanged, this,
            [this](int, int) { schedule_row_viewport_mask_update(); });
    connect(scroll_bar, &QScrollBar::valueChanged, this, [this](int) { schedule_row_viewport_mask_update(); });
  };
  connect_scroll_bar(verticalScrollBar());
  connect_scroll_bar(horizontalScrollBar());
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* focused) {
    if (focused == nullptr || focused->window() == window()) {
      schedule_row_viewport_mask_update();
    }
  });
  install_scroll_bar_container_event_filters();
}

void LayerListWidget::set_drop_finished_callback(std::function<void()> callback) {
  drop_finished_callback_ = std::move(callback);
}

void LayerListWidget::set_clip_boundary_callbacks(std::function<bool(LayerId, LayerId)> can_toggle,
                                                  std::function<void(LayerId)> toggle) {
  clip_boundary_can_toggle_ = std::move(can_toggle);
  clip_boundary_toggle_ = std::move(toggle);
  viewport()->setMouseTracking(true);
}

std::optional<LayerListWidget::ClipBoundaryHit> LayerListWidget::clip_boundary_hit(QPoint viewport_position) const {
  constexpr int kBoundaryBand = 5;
  auto* hit_item = itemAt(viewport_position);
  if (hit_item == nullptr) {
    return std::nullopt;
  }
  const auto rect = visualItemRect(hit_item);
  const auto hit_row = row(hit_item);
  QListWidgetItem* upper = nullptr;
  QListWidgetItem* lower = nullptr;
  if (viewport_position.y() <= rect.top() + kBoundaryBand) {
    upper = item(hit_row - 1);
    lower = hit_item;
  } else if (viewport_position.y() >= rect.bottom() - kBoundaryBand) {
    upper = hit_item;
    lower = item(hit_row + 1);
  }
  if (upper == nullptr || lower == nullptr) {
    return std::nullopt;
  }
  return ClipBoundaryHit{static_cast<LayerId>(upper->data(kLayerIdRole).toULongLong()),
                         static_cast<LayerId>(lower->data(kLayerIdRole).toULongLong())};
}

void LayerListWidget::update_clip_boundary_cursor(QWidget* hover_widget, QPoint viewport_position,
                                                  Qt::KeyboardModifiers modifiers) {
  bool active = false;
  if ((modifiers & Qt::AltModifier) != 0 && clip_boundary_can_toggle_ && !drop_in_progress_) {
    if (const auto hit = clip_boundary_hit(viewport_position);
        hit.has_value() && clip_boundary_can_toggle_(hit->upper, hit->lower)) {
      active = true;
    }
  }
  if (!active) {
    clear_clip_boundary_cursor();
    return;
  }
  auto* cursor_widget = hover_widget != nullptr ? hover_widget : viewport();
  if (clip_cursor_widget_ == cursor_widget) {
    return;
  }
  clear_clip_boundary_cursor();
  static const QCursor clip_cursor(simple_icon(QStringLiteral("clip"), QColor(235, 240, 248)).pixmap(20, 20));
  cursor_widget->setCursor(clip_cursor);
  clip_cursor_widget_ = cursor_widget;
}

void LayerListWidget::clear_clip_boundary_cursor() {
  if (!clip_cursor_widget_.isNull()) {
    clip_cursor_widget_->unsetCursor();
  }
  clip_cursor_widget_.clear();
}

bool LayerListWidget::handle_clip_boundary_press(QPoint viewport_position, Qt::KeyboardModifiers modifiers) {
  if ((modifiers & Qt::AltModifier) == 0 || !clip_boundary_toggle_ || !clip_boundary_can_toggle_ ||
      drop_in_progress_) {
    return false;
  }
  const auto hit = clip_boundary_hit(viewport_position);
  if (!hit.has_value() || !clip_boundary_can_toggle_(hit->upper, hit->lower)) {
    return false;
  }
  clear_clip_boundary_cursor();
  // Deferred: the toggle rebuilds the layer rows, and this press may originate
  // from a row child's event filter.
  QTimer::singleShot(0, this, [this, upper = hit->upper] {
    if (clip_boundary_toggle_) {
      clip_boundary_toggle_(upper);
    }
  });
  return true;
}

void LayerListWidget::set_ctrl_click_callback(std::function<void(QListWidgetItem*, LayerCtrlClickTarget)> callback) {
  ctrl_click_callback_ = std::move(callback);
}

void LayerListWidget::set_thumbnail_click_callback(
    std::function<void(QListWidgetItem*, LayerCtrlClickTarget, Qt::KeyboardModifiers)> callback) {
  thumbnail_click_callback_ = std::move(callback);
}

void LayerListWidget::set_item_double_click_callback(std::function<void(QListWidgetItem*)> callback) {
  item_double_click_callback_ = std::move(callback);
}

void LayerListWidget::set_smart_filter_double_click_callback(
    std::function<void(QListWidgetItem*, std::size_t)> callback) {
  smart_filter_double_click_callback_ = std::move(callback);
}

bool LayerListWidget::drop_in_progress() const noexcept {
  return drop_in_progress_;
}

void LayerListWidget::set_drag_blocked(bool blocked) {
  drag_blocked_ = blocked;
  if (!blocked) {
    suppress_drag_select_until_release_ = false;
  }
}

void LayerListWidget::set_drag_blocked_callback(std::function<void()> callback) {
  drag_blocked_callback_ = std::move(callback);
}

std::optional<LayerDropRequest> LayerListWidget::take_drop_request() {
  auto request = std::move(pending_drop_request_);
  pending_drop_request_.reset();
  return request;
}

void LayerListWidget::refresh_row_widths() {
  if (updating_row_widths_) {
    return;
  }

  install_scroll_bar_container_event_filters();
  updating_row_widths_ = true;
  const auto viewport_width = viewport() != nullptr ? viewport()->width() : width();
  for (int row_index = 0; row_index < count(); ++row_index) {
    auto* layer_item = item(row_index);
    auto* row_widget = layer_item != nullptr ? itemWidget(layer_item) : nullptr;
    if (layer_item == nullptr || row_widget == nullptr) {
      continue;
    }

    const auto current_size = layer_item->sizeHint();
    const auto row_size = row_widget->sizeHint();
    const auto target_width = std::max(viewport_width, row_size.width());
    const auto target_height = current_size.height() > 0 ? current_size.height() : std::max(1, row_size.height());
    const QSize next_size(target_width, target_height);
    if (current_size != next_size) {
      layer_item->setSizeHint(next_size);
    }
  }
  updating_row_widths_ = false;
  update_row_viewport_masks();
  schedule_row_viewport_mask_update();
}

void LayerListWidget::schedule_row_viewport_mask_update() {
  if (row_mask_update_pending_) {
    return;
  }
  row_mask_update_pending_ = true;
  QTimer::singleShot(0, this, [this] {
    row_mask_update_pending_ = false;
    update_row_viewport_masks();
  });
}

void LayerListWidget::update_row_viewport_masks() {
  if (viewport() == nullptr) {
    return;
  }
  for (auto* scroll_bar : {verticalScrollBar(), horizontalScrollBar()}) {
    if (scroll_bar != nullptr && scroll_bar->isVisible()) {
      scroll_bar->raise();
    }
  }
  for (int row_index = 0; row_index < count(); ++row_index) {
    auto* layer_item = item(row_index);
    auto* row_widget = layer_item != nullptr ? itemWidget(layer_item) : nullptr;
    if (row_widget == nullptr) {
      continue;
    }
    const QRect viewport_rect_in_row(row_widget->mapFrom(viewport(), QPoint(0, 0)), viewport()->size());
    QRegion mask(row_widget->rect().intersected(viewport_rect_in_row));
    for (auto* scroll_bar : {verticalScrollBar(), horizontalScrollBar()}) {
      if (scroll_bar == nullptr || scroll_bar->parentWidget() == nullptr ||
          scroll_bar->maximum() <= scroll_bar->minimum() ||
          (scroll_bar->width() <= 0 || scroll_bar->height() <= 0)) {
        continue;
      }
      const QRect scroll_rect_in_row(row_widget->mapFrom(scroll_bar->parentWidget(), scroll_bar->geometry().topLeft()),
                                     scroll_bar->size());
      mask = mask.subtracted(QRegion(scroll_rect_in_row));
    }
    row_widget->setMask(mask);
  }
}

QScrollBar* LayerListWidget::scroll_bar_at_global_position(QPoint global_position) const {
  for (auto* scroll_bar : {verticalScrollBar(), horizontalScrollBar()}) {
    if (scroll_bar == nullptr || !scroll_bar->isVisibleTo(const_cast<LayerListWidget*>(this)) ||
        scroll_bar->maximum() <= scroll_bar->minimum()) {
      continue;
    }
    const QRect global_rect(scroll_bar->mapToGlobal(QPoint(0, 0)), scroll_bar->size());
    if (global_rect.contains(global_position)) {
      return scroll_bar;
    }
  }
  return nullptr;
}

bool LayerListWidget::redirect_mouse_event_to_scroll_bar(QObject* watched, QMouseEvent* event) {
  if (event == nullptr) {
    return false;
  }
  Q_UNUSED(watched);

  auto* scroll_bar = active_redirect_scroll_bar_.data();
  const auto global_position = event->globalPosition().toPoint();
  if (scroll_bar == nullptr) {
    scroll_bar = scroll_bar_at_global_position(global_position);
  }
  if (scroll_bar == nullptr) {
    return false;
  }

  const auto type = event->type();
  if (type == QEvent::MouseButtonPress && event->button() != Qt::LeftButton) {
    return false;
  }
  if (type == QEvent::MouseButtonPress) {
    row_widget_drag_candidate_ = false;
    pending_single_select_on_release_ = false;
    drag_anchor_layer_id_.reset();
    const auto option = scroll_bar_style_option(*scroll_bar);
    const auto local_position = scroll_bar->mapFromGlobal(global_position);
    const auto hit =
        scroll_bar->style()->hitTestComplexControl(QStyle::CC_ScrollBar, &option, local_position, scroll_bar);
    if (hit == QStyle::SC_ScrollBarSlider) {
      active_redirect_scroll_bar_ = scroll_bar;
      scroll_bar_drag_start_global_ = global_position;
      scroll_bar_drag_start_value_ = scroll_bar->value();
      scroll_bar_drag_travel_ = std::max(1, scroll_bar_drag_travel(*scroll_bar, option));
    } else if (hit == QStyle::SC_ScrollBarAddLine) {
      scroll_bar->setValue(std::clamp(scroll_bar->value() + scroll_bar->singleStep(), scroll_bar->minimum(),
                                      scroll_bar->maximum()));
    } else if (hit == QStyle::SC_ScrollBarSubLine) {
      scroll_bar->setValue(std::clamp(scroll_bar->value() - scroll_bar->singleStep(), scroll_bar->minimum(),
                                      scroll_bar->maximum()));
    } else if (hit == QStyle::SC_ScrollBarAddPage) {
      scroll_bar->setValue(std::clamp(scroll_bar->value() + scroll_bar->pageStep(), scroll_bar->minimum(),
                                      scroll_bar->maximum()));
    } else if (hit == QStyle::SC_ScrollBarSubPage) {
      scroll_bar->setValue(std::clamp(scroll_bar->value() - scroll_bar->pageStep(), scroll_bar->minimum(),
                                      scroll_bar->maximum()));
    }
  } else if (active_redirect_scroll_bar_ == nullptr) {
    return false;
  } else if (type == QEvent::MouseMove && (event->buttons() & Qt::LeftButton) != 0) {
    const auto pixel_delta = scroll_bar->orientation() == Qt::Vertical
                                 ? global_position.y() - scroll_bar_drag_start_global_.y()
                                 : global_position.x() - scroll_bar_drag_start_global_.x();
    const auto range = scroll_bar->maximum() - scroll_bar->minimum();
    auto value_delta = static_cast<int>(std::round(static_cast<double>(pixel_delta) * range /
                                                   std::max(1, scroll_bar_drag_travel_)));
    if (scroll_bar->invertedAppearance()) {
      value_delta = -value_delta;
    }
    scroll_bar->setValue(
        std::clamp(scroll_bar_drag_start_value_ + value_delta, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  if (type == QEvent::MouseButtonRelease && event->button() == Qt::LeftButton) {
    active_redirect_scroll_bar_.clear();
    scroll_bar_drag_travel_ = 0;
  }
  event->accept();
  return true;
}

void LayerListWidget::install_scroll_bar_container_event_filters() {
  for (auto* scroll_bar : {verticalScrollBar(), horizontalScrollBar()}) {
    if (scroll_bar == nullptr) {
      continue;
    }
    if (!scroll_bar->property(kLayerScrollBarContainerFilterProperty).toBool()) {
      scroll_bar->setProperty(kLayerScrollBarContainerFilterProperty, true);
      scroll_bar->installEventFilter(this);
    }
    auto* container = scroll_bar->parentWidget();
    if (container == nullptr || container == this || container == viewport() ||
        container->property(kLayerScrollBarContainerFilterProperty).toBool()) {
      continue;
    }
    container->setProperty(kLayerScrollBarContainerFilterProperty, true);
    container->installEventFilter(this);
  }
}

bool LayerListWidget::event(QEvent* event) {
  switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
      if (redirect_mouse_event_to_scroll_bar(this, static_cast<QMouseEvent*>(event))) {
        return true;
      }
      break;
    default:
      break;
  }
  if (event->type() == QEvent::Drop) {
    drop_event_uses_viewport_coordinates_ = false;
    dropEvent(static_cast<QDropEvent*>(event));
    drop_event_uses_viewport_coordinates_ = true;
    return event->isAccepted();
  }
  if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show ||
      event->type() == QEvent::WindowActivate || event->type() == QEvent::ActivationChange) {
    schedule_row_viewport_mask_update();
  }
  return QListWidget::event(event);
}

bool LayerListWidget::eventFilter(QObject* watched, QEvent* event) {
  // This is an application-wide event filter, so it runs before Qt's
  // disabled-widget gate. When the list is disabled (e.g. a preview dialog holds
  // the document edit lock) we must ignore mouse/wheel interaction ourselves --
  // otherwise clicks still drive selection and double-clicks reopen dialogs,
  // defeating the lock and crashing via reentrant list rebuilds.
  if (!isEnabled()) {
    switch (event->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
      case QEvent::MouseButtonRelease:
      case QEvent::Wheel:
        return QListWidget::eventFilter(watched, event);
      default:
        break;
    }
  }
  switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
      if (redirect_mouse_event_to_scroll_bar(watched, static_cast<QMouseEvent*>(event))) {
        return true;
      }
      break;
    default:
      break;
  }
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
        handle_clip_boundary_press(viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos())),
                                   mouse_event->modifiers())) {
      event->accept();
      return true;
    }
    if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
        (mouse_event->modifiers() & Qt::ControlModifier) != 0 &&
        widget->objectName() != QStringLiteral("layerMaskLinkButton")) {
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
               !layer_row_button_owns_clicks(widget->objectName())) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      if (auto* item = itemAt(viewport_pos); item != nullptr) {
        if (const auto target = ctrl_click_target(item, viewport_pos);
            target.has_value() && *target != LayerCtrlClickTarget::ContentThumbnail &&
            thumbnail_click_callback_) {
          thumbnail_click_callback_(item, *target, mouse_event->modifiers());
          event->accept();
          return true;
        }
        select_range_to_item(item);
        event->accept();
        return true;
      }
    } else if (widget != nullptr && mouse_event->button() == Qt::LeftButton &&
               (mouse_event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0 &&
               !layer_row_button_owns_clicks(widget->objectName())) {
      const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
      if (auto* item = itemAt(viewport_pos); item != nullptr) {
        begin_single_drag_item(item);
        if (const auto target = ctrl_click_target(item, viewport_pos); target.has_value() && thumbnail_click_callback_) {
          thumbnail_click_callback_(item, *target, mouse_event->modifiers());
        }
        drag_start_position_ = viewport_pos;
        row_widget_drag_candidate_ = true;
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseButtonDblClick) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && mouse_event->button() == Qt::LeftButton) {
      if (!layer_row_button_owns_clicks(widget->objectName())) {
        const auto viewport_pos = viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos()));
        if (handle_item_double_click(itemAt(viewport_pos), viewport_pos)) {
          event->accept();
          return true;
        }
      }
    }
  } else if (event->type() == QEvent::MouseMove) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && mouse_event->buttons() == Qt::NoButton) {
      update_clip_boundary_cursor(widget, viewport()->mapFromGlobal(widget->mapToGlobal(mouse_event->pos())),
                                  mouse_event->modifiers());
    }
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
    suppress_drag_select_until_release_ = false;
    finish_pending_single_select();
    drag_anchor_layer_id_.reset();
  } else if (event->type() == QEvent::Leave) {
    if (!clip_cursor_widget_.isNull() && watched == clip_cursor_widget_) {
      clear_clip_boundary_cursor();
    }
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
  switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
      if (redirect_mouse_event_to_scroll_bar(viewport(), static_cast<QMouseEvent*>(event))) {
        return true;
      }
      break;
    default:
      break;
  }
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
    if (mouse_event->button() == Qt::LeftButton &&
        handle_clip_boundary_press(mouse_event->pos(), mouse_event->modifiers())) {
      event->accept();
      return true;
    }
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
        if (const auto target = ctrl_click_target(item, mouse_event->pos());
            target.has_value() && *target != LayerCtrlClickTarget::ContentThumbnail &&
            thumbnail_click_callback_) {
          thumbnail_click_callback_(item, *target, mouse_event->modifiers());
          event->accept();
          return true;
        }
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
          thumbnail_click_callback_(item, *target, mouse_event->modifiers());
        }
        drag_start_position_ = mouse_event->pos();
        row_widget_drag_candidate_ = true;
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseButtonDblClick) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (mouse_event->button() == Qt::LeftButton &&
        handle_item_double_click(itemAt(mouse_event->pos()), mouse_event->pos())) {
      event->accept();
      return true;
    }
  } else if (event->type() == QEvent::MouseMove) {
    auto* mouse_event = static_cast<QMouseEvent*>(event);
    if (mouse_event->buttons() == Qt::NoButton) {
      update_clip_boundary_cursor(nullptr, mouse_event->pos(), mouse_event->modifiers());
    }
    if (suppress_drag_select_until_release_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
      event->accept();
      return true;
    }
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
    suppress_drag_select_until_release_ = false;
    finish_pending_single_select();
    drag_anchor_layer_id_.reset();
  } else if (event->type() == QEvent::Leave) {
    if ((QApplication::mouseButtons() & Qt::LeftButton) == 0) {
      row_widget_drag_candidate_ = false;
      suppress_drag_select_until_release_ = false;
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
  const auto global_pos = viewport()->mapToGlobal(viewport_pos);
  const auto contains_global_point = [global_pos](const QWidget* widget) {
    return widget != nullptr && widget->isVisible() &&
           widget->rect().contains(widget->mapFromGlobal(global_pos));
  };
  if (auto* content_thumbnail = row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail"));
      contains_global_point(content_thumbnail)) {
    return LayerCtrlClickTarget::ContentThumbnail;
  }
  if (auto* mask_thumbnail = row->findChild<QWidget*>(QStringLiteral("layerMaskThumbnail"));
      contains_global_point(mask_thumbnail)) {
    return LayerCtrlClickTarget::MaskThumbnail;
  }
  if (auto* vector_mask_thumbnail =
          row->findChild<QWidget*>(QStringLiteral("layerVectorMaskThumbnail"));
      contains_global_point(vector_mask_thumbnail)) {
    return LayerCtrlClickTarget::VectorMaskThumbnail;
  }
  if (auto* smart_filter_mask_thumbnail =
          row->findChild<QWidget*>(QStringLiteral("layerSmartFilterMaskThumbnail"));
      contains_global_point(smart_filter_mask_thumbnail)) {
    return LayerCtrlClickTarget::SmartFilterMaskThumbnail;
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
    set_current_item_preserving_scroll(item, QItemSelectionModel::NoUpdate);
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
  const auto vertical_scroll_value = verticalScrollBar() != nullptr ? verticalScrollBar()->value() : 0;
  const auto horizontal_scroll_value = horizontalScrollBar() != nullptr ? horizontalScrollBar()->value() : 0;
  selectionModel()->setCurrentIndex(target_index, QItemSelectionModel::NoUpdate);
  selectionModel()->select(range, QItemSelectionModel::ClearAndSelect);
  if (auto* scroll_bar = verticalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(vertical_scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  if (auto* scroll_bar = horizontalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(horizontal_scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
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
  set_current_item_preserving_scroll(item, QItemSelectionModel::NoUpdate);
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
  set_current_item_preserving_scroll(item, QItemSelectionModel::ClearAndSelect);
  viewport()->setUpdatesEnabled(viewport_updates_enabled);
  setUpdatesEnabled(list_updates_enabled);
  viewport()->update();
}

void LayerListWidget::set_current_item_preserving_scroll(QListWidgetItem* item,
                                                         QItemSelectionModel::SelectionFlags command) {
  if (item == nullptr) {
    return;
  }
  const auto vertical_scroll_value = verticalScrollBar() != nullptr ? verticalScrollBar()->value() : 0;
  const auto horizontal_scroll_value = horizontalScrollBar() != nullptr ? horizontalScrollBar()->value() : 0;
  setCurrentItem(item, command);
  if (auto* scroll_bar = verticalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(vertical_scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
  if (auto* scroll_bar = horizontalScrollBar(); scroll_bar != nullptr) {
    scroll_bar->setValue(std::clamp(horizontal_scroll_value, scroll_bar->minimum(), scroll_bar->maximum()));
  }
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
  if (drag_blocked_) {
    // Both row-widget eventFilter paths call startDrag directly, so this gate
    // covers every initiation path, not just Qt's dragEnabled one.
    drag_anchor_layer_id_.reset();
    dragged_layer_ids_.clear();
    pending_single_select_on_release_ = false;
    suppress_drag_select_until_release_ = true;
    if (drag_blocked_callback_) {
      drag_blocked_callback_();
    }
    return;
  }
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
  if (drag_blocked_) {
    event->ignore();
    return;
  }
  keep_drag_anchor_selected();
  update_drop_preview(event->position().toPoint());
  update_auto_scroll(event->position().toPoint());
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void LayerListWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (drag_blocked_) {
    event->ignore();
    return;
  }
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
  if (drag_blocked_) {
    // The empty-ids fallback below reorders from the selection, so an external
    // drop while filtered must be refused outright.
    event->ignore();
    return;
  }
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

bool LayerListWidget::handle_item_double_click(QListWidgetItem* item, QPoint viewport_pos) {
  if (item == nullptr || !item_double_click_callback_) {
    return false;
  }

  row_widget_drag_candidate_ = false;
  pending_single_select_on_release_ = false;
  drag_anchor_layer_id_.reset();
  if (currentItem() != item || !item->isSelected()) {
    set_current_item_preserving_scroll(item, QItemSelectionModel::ClearAndSelect);
  }
  // A double-click inside a Smart Filter entry row edits that filter's
  // settings (the Photoshop behavior) instead of the layer's styles.
  if (smart_filter_double_click_callback_) {
    if (auto* row = itemWidget(item); row != nullptr) {
      const auto global_pos = viewport()->mapToGlobal(viewport_pos);
      for (auto* entry_row :
           row->findChildren<QWidget*>(QStringLiteral("layerSmartFilterEntryRow"))) {
        if (!entry_row->isVisible() ||
            !entry_row->rect().contains(entry_row->mapFromGlobal(global_pos))) {
          continue;
        }
        const auto execution_index = entry_row->property("smartFilterExecutionIndex");
        if (execution_index.isValid()) {
          smart_filter_double_click_callback_(
              item, static_cast<std::size_t>(execution_index.toULongLong()));
          return true;
        }
      }
    }
  }
  item_double_click_callback_(item);
  return true;
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

void LayerListWidget::resizeEvent(QResizeEvent* event) {
  QListWidget::resizeEvent(event);
  refresh_row_widths();
  schedule_row_viewport_mask_update();
}

void LayerListWidget::scrollContentsBy(int dx, int dy) {
  QListWidget::scrollContentsBy(dx, dy);
  update_row_viewport_masks();
  schedule_row_viewport_mask_update();
}

}  // namespace patchy::ui
