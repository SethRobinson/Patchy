// CanvasWidget's Move-tool machinery, split out of canvas_widget.cpp:
// movable-layer enumeration, the move hover outline, the moving-layer
// outline/bounds/dirty-rect/dirty-region helpers with the outline-preview
// policy, and move_active_layer_by. Pure function moves from
// canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QThread>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QTabletEvent>
#include <QTimerEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QtGlobal>

#if defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)
#include <QtCore/private/qthread_p.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

bool move_layer_has_expensive_style(const Layer& layer) {
  const auto& style = layer.layer_style();
  return style.effects_visible && !style.empty();
}

constexpr std::int64_t kMoveOutlineDirtyAreaThreshold = 4'000'000;
constexpr std::int64_t kStyledMoveOutlineDirtyAreaThreshold = 1'000'000;
// The proxy snapshot is downscaled to at most this many pixels (mirrors
// kTransformProxyMaxPixels) and refused outright above the last-resort cap,
// where even the one-time snapshot render would hitch for seconds; such drags
// keep the dashed-outline fallback.
constexpr std::int64_t kMoveProxyMaxPixels = 4'000'000;
constexpr std::int64_t kMoveProxyLastResortSnapshotArea = 80'000'000;

// The compositor clips every render to the canvas, so a moving set that hangs
// off it used to snapshot without its off-canvas content, and a set lying
// entirely on the pasteboard could not snapshot at all (the drag then kept the
// dashed outline even once it reached the canvas). Returns the translation
// that brings `effect_union` as far onto the canvas as it fits; the snapshot
// renders there through bounds overrides and the proxy rect maps back. The
// translation is a whole number of 2^level blocks so the preview-scaled copies
// shift by an exact scaled amount.
QPoint move_proxy_snapshot_shift(QRect effect_union, QRect canvas_rect, int level) {
  const int block = 1 << std::max(0, level);
  const auto axis_shift = [block](int low, int size, int canvas_size) {
    const auto slack = canvas_size - size;
    const auto target = std::clamp(low, std::min(0, slack), std::max(0, slack));
    const auto shift = target - low;
    // Round away from zero: undershooting would leave the far edge clipped.
    return shift >= 0 ? (shift + block - 1) / block * block : -((-shift + block - 1) / block * block);
  };
  return QPoint(axis_shift(effect_union.x(), effect_union.width(), canvas_rect.width()),
                axis_shift(effect_union.y(), effect_union.height(), canvas_rect.height()));
}

// Bounds overrides that render the moving set translated by `shift`. `source`
// is the document the snapshot renders from: the real one, or the
// preview-scaled copy at `level`, whose layers shift by the scaled amount.
std::vector<std::pair<LayerId, Rect>> move_proxy_shifted_bounds(const Document& source,
                                                                const std::vector<LayerId>& moving_ids, QPoint shift,
                                                                int level) {
  const int block = 1 << std::max(0, level);
  std::vector<std::pair<LayerId, Rect>> bounds;
  bounds.reserve(moving_ids.size());
  for (const auto id : moving_ids) {
    if (const auto* layer = source.find_layer(id)) {
      auto shifted = layer->bounds();
      shifted.x += shift.x() / block;
      shifted.y += shift.y() / block;
      bounds.emplace_back(id, shifted);
    }
  }
  return bounds;
}

// Nesting depth of the running event loops. Qt's single-threaded wasm build declares
// QThread::loopLevel() but never defines it (the Safari variant fails to link), so it reads
// the same counter from the thread data; every wasm build links Qt6::CorePrivate.
int current_event_loop_level() {
#if defined(Q_OS_WASM) && !defined(__EMSCRIPTEN_PTHREADS__)
  return QThreadData::current()->loopLevel;
#else
  return QThread::currentThread()->loopLevel();
#endif
}

}  // namespace

void CanvasWidget::close_canvas_context_menu() {
  context_press_pos_.reset();
  if (canvas_context_menu_) {
    auto* menu = canvas_context_menu_.data();
    // Clear first: the move-layer entries key their staleness on this pointer. The hide
    // retires the menu; nothing here deletes it (see show_canvas_context_menu).
    canvas_context_menu_.clear();
    menu->close();
    retire_canvas_context_menu(menu);
  }
}

void CanvasWidget::retire_canvas_context_menu(QMenu* menu) {
  if (menu == nullptr) {
    return;
  }
  for (const auto& retired : retired_context_menus_) {
    if (retired.menu == menu) {
      return;
    }
  }
  retired_context_menus_.push_back(RetiredContextMenu{menu, current_event_loop_level()});
}

void CanvasWidget::reap_retired_context_menus() {
  // A retired menu whose pick opened a dialog can still be mid-dispatch while a nested
  // event loop runs above it. Below or at the loop level it hid in, that dispatch has
  // returned (a deeper loop would have to be running for it to be live), so deleting is
  // safe; anything deeper waits for the next reap or for QMenu::triggered.
  const auto level = current_event_loop_level();
  std::erase_if(retired_context_menus_, [level](const RetiredContextMenu& retired) {
    if (retired.menu.isNull()) {
      return true;
    }
    if (level <= retired.loop_level) {
      retired.menu->deleteLater();
      return true;
    }
    return false;
  });
}

// The canvas right-click menu (a right release within the drag distance of
// its press; canvas_widget_events.cpp). One builder, three sections: the Move
// tool's layers-under-the-pointer entries, the host's selection commands
// (Remove Object, Fill, ...) when the click landed on the selection, and the
// host's shape commands (Shape Appearance, Free Transform, ...) when it landed
// on the active vector shape layer, or with the Move tool its layer commands
// (Free Transform) when it landed inside the Move outline of any other active
// leaf layer whose position is not locked. Path tools keep their own menu. A popup,
// not exec: the entries revalidate their target when picked, so a stale menu
// after a tool or document change is inert.
//
// Lifetime (September 2026 crash): the menu must outlive the dispatch of the click that
// picked an entry. Qt's window-level mouse handler keeps using the popup's window object
// after the entry's handler returns, and a handler that runs a modal dialog (Stroke
// Selection) nests an event loop inside that dispatch, so a deleteLater posted from
// inside it destroyed the menu under Qt's feet. Hiding therefore only retires the menu;
// QMenu::triggered, which fires after the handler returns and still inside the dispatch,
// posts the deferred delete Qt processes once that dispatch has unwound, and dismissed
// menus are reaped by the next menu at the same loop level.
bool CanvasWidget::show_canvas_context_menu(QPoint widget_point, QPoint global_position) {
  close_canvas_context_menu();
  reap_retired_context_menus();
  if (document_ == nullptr || pointer_gesture_active() || transforming_layer_ || warping_layer_ ||
      path_transform_active_) {
    return false;
  }
  if (path_edit_tool_active() && show_path_context_menu(QPointF(widget_point), global_position)) {
    return true;
  }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("canvasContextMenu"));
  canvas_context_menu_ = menu;
  connect(menu, &QMenu::aboutToHide, this, [this, menu] { retire_canvas_context_menu(menu); });
  connect(menu, &QMenu::triggered, this, [this, menu](QAction*) {
    std::erase_if(retired_context_menus_,
                  [menu](const RetiredContextMenu& retired) { return retired.menu == menu; });
    menu->deleteLater();
  });
  add_move_layer_menu_entries(*menu, widget_point);
  const auto document_point = document_position(widget_point);
  // The host's QActions, so hotkeys and enable state stay in step; nullptr is
  // a separator. A section with no real action adds nothing.
  const auto append_section = [menu](const QList<QAction*>& actions) {
    bool any_action = false;
    for (auto* action : actions) {
      any_action = any_action || action != nullptr;
    }
    if (!any_action) {
      return;
    }
    if (!menu->isEmpty()) {
      menu->addSeparator();
    }
    for (auto* action : actions) {
      if (action == nullptr) {
        menu->addSeparator();
      } else {
        menu->addAction(action);
      }
    }
  };
  if (has_selection() && selection_alpha_at(document_point) != 0U && selection_context_actions_callback_) {
    append_section(selection_context_actions_callback_());
  }
  if (!edit_locked_) {
    const auto& document = std::as_const(*document_);
    const auto active_id = document.active_layer_id();
    const auto* active = active_id.has_value() ? document.find_layer(*active_id) : nullptr;
    const bool is_shape = active != nullptr && layer_is_vector_shape(*active);
    if (is_shape && shape_context_actions_callback_ &&
        active->bounds().contains(document_point.x(), document_point.y())) {
      append_section(shape_context_actions_callback_());
    } else if (!is_shape && active != nullptr && layer_context_actions_callback_ && tool_ == CanvasTool::Move &&
               active->kind() != LayerKind::Group && !layer_effectively_locks_position(*active) &&
               move_layer_rect_contains_document_point(*active, document_point)) {
      append_section(layer_context_actions_callback_());
    }
  }
  if (menu->isEmpty()) {
    canvas_context_menu_.clear();
    menu->deleteLater();
    return false;
  }
  menu->popup(global_position);
  return true;
}

