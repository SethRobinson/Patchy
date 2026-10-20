// CanvasWidget's artboard surface (docs/artboards.md): the backdrop of an artboard document
// (checkerboard inside the artboards only, the pasteboard color between them), every
// artboard's outline and name label, and the Artboard tool. The tool lays out a new
// artboard by dragging on empty space, moves an artboard (with its content) by dragging it
// or its label, resizes it by its eight handles, and adds one beside it with the add
// buttons. Like the Crop tool it never edits the document: every gesture ends in an
// ArtboardEditRequest that MainWindow applies with an undo step.

#include "ui/canvas_widget.hpp"

#include "core/artboard.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/pixel_depth.hpp"
#include "core/vector_shape.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/image_document_io.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QScrollBar>
#include <Qt>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace patchy::ui {

namespace {

constexpr double kLabelGap = 4.0;          // widget px between the label baseline and the artboard
constexpr double kLabelMinWidth = 24.0;    // a clipped label keeps a hit area and its ellipsis
constexpr double kLabelCollisionGap = 6.0; // widget px kept before a neighbouring label on the same row
constexpr double kAddButtonRadius = 9.0;   // the "+" buttons beside the selected artboard
constexpr double kAddButtonOffset = 22.0;  // their centers, this far outside its edge
constexpr double kHandleSize = 8.0;

QRect qrect(const Rect& rect) {
  return QRect(rect.x, rect.y, rect.width, rect.height);
}

QFont artboard_label_font(const QWidget& widget) {
  QFont font = widget.font();
  font.setPointSizeF(std::max(7.0, font.pointSizeF() > 0.0 ? font.pointSizeF() - 0.5 : 8.5));
  return font;
}

}  // namespace

