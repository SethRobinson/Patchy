// CanvasWidget's Move-tool machinery, split out of canvas_widget.cpp:
// movable-layer enumeration, the move hover outline, the moving-layer
// outline/bounds/dirty-rect/dirty-region helpers with the outline-preview
// policy, and move_active_layer_by. Pure function moves from
// canvas_widget.cpp; behavior must stay identical.

#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"

#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/smart_filter.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/image_document_io.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/tool_cursors.hpp"

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

constexpr std::int64_t kMoveOutlineDirtyAreaThreshold = 4'000'000;
constexpr std::int64_t kStyledMoveOutlineDirtyAreaThreshold = 1'000'000;

}  // namespace

std::vector<LayerId> CanvasWidget::movable_layer_ids() const {
  std::vector<LayerId> ids;
  if (document_ == nullptr || layer_edit_target_ == LayerEditTarget::SmartFilterMask) {
    return ids;
  }

  const auto add_if_movable = [this, &ids](const Layer& layer, LayerLockFlags selected_ancestor_lock_flags) {
    if (std::find(ids.begin(), ids.end(), layer.id()) != ids.end()) {
      return;
    }
    if (((selected_ancestor_lock_flags | patchy::layer_effective_lock_flags(document_->layers(), layer.id())) &
         kLayerLockPosition) != kLayerLockNone) {
      return;
    }
    if (!layer_has_movable_pixels(layer)) {
      return;
    }
    ids.push_back(layer.id());
  };

  const std::function<void(const Layer&, LayerLockFlags)> add_movable_layer_tree = [&](const Layer& layer,
                                                                                       LayerLockFlags ancestor_flags) {
    const auto effective_flags = ancestor_flags | patchy::layer_lock_flags(layer);
    if (layer.kind() == LayerKind::Group) {
      for (const auto& child : layer.children()) {
        add_movable_layer_tree(child, effective_flags);
      }
      return;
    }
    add_if_movable(layer, effective_flags);
  };

  auto add_movable_by_id = [&](LayerId id) {
    if (const auto* layer = document_->find_layer(id); layer != nullptr) {
      add_movable_layer_tree(*layer, kLayerLockNone);
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

std::optional<QRect> CanvasWidget::move_hover_outline_rect_at(QPoint widget_position,
                                                              Qt::KeyboardModifiers modifiers) const {
  if (document_ == nullptr || tool_ != CanvasTool::Move || moving_layer_ || transforming_layer_ || dragging_transform_ ||
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
  std::vector<std::pair<LayerId, Rect>> bounds;
  bounds.reserve(moving_layers_.size());
  for (const auto& moving_layer : moving_layers_) {
    auto moved = moving_layer.original_bounds;
    moved.x += delta.x();
    moved.y += delta.y();
    bounds.emplace_back(moving_layer.id, moved);
  }
  return bounds;
}

QRect CanvasWidget::moving_layers_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  return moving_layers_dirty_region(old_delta, new_delta).boundingRect();
}

QRegion CanvasWidget::moving_layers_dirty_region(QPoint old_delta, QPoint new_delta) const {
  QRegion region;
  if (document_ == nullptr) {
    return region;
  }
  for (const auto& moving_layer : moving_layers_) {
    auto* layer = document_->find_layer(moving_layer.id);
    if (layer == nullptr) {
      continue;
    }
    auto old_bounds = moving_layer.original_bounds;
    old_bounds.x += old_delta.x();
    old_bounds.y += old_delta.y();
    auto new_bounds = moving_layer.original_bounds;
    new_bounds.x += new_delta.x();
    new_bounds.y += new_delta.y();
    const auto old_rect = to_qrect(layer_bounds_with_effects(*layer, old_bounds));
    const auto new_rect = to_qrect(layer_bounds_with_effects(*layer, new_bounds));
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
  return dirty.adjusted(-2, -2, 2, 2).intersected(QRect(0, 0, document_->width(), document_->height()));
}

bool CanvasWidget::moving_layers_should_use_outline_preview(QPoint old_delta, QPoint new_delta) const {
  if (document_ == nullptr || moving_layers_.empty()) {
    return false;
  }
  const auto dirty =
      moving_layers_dirty_rect(old_delta, new_delta).intersected(QRect(0, 0, document_->width(), document_->height()));
  if (dirty.isEmpty()) {
    return false;
  }
  const auto dirty_area = static_cast<std::int64_t>(dirty.width()) * static_cast<std::int64_t>(dirty.height());
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
  const bool rerender_smart_filters =
      std::any_of(layer_ids.begin(), layer_ids.end(), [this](LayerId id) {
        const auto* layer = document_->find_layer(id);
        return layer != nullptr &&
               move_layer_requires_smart_filter_rerender(*layer);
      });
  std::optional<Document> rollback_document;
  if (rerender_smart_filters) {
    rollback_document.emplace(*document_);
  } else if (before_edit_callback_) {
    before_edit_callback_(layer_ids.size() >= 2U ? tr("Nudge layers")
                                                  : tr("Nudge layer"));
  }
  QRegion dirty;
  for (const auto id : layer_ids) {
    auto* layer = document_->find_layer(id);
    if (layer == nullptr) {
      continue;
    }
    const auto old_bounds = layer->bounds();
    dirty += to_qrect(layer_bounds_with_effects(*layer, old_bounds));
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
      dirty += to_qrect(layer_bounds_with_effects(*layer, layer->bounds()));
    }
  }
  if (rerender_smart_filters && rollback_document.has_value()) {
    auto committed_document = *document_;
    *document_ = std::move(*rollback_document);
    if (before_edit_callback_) {
      before_edit_callback_(layer_ids.size() >= 2U ? tr("Nudge layers")
                                                    : tr("Nudge layer"));
    }
    *document_ = std::move(committed_document);
  }
  return dirty;
}

}  // namespace patchy::ui