bool CanvasWidget::add_move_layer_menu_entries(QMenu& menu, QPoint widget_point) {
  if (document_ == nullptr || tool_ != CanvasTool::Move || edit_locked_ || pointer_gesture_active() ||
      transforming_layer_ || warping_layer_ || path_transform_active_) {
    return false;
  }
  const auto point = document_position(widget_point);
  if (!document_contains(point)) {
    return false;
  }

  // Walk the whole stack once, including occluded leaves and collapsed folders.
  // Locks prevent moving a layer, but must not prevent explicitly selecting it.
  std::vector<std::pair<LayerId, QString>> matches;
  const auto collect = [&](const auto& self, const std::vector<Layer>& layers, const QString& prefix) -> void {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      if (!layer.visible() || layer.opacity() <= 0.0F) {
        continue;
      }
      const auto name = prefix + QString::fromStdString(layer.name());
      if (layer.kind() == LayerKind::Group) {
        if (layer_mask_alpha_at(layer, point.x(), point.y()) >= 8.0F / 255.0F) {
          self(self, layer.children(), name + QStringLiteral(" / "));
        }
      } else if (layer_is_text(layer) ? layer.bounds().contains(point.x(), point.y())
                                     : pixel_layer_contains_document_point(layer, point, true)) {
        matches.emplace_back(layer.id(), name);
      }
    }
  };
  collect(collect, std::as_const(*document_).layers(), QString());
  if (matches.empty()) {
    return false;
  }

  auto* menu_ptr = &menu;
  const auto select = [this, menu_ptr, source_document = document_](std::vector<LayerId> ids, LayerId active) {
    if (canvas_context_menu_ != menu_ptr || document_ != source_document || !isVisible() ||
        tool_ != CanvasTool::Move || edit_locked_ || pointer_gesture_active() ||
        transforming_layer_ || warping_layer_ || path_transform_active_) {
      return;
    }
    // The document can change while a popup is open. Validate the leaf IDs in
    // one tree walk so Select All does not search the document for every hit.
    const auto& document = std::as_const(*document_);
    if (root_drop_layer_ids(document.layers(), ids).size() != ids.size()) {
      return;
    }
    request_layer_selection(std::move(ids), active);
  };
  std::vector<LayerId> ids;
  QSet<LayerId> selected(selected_layer_ids_.begin(), selected_layer_ids_.end());
  if (selected.isEmpty() && document_->active_layer_id().has_value()) {
    selected.insert(*document_->active_layer_id());
  }
  for (const auto& [id, name] : matches) {
    auto label = name;
    label.replace(QStringLiteral("&"), QStringLiteral("&&"));
    auto* action = menu.addAction(label);
    action->setData(QVariant::fromValue<qulonglong>(id));
    action->setCheckable(true);
    action->setChecked(selected.contains(id));
    connect(action, &QAction::triggered, menu_ptr, [select, id] { select({id}, id); });
    ids.push_back(id);
  }
  if (ids.size() > 1U) {
    menu.addSeparator();
    auto* action = menu.addAction(tr("Select All Layers Here"));
    action->setObjectName(QStringLiteral("moveMenuSelectAllLayersAction"));
    connect(action, &QAction::triggered, menu_ptr, [select, ids] { select(ids, ids.front()); });
  }
  return true;
}

void CanvasWidget::begin_move_layer_selection(QMouseEvent* event, const Layer* clicked_layer,
                                             bool rectangle_allowed) {
  event->accept();
  if (document_ == nullptr || event->button() != Qt::LeftButton) {
    return;
  }
  MoveLayerSelectionGesture gesture;
  gesture.press_widget = event->pos();
  gesture.anchor_document = document_position_f(event->position());
  gesture.current_document = gesture.anchor_document;
  gesture.selected_ids = selected_layer_ids_;
  gesture.active_id = document_->active_layer_id();
  if (gesture.selected_ids.empty() && gesture.active_id.has_value()) {
    gesture.selected_ids.push_back(*gesture.active_id);
  }
  if (clicked_layer != nullptr) {
    gesture.clicked_id = clicked_layer->id();
  }
  gesture.rectangle_allowed = rectangle_allowed;
  gesture.additive = event->modifiers().testFlag(Qt::ShiftModifier);
  move_layer_selection_gesture_ = std::move(gesture);
  clear_move_hover_outline();
}

QRect CanvasWidget::move_layer_selection_widget_rect() const {
  if (!move_layer_selection_gesture_ || !move_layer_selection_gesture_->dragging_rectangle) {
    return {};
  }
  const auto& gesture = *move_layer_selection_gesture_;
  return QRectF(widget_position_f(gesture.anchor_document), widget_position_f(gesture.current_document))
      .normalized().toAlignedRect().adjusted(-2, -2, 2, 2);
}

bool CanvasWidget::update_move_layer_selection(QMouseEvent* event) {
  if (!move_layer_selection_gesture_) {
    return false;
  }
  auto& gesture = *move_layer_selection_gesture_;
  if (!gesture.dragging_rectangle &&
      (event->pos() - gesture.press_widget).manhattanLength() < QApplication::startDragDistance()) {
    return true;
  }
  if (gesture.rectangle_allowed) {
    const auto old_rect = move_layer_selection_widget_rect();
    gesture.dragging_rectangle = true;
    gesture.current_document = document_position_f(event->position());
    // Only the rectangle changes while dragging; bounds collection and the
    // panel round-trip happen once, on release.
    update(old_rect.united(move_layer_selection_widget_rect()));
    return true;
  }

  // Shift-drag adds an unselected target; a plain drag keeps the selected set.
  // Once promoted to a move, later Ctrl changes cannot turn it into a box.
  auto pending = std::move(*move_layer_selection_gesture_);
  move_layer_selection_gesture_.reset();
  if (pending.additive && pending.clicked_id.has_value() &&
      std::find(pending.selected_ids.begin(), pending.selected_ids.end(), *pending.clicked_id) ==
          pending.selected_ids.end()) {
    pending.selected_ids.push_back(*pending.clicked_id);
    request_layer_selection(std::move(pending.selected_ids), *pending.clicked_id);
  }
  const auto ids = movable_layer_ids();
  if (ids.empty()) {
    return true;
  }
  begin_move_drag(ids, document_position(pending.press_widget), pending.press_widget);
  return false;
}