void CanvasWidget::set_artboard_edit_requested_callback(std::function<void(const ArtboardEditRequest&)> callback) {
  artboard_edit_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_artboard_default_size(QSize size) noexcept {
  if (size.width() > 0 && size.height() > 0) {
    artboard_default_size_ = size;
  }
}

bool CanvasWidget::artboard_drag_active() const noexcept {
  return artboard_drag_mode_ != ArtboardDragMode::None;
}

void CanvasWidget::set_layers_moved_callback(std::function<void(const std::vector<LayerId>&)> callback) {
  layers_moved_callback_ = std::move(callback);
}

void CanvasWidget::set_artboard_auto_nest(bool enabled) noexcept {
  artboard_auto_nest_ = enabled;
}

std::vector<ArtboardNestMove> CanvasWidget::move_nest_moves_for_delta(QPoint delta) const {
  if (!artboard_auto_nest_ || document_ == nullptr || move_nest_root_ids_.empty()) {
    return {};
  }
  return artboard_nest_moves(std::as_const(*document_), move_nest_root_ids_, delta.x(), delta.y());
}

const Document& CanvasWidget::move_patch_source(const Document& base, int level, QPoint delta) {
  auto moves = move_nest_moves_for_delta(delta);
  if (moves.empty()) {
    return base;
  }
  // One copy per change of destination, not per frame: the moves only change when the
  // drag carries a layer across an artboard edge. Ids are shared with the scaled preview
  // document, so the same moves re-nest either one.
  if (!move_nest_preview_document_.has_value() || moves != move_nest_preview_moves_ ||
      move_nest_preview_document_level_ != level || move_nest_preview_document_base_ != &base) {
    move_nest_preview_document_.emplace(base);
    static_cast<void>(apply_artboard_nest_moves(*move_nest_preview_document_, moves));
    move_nest_preview_moves_ = std::move(moves);
    move_nest_preview_document_level_ = level;
    move_nest_preview_document_base_ = &base;
  }
  return *move_nest_preview_document_;
}

std::optional<QRect> CanvasWidget::move_proxy_clip_document_rect(QPoint delta) const {
  if (document_ == nullptr || moving_layers_.empty() || !document_has_artboards(std::as_const(*document_))) {
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  const auto moves = move_nest_moves_for_delta(delta);
  std::optional<std::optional<LayerId>> common;
  for (const auto& moving : moving_layers_) {
    const auto* owner = artboard_owning_layer(document, moving.id);
    std::optional<LayerId> clip = owner != nullptr ? std::optional<LayerId>(owner->id()) : std::nullopt;
    for (const auto& move : moves) {
      const auto* root = document.find_layer(move.id);
      if (root != nullptr && (root->id() == moving.id || layer_contains_descendant(*root, moving.id))) {
        clip = move.target;
      }
    }
    if (common.has_value() && *common != clip) {
      return std::nullopt;  // content landing in different artboards: no single clip
    }
    common = clip;
  }
  if (!common.has_value() || !common->has_value()) {
    return std::nullopt;
  }
  const auto* artboard = document.find_layer(**common);
  return artboard != nullptr && artboard->artboard().has_value() ? std::optional<QRect>(qrect(artboard->artboard()->rect))
                                                                 : std::nullopt;
}

bool CanvasWidget::move_proxy_allowed_for_artboards() const {
  if (document_ == nullptr) {
    return true;
  }
  // The proxy draws the moving content over the base frame, clipped to one artboard; an
  // artboard that blends, fades, masks or styles its content cannot be faked that way.
  for (const auto* artboard : document_artboards(std::as_const(*document_))) {
    if (!artboard_composites_plainly(*artboard)) {
      return false;
    }
  }
  return true;
}

Document CanvasWidget::move_proxy_snapshot_source(const Document& source) const {
  Document snapshot = source;
  if (!document_has_artboards(source)) {
    return snapshot;
  }
  for (auto& layer : snapshot.layers()) {
    if (!layer_is_artboard(std::as_const(layer))) {
      continue;
    }
    const bool owns_moving = std::any_of(moving_layers_.begin(), moving_layers_.end(), [&](const MovingLayer& moving) {
      return layer_contains_descendant(std::as_const(layer), moving.id);
    });
    if (owns_moving) {
      layer.clear_artboard();
    }
  }
  return snapshot;
}

std::optional<LayerId> CanvasWidget::selected_root_artboard_id() const {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  std::vector<LayerId> roots;
  if (!selected_layer_ids_.empty()) {
    roots = root_drop_layer_ids(document.layers(), selected_layer_ids_);
  } else if (const auto active = document.active_layer_id(); active.has_value()) {
    roots = {*active};
  }
  if (roots.size() != 1U) {
    return std::nullopt;
  }
  const auto* layer = document.find_layer(roots.front());
  return layer != nullptr && layer_is_artboard(*layer) ? std::optional<LayerId>(layer->id()) : std::nullopt;
}

void CanvasWidget::begin_artboard_move_drag(const Layer& artboard, QMouseEvent* event) {
  artboard_drag_mode_ = ArtboardDragMode::Move;
  artboard_drag_id_ = artboard.id();
  artboard_drag_start_rect_ = qrect(artboard.artboard()->rect);
  artboard_drag_rect_ = artboard_drag_start_rect_;
  artboard_drag_start_point_ = document_position_f(event->position());
  artboard_press_widget_point_ = event->pos();
  artboard_drag_moved_ = false;
  artboard_drag_duplicate_ = (event->modifiers() & Qt::AltModifier) != 0;
  artboard_drag_ghost_ = QImage();
  artboard_drag_vacated_ = QImage();
}

bool CanvasWidget::handle_move_tool_artboard_press(QMouseEvent* event) {
  if (tool_ != CanvasTool::Move || event->button() != Qt::LeftButton || document_ == nullptr || edit_locked_) {
    return false;
  }
  const auto& document = std::as_const(*document_);
  const auto point = document_position(event->pos());
  // With an artboard itself selected (not a layer inside it), the Move tool moves the
  // artboard: a press inside it starts the same gesture as the Artboard tool's.
  if ((event->modifiers() & Qt::ControlModifier) == 0) {
    if (const auto id = selected_root_artboard_id(); id.has_value()) {
      const auto* artboard = document.find_layer(*id);
      if (artboard != nullptr && artboard->visible() && artboard->artboard()->rect.contains(point.x(), point.y())) {
        begin_artboard_move_drag(*artboard, event);
        return true;
      }
    }
  }
  // Auto-Select (or Ctrl) on an artboard's empty area picks the artboard, the way it
  // picks the layer under the pointer elsewhere (GitHub issue 259): the hover outline
  // framed it, the click selects it in the Layers panel and a drag moves it.
  const auto id = move_artboard_target_at(event->pos(), event->modifiers());
  if (!id.has_value()) {
    return false;
  }
  const auto* artboard = document.find_layer(*id);
  if (artboard == nullptr || !artboard->artboard().has_value()) {
    return false;
  }
  request_artboard_edit({ArtboardEditRequest::Kind::Select, id, {}, {}, 0, false});
  begin_artboard_move_drag(*artboard, event);
  return true;
}

std::optional<LayerId> CanvasWidget::move_artboard_target_at(QPoint widget_position,
                                                              Qt::KeyboardModifiers modifiers) const {
  if (document_ == nullptr || tool_ != CanvasTool::Move) {
    return std::nullopt;
  }
  if (const auto label = artboard_label_at(widget_position); label.has_value()) {
    return label;
  }
  // Only a selecting press reads the artboard under the pointer: with Auto-Select off a
  // plain press drags the selection from anywhere, and Shift adds to it.
  if ((!auto_select_layer_ && !modifiers.testFlag(Qt::ControlModifier)) || modifiers.testFlag(Qt::ShiftModifier)) {
    return std::nullopt;
  }
  // The passive transform box's edges and corners keep their grabs.
  if (const auto rect = move_transform_controls_rect(); rect.has_value()) {
    const auto handle = transform_handle_at(widget_position, *rect, 0.0, TransformHitZones::EdgesAndCorners);
    if (handle != TransformHandle::None && handle != TransformHandle::Move) {
      return std::nullopt;
    }
  }
  const auto& document = std::as_const(*document_);
  const auto point = document_position(widget_position);
  // Any layer under the pointer, locked or not, is the pick there (a locked one reports
  // its lock); the artboard is the pick only on its empty area.
  if (topmost_move_layer_at(point, false) != nullptr) {
    return std::nullopt;
  }
  const auto* artboard = artboard_at(document, point.x(), point.y());
  if (artboard == nullptr || !artboard->visible()) {
    return std::nullopt;
  }
  return artboard->id();
}

void CanvasWidget::request_artboard_edit(const ArtboardEditRequest& request) {
  if (artboard_edit_requested_callback_) {
    artboard_edit_requested_callback_(request);
  }
}

std::optional<LayerId> CanvasWidget::selected_artboard_id() const {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  const auto active = document.active_layer_id();
  if (!active.has_value()) {
    return std::nullopt;
  }
  const auto* owner = artboard_owning_layer(document, *active);
  return owner != nullptr ? std::optional<LayerId>(owner->id()) : std::nullopt;
}

QRectF CanvasWidget::artboard_label_widget_rect(const Layer& artboard) const {
  const auto& rect = artboard.artboard()->rect;
  const QFontMetricsF metrics(artboard_label_font(*this));
  const auto top_left = widget_position_f(QPointF(rect.x, rect.y));
  const auto height = metrics.height();
  const auto top = top_left.y() - kLabelGap - height;
  // The whole name, past a narrow board if need be (the overlay reads the backing and
  // halos a straddle). It gives way only to another label on its row that starts
  // inside it, and to the visible canvas edge, and then only as far as needed.
  auto right = top_left.x() + metrics.horizontalAdvance(QString::fromStdString(artboard.name())) + 2.0;
  const double visible_right =
      width() - (vertical_scroll_bar_ != nullptr && vertical_scroll_bar_->isVisible() ? vertical_scroll_bar_->width() : 0);
  right = std::min(right, visible_right - 2.0);
  if (document_ != nullptr) {
    for (const auto* other : document_artboards(std::as_const(*document_))) {
      if (other->id() == artboard.id() || !other->visible()) {
        continue;
      }
      const auto other_top_left = widget_position_f(QPointF(other->artboard()->rect.x, other->artboard()->rect.y));
      if (other_top_left.x() > top_left.x() && other_top_left.x() < right &&
          std::abs(other_top_left.y() - top_left.y()) < height) {
        right = other_top_left.x() - kLabelCollisionGap;
      }
    }
  }
  return QRectF(top_left.x(), top, std::max(kLabelMinWidth, right - top_left.x()), height);
}

std::optional<LayerId> CanvasWidget::artboard_label_at(QPoint widget_point) const {
  if (document_ == nullptr) {
    return std::nullopt;
  }
  for (const auto* artboard : document_artboards(std::as_const(*document_))) {
    if (artboard->visible() && artboard_label_widget_rect(*artboard).adjusted(-2.0, -2.0, 2.0, 2.0).contains(widget_point)) {
      return artboard->id();
    }
  }
  return std::nullopt;
}

QRectF CanvasWidget::artboard_add_button_widget_rect(QRect artboard_rect, int side) const {
  const auto widget = widget_rect_for_document_rect(QRectF(artboard_rect));
  QPointF center;
  switch (side) {
    case 1:
      center = QPointF(widget.center().x(), widget.bottom() + kAddButtonOffset);
      break;
    case 2:
      center = QPointF(widget.left() - kAddButtonOffset, widget.center().y());
      break;
    case 3:
      center = QPointF(widget.center().x(), widget.top() - kAddButtonOffset);
      break;
    default:
      center = QPointF(widget.right() + kAddButtonOffset, widget.center().y());
      break;
  }
  return QRectF(center.x() - kAddButtonRadius, center.y() - kAddButtonRadius, kAddButtonRadius * 2.0,
                kAddButtonRadius * 2.0);
}

int CanvasWidget::artboard_add_button_at(QPoint widget_point) const {
  if (tool_ != CanvasTool::Artboard || document_ == nullptr || artboard_drag_active()) {
    return -1;
  }
  const auto id = selected_artboard_id();
  const auto* artboard = id.has_value() ? std::as_const(*document_).find_layer(*id) : nullptr;
  if (artboard == nullptr) {
    return -1;
  }
  for (int side = 0; side < 4; ++side) {
    // The top button would sit on the label; the label wins there.
    if (side == 3 && artboard_label_widget_rect(*artboard).contains(widget_point)) {
      continue;
    }
    if (artboard_add_button_widget_rect(qrect(artboard->artboard()->rect), side).contains(widget_point)) {
      return side;
    }
  }
  return -1;
}

CanvasWidget::TransformHandle CanvasWidget::artboard_handle_at(QPoint widget_point) const {
  if (tool_ != CanvasTool::Artboard || document_ == nullptr) {
    return TransformHandle::None;
  }
  const auto id = selected_artboard_id();
  const auto* artboard = id.has_value() ? std::as_const(*document_).find_layer(*id) : nullptr;
  if (artboard == nullptr) {
    return TransformHandle::None;
  }
  const auto handle = transform_handle_at(widget_point, QRectF(qrect(artboard->artboard()->rect)), 0.0);
  return handle == TransformHandle::Rotate || handle == TransformHandle::Move ? TransformHandle::None : handle;
}

QRect CanvasWidget::artboard_create_rect(QPoint anchor, QPoint current, Qt::KeyboardModifiers modifiers) const {
  auto delta = current - anchor;
  if ((modifiers & Qt::ShiftModifier) != 0) {
    const auto side = std::max(std::abs(delta.x()), std::abs(delta.y()));
    delta = QPoint(delta.x() < 0 ? -side : side, delta.y() < 0 ? -side : side);
  }
  if ((modifiers & Qt::AltModifier) != 0) {
    // Alt draws from the center, like the Crop tool and the marquees.
    const auto half_w = std::max(1, std::abs(delta.x()));
    const auto half_h = std::max(1, std::abs(delta.y()));
    return QRect(anchor.x() - half_w, anchor.y() - half_h, half_w * 2, half_h * 2);
  }
  // Edges, not pixels: from x 20 to x 120 is 100 px (QRect(a, b) would count both ends).
  const auto end = anchor + delta;
  return QRect(std::min(anchor.x(), end.x()), std::min(anchor.y(), end.y()), std::max(1, std::abs(delta.x())),
               std::max(1, std::abs(delta.y())));
}

QRect CanvasWidget::artboard_resize_rect(QPoint point, Qt::KeyboardModifiers modifiers) const {
  const auto start = artboard_drag_start_rect_;
  const auto handle = artboard_drag_handle_;
  const bool moves_left = handle == TransformHandle::TopLeft || handle == TransformHandle::Left ||
                          handle == TransformHandle::BottomLeft;
  const bool moves_right = handle == TransformHandle::TopRight || handle == TransformHandle::Right ||
                           handle == TransformHandle::BottomRight;
  const bool moves_top = handle == TransformHandle::TopLeft || handle == TransformHandle::Top ||
                         handle == TransformHandle::TopRight;
  const bool moves_bottom = handle == TransformHandle::BottomLeft || handle == TransformHandle::Bottom ||
                            handle == TransformHandle::BottomRight;
  const bool corner = (moves_left || moves_right) && (moves_top || moves_bottom);
  const double ratio = corner && (modifiers & Qt::ShiftModifier) != 0 && start.height() > 0
                           ? static_cast<double>(start.width()) / start.height()
                           : 0.0;
  if ((modifiers & Qt::AltModifier) != 0) {
    return center_anchored_handle_rect(start, moves_left || moves_right, moves_top || moves_bottom, point, ratio);
  }
  auto left = start.x();
  auto top = start.y();
  auto right = start.x() + start.width();
  auto bottom = start.y() + start.height();
  if (moves_left) {
    left = point.x();
  }
  if (moves_right) {
    right = point.x();
  }
  if (moves_top) {
    top = point.y();
  }
  if (moves_bottom) {
    bottom = point.y();
  }
  QRect rect(std::min(left, right), std::min(top, bottom), std::max(1, std::abs(right - left)),
             std::max(1, std::abs(bottom - top)));
  if (ratio > 0.0) {
    // Shift on a corner keeps the starting aspect: the larger change wins.
    const auto anchor_x = moves_left ? start.x() + start.width() : start.x();
    const auto anchor_y = moves_top ? start.y() + start.height() : start.y();
    auto width = rect.width();
    auto height = rect.height();
    if (static_cast<double>(width) / height > ratio) {
      height = std::max(1, static_cast<int>(std::lround(width / ratio)));
    } else {
      width = std::max(1, static_cast<int>(std::lround(height * ratio)));
    }
    rect = QRect(point.x() < anchor_x ? anchor_x - width : anchor_x, point.y() < anchor_y ? anchor_y - height : anchor_y,
                 width, height);
  }
  return rect;
}

bool CanvasWidget::handle_artboard_label_press(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || document_ == nullptr || edit_locked_) {
    return false;
  }
  const auto label = artboard_label_at(event->pos());
  if (!label.has_value()) {
    return false;
  }
  request_artboard_edit({ArtboardEditRequest::Kind::Select, label, {}, {}, 0, false});
  // A press on the label also starts a move of the whole artboard, with any tool that
  // shows labels (Move and Artboard), as in Photoshop.
  if (tool_ == CanvasTool::Move || tool_ == CanvasTool::Artboard) {
    const auto* artboard = std::as_const(*document_).find_layer(*label);
    if (artboard != nullptr && artboard->artboard().has_value()) {
      begin_artboard_move_drag(*artboard, event);
    }
  }
  return true;
}

bool CanvasWidget::handle_artboard_label_double_click(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || document_ == nullptr || edit_locked_) {
    return false;
  }
  const auto label = artboard_label_at(event->pos());
  if (!label.has_value()) {
    return false;
  }
  cancel_artboard_drag();
  request_artboard_edit({ArtboardEditRequest::Kind::Rename, label, {}, {}, 0, false});
  return true;
}

