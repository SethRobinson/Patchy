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
// The proxy snapshot is downscaled to at most this many pixels (mirrors
// kTransformProxyMaxPixels) and refused outright above the last-resort cap,
// where even the one-time snapshot render would hitch for seconds; such drags
// keep the dashed-outline fallback.
constexpr std::int64_t kMoveProxyMaxPixels = 4'000'000;
constexpr std::int64_t kMoveProxyLastResortSnapshotArea = 80'000'000;

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
  QRegion region;
  if (document_ == nullptr) {
    return region;
  }
  for (const auto& moving_layer : moving_layers_) {
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
  return dirty.adjusted(-2, -2, 2, 2).intersected(QRect(0, 0, document_->width(), document_->height()));
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
  snapshot_rect = snapshot_rect.intersected(canvas_rect);
  if (snapshot_rect.isEmpty()) {
    return false;
  }
  const auto snapshot_area =
      static_cast<std::int64_t>(snapshot_rect.width()) * static_cast<std::int64_t>(snapshot_rect.height());
  if (snapshot_area > kMoveProxyLastResortSnapshotArea) {
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

  auto snapshot = qimage_from_document_rect_with_hidden_layers(*document_, snapshot_rect, true, hidden);
  if (snapshot.isNull()) {
    return false;
  }
  if (snapshot_area > kMoveProxyMaxPixels) {
    const auto scale = std::sqrt(static_cast<double>(kMoveProxyMaxPixels) / static_cast<double>(snapshot_area));
    const QSize proxy_size(std::max(1, static_cast<int>(std::lround(snapshot.width() * scale))),
                           std::max(1, static_cast<int>(std::lround(snapshot.height() * scale))));
    snapshot = snapshot.scaled(proxy_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  move_proxy_image_ = snapshot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  move_proxy_document_rect_ = snapshot_rect;
  return !move_proxy_image_.isNull();
}

QRect CanvasWidget::move_proxy_dirty_rect(QPoint old_delta, QPoint new_delta) const {
  if (document_ == nullptr || move_proxy_document_rect_.isEmpty()) {
    return {};
  }
  auto dirty =
      move_proxy_document_rect_.translated(old_delta).united(move_proxy_document_rect_.translated(new_delta));
  const auto outline_dirty = moving_layers_outline_dirty_rect(old_delta, new_delta);
  if (!outline_dirty.isEmpty()) {
    dirty = dirty.united(outline_dirty);
  }
  return dirty.adjusted(-2, -2, 2, 2).intersected(QRect(0, 0, document_->width(), document_->height()));
}

void CanvasWidget::clear_move_proxy() noexcept {
  move_drag_uses_proxy_preview_ = false;
  move_proxy_image_ = QImage();
  move_proxy_document_rect_ = QRect();
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
  for (const auto id : layer_ids) {
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
    if (before_edit_callback_) {
      before_edit_callback_(layer_ids.size() >= 2U ? tr("Nudge layers")
                                                    : tr("Nudge layer"));
    }
    *document_ = std::move(committed_document);
  }
  return dirty;
}

}  // namespace patchy::ui