void CanvasWidget::finish_move_layer_selection(QMouseEvent* event) {
  auto gesture = std::move(*move_layer_selection_gesture_);
  move_layer_selection_gesture_.reset();
  update();
  if (document_ == nullptr) {
    return;
  }
  const auto rectangle = gesture.rectangle_allowed &&
      (gesture.dragging_rectangle ||
       (event->pos() - gesture.press_widget).manhattanLength() >= QApplication::startDragDistance());
  auto ids = gesture.selected_ids;
  auto active = gesture.active_id;
  if (rectangle) {
    const auto box = QRectF(gesture.anchor_document, document_position_f(event->position())).normalized()
                         .intersected(QRectF(0, 0, document_->width(), document_->height()));
    if (box.isEmpty()) {
      // A rectangle drawn entirely on the pasteboard keeps the selection:
      // matching is clipped to the document, so the box may well enclose
      // off-canvas artwork this pass cannot see. Only in-document empty
      // rectangles and empty clicks deselect (below).
      return;
    }
    std::vector<LayerId> matches;
    const auto collect = [&](const auto& self, const std::vector<Layer>& layers,
                             LayerLockFlags ancestor_flags) -> void {
      for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        const auto& layer = *it;
        const auto flags = ancestor_flags | patchy::layer_lock_flags(layer);
        if (!layer.visible() || layer.opacity() <= 0.0F || (flags & kLayerLockPosition) != kLayerLockNone) {
          continue;
        }
        if (layer.kind() == LayerKind::Group) {
          self(self, layer.children(), flags);
        } else if (const auto bounds = move_layer_outline_bounds(layer); bounds.has_value() &&
                   box.intersects(QRectF(bounds->x, bounds->y, bounds->width, bounds->height))) {
          matches.push_back(layer.id());
        }
      }
    };
    collect(collect, std::as_const(*document_).layers(), kLayerLockNone);
    if (matches.empty()) {
      if (!gesture.additive) {
        request_layer_deselection();
      }
      return;
    }
    if (!gesture.additive) {
      ids = matches;
    } else {
      QSet<LayerId> selected(ids.begin(), ids.end());
      for (const auto id : matches) {
        if (!selected.contains(id)) {
          ids.push_back(id);
          selected.insert(id);
        }
      }
    }
    if (!active.has_value() || std::find(ids.begin(), ids.end(), *active) == ids.end()) {
      active = matches.front();
    }
  } else if (gesture.clicked_id.has_value() && !gesture.rectangle_allowed && !gesture.additive) {
    // This was a plain click on a selected member, not a modifier toggle.
    ids = {*gesture.clicked_id};
    active = gesture.clicked_id;
  } else if (gesture.clicked_id.has_value()) {
    const auto found = std::find(ids.begin(), ids.end(), *gesture.clicked_id);
    if (found == ids.end()) {
      ids.push_back(*gesture.clicked_id);
      active = gesture.clicked_id;
    } else {
      if (ids.size() <= 1U) {
        return;
      }
      ids.erase(found);
      if (!active.has_value() || std::find(ids.begin(), ids.end(), *active) == ids.end()) {
        active = ids.front();
      }
    }
  } else {
    // A plain click on empty space (inside the document or on the pasteboard)
    // that never became a rectangle: deselect every layer. Shift keeps it.
    if (!gesture.additive) {
      request_layer_deselection();
    }
    return;
  }
  request_layer_selection(std::move(ids), *active);
}

void CanvasWidget::cancel_move_layer_selection() {
  if (!move_layer_selection_gesture_) {
    return;
  }
  const auto dirty = move_layer_selection_widget_rect();
  move_layer_selection_gesture_.reset();
  if (!dirty.isEmpty()) {
    update(dirty);
  }
}

void CanvasWidget::draw_move_layer_selection(QPainter& painter) const {
  if (!move_layer_selection_gesture_ || !move_layer_selection_gesture_->dragging_rectangle) {
    return;
  }
  const auto& gesture = *move_layer_selection_gesture_;
  painter.save();
  QPen pen(theme().canvas_layer_selection_border, 1.0);
  pen.setCosmetic(true);
  painter.setPen(pen);
  painter.setBrush(theme().canvas_layer_selection_fill);
  painter.drawRect(QRectF(widget_position_f(gesture.anchor_document),
                         widget_position_f(gesture.current_document)).normalized());
  painter.restore();
}

void CanvasWidget::begin_move_drag(const std::vector<LayerId>& layer_ids, QPoint document_point,
                                   QPoint widget_point) {
  cancel_move_preview();
  move_drag_pending_ = true;
  moving_layer_ = false;
  move_start_ = document_point;
  begin_axis_constrained_stroke(QPointF(move_start_));
  move_press_widget_position_ = widget_point;
  move_preview_delta_ = QPoint();
  moving_layers_.clear();
  moving_layers_use_outline_preview_ = false;
  move_external_change_during_drag_ = false;
  // Reuse the retained base/proxy when this press re-drags exactly the
  // retained selection at the composite level they were built at: the
  // commit translated the proxy rect and the base never contained the
  // moving set, so both are current.
  auto sorted_press_ids = layer_ids;
  std::sort(sorted_press_ids.begin(), sorted_press_ids.end());
  if (!retained_move_ids_.empty() && sorted_press_ids == retained_move_ids_ &&
      preview_composite_level_for_zoom(zoom_) == retained_move_composite_level_ && !move_base_cache_.isNull()) {
    // Counted only when the press becomes a real drag (the caches build
    // lazily at the first move, so a plain click skips nothing).
    move_press_reused_retained_caches_ = true;
    move_drag_uses_proxy_preview_ = false;
  } else {
    move_press_reused_retained_caches_ = false;
    clear_retained_move_caches();
  }
  moving_layers_.reserve(layer_ids.size());
  // Selected groups were flattened to leaves above, so a style on the folder
  // itself is invisible to the per-leaf check: fold every styled ancestor's
  // expense and padding back into each leaf's entry.
  const auto ancestor_style_info = collect_ancestor_group_style_info(std::as_const(*document_).layers());
  for (const auto id : layer_ids) {
    auto* layer = document_->find_layer(id);
    if (layer != nullptr) {
      auto expensive_style = move_layer_has_expensive_style(*layer);
      int ancestor_effect_padding = 0;
      if (const auto found = ancestor_style_info.find(id); found != ancestor_style_info.end()) {
        expensive_style = expensive_style || found->second.styled;
        ancestor_effect_padding = found->second.effect_padding;
      }
      moving_layers_.push_back(MovingLayer{id, layer->bounds(), move_layer_outline_bounds(*layer), expensive_style,
                                           ancestor_effect_padding});
    }
  }
  // The slow-frame latch persists across drags of the same moving set at the
  // same composite level; a different set or level re-prices from a live
  // frame. (Must run after the moving_layers_ build: the key hashes it.)
  if (const auto latch_key = move_live_latch_key(); latch_key != move_live_latch_key_) {
    move_live_frame_slow_ = false;
    move_live_latch_key_ = latch_key;
  }
  move_preview_patches_.clear();
  move_preview_patches_delta_.reset();
  move_preview_patches_scale_level_ = 0;
}