bool CanvasWidget::handle_artboard_press(QMouseEvent* event) {
  if (tool_ != CanvasTool::Artboard || event->button() != Qt::LeftButton || document_ == nullptr) {
    return false;
  }
  if (edit_locked_) {
    return true;  // the tool owns the press; nothing may change
  }
  const auto& document = std::as_const(*document_);
  artboard_press_widget_point_ = event->pos();
  artboard_drag_start_point_ = document_position_f(event->position());
  artboard_drag_moved_ = false;
  artboard_drag_duplicate_ = false;
  artboard_drag_ghost_ = QImage();
  artboard_drag_vacated_ = QImage();

  // The add buttons, then the selected artboard's handles, then any label.
  if (const auto side = artboard_add_button_at(event->pos()); side >= 0) {
    request_artboard_edit({ArtboardEditRequest::Kind::AddAdjacent, selected_artboard_id(), {}, {}, side, false});
    return true;
  }
  if (const auto handle = artboard_handle_at(event->pos()); handle != TransformHandle::None) {
    const auto id = selected_artboard_id();
    artboard_drag_mode_ = ArtboardDragMode::Resize;
    artboard_drag_id_ = id;
    artboard_drag_handle_ = handle;
    artboard_drag_start_rect_ = qrect(document.find_layer(*id)->artboard()->rect);
    artboard_drag_rect_ = artboard_drag_start_rect_;
    return true;
  }
  if (handle_artboard_label_press(event)) {
    return true;
  }
  // Inside an artboard: select it, and a drag moves it with its content (Alt copies it).
  const auto point = document_position(event->pos());
  if (const auto* artboard = artboard_at(document, point.x(), point.y()); artboard != nullptr) {
    request_artboard_edit({ArtboardEditRequest::Kind::Select, artboard->id(), {}, {}, 0, false});
    artboard_drag_mode_ = ArtboardDragMode::Move;
    artboard_drag_id_ = artboard->id();
    artboard_drag_start_rect_ = qrect(artboard->artboard()->rect);
    artboard_drag_rect_ = artboard_drag_start_rect_;
    artboard_drag_duplicate_ = (event->modifiers() & Qt::AltModifier) != 0;
    return true;
  }
  // Empty space (pasteboard, gaps, loose content): lay out a new artboard.
  artboard_drag_mode_ = ArtboardDragMode::Create;
  artboard_drag_id_.reset();
  artboard_drag_anchor_document_ = snapped_document_point(point);
  artboard_drag_rect_ = QRect(artboard_drag_anchor_document_, QSize(1, 1));
  return true;
}

void CanvasWidget::update_artboard_drag(QMouseEvent* event) {
  if (artboard_drag_mode_ == ArtboardDragMode::None) {
    return;
  }
  if (!artboard_drag_moved_ &&
      (event->pos() - artboard_press_widget_point_).manhattanLength() >= QApplication::startDragDistance()) {
    artboard_drag_moved_ = true;
    if (artboard_drag_mode_ == ArtboardDragMode::Move && document_ != nullptr && artboard_drag_id_.has_value()) {
      // The artboard alone (its paper and content, nothing around or over it), carried
      // along at the moving rect, and the canvas without it where it was.
      const auto& document = std::as_const(*document_);
      const auto rect = artboard_drag_start_rect_.intersected(QRect(0, 0, document.width(), document.height()));
      if (!rect.isEmpty()) {
        std::vector<LayerId> others;
        for (const auto& layer : document.layers()) {
          if (layer.id() != *artboard_drag_id_) {
            others.push_back(layer.id());
          }
        }
        artboard_drag_ghost_ = qimage_from_document_rect_with_hidden_layers_banded(document, rect, true, others);
        if (!artboard_drag_duplicate_) {
          artboard_drag_vacated_ =
              qimage_from_document_rect_with_hidden_layers_banded(document, rect, true, {*artboard_drag_id_});
        }
      }
    }
  }
  if (!artboard_drag_moved_) {
    return;
  }
  const auto point = document_position_f(event->position());
  // The pixel under the pointer, as the anchor was taken: a drag from pixel 20 to pixel
  // 120 is 100 px wide.
  const auto pixel = document_position(event->pos());
  switch (artboard_drag_mode_) {
    case ArtboardDragMode::Create:
      artboard_drag_rect_ =
          artboard_create_rect(artboard_drag_anchor_document_, snapped_document_point(pixel), event->modifiers());
      break;
    case ArtboardDragMode::Move: {
      const QPoint raw_delta(static_cast<int>(std::lround(point.x() - artboard_drag_start_point_.x())),
                             static_cast<int>(std::lround(point.y() - artboard_drag_start_point_.y())));
      auto delta = snapped_rect_delta(artboard_drag_start_rect_, raw_delta);
      if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        // Shift keeps the move on one axis.
        if (std::abs(delta.x()) >= std::abs(delta.y())) {
          delta.setY(0);
        } else {
          delta.setX(0);
        }
      }
      artboard_drag_rect_ = artboard_drag_start_rect_.translated(delta);
      artboard_drag_duplicate_ = artboard_drag_duplicate_ || (event->modifiers() & Qt::AltModifier) != 0;
      break;
    }
    case ArtboardDragMode::Resize:
      artboard_drag_rect_ = artboard_resize_rect(snapped_document_point(pixel), event->modifiers());
      break;
    case ArtboardDragMode::None:
      break;
  }
  update();
}