void CanvasWidget::collect_movable_leaf_ids(const Layer& root, LayerLockFlags ancestor_flags,
                                           std::vector<LayerId>& ids) const {
  const auto effective_flags = ancestor_flags | patchy::layer_lock_flags(root);
  if (root.kind() == LayerKind::Group) {
    for (const auto& child : root.children()) {
      collect_movable_leaf_ids(child, effective_flags, ids);
    }
    return;
  }
  if (std::find(ids.begin(), ids.end(), root.id()) != ids.end()) {
    return;
  }
  if (((effective_flags | patchy::layer_effective_lock_flags(document_->layers(), root.id())) &
       kLayerLockPosition) != kLayerLockNone) {
    return;
  }
  if (!layer_has_movable_pixels(root)) {
    return;
  }
  ids.push_back(root.id());
}

std::vector<LayerId> CanvasWidget::alignment_root_ids(const std::vector<LayerId>& root_ids) const {
  if (document_ == nullptr || layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    return {};
  }
  const auto& layers = std::as_const(*document_).layers();
  if (!root_ids.empty()) {
    return root_drop_layer_ids(layers, root_ids);
  }
  if (!selected_layer_ids_.empty()) {
    auto roots = root_drop_layer_ids(layers, selected_layer_ids_);
    if (!roots.empty()) {
      return roots;
    }
  }
  if (const auto active = std::as_const(*document_).active_layer_id(); active.has_value()) {
    return {*active};
  }
  return {};
}

std::vector<LayerId> CanvasWidget::movable_layer_ids() const {
  std::vector<LayerId> ids;
  if (document_ == nullptr || layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    return ids;
  }
  const auto add_movable_by_id = [&](LayerId id) {
    if (const auto* layer = std::as_const(*document_).find_layer(id); layer != nullptr) {
      collect_movable_leaf_ids(*layer, kLayerLockNone, ids);
    }
  };
  if (!selected_layer_ids_.empty()) {
    for (const auto id : root_drop_layer_ids(document_->layers(), selected_layer_ids_)) {
      add_movable_by_id(id);
    }
  }
  if (ids.empty()) {
    if (const auto active = document_->active_layer_id(); active.has_value()) {
      add_movable_by_id(*active);
    }
  }
  return ids;
}

namespace {

// A leaf's Align rect: the Move rect (opaque raster extent or text frame), else
// the render bounds, so 16-bit and other Move-rect-less layers still count.
std::optional<Rect> alignment_leaf_rect(const Layer& layer) {
  auto rect = move_layer_outline_bounds(layer);
  if (rect.has_value() && !rect->empty()) {
    return rect;
  }
  const auto render = layer_render_bounds(layer);
  if (render.empty()) {
    return std::nullopt;
  }
  return render;
}

// Per-leaf deltas for a set of units (every leaf of a unit moves by the unit's
// delta), dropping the units that already sit where they belong.
std::vector<std::pair<LayerId, QPoint>> alignment_layer_deltas(const std::vector<CanvasWidget::AlignmentUnit>& units,
                                                               const std::vector<AlignmentOffset>& offsets) {
  std::vector<std::pair<LayerId, QPoint>> deltas;
  for (std::size_t i = 0; i < units.size() && i < offsets.size(); ++i) {
    if (offsets[i].is_zero()) {
      continue;
    }
    for (const auto leaf_id : units[i].leaf_ids) {
      deltas.emplace_back(leaf_id, QPoint(offsets[i].dx, offsets[i].dy));
    }
  }
  return deltas;
}

}  // namespace

std::vector<CanvasWidget::AlignmentUnit> CanvasWidget::alignment_units(const std::vector<LayerId>& root_ids) const {
  std::vector<AlignmentUnit> units;
  if (document_ == nullptr) {
    return units;
  }
  const auto& document = std::as_const(*document_);
  for (const auto root_id : alignment_root_ids(root_ids)) {
    const auto* root = document.find_layer(root_id);
    if (root == nullptr) {
      continue;
    }
    AlignmentUnit unit;
    unit.root = root_id;
    collect_movable_leaf_ids(*root, kLayerLockNone, unit.leaf_ids);
    std::optional<Rect> bounds;
    for (const auto leaf_id : unit.leaf_ids) {
      const auto* leaf = document.find_layer(leaf_id);
      if (leaf == nullptr) {
        continue;
      }
      if (const auto rect = alignment_leaf_rect(*leaf); rect.has_value()) {
        bounds = bounds.has_value() ? unite_rect(*bounds, *rect) : *rect;
      }
    }
    if (unit.leaf_ids.empty() || !bounds.has_value() || bounds->empty()) {
      continue;
    }
    unit.bounds = *bounds;
    units.push_back(std::move(unit));
  }
  return units;
}

int CanvasWidget::alignment_unit_count(const std::vector<LayerId>& root_ids) const {
  if (document_ == nullptr) {
    return 0;
  }
  const auto& document = std::as_const(*document_);
  int count = 0;
  for (const auto root_id : alignment_root_ids(root_ids)) {
    const auto* root = document.find_layer(root_id);
    if (root == nullptr) {
      continue;
    }
    std::vector<LayerId> leaf_ids;
    collect_movable_leaf_ids(*root, kLayerLockNone, leaf_ids);
    if (!leaf_ids.empty()) {
      ++count;
    }
  }
  return count;
}

CanvasWidget::LayerAlignmentResult CanvasWidget::align_layers(AlignEdge edge, bool align_to_canvas,
                                                              const std::vector<LayerId>& root_ids,
                                                              bool record_history) {
  LayerAlignmentResult result;
  if (document_ == nullptr) {
    return result;
  }
  const auto units = alignment_units(root_ids);
  result.unit_count = static_cast<int>(units.size());
  if (units.empty()) {
    return result;
  }
  // Photoshop's Align To rule: an active selection is the reference unless the
  // user asked for the canvas; a lone unit has nothing else to line up with,
  // so it aligns to the canvas; otherwise the units align among themselves.
  Rect reference;
  if (!align_to_canvas && !selection_.isEmpty()) {
    reference = to_core_rect(selection_.boundingRect());
  } else if (align_to_canvas || units.size() == 1U) {
    reference = Rect::from_size(document_->width(), document_->height());
  } else {
    for (const auto& unit : units) {
      reference = reference.empty() ? unit.bounds : unite_rect(reference, unit.bounds);
    }
  }
  std::vector<Rect> rects;
  rects.reserve(units.size());
  for (const auto& unit : units) {
    rects.push_back(unit.bounds);
  }
  const auto deltas = alignment_layer_deltas(units, compute_align_deltas(rects, reference, edge));
  result.moved_layers = static_cast<int>(deltas.size());
  if (!deltas.empty()) {
    result.dirty = offset_layers(deltas, tr("Align Layers"), record_history);
  }
  return result;
}