void CanvasWidget::finish_artboard_drag(QMouseEvent* event) {
  if (artboard_drag_mode_ == ArtboardDragMode::None) {
    return;
  }
  update_artboard_drag(event);
  const auto mode = artboard_drag_mode_;
  const auto id = artboard_drag_id_;
  const auto moved = artboard_drag_moved_;
  const auto rect = artboard_drag_rect_;
  const QRect start = artboard_drag_start_rect_;
  const auto duplicate = artboard_drag_duplicate_;
  cancel_artboard_drag();
  if (!moved) {
    return;  // a click: the press already selected what it hit
  }
  switch (mode) {
    case ArtboardDragMode::Create:
      if (rect.width() >= 2 && rect.height() >= 2) {
        request_artboard_edit({ArtboardEditRequest::Kind::Create, std::nullopt, rect, {}, 0, false});
      }
      break;
    case ArtboardDragMode::Move:
      if (rect.topLeft() != start.topLeft() || duplicate) {
        request_artboard_edit(
            {ArtboardEditRequest::Kind::Move, id, rect, rect.topLeft() - start.topLeft(), 0, duplicate});
      }
      break;
    case ArtboardDragMode::Resize:
      if (rect != start) {
        request_artboard_edit({ArtboardEditRequest::Kind::Resize, id, rect, {}, 0, false});
      }
      break;
    case ArtboardDragMode::None:
      break;
  }
}

void CanvasWidget::cancel_artboard_drag() {
  const bool had_drag = artboard_drag_mode_ != ArtboardDragMode::None;
  artboard_drag_mode_ = ArtboardDragMode::None;
  artboard_drag_id_.reset();
  artboard_drag_handle_ = TransformHandle::None;
  artboard_drag_moved_ = false;
  artboard_drag_duplicate_ = false;
  artboard_drag_ghost_ = QImage();
  artboard_drag_vacated_ = QImage();
  if (had_drag) {
    update();
  }
}

bool CanvasWidget::nudge_selected_artboard(QPoint delta) {
  if ((tool_ != CanvasTool::Artboard && tool_ != CanvasTool::Move) || delta.isNull() || edit_locked_ ||
      artboard_drag_active()) {
    return false;
  }
  // The Artboard tool nudges the artboard of whatever is selected; the Move tool only an
  // artboard selected itself (a layer inside one nudges as a layer).
  const auto id = tool_ == CanvasTool::Artboard ? selected_artboard_id() : selected_root_artboard_id();
  if (!id.has_value()) {
    return false;
  }
  const auto* artboard = std::as_const(*document_).find_layer(*id);
  const auto& rect = artboard->artboard()->rect;
  request_artboard_edit({ArtboardEditRequest::Kind::Move, id,
                         QRect(rect.x + delta.x(), rect.y + delta.y(), rect.width, rect.height), delta, 0, false,
                         true});
  return true;
}