CanvasWidget::LayerAlignmentResult CanvasWidget::distribute_layers(DistributeMode mode,
                                                                   const std::vector<LayerId>& root_ids,
                                                                   bool record_history) {
  LayerAlignmentResult result;
  if (document_ == nullptr) {
    return result;
  }
  const auto units = alignment_units(root_ids);
  result.unit_count = static_cast<int>(units.size());
  if (units.size() < 3U) {
    return result;
  }
  std::vector<Rect> rects;
  rects.reserve(units.size());
  for (const auto& unit : units) {
    rects.push_back(unit.bounds);
  }
  const auto deltas = alignment_layer_deltas(units, compute_distribute_deltas(rects, mode));
  result.moved_layers = static_cast<int>(deltas.size());
  if (!deltas.empty()) {
    result.dirty = offset_layers(deltas, tr("Distribute Layers"), record_history);
  }
  return result;
}

std::optional<QRect> CanvasWidget::move_hover_outline_rect_at(QPoint widget_position,
                                                              Qt::KeyboardModifiers modifiers) const {
  if (document_ == nullptr || tool_ != CanvasTool::Move || move_layer_selection_gesture_ ||
      moving_layer_ || transforming_layer_ || dragging_transform_ ||
      panning_ || dragging_guide_ || creating_guide_ || widget_position_in_ruler(widget_position)) {
    return std::nullopt;
  }

  const auto guide_drag_allowed = tool_ == CanvasTool::Move || modifiers.testFlag(Qt::ControlModifier);
  if (guide_drag_allowed && !guides_locked_ && guide_at_widget_position(widget_position) >= 0) {
    return std::nullopt;
  }

  const auto document_point = document_position(widget_position);
  if (!document_contains(document_point)) {
    return std::nullopt;
  }

  auto* hit_layer = topmost_move_layer_at(document_point, true);
  if (hit_layer == nullptr) {
    return std::nullopt;
  }

  const auto selected_move_layer_ids = movable_layer_ids();
  if (!auto_select_layer_) {
    if (std::find(selected_move_layer_ids.begin(), selected_move_layer_ids.end(), hit_layer->id()) ==
        selected_move_layer_ids.end()) {
      return std::nullopt;
    }
  }
  if (show_transform_controls_ && auto_select_layer_) {
    if (!selected_layer_ids_.empty()) {
      if (selected_layer_ids_.size() == 1U && selected_layer_ids_.front() == hit_layer->id()) {
        return std::nullopt;
      }
    } else if (const auto active = document_->active_layer_id(); active.has_value() && *active == hit_layer->id()) {
      return std::nullopt;
    }
  }

  const auto bounds = move_layer_outline_bounds(*hit_layer);
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  const QRect outline(bounds->x, bounds->y, bounds->width, bounds->height);
  if (outline.isEmpty()) {
    return std::nullopt;
  }
  return outline;
}

void CanvasWidget::update_move_hover_outline(QPoint widget_position, Qt::KeyboardModifiers modifiers) {
  const auto next = move_hover_outline_rect_at(widget_position, modifiers);
  if (move_hover_outline_rect_ == next) {
    return;
  }

  QRect dirty;
  if (move_hover_outline_rect_.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*move_hover_outline_rect_));
  }
  if (next.has_value()) {
    dirty = dirty.united(widget_rect_for_document_rect(*next));
  }
  move_hover_outline_rect_ = next;

  if (!dirty.isEmpty()) {
    update(dirty);
  } else {
    update();
  }
}

void CanvasWidget::clear_move_hover_outline() {
  if (!move_hover_outline_rect_.has_value()) {
    return;
  }

  const auto dirty = widget_rect_for_document_rect(*move_hover_outline_rect_);
  move_hover_outline_rect_.reset();
  if (!dirty.isEmpty()) {
    update(dirty);
  } else {
    update();
  }
}

QRect CanvasWidget::moving_layer_outline_rect(const MovingLayer& moving_layer, QPoint delta) const {
  if (!moving_layer.original_opaque_bounds.has_value()) {
    return {};
  }

  auto bounds = *moving_layer.original_opaque_bounds;
  bounds.x += delta.x();
  bounds.y += delta.y();
  return QRect(bounds.x, bounds.y, bounds.width, bounds.height);
}

std::vector<std::pair<LayerId, Rect>> CanvasWidget::moving_layer_bounds(QPoint delta) const {
  return moving_layer_bounds(moving_layers_, delta);
}

std::vector<std::pair<LayerId, Rect>> CanvasWidget::moving_layer_bounds(
    const std::vector<MovingLayer>& moving_layers, QPoint delta) const {
  std::vector<std::pair<LayerId, Rect>> bounds;
  bounds.reserve(moving_layers.size());
  for (const auto& moving_layer : moving_layers) {
    auto moved = moving_layer.original_bounds;
    moved.x += delta.x();
    moved.y += delta.y();
    bounds.emplace_back(moving_layer.id, moved);
  }
  return bounds;
}

QRect CanvasWidget::moving_layer_effect_rect(const Layer& layer, const MovingLayer& moving_layer,
                                             QPoint delta) const {
  auto bounds = moving_layer.original_bounds;
  bounds.x += delta.x();
  bounds.y += delta.y();
  auto with_effects = layer_bounds_with_effects(layer, bounds);
  if (!with_effects.empty() && moving_layer.ancestor_effect_padding > 0) {
    // A styled ancestor group's shadow/glow moves with the leaf; without this
    // outset the preview and commit patches stop short of the effect spill.
    with_effects = outset_rect(with_effects, moving_layer.ancestor_effect_padding);
  }
  return to_qrect(with_effects);
}

QRegion CanvasWidget::moving_layers_dirty_region(QPoint old_delta, QPoint new_delta) const {
  return moving_layers_dirty_region(moving_layers_, old_delta, new_delta);
}

QRegion CanvasWidget::moving_layers_dirty_region(const std::vector<MovingLayer>& moving_layers,
                                                 QPoint old_delta, QPoint new_delta) const {
  QRegion region;
  if (document_ == nullptr) {
    return region;
  }
  for (const auto& moving_layer : moving_layers) {
    auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    const auto old_rect = moving_layer_effect_rect(*layer, moving_layer, old_delta);
    const auto new_rect = moving_layer_effect_rect(*layer, moving_layer, new_delta);
    if (!old_rect.isEmpty()) {
      region += old_rect;
    }
    if (!new_rect.isEmpty()) {
      region += new_rect;
    }
  }
  return region;
}

QRect CanvasWidget::moving_layers_outline_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  QRect dirty;
  if (document_ == nullptr) {
    return dirty;
  }
  for (const auto& moving_layer : moving_layers_) {
    const auto old_outline = moving_layer_outline_rect(moving_layer, old_delta);
    if (!old_outline.isEmpty()) {
      dirty = dirty.united(old_outline);
    }
    const auto new_outline = moving_layer_outline_rect(moving_layer, new_delta);
    if (!new_outline.isEmpty()) {
      dirty = dirty.united(new_outline);
    }
  }
  if (dirty.isEmpty()) {
    return dirty;
  }
  // Deliberately not clipped to the canvas: paint draws the dashed outline on
  // the pasteboard too, and a canvas-clipped repaint left a layer dragged from
  // (or across) the pasteboard invisible until it reached the canvas.
  return dirty.adjusted(-2, -2, 2, 2);
}

void CanvasWidget::cancel_move_preview() noexcept {
  ++move_preview_generation_;
  set_move_preview_requested(false);
  if (move_preview_cancel_) move_preview_cancel_->store(true);
}

bool CanvasWidget::request_move_preview() {
  if constexpr (kBackgroundWorkRunsInline) return false;
  if (document_ == nullptr || moving_layers_.empty()) return false;
  set_move_preview_requested(true);
  if (move_preview_in_flight_) return true;

  const auto generation = ++move_preview_generation_;
  const auto level = preview_composite_level_for_zoom(zoom_);
  const auto key = move_live_latch_key();
  auto snapshot = std::make_shared<const Document>(*document_);
  auto scaled = std::make_shared<std::optional<Document>>();
  auto scale_cache = preview_scale_cache_ ? std::make_shared<PreviewScaleCache>(*preview_scale_cache_)
                                        : std::make_shared<PreviewScaleCache>();
  if (level >= 1 && preview_scaled_document_ && preview_scaled_document_level_ == level) {
    scaled->emplace(*preview_scaled_document_);
  }
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  QRect proxy_rect;
  std::vector<LayerId> moving_ids;
  for (const auto& moving : moving_layers_) {
    moving_ids.push_back(moving.id);
    if (const auto* layer = snapshot->find_layer(moving.id)) {
      proxy_rect = proxy_rect.united(moving_layer_effect_rect(*layer, moving, QPoint()));
    }
  }
  // Same off-canvas shift as ensure_move_proxy_image: proxy_rect stays in
  // shifted (render) space until the completion maps it back.
  const auto proxy_shift =
      proxy_rect.isEmpty() ? QPoint() : move_proxy_snapshot_shift(proxy_rect, canvas_rect, level);
  proxy_rect.translate(proxy_shift);
  const bool clipped = !canvas_rect.contains(proxy_rect);
  proxy_rect = proxy_rect.intersected(canvas_rect);
  if (level >= 1) proxy_rect = rect_aligned_to_mip_grid(proxy_rect, level).intersected(canvas_rect);
  std::vector<LayerId> hidden;
  const std::function<void(const Layer&)> collect_hidden = [&](const Layer& layer) {
    if (layer.kind() == LayerKind::Group) {
      for (const auto& child : layer.children()) collect_hidden(child);
    } else if (layer.kind() != LayerKind::Adjustment &&
               std::find(moving_ids.begin(), moving_ids.end(), layer.id()) == moving_ids.end()) {
      hidden.push_back(layer.id());
    }
  };
  for (const auto& layer : snapshot->layers()) collect_hidden(layer);
  auto base = move_base_cache_scale_level_ == level ? move_base_cache_ : QImage();
  const auto cancelled = std::make_shared<std::atomic_bool>(false);
  move_preview_cancel_ = cancelled;
  move_preview_in_flight_ = true;
  const int delay = std::max(0, qEnvironmentVariableIntValue("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS"));
  auto* app = QApplication::instance();
  const QPointer<CanvasWidget> widget(this);
  run_tracked_background_worker([widget, app, generation, level, key, snapshot, scaled, moving_ids, hidden,
                                 proxy_rect, proxy_shift, clipped, cancelled, delay, scale_cache,
                                 base = std::move(base)]() mutable {
    QImage proxy;
    try {
      if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      if (!cancelled->load() && level >= 1 && !*scaled) {
        scaled->emplace(build_preview_scaled_document(*snapshot, level, scale_cache.get()));
      }
      const auto& source = *scaled ? **scaled : *snapshot;
      if (!cancelled->load() && base.isNull()) {
        base = qimage_from_document_rect_with_hidden_layers_banded(
                   source, QRect(0, 0, source.width(), source.height()), true, moving_ids)
                   .convertToFormat(QImage::Format_RGBA8888);
      }
      const auto area = static_cast<std::int64_t>(proxy_rect.width()) * proxy_rect.height();
      if (!cancelled->load() && !proxy_rect.isEmpty() &&
          (level >= 1 || area <= kMoveProxyLastResortSnapshotArea)) {
        const auto source_rect = level >= 1 ? preview_scaled_document_rect(proxy_rect, level) : proxy_rect;
        proxy = proxy_shift.isNull()
                    ? qimage_from_document_rect_with_hidden_layers_banded(source, source_rect, true, hidden)
                    : qimage_from_document_rect_with_hidden_layers_and_layer_bounds_banded(
                          source, source_rect, true, hidden,
                          move_proxy_shifted_bounds(source, moving_ids, proxy_shift, level));
        const auto pixels = static_cast<std::int64_t>(proxy.width()) * proxy.height();
        if (pixels > kMoveProxyMaxPixels) {
          const auto scale = std::sqrt(static_cast<double>(kMoveProxyMaxPixels) / static_cast<double>(pixels));
          proxy = proxy.scaled(QSize(std::max(1, static_cast<int>(std::lround(proxy.width() * scale))),
                                     std::max(1, static_cast<int>(std::lround(proxy.height() * scale)))),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        proxy = proxy.convertToFormat(QImage::Format_ARGB32_Premultiplied);
      }
    } catch (...) {
      base = {};
      proxy = {};
    }
    QMetaObject::invokeMethod(app, [widget, generation, level, key, scaled, scale_cache, cancelled, proxy_rect,
                                  proxy_shift, clipped, base = std::move(base),
                                  proxy = std::move(proxy)]() mutable {
      if (!widget) return;
      widget->move_preview_in_flight_ = false;
      if (!cancelled->load() && generation == widget->move_preview_generation_ && widget->moving_layer_ &&
          key == widget->move_live_latch_key() && level == preview_composite_level_for_zoom(widget->zoom_)) {
        widget->set_move_preview_requested(false);
        if (*scaled) {
          widget->preview_scaled_document_ = std::move(*scaled);
          widget->preview_scaled_document_level_ = level;
          widget->preview_scale_cache_ = std::move(scale_cache);
        }
        if (!base.isNull() && !proxy.isNull()) {
          widget->move_base_cache_ = std::move(base);
          widget->move_base_cache_scale_level_ = level;
          widget->move_proxy_image_ = std::move(proxy);
          widget->move_proxy_document_rect_ = proxy_rect.translated(-proxy_shift);
          widget->move_proxy_rect_canvas_clipped_ = clipped;
          widget->moving_layers_use_outline_preview_ = false;
          widget->move_drag_uses_proxy_preview_ = true;
          widget->move_live_frame_slow_ = true;
          ++widget->render_cache_diagnostics_.move_proxy_previews;
        }
        // Paint uses the current delta, including moves delivered during work.
        widget->update();
      } else if (widget->move_preview_requested_ && widget->moving_layer_) {
        widget->request_move_preview();
      }
    }, Qt::QueuedConnection);
  });
  return true;
}

bool CanvasWidget::ensure_move_proxy_image() {
  if (!move_proxy_image_.isNull()) {
    return true;
  }
  if (document_ == nullptr || moving_layers_.empty()) {
    return false;
  }

  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  QRect snapshot_rect;
  for (const auto& moving_layer : moving_layers_) {
    const auto* layer = std::as_const(*document_).find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    snapshot_rect = snapshot_rect.united(moving_layer_effect_rect(*layer, moving_layer, QPoint()));
  }
  if (snapshot_rect.isEmpty()) {
    return false;
  }
  // Display-resolution compositing: build the snapshot from the preview-scaled
  // document when zoomed out. The scaled render is cheap enough that the
  // last-resort area cap only applies to full-res snapshots.
  const auto composite_level = preview_composite_level_for_zoom(zoom_);
  Document* scaled_document = composite_level >= 1 ? preview_scaled_document_for_level(composite_level) : nullptr;
  // A set hanging off the canvas snapshots shifted onto it (see
  // move_proxy_snapshot_shift); only what still does not fit stays clipped.
  const auto snapshot_shift =
      move_proxy_snapshot_shift(snapshot_rect, canvas_rect, scaled_document != nullptr ? composite_level : 0);
  snapshot_rect.translate(snapshot_shift);
  move_proxy_rect_canvas_clipped_ = !canvas_rect.contains(snapshot_rect);
  snapshot_rect = snapshot_rect.intersected(canvas_rect);
  if (snapshot_rect.isEmpty()) {
    return false;
  }
  if (scaled_document != nullptr) {
    snapshot_rect = rect_aligned_to_mip_grid(snapshot_rect, composite_level).intersected(canvas_rect);
  }
  const auto snapshot_area =
      static_cast<std::int64_t>(snapshot_rect.width()) * static_cast<std::int64_t>(snapshot_rect.height());
  if (scaled_document == nullptr && snapshot_area > kMoveProxyLastResortSnapshotArea) {
    return false;
  }

  // Hide every non-moving pixel-bearing leaf but keep groups and adjustment
  // layers rendering: styled ancestor folders bake their effects around the
  // moving silhouette, and adjustment layers (which act on whatever composite
  // is below them, pass-through folders included) keep the snapshot's colors
  // close to the final composite. Blends against the real backdrop and content
  // clipped at the canvas edge stay approximate until release.
  const auto is_moving = [this](LayerId id) {
    return std::any_of(moving_layers_.begin(), moving_layers_.end(),
                       [id](const MovingLayer& moving_layer) { return moving_layer.id == id; });
  };
  std::vector<LayerId> hidden;
  const std::function<void(const Layer&)> collect_hidden = [&](const Layer& layer) {
    if (layer.kind() == LayerKind::Group) {
      for (const auto& child : layer.children()) {
        collect_hidden(child);
      }
      return;
    }
    if (layer.kind() != LayerKind::Adjustment && !is_moving(layer.id())) {
      hidden.push_back(layer.id());
    }
  };
  for (const auto& layer : std::as_const(*document_).layers()) {
    collect_hidden(layer);
  }

  // Banded: the snapshot is small but crosses the styled stack, and this
  // render is the other half of the latch hitch (preview-only, so the band
  // divergence class is acceptable).
  const Document& snapshot_source = scaled_document != nullptr ? *scaled_document : std::as_const(*document_);
  const auto source_level = scaled_document != nullptr ? composite_level : 0;
  const auto source_rect =
      scaled_document != nullptr ? preview_scaled_document_rect(snapshot_rect, composite_level) : snapshot_rect;
  std::vector<LayerId> moving_ids;
  moving_ids.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    moving_ids.push_back(moving_layer.id);
  }
  auto snapshot =
      snapshot_shift.isNull()
          ? qimage_from_document_rect_with_hidden_layers_banded(snapshot_source, source_rect, true, hidden)
          : qimage_from_document_rect_with_hidden_layers_and_layer_bounds_banded(
                snapshot_source, source_rect, true, hidden,
                move_proxy_shifted_bounds(snapshot_source, moving_ids, snapshot_shift, source_level));
  if (snapshot.isNull()) {
    return false;
  }
  const auto rendered_area =
      static_cast<std::int64_t>(snapshot.width()) * static_cast<std::int64_t>(snapshot.height());
  if (rendered_area > kMoveProxyMaxPixels) {
    const auto scale = std::sqrt(static_cast<double>(kMoveProxyMaxPixels) / static_cast<double>(rendered_area));
    const QSize proxy_size(std::max(1, static_cast<int>(std::lround(snapshot.width() * scale))),
                           std::max(1, static_cast<int>(std::lround(snapshot.height() * scale))));
    snapshot = snapshot.scaled(proxy_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  move_proxy_image_ = snapshot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  move_proxy_document_rect_ = snapshot_rect.translated(-snapshot_shift);
  return !move_proxy_image_.isNull();
}

QRect CanvasWidget::move_proxy_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  if (document_ == nullptr || move_proxy_document_rect_.isEmpty()) {
    return {};
  }
  // The blit only shows inside the canvas; the outline repaints unclipped.
  auto dirty = move_proxy_document_rect_.translated(old_delta)
                   .united(move_proxy_document_rect_.translated(new_delta))
                   .adjusted(-2, -2, 2, 2)
                   .intersected(QRect(0, 0, document_->width(), document_->height()));
  const auto outline_dirty = moving_layers_outline_dirty_rect(old_delta, new_delta);
  if (!outline_dirty.isEmpty()) {
    dirty = dirty.united(outline_dirty);
  }
  return dirty;
}

void CanvasWidget::clear_move_proxy() noexcept {
  // Per-drag proxy state only. move_live_frame_slow_ deliberately survives:
  // it persists across drags of the same moving set (reset_move_live_latch and
  // the press-time key check own its lifetime), mirroring the transform
  // session latch, so a re-drag latches the proxy on its first frame instead
  // of re-paying a slow live frame.
  move_drag_uses_proxy_preview_ = false;
  move_proxy_image_ = QImage();
  move_proxy_document_rect_ = QRect();
  move_proxy_rect_canvas_clipped_ = false;
}

void CanvasWidget::retain_move_preview_caches(const std::vector<LayerId>& committed_ids, QPoint committed_delta,
                                              bool proxy_content_complete) {
  // Per-drag latch state resets either way; only the images and their keys
  // survive for the next drag of the same selection.
  move_drag_uses_proxy_preview_ = false;
  if (move_base_cache_.isNull()) {
    clear_retained_move_caches();
    return;
  }
  if (!proxy_content_complete) {
    // The base excludes the moving set entirely, so it survives the commit
    // unconditionally; a clipped (or never-built) snapshot re-renders fresh.
    move_proxy_image_ = QImage();
    move_proxy_document_rect_ = QRect();
    move_proxy_rect_canvas_clipped_ = false;
  } else if (!committed_delta.isNull()) {
    // The snapshot content translates rigidly with the commit. The translated
    // rect loses its mip-grid alignment, which only shifts the downsample
    // phase of AA edges - the same approximation every mid-drag blit already
    // has (deep-zoom >= 8x keeps its documented grid caveat).
    move_proxy_document_rect_.translate(committed_delta);
  }
  retained_move_ids_ = committed_ids;
  std::sort(retained_move_ids_.begin(), retained_move_ids_.end());
  retained_move_composite_level_ = move_base_cache_scale_level_;
}

void CanvasWidget::clear_retained_move_caches() noexcept {
  cancel_move_preview();
  retained_move_ids_.clear();
  retained_move_composite_level_ = -1;
  clear_move_base_cache();
  clear_move_proxy();
}

void CanvasWidget::invalidate_retained_move_caches() noexcept {
  cancel_move_preview();
  if (moving_layer_ || move_drag_pending_) {
    // Clearing mid-drag would strand the in-flight proxy blit; flag it so the
    // release skips retention instead.
    move_external_change_during_drag_ = true;
    return;
  }
  clear_retained_move_caches();
}

std::uint64_t CanvasWidget::move_live_latch_key() const {
  if (moving_layers_.empty()) {
    return 0;
  }
  std::vector<LayerId> ids;
  ids.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    ids.push_back(moving_layer.id);
  }
  std::sort(ids.begin(), ids.end());
  // FNV-1a over the sorted ids plus the composite level: the latch prices a
  // (moving set, display resolution) pair, so changing either re-prices the
  // drag from a live frame.
  auto hash = std::uint64_t{1469598103934665603ULL};
  const auto mix = [&hash](std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      hash ^= (value >> (i * 8)) & 0xFFU;
      hash *= 1099511628211ULL;
    }
  };
  for (const auto id : ids) {
    mix(static_cast<std::uint64_t>(id));
  }
  mix(static_cast<std::uint64_t>(preview_composite_level_for_zoom(zoom_)) + 1);
  return hash;
}