void CanvasWidget::update_artboard_tool_cursor(QPoint widget_point) {
  if (tool_ != CanvasTool::Artboard || document_ == nullptr) {
    return;
  }
  if (artboard_drag_mode_ == ArtboardDragMode::Move) {
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (artboard_add_button_at(widget_point) >= 0) {
    setCursor(Qt::PointingHandCursor);
    return;
  }
  switch (artboard_drag_mode_ == ArtboardDragMode::Resize ? artboard_drag_handle_ : artboard_handle_at(widget_point)) {
    case TransformHandle::TopLeft:
    case TransformHandle::BottomRight:
      setCursor(Qt::SizeFDiagCursor);
      return;
    case TransformHandle::TopRight:
    case TransformHandle::BottomLeft:
      setCursor(Qt::SizeBDiagCursor);
      return;
    case TransformHandle::Left:
    case TransformHandle::Right:
      setCursor(Qt::SizeHorCursor);
      return;
    case TransformHandle::Top:
    case TransformHandle::Bottom:
      setCursor(Qt::SizeVerCursor);
      return;
    default:
      break;
  }
  const auto point = document_position(widget_point);
  if (artboard_label_at(widget_point).has_value() || artboard_at(std::as_const(*document_), point.x(), point.y()) != nullptr) {
    setCursor(Qt::OpenHandCursor);
    return;
  }
  setCursor(Qt::CrossCursor);
}

void CanvasWidget::draw_canvas_backdrop(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const {
  if (document_ == nullptr || !document_has_artboards(std::as_const(*document_))) {
    draw_checkerboard(painter, target_rect, exposed_rect);
    return;
  }
  // An artboard document: between the artboards is pasteboard (the composite there is
  // transparent, and only loose layers paint it); inside each artboard the usual
  // checkerboard shows what a Transparent background lets through.
  painter.fillRect(target_rect.intersected(QRectF(exposed_rect)), backdrop_color());
  for (const auto* artboard : document_artboards(std::as_const(*document_))) {
    if (!artboard->visible()) {
      continue;
    }
    const auto& rect = artboard->artboard()->rect;
    draw_checkerboard(painter, widget_rect_for_document_rect(QRectF(rect.x, rect.y, rect.width, rect.height)).intersected(target_rect),
                      exposed_rect);
  }
}

namespace {

// Perceived lightness of an sRGB color on 0..255, for the label ink decision.
double srgb_luma(const QColor& color) {
  return 0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue();
}

}  // namespace

CanvasWidget::ArtboardLabelBacking CanvasWidget::artboard_label_backing(const std::vector<const Layer*>& artboards,
                                                                        const QRectF& label_widget_rect) const {
  if (document_ == nullptr || label_widget_rect.isEmpty()) {
    return {};
  }
  const auto& document = std::as_const(*document_);
  // The frame is sampled at document resolution when the cache holds one; a label
  // gets a small fixed grid of samples whatever its size or the zoom.
  const bool frame_available = !render_cache_.isNull() && render_cache_.width() == document.width() &&
                               render_cache_.height() == document.height();
  const auto checkerboard =
      QColor((checkerboard_dark_square().red() + checkerboard_light_square().red()) / 2,
             (checkerboard_dark_square().green() + checkerboard_light_square().green()) / 2,
             (checkerboard_dark_square().blue() + checkerboard_light_square().blue()) / 2);
  const auto pasteboard = backdrop_color();
  constexpr int kColumns = 6;
  constexpr int kRows = 3;
  // The crossover where the dark ink starts to contrast better than the light one.
  constexpr double kLightBackingLuma = 118.0;
  int light_samples = 0;
  int samples = 0;
  for (int row = 0; row < kRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const QPointF widget_point(label_widget_rect.left() + (column + 0.5) * label_widget_rect.width() / kColumns,
                                 label_widget_rect.top() + (row + 0.5) * label_widget_rect.height() / kRows);
      const auto document_point = document_position_f(widget_point);
      const int x = static_cast<int>(std::floor(document_point.x()));
      const int y = static_cast<int>(std::floor(document_point.y()));
      // What shows where the composite is clear: inside an artboard its paper (the
      // checkerboard for a Transparent one), between artboards the pasteboard.
      QColor under = pasteboard;
      for (const auto* artboard : artboards) {
        if (artboard->visible() && artboard->artboard()->rect.contains(x, y)) {
          const auto paper = artboard_background_rgba(*artboard->artboard());
          under = paper.has_value() && (*paper)[3] == 255 ? QColor((*paper)[0], (*paper)[1], (*paper)[2])
                                                           : checkerboard;
          break;
        }
      }
      QColor shown = under;
      if (frame_available && x >= 0 && y >= 0 && x < document.width() && y < document.height()) {
        const auto pixel = render_cache_.pixelColor(x, y);
        const double alpha = pixel.alphaF();
        shown = QColor::fromRgbF(static_cast<float>(pixel.redF() * alpha + under.redF() * (1.0 - alpha)),
                                 static_cast<float>(pixel.greenF() * alpha + under.greenF() * (1.0 - alpha)),
                                 static_cast<float>(pixel.blueF() * alpha + under.blueF() * (1.0 - alpha)));
      }
      if (srgb_luma(shown) > kLightBackingLuma) {
        ++light_samples;
      }
      ++samples;
    }
  }
  // Mixed from a sample row's worth of the minority on: a label hanging a few pixels
  // over a neighbouring board's edge gets the halo, a stray dark stroke does not.
  const int minority = std::min(light_samples, samples - light_samples);
  return {light_samples * 2 > samples, minority >= kColumns};
}

void CanvasWidget::draw_artboard_overlay(QPainter& painter) const {
  if (document_ == nullptr) {
    return;
  }
  const auto& document = std::as_const(*document_);
  const auto artboards = document_artboards(document);
  const auto selected = selected_artboard_id();
  const auto& colors = theme();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, false);
  const auto font = artboard_label_font(*this);
  painter.setFont(font);
  const QFontMetricsF metrics(font);

  // The moving artboard at the drag rect, and the canvas without it where it was (an
  // Alt-drag copy leaves the original in place).
  if (artboard_drag_mode_ == ArtboardDragMode::Move && artboard_drag_moved_ && !artboard_drag_ghost_.isNull()) {
    const auto clipped = artboard_drag_start_rect_.intersected(QRect(0, 0, document_->width(), document_->height()));
    const auto offset = artboard_drag_rect_.topLeft() - artboard_drag_start_rect_.topLeft();
    if (!artboard_drag_vacated_.isNull() && !artboard_drag_duplicate_) {
      painter.drawImage(widget_rect_for_document_rect(QRectF(clipped)), artboard_drag_vacated_);
    }
    painter.drawImage(widget_rect_for_document_rect(QRectF(clipped.translated(offset))), artboard_drag_ghost_);
  }

  // The rect an artboard is shown at: its own, or the drag rect while it is carried.
  const auto shown_rect = [&](const Layer& artboard) {
    const bool is_dragged = artboard_drag_active() && artboard_drag_moved_ && artboard_drag_id_ == artboard.id() &&
                            artboard_drag_mode_ != ArtboardDragMode::Create;
    if (is_dragged && !artboard_drag_duplicate_) {
      return Rect{artboard_drag_rect_.x(), artboard_drag_rect_.y(), artboard_drag_rect_.width(),
                  artboard_drag_rect_.height()};
    }
    return artboard.artboard()->rect;
  };
  // Every outline first, then every label: a label hanging over a neighbouring board
  // (its name sits above its own top edge) must not be crossed by that board's outline.
  for (const auto* artboard : artboards) {
    if (!artboard->visible()) {
      continue;
    }
    const auto rect = shown_rect(*artboard);
    const bool is_selected = selected.has_value() && *selected == artboard->id();
    const bool is_dragged = artboard_drag_active() && artboard_drag_moved_ && artboard_drag_id_ == artboard->id() &&
                            artboard_drag_mode_ != ArtboardDragMode::Create;
    const auto widget_rect = widget_rect_for_document_rect(QRectF(rect.x, rect.y, rect.width, rect.height));
    QPen outline(is_selected ? colors.canvas_artboard_selected : colors.canvas_artboard_outline, is_selected ? 2.0 : 1.0);
    outline.setCosmetic(true);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(widget_rect.adjusted(0.5, 0.5, -0.5, -0.5));
    if (is_dragged && artboard_drag_duplicate_) {
      // An Alt-drag copies: the copy's outline follows the pointer.
      QPen copy_pen(colors.canvas_artboard_selected, 1.0, Qt::DashLine);
      copy_pen.setCosmetic(true);
      painter.setPen(copy_pen);
      painter.drawRect(widget_rect_for_document_rect(QRectF(artboard_drag_rect_)).adjusted(0.5, 0.5, -0.5, -0.5));
    }
  }
  for (const auto* artboard : artboards) {
    if (!artboard->visible()) {
      continue;
    }
    const auto rect = shown_rect(*artboard);
    const bool is_selected = selected.has_value() && *selected == artboard->id();
    const auto widget_rect = widget_rect_for_document_rect(QRectF(rect.x, rect.y, rect.width, rect.height));
    // The name, above the top-left corner and elided to the artboard's width.
    const auto label = artboard_label_widget_rect(*artboard).translated(widget_rect.topLeft() -
                                                                        widget_position_f(QPointF(artboard->artboard()->rect.x,
                                                                                                  artboard->artboard()->rect.y)));
    const auto text = metrics.elidedText(QString::fromStdString(artboard->name()), Qt::ElideRight, label.width());
    // The ink follows the backing (GitHub issue 260); a hovered label (GitHub issue 259)
    // and a selected one take the selected color. A label straddling light and dark (over
    // a neighbouring board's edge) gets a one-pixel halo in the other ink, so the part on
    // the pasteboard reads as well as the part on the paper.
    const bool is_hovered = tool_ == CanvasTool::Move && move_hover_artboard_id_.has_value() &&
                            *move_hover_artboard_id_ == artboard->id();
    const auto backing = artboard_label_backing(artboards, label);
    const auto label_ink = backing.light ? colors.canvas_artboard_label_on_light : colors.canvas_artboard_label;
    if (backing.mixed) {
      painter.setPen(backing.light ? colors.canvas_artboard_label : colors.canvas_artboard_label_on_light);
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx != 0 || dy != 0) {
            painter.drawText(label.translated(dx, dy), Qt::AlignLeft | Qt::AlignVCenter, text);
          }
        }
      }
    }
    painter.setPen(is_selected || is_hovered ? colors.canvas_artboard_selected : label_ink);
    painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, text);
  }

  if (tool_ == CanvasTool::Artboard) {
    if (artboard_drag_mode_ == ArtboardDragMode::Create && artboard_drag_moved_) {
      QPen pen(colors.canvas_artboard_selected, 1.0, Qt::DashLine);
      pen.setCosmetic(true);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(widget_rect_for_document_rect(QRectF(artboard_drag_rect_)).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    const auto* artboard = selected.has_value() ? document.find_layer(*selected) : nullptr;
    if (artboard != nullptr && artboard->visible() && artboard_drag_mode_ != ArtboardDragMode::Move) {
      const auto rect = artboard_drag_mode_ == ArtboardDragMode::Resize
                            ? QRectF(artboard_drag_rect_)
                            : QRectF(artboard->artboard()->rect.x, artboard->artboard()->rect.y,
                                     artboard->artboard()->rect.width, artboard->artboard()->rect.height);
      if (artboard_drag_mode_ == ArtboardDragMode::Resize) {
        QPen pen(colors.canvas_artboard_selected, 1.0, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(widget_rect_for_document_rect(rect).adjusted(0.5, 0.5, -0.5, -0.5));
      }
      painter.setPen(QPen(colors.canvas_artboard_selected, 1.0));
      painter.setBrush(colors.canvas_artboard_handle);
      for (const auto handle : {TransformHandle::TopLeft, TransformHandle::Top, TransformHandle::TopRight,
                                TransformHandle::Right, TransformHandle::BottomRight, TransformHandle::Bottom,
                                TransformHandle::BottomLeft, TransformHandle::Left}) {
        const auto point = transform_handle_position(handle, rect, 0.0);
        painter.drawRect(QRectF(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize));
      }
      if (!artboard_drag_active()) {
        // Add buttons: a "+" in a circle beside each edge adds an artboard of this size there.
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (int side = 0; side < 4; ++side) {
          const auto button = artboard_add_button_widget_rect(qrect(artboard->artboard()->rect), side);
          painter.setPen(QPen(colors.canvas_artboard_selected, 1.2));
          painter.setBrush(colors.canvas_hud_bg);
          painter.drawEllipse(button);
          painter.setPen(QPen(colors.canvas_hud_text, 1.6, Qt::SolidLine, Qt::RoundCap));
          const auto center = button.center();
          painter.drawLine(QPointF(center.x() - 4.0, center.y()), QPointF(center.x() + 4.0, center.y()));
          painter.drawLine(QPointF(center.x(), center.y() - 4.0), QPointF(center.x(), center.y() + 4.0));
        }
      }
    }
  }
  painter.restore();
}

namespace {

// The topmost visible pixel layer in `layers` (visible folders searched) that a brush can
// paint: not locked, not a procedural (text, shape, Smart Object) raster.
const Layer* topmost_paintable_pixel_layer(const std::vector<Layer>& layers, const std::vector<Layer>& roots) {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if (!it->visible()) {
      continue;
    }
    if (it->kind() == LayerKind::Group) {
      if (const auto* found = topmost_paintable_pixel_layer(it->children(), roots); found != nullptr) {
        return found;
      }
      continue;
    }
    if (it->kind() == LayerKind::Pixel && !layer_pixels_are_procedural(*it) && !layer_locks_image_pixels(*it) &&
        !layer_is_effectively_locked(roots, it->id())) {
      return &*it;
    }
  }
  return nullptr;
}

}  // namespace

std::optional<LayerId> CanvasWidget::retarget_paint_to_clicked_artboard(QPoint document_point) {
  if (document_ == nullptr || quick_mask_active_ || layer_edit_target_ != LayerEditTarget::Content ||
      !document_has_artboards(std::as_const(*document_))) {
    return std::nullopt;
  }
  const auto& document = std::as_const(*document_);
  const auto* clicked = artboard_at(document, document_point.x(), document_point.y());
  if (clicked == nullptr) {
    return std::nullopt;
  }
  const auto active = document.active_layer_id();
  if (active.has_value() && *active != clicked->id()) {
    if (const auto* owner = artboard_owning_layer(document, *active); owner != nullptr && owner->id() == clicked->id()) {
      return std::nullopt;  // the active layer is already on the clicked artboard
    }
  }
  const auto artboard_name = QString::fromStdString(clicked->name());
  if (const auto* target = topmost_paintable_pixel_layer(clicked->children(), document.layers()); target != nullptr) {
    const auto target_id = target->id();
    const auto target_name = QString::fromStdString(target->name());
    request_layer_selection({target_id}, target_id);
    if (status_callback_) {
      status_callback_(tr("Painting on %1 in %2").arg(target_name, artboard_name));
    }
    return std::nullopt;
  }
  return clicked->id();
}

void CanvasWidget::add_paint_layer_on_artboard(std::optional<LayerId> artboard_id) {
  if (!artboard_id.has_value() || document_ == nullptr) {
    return;
  }
  auto* artboard = document_->find_layer(*artboard_id);
  if (artboard == nullptr || !layer_is_artboard(std::as_const(*artboard))) {
    return;
  }
  // The artboard's size, not the canvas: a grid of imported pages can be a 100+ Mpx canvas.
  const auto rect = std::as_const(*artboard).artboard()->rect;
  PixelBuffer pixels(std::max(1, rect.width), std::max(1, rect.height),
                     with_bit_depth(PixelFormat::rgba8(), std::as_const(*document_).color_state().bit_depth));
  pixels.clear(0);
  const auto name = default_new_layer_name(std::as_const(*document_));
  Layer layer(document_->allocate_layer_id(), name.toStdString(), std::move(pixels));
  layer.set_bounds(rect);
  const auto layer_id = layer.id();
  artboard->add_child(std::move(layer));
  document_->set_active_layer(layer_id);
  if (layers_moved_callback_) {
    layers_moved_callback_({layer_id});  // the host refreshes its panels for the new row
  }
  request_layer_selection({layer_id}, layer_id);
  if (status_callback_) {
    status_callback_(tr("Added %1 to %2 to paint on").arg(name, QString::fromStdString(std::as_const(*artboard).name())));
  }
}

}  // namespace patchy::ui