void CanvasWidget::reset_move_live_latch() noexcept {
  move_live_frame_slow_ = false;
  move_live_latch_key_ = 0;
}

Document* CanvasWidget::preview_scaled_document_for_level(int level) {
  if (document_ == nullptr || level < 1) {
    return nullptr;
  }
  if (preview_scaled_document_.has_value() && preview_scaled_document_level_ == level) {
    return &*preview_scaled_document_;
  }
  if (!preview_scale_cache_) preview_scale_cache_ = std::make_shared<PreviewScaleCache>();
  preview_scaled_document_.emplace(build_preview_scaled_document(std::as_const(*document_), level,
                                                                preview_scale_cache_.get()));
  preview_scaled_document_level_ = level;
  return &*preview_scaled_document_;
}

void CanvasWidget::clear_preview_scaled_document() noexcept {
  cancel_move_preview();
  preview_scaled_document_.reset();
  preview_scaled_document_level_ = 0;
}

void CanvasWidget::retarget_preview_scaled_for_committed_move(const std::vector<LayerId>& committed_ids) {
  if (!preview_scaled_document_.has_value() || preview_scaled_document_level_ < 1 || document_ == nullptr) {
    return;
  }
  for (const auto id : committed_ids) {
    const auto* real = std::as_const(*document_).find_layer(id);
    auto* scaled = preview_scaled_document_->find_layer(id);
    if (real == nullptr || scaled == nullptr) {
      clear_preview_scaled_document();
      return;
    }
    retarget_preview_scaled_layer_bounds(*scaled, *real, preview_scaled_document_level_);
  }
}

bool CanvasWidget::moving_layers_should_use_outline_preview(QPoint old_delta, QPoint new_delta) const {
  if (document_ == nullptr || moving_layers_.empty()) {
    return false;
  }
  // Cost metric: SUM every moving layer's canvas-clipped effect rect instead of
  // taking one bounding box. Every preview patch recomposites each moving layer
  // it covers, so a dragged folder of stacked copies costs the per-layer sum
  // while its bounding box stays small (a 21-copy poster stack measured ~0.3
  // Mpx by box but ~6 Mpx of per-frame composite work). For disjoint layers the
  // sum is at most the old bounding-box metric, and for a single layer the
  // old/new average matches it, so the thresholds keep their calibration.
  const QRect canvas_rect(0, 0, document_->width(), document_->height());
  std::int64_t summed_area = 0;
  for (const auto& moving_layer : moving_layers_) {
    const auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    for (const auto delta : {old_delta, new_delta}) {
      const auto rect = moving_layer_effect_rect(*layer, moving_layer, delta).intersected(canvas_rect);
      summed_area += static_cast<std::int64_t>(rect.width()) * static_cast<std::int64_t>(rect.height());
    }
  }
  const auto dirty_area = summed_area / 2;
  if (dirty_area <= 0) {
    return false;
  }
  if (dirty_area >= kMoveOutlineDirtyAreaThreshold) {
    return true;
  }
  if (dirty_area < kStyledMoveOutlineDirtyAreaThreshold) {
    return false;
  }
  return std::any_of(moving_layers_.begin(), moving_layers_.end(),
                     [](const MovingLayer& moving_layer) { return moving_layer.expensive_style; });
}

QRegion CanvasWidget::move_active_layer_by(QPoint delta) {
  if (document_ == nullptr || delta.isNull()) {
    return {};
  }
  const auto layer_ids = movable_layer_ids();
  std::vector<std::pair<LayerId, QPoint>> deltas;
  deltas.reserve(layer_ids.size());
  for (const auto id : layer_ids) {
    deltas.emplace_back(id, delta);
  }
  return offset_layers(deltas, layer_ids.size() >= 2U ? tr("Nudge layers") : tr("Nudge layer"));
}

QRegion CanvasWidget::offset_layers(const std::vector<std::pair<LayerId, QPoint>>& deltas,
                                    const QString& undo_label, bool record_history) {
  if (document_ == nullptr) {
    return {};
  }
  std::vector<std::pair<LayerId, QPoint>> moves;
  moves.reserve(deltas.size());
  for (const auto& entry : deltas) {
    if (!entry.second.isNull() && document_->find_layer(entry.first) != nullptr) {
      moves.push_back(entry);
    }
  }
  if (moves.empty()) {
    return {};
  }
  const bool rerender_smart_filters =
      std::any_of(moves.begin(), moves.end(), [this](const std::pair<LayerId, QPoint>& move) {
        const auto* layer = document_->find_layer(move.first);
        return layer != nullptr &&
               move_layer_requires_smart_filter_rerender(*layer);
      });
  std::optional<Document> rollback_document;
  if (rerender_smart_filters) {
    rollback_document.emplace(*document_);
  } else if (record_history && before_edit_callback_) {
    before_edit_callback_(undo_label);
  }
  QRegion dirty;
  // Same styled-ancestor blind spot as the drag path: a nudged child of a
  // styled folder moves the folder's shadow/glow too, so pad its dirty rects.
  const auto ancestor_style_info = collect_ancestor_group_style_info(std::as_const(*document_).layers());
  const auto ancestor_padded = [&ancestor_style_info](LayerId id, Rect with_effects) {
    if (const auto found = ancestor_style_info.find(id);
        found != ancestor_style_info.end() && found->second.effect_padding > 0 && !with_effects.empty()) {
      with_effects = outset_rect(with_effects, found->second.effect_padding);
    }
    return to_qrect(with_effects);
  };
  for (const auto& [id, delta] : moves) {
    auto* layer = document_->find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    const auto old_bounds = layer->bounds();
    dirty += ancestor_padded(id, layer_bounds_with_effects(*layer, old_bounds));
    auto bounds = old_bounds;
    bounds.x += delta.x();
    bounds.y += delta.y();
    layer->set_bounds(bounds);
    patchy::translate_moved_layer_metadata(*layer, delta.x(), delta.y(), document_->width(), document_->height());
    if (move_layer_requires_smart_filter_rerender(*layer) &&
        (!smart_object_transform_render_callback_ ||
         !smart_object_transform_render_callback_(id))) {
      if (rollback_document.has_value()) {
        *document_ = std::move(*rollback_document);
      }
      return dirty;
    }
    layer = document_->find_layer(id);
    if (layer != nullptr) {
      dirty += ancestor_padded(id, layer_bounds_with_effects(*layer, layer->bounds()));
    }
  }
  if (rerender_smart_filters && rollback_document.has_value()) {
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (record_history && before_edit_callback_) {
      before_edit_callback_(undo_label);
    }
    *document_ = std::move(committed_document);
  }
  return dirty;
}

}  // namespace patchy::ui
