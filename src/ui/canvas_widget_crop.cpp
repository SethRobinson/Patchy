// CanvasWidget's Crop tool session. Picking the tool frames the canvas (or the
// active selection) with handles; a drag inside that default box lays out a
// new document-space rect (which may extend past the canvas onto the
// pasteboard), handles adjust it, and Enter/Apply hands the rect to MainWindow
// through the crop-commit callback; the document itself is only mutated there.
// Escape resets the box to the canvas; a tool switch, an edit lock, or a
// document swap cancel the session without committing (the swap then frames
// the new document).

#include "ui/canvas_widget.hpp"
#include "ui/theme_palette.hpp"

#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/ruler_tool.hpp"
#include "ui/tool_cursors.hpp"

#include <QApplication>
#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <Qt>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;

// Normalizes to (-180, 180] so the readout and snap behave near the wrap.
double normalized_degrees(double degrees) {
  degrees = std::fmod(degrees, 360.0);
  if (degrees > 180.0) {
    degrees -= 360.0;
  } else if (degrees <= -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

// The composition guides inside the crop box, in the box's local frame (origin
// at its center). Axis-aligned lines come in groups drawn dark-then-light per
// group, which keeps the original rule-of-thirds pixels exact; oblique lines and
// the spiral's arcs are drawn antialiased and clipped to the box.
struct CropGuides {
  std::vector<std::vector<QLineF>> axis_groups;
  std::vector<QLineF> oblique;
  QPainterPath arcs;
};

QLineF vertical_guide(const QRectF& box, double x) {
  return {QPointF(x, box.top()), QPointF(x, box.bottom())};
}

QLineF horizontal_guide(const QRectF& box, double y) {
  return {QPointF(box.left(), y), QPointF(box.right(), y)};
}

QPointF foot_on_line(QPointF point, QPointF a, QPointF b) {
  const auto direction = b - a;
  const auto length_squared = QPointF::dotProduct(direction, direction);
  if (length_squared <= 0.0) {
    return a;
  }
  return a + direction * (QPointF::dotProduct(point - a, direction) / length_squared);
}

// Fibonacci-style golden spiral: a golden rectangle (phi x 1) split into a
// square and a smaller golden rectangle ten times over, one quarter arc per
// square. It is stretched to the box, turned upright for a portrait box, and
// flipped horizontally (orientation bit 0) and vertically (bit 1).
void add_golden_spiral(CropGuides& guides, const QRectF& box, int orientation) {
  constexpr double phi = std::numbers::phi;
  const bool portrait = box.height() > box.width();
  const auto map = [&](QPointF point) {
    auto along = point.x() / phi;
    auto across = point.y();
    if ((orientation & 1) != 0) {
      along = 1.0 - along;
    }
    if ((orientation & 2) != 0) {
      across = 1.0 - across;
    }
    return portrait ? QPointF(box.left() + across * box.width(), box.top() + along * box.height())
                    : QPointF(box.left() + along * box.width(), box.top() + across * box.height());
  };
  std::vector<QLineF> dividers;
  QRectF rest(0.0, 0.0, phi, 1.0);
  constexpr int kArcSamples = 24;
  for (int step = 0; step < 10; ++step) {
    // The square comes off the left, top, right, then bottom; each arc
    // is centered on the square's corner that touches the remaining rectangle.
    QRectF square;
    QPointF center;
    QLineF divider;
    switch (step % 4) {
      case 0:
        square = QRectF(rest.left(), rest.top(), rest.height(), rest.height());
        center = square.bottomRight();
        divider = QLineF(square.topRight(), square.bottomRight());
        rest.setLeft(square.right());
        break;
      case 1:
        square = QRectF(rest.left(), rest.top(), rest.width(), rest.width());
        center = square.bottomLeft();
        divider = QLineF(square.bottomLeft(), square.bottomRight());
        rest.setTop(square.bottom());
        break;
      case 2:
        square = QRectF(rest.right() - rest.height(), rest.top(), rest.height(), rest.height());
        center = square.topLeft();
        divider = QLineF(square.topLeft(), square.bottomLeft());
        rest.setRight(square.left());
        break;
      default:
        square = QRectF(rest.left(), rest.bottom() - rest.width(), rest.width(), rest.width());
        center = square.topRight();
        divider = QLineF(square.topLeft(), square.topRight());
        rest.setBottom(square.top());
        break;
    }
    dividers.emplace_back(map(divider.p1()), map(divider.p2()));
    // Angles in the y-down frame: each arc turns a quarter clockwise on screen,
    // starting at 180, 270, 0 and 90 degrees for the four cuts.
    const auto radius = square.width();
    const auto start_degrees = 180.0 + 90.0 * (step % 4);
    for (int i = 0; i <= kArcSamples; ++i) {
      const auto radians = (start_degrees + 90.0 * i / kArcSamples) * std::numbers::pi / 180.0;
      const auto point = map(center + QPointF(std::cos(radians) * radius, std::sin(radians) * radius));
      if (step == 0 && i == 0) {
        guides.arcs.moveTo(point);
      } else {
        guides.arcs.lineTo(point);
      }
    }
  }
  guides.axis_groups.push_back(std::move(dividers));
}

CropGuides crop_overlay_guides(CanvasWidget::CropOverlay overlay, int orientation, const QRectF& box) {
  using Overlay = CanvasWidget::CropOverlay;
  CropGuides guides;
  const auto divide = [&](int count_x, int count_y) {
    for (int i = 1; i < std::max(count_x, count_y); ++i) {
      std::vector<QLineF> group;
      if (i < count_x) {
        group.push_back(vertical_guide(box, box.left() + box.width() * i / static_cast<double>(count_x)));
      }
      if (i < count_y) {
        group.push_back(horizontal_guide(box, box.top() + box.height() * i / static_cast<double>(count_y)));
      }
      guides.axis_groups.push_back(std::move(group));
    }
  };
  switch (overlay) {
    case Overlay::None:
      break;
    case Overlay::Thirds:
      divide(3, 3);
      break;
    case Overlay::Grid: {
      // Near-square cells, six across the box's shorter side.
      static constexpr int kShortCells = 6;
      static constexpr int kMaxLongCells = 48;
      const auto long_cells = [](double long_side, double short_side) {
        return std::clamp(static_cast<int>(std::lround(kShortCells * long_side / short_side)), kShortCells,
                          kMaxLongCells);
      };
      if (box.width() >= box.height()) {
        divide(long_cells(box.width(), box.height()), kShortCells);
      } else {
        divide(kShortCells, long_cells(box.height(), box.width()));
      }
      break;
    }
    case Overlay::Diagonal:
      guides.oblique = {QLineF(box.topLeft(), box.bottomRight()), QLineF(box.topRight(), box.bottomLeft())};
      break;
    case Overlay::Triangle: {
      // One diagonal, plus a line from each remaining corner meeting it at a
      // right angle; orientation 1 mirrors it.
      const bool mirrored = (orientation & 1) != 0;
      const auto a = mirrored ? box.topRight() : box.topLeft();
      const auto b = mirrored ? box.bottomLeft() : box.bottomRight();
      const auto c = mirrored ? box.topLeft() : box.topRight();
      const auto d = mirrored ? box.bottomRight() : box.bottomLeft();
      guides.oblique = {QLineF(a, b), QLineF(c, foot_on_line(c, a, b)), QLineF(d, foot_on_line(d, a, b))};
      break;
    }
    case Overlay::GoldenRatio: {
      // Lines at 1 / phi^2 and 1 / phi (0.382 and 0.618) of each side.
      constexpr double near_fraction = 1.0 / (std::numbers::phi * std::numbers::phi);
      constexpr double far_fraction = 1.0 / std::numbers::phi;
      for (const auto fraction : {near_fraction, far_fraction}) {
        guides.axis_groups.push_back({vertical_guide(box, box.left() + box.width() * fraction),
                                      horizontal_guide(box, box.top() + box.height() * fraction)});
      }
      break;
    }
    case Overlay::GoldenSpiral:
      add_golden_spiral(guides, box, orientation);
      break;
  }
  return guides;
}

// The dark twin of a light guide line sits one pixel to the right of a vertical
// line and one pixel below any other, measured square to the line so an
// antialiased oblique twin clears the light line as fully as a straight one.
QLineF dark_twin(const QLineF& line) {
  if (line.dx() == 0.0) {
    return line.translated(1.0, 0.0);
  }
  if (line.dy() == 0.0) {
    return line.translated(0.0, 1.0);
  }
  const auto length = std::hypot(line.dx(), line.dy());
  QPointF normal(-line.dy() / length, line.dx() / length);
  if (normal.y() < 0.0) {
    normal = -normal;
  }
  return line.translated(normal);
}

}  // namespace

bool CanvasWidget::crop_session_active() const noexcept {
  return crop_session_active_;
}

double CanvasWidget::crop_session_angle() const noexcept {
  return crop_angle_;
}

std::optional<QRect> CanvasWidget::crop_session_rect() const noexcept {
  if (!crop_session_active_) {
    return std::nullopt;
  }
  return crop_rect_;
}

bool CanvasWidget::crop_session_has_changes() const noexcept {
  return crop_session_active_ && (crop_angle_ != 0.0 || crop_rect_ != canvas_document_rect());
}

bool CanvasWidget::crop_session_can_reset() const {
  // With a ratio set the automatic frame is smaller than the canvas, so it has
  // changes to apply but nothing to reset (GitHub issue 318).
  return crop_session_active_ && (crop_box_from_selection_ || crop_angle_ != 0.0 ||
                                  crop_rect_ != ratio_fitted_crop_rect(canvas_document_rect()));
}

QRect CanvasWidget::canvas_document_rect() const noexcept {
  return document_ != nullptr ? QRect(0, 0, document_->width(), document_->height()) : QRect();
}

QRect CanvasWidget::ratio_fitted_crop_rect(QRect within) const {
  if (crop_ratio_w_ <= 0.0 || crop_ratio_h_ <= 0.0 || within.isEmpty()) {
    return within;
  }
  const auto ratio = crop_ratio_w_ / crop_ratio_h_;
  auto width = within.width();
  auto height = within.height();
  if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
    width = std::max(1, static_cast<int>(std::lround(height * ratio)));
  } else {
    height = std::max(1, static_cast<int>(std::lround(width / ratio)));
  }
  return QRect(within.x() + (within.width() - width) / 2, within.y() + (within.height() - height) / 2, width,
               height);
}

void CanvasWidget::set_crop_ratio(double width, double height) noexcept {
  const auto new_w = std::max(0.0, width);
  const auto new_h = std::max(0.0, height);
  const auto changed = new_w != crop_ratio_w_ || new_h != crop_ratio_h_;
  crop_ratio_w_ = new_w;
  crop_ratio_h_ = new_h;
  // A new ratio reshapes the pending box at once, so typing a ratio or picking
  // a preset shows the result (GitHub issue 66). The automatic canvas frame
  // re-fits from the whole canvas (so Clear grows it back); a custom box keeps
  // its width and center and derives the height, which makes the result
  // independent of the intermediate ratios a field passes through while it is
  // typed digit by digit (fitting inside itself would shrink cumulatively).
  // Clear leaves a custom box alone.
  if (!changed || !crop_session_active_) {
    return;
  }
  const auto constrained = crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0;
  QRect fitted = crop_rect_;
  if (crop_box_is_default_) {
    fitted = ratio_fitted_crop_rect(canvas_document_rect());
  } else if (constrained) {
    const auto derived_height =
        std::max(1, static_cast<int>(std::lround(crop_rect_.width() * crop_ratio_h_ / crop_ratio_w_)));
    fitted = QRect(crop_rect_.x(), crop_rect_.y() + (crop_rect_.height() - derived_height) / 2,
                   crop_rect_.width(), derived_height);
  }
  if (fitted != crop_rect_) {
    crop_rect_ = fitted;
    update();
    notify_crop_session_changed();
  }
}

void CanvasWidget::swap_crop_ratio() {
  std::swap(crop_ratio_w_, crop_ratio_h_);
  if (!crop_session_active_ || crop_ratio_w_ == crop_ratio_h_) {
    return;
  }
  QRect swapped = crop_rect_;
  if (crop_box_is_default_) {
    swapped = ratio_fitted_crop_rect(canvas_document_rect());
  } else if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
    // Turned in place: the box that fitted W : H fits H : W exactly once transposed.
    swapped = QRect(crop_rect_.x() + (crop_rect_.width() - crop_rect_.height()) / 2,
                    crop_rect_.y() + (crop_rect_.height() - crop_rect_.width()) / 2, crop_rect_.height(),
                    crop_rect_.width());
  }
  if (swapped != crop_rect_) {
    crop_rect_ = swapped;
    update();
    notify_crop_session_changed();
  }
}

double CanvasWidget::crop_ratio_width() const noexcept {
  return crop_ratio_w_;
}

double CanvasWidget::crop_ratio_height() const noexcept {
  return crop_ratio_h_;
}

void CanvasWidget::set_crop_overlay(CropOverlay overlay, int orientation, bool only_while_dragging) {
  orientation = ((orientation % 4) + 4) % 4;
  if (overlay == crop_overlay_ && orientation == crop_overlay_orientation_ &&
      only_while_dragging == crop_overlay_only_while_dragging_) {
    return;
  }
  crop_overlay_ = overlay;
  crop_overlay_orientation_ = orientation;
  crop_overlay_only_while_dragging_ = only_while_dragging;
  if (crop_session_active_ || crop_dragging_out_) {
    update();
  }
}

CanvasWidget::CropOverlay CanvasWidget::crop_overlay() const noexcept {
  return crop_overlay_;
}

int CanvasWidget::crop_overlay_orientation() const noexcept {
  return crop_overlay_orientation_;
}

bool CanvasWidget::crop_overlay_only_while_dragging() const noexcept {
  return crop_overlay_only_while_dragging_;
}

void CanvasWidget::set_crop_commit_requested_callback(std::function<void(QRect, double)> callback) {
  crop_commit_requested_callback_ = std::move(callback);
}

void CanvasWidget::set_crop_session_changed_callback(std::function<void()> callback) {
  crop_session_changed_callback_ = std::move(callback);
}

void CanvasWidget::notify_crop_session_changed() {
  if (crop_session_changed_callback_) {
    crop_session_changed_callback_();
  }
}

bool CanvasWidget::crop_view_follows_box() const noexcept {
  return crop_overlay_options_.move_image && crop_session_active_ && tool_ == CanvasTool::Crop;
}

QPointF CanvasWidget::crop_box_center_document() const noexcept {
  return QPointF(static_cast<double>(crop_rect_.x()) + crop_rect_.width() / 2.0,
                 static_cast<double>(crop_rect_.y()) + crop_rect_.height() / 2.0);
}

// Move Image Under Crop Box (GitHub issue 699). The view's angle is the user's own plus
// this session's turn, which is minus the box's angle while the view follows the box:
// the box then shows as upright as the document did before, and the image is what turns.
// Only the angle and the pan change here. The zoom is never touched: a rotation that set
// the zoom, or tested whether a canvas edge shows inside the box, is outside the limits
// in docs/legal-constraints.md ("Crop interaction").
bool CanvasWidget::sync_crop_view_turn(QPointF anchor_document, QPointF anchor_widget) {
  crop_view_turn_degrees_ = crop_view_follows_box() && crop_angle_ != 0.0 ? -crop_angle_ : 0.0;
  view_rotation_degrees_ =
      crop_view_turn_degrees_ == 0.0
          ? view_user_rotation_degrees_
          : CanvasViewTransform::normalized_angle(view_user_rotation_degrees_ + crop_view_turn_degrees_);
  const auto placed = view_transform().pan_placing(anchor_document, anchor_widget);
  pan_ = placed;
  constrain_pan();
  return pan_ == placed;
}

bool CanvasWidget::place_crop_box(QRect rect, double angle, QPointF center_widget) {
  const auto old_rect = crop_rect_;
  const auto old_angle = crop_angle_;
  const auto old_pan = pan_;
  const auto old_rotation = view_rotation_degrees_;
  const auto old_turn = crop_view_turn_degrees_;
  crop_rect_ = rect;
  crop_angle_ = angle;
  if (!sync_crop_view_turn(crop_box_center_document(), center_widget)) {
    // The pan limits keep part of the document in view; the box does not give way.
    crop_rect_ = old_rect;
    crop_angle_ = old_angle;
    pan_ = old_pan;
    view_rotation_degrees_ = old_rotation;
    crop_view_turn_degrees_ = old_turn;
    return false;
  }
  if (pan_ != old_pan || view_rotation_degrees_ != old_rotation) {
    notify_view_changed();
  }
  update();
  return true;
}

void CanvasWidget::set_crop_handle_cursor(TransformHandle handle, bool dragging) {
  if (handle == TransformHandle::Move && crop_view_follows_box()) {
    // Inside the box a drag slides the image, as the Hand does.
    setCursor(dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    return;
  }
  set_transform_cursor_for_handle(handle, crop_angle_);
}

void CanvasWidget::center_crop_box_in_view() {
  if (!crop_session_active_ || crop_rect_.isEmpty() || document_ == nullptr || width() <= 0 || height() <= 0) {
    return;
  }
  // A pan and nothing else: the zoom stays the user's (docs/legal-constraints.md,
  // "Crop interaction": no zoom follows a box change by itself).
  const auto old_pan = pan_;
  pan_ = view_transform().pan_placing(crop_box_center_document(), view_content_rect().center());
  constrain_pan();
  if (pan_ != old_pan) {
    update();
    notify_view_changed();
  }
}

void CanvasWidget::apply_crop_move_image_change() {
  if (!crop_session_active_ || tool_ != CanvasTool::Crop) {
    return;
  }
  // A drag in flight was measured for the other mode: it ends here, as on focus loss.
  crop_dragging_out_ = false;
  crop_rotating_ = false;
  crop_straightening_ = false;
  crop_drag_handle_ = TransformHandle::None;
  spacebar_repositioning_drag_rect_ = false;
  const auto wanted = crop_view_follows_box() && crop_angle_ != 0.0 ? -crop_angle_ : 0.0;
  if (wanted != crop_view_turn_degrees_) {
    // The image turns about the box's center, which stays where it is.
    const auto center = crop_box_center_document();
    sync_crop_view_turn(center, view_transform().map_to_widget(center));
    update_tool_cursor();
    update();
    notify_view_changed();
  }
  // Switching the option on with a box the user made centers that box.
  if (crop_view_follows_box() && crop_session_can_reset()) {
    center_crop_box_in_view();
  }
  update_tool_cursor();
}

void CanvasWidget::reset_crop_session_state() {
  // A session that turned the view gives the turn back: the image turns about the
  // box's center to the angle the user had (Move Image Under Crop Box).
  const auto had_turn = crop_view_turn_degrees_ != 0.0;
  const auto turn_anchor = crop_box_center_document();
  const auto turn_anchor_widget = had_turn ? view_transform().map_to_widget(turn_anchor) : QPointF();
  crop_session_active_ = false;
  crop_box_is_default_ = false;
  crop_box_from_selection_ = false;
  crop_dragging_out_ = false;
  crop_rotating_ = false;
  crop_drag_handle_ = TransformHandle::None;
  crop_anchor_document_ = QPoint();
  crop_current_document_ = QPoint();
  crop_press_widget_point_ = QPoint();
  crop_rect_ = QRect();
  crop_drag_start_rect_ = QRect();
  crop_drag_start_point_ = QPointF();
  crop_angle_ = 0.0;
  crop_rotate_start_angle_ = 0.0;
  crop_rotate_start_vector_degrees_ = 0.0;
  crop_square_constrained_ = false;
  crop_from_center_ = false;
  crop_hold_center_widget_ = QPointF();
  crop_straighten_armed_ = false;
  crop_straightening_ = false;
  crop_straighten_from_button_ = false;
  if (had_turn) {
    sync_crop_view_turn(turn_anchor, turn_anchor_widget);
    update();
    notify_view_changed();
  }
}

void CanvasWidget::commit_crop_session() {
  if (!crop_session_active_ || crop_rect_.isEmpty()) {
    return;
  }
  // The default canvas frame would re-create the same document and leave a
  // pointless undo step behind.
  if (!crop_session_has_changes()) {
    if (status_callback_) {
      status_callback_(tr("Nothing to crop: the box matches the canvas"));
    }
    return;
  }
  // State is left intact: a refused commit (smart filters, unparsed smart
  // objects) keeps the session adjustable; a successful one cancels it from
  // MainWindow before the document reset (which frames the new canvas).
  if (crop_commit_requested_callback_) {
    crop_commit_requested_callback_(crop_rect_, crop_angle_);
  }
}

void CanvasWidget::cancel_crop_session() {
  const auto had_session = crop_session_active_;
  if (!had_session && !crop_dragging_out_ && !crop_rotating_ &&
      crop_drag_handle_ == TransformHandle::None) {
    return;
  }
  // Leaving the tool with the untouched canvas frame is not worth a message.
  const auto had_changes = crop_session_can_reset();
  reset_crop_session_state();
  update_tool_cursor();
  update();
  if (had_changes && status_callback_) {
    status_callback_(tr("Crop cancelled"));
  }
  notify_crop_session_changed();
}

void CanvasWidget::begin_default_crop_session() {
  if (tool_ != CanvasTool::Crop || document_ == nullptr || edit_locked_) {
    return;
  }
  reset_crop_session_state();
  crop_session_active_ = true;
  if (!selection_.isEmpty() && !quick_mask_active_) {
    // The active selection becomes the crop box, overriding the ratio for this
    // box (GitHub issue 66 item 5); the marching ants hide while the session
    // runs so the box is the only outline. A commit clears the selection, a
    // tool switch brings it back.
    crop_rect_ = selection_.boundingRect();
    crop_box_is_default_ = false;
    crop_box_from_selection_ = true;
  } else {
    crop_rect_ = ratio_fitted_crop_rect(canvas_document_rect());
    crop_box_is_default_ = true;
  }
  update_tool_cursor();
  update();
  notify_crop_session_changed();
}

void CanvasWidget::set_crop_keep_proportions(bool keep) noexcept {
  crop_keep_proportions_ = keep;
}

bool CanvasWidget::crop_keep_proportions() const noexcept {
  return crop_keep_proportions_;
}

void CanvasWidget::follow_selection_into_crop_session() {
  if (tool_ != CanvasTool::Crop || !crop_session_active_ || crop_dragging_out_ || crop_rotating_ ||
      crop_drag_handle_ != TransformHandle::None) {
    return;
  }
  if (!crop_box_is_default_ && !crop_box_from_selection_) {
    return;
  }
  begin_default_crop_session();
}

void CanvasWidget::set_crop_session_size(QSize size) {
  if (!crop_session_active_ || size.width() < 1 || size.height() < 1) {
    return;
  }
  const QRect rect(crop_rect_.x() + (crop_rect_.width() - size.width()) / 2,
                   crop_rect_.y() + (crop_rect_.height() - size.height()) / 2, size.width(), size.height());
  if (rect == crop_rect_) {
    return;
  }
  crop_rect_ = rect;
  crop_box_is_default_ = false;
  update();
  notify_crop_session_changed();
}

void CanvasWidget::reset_crop_session_to_canvas() {
  if (tool_ != CanvasTool::Crop || document_ == nullptr || edit_locked_) {
    cancel_crop_session();
    return;
  }
  const auto had_changes = crop_session_can_reset();
  reset_crop_session_state();
  crop_session_active_ = true;
  crop_box_is_default_ = true;
  crop_rect_ = ratio_fitted_crop_rect(canvas_document_rect());
  if (had_changes && crop_view_follows_box()) {
    // The gestures moved the view; the frame goes back to the middle of it (a pan only).
    center_crop_box_in_view();
  }
  update_tool_cursor();
  update();
  if (had_changes && status_callback_) {
    status_callback_(tr("Crop box reset to the canvas"));
  }
  notify_crop_session_changed();
}

// The third deliberately-unmerged twin of marquee_selection_rect and
// shape_drag_rect (docs/tools.md): crop has no combine modes or fixed size,
// but its anchor may live on the pasteboard, so nothing is edge-clamped.
QRect CanvasWidget::crop_drag_rect(QPoint anchor, QPoint current) const {
  QRect rect;
  if (crop_from_center_) {
    // Alt draws from the center (GitHub issue 78): the press point is the
    // center, so the box grows symmetrically and is twice the drag extent. A
    // set ratio or Shift's 1:1 latch still constrains the result.
    const auto delta = current - anchor;
    auto half_w = std::abs(delta.x());
    auto half_h = std::abs(delta.y());
    if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
      const auto ratio = crop_ratio_w_ / crop_ratio_h_;
      if (static_cast<double>(half_w) / std::max(1, half_h) > ratio) {
        half_h = std::max(0, static_cast<int>(std::round(half_w / ratio)));
      } else {
        half_w = std::max(0, static_cast<int>(std::round(half_h * ratio)));
      }
    } else if (crop_square_constrained_) {
      half_w = half_h = std::min(half_w, half_h);
    }
    rect = QRect(anchor.x() - half_w, anchor.y() - half_h, half_w * 2, half_h * 2);
  } else if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
    const auto ratio = crop_ratio_w_ / crop_ratio_h_;
    const auto delta = current - anchor;
    auto width = std::max(1, std::abs(delta.x()));
    auto height = std::max(1, std::abs(delta.y()));
    if (static_cast<double>(width) / static_cast<double>(height) > ratio) {
      width = std::max(1, static_cast<int>(std::round(height * ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / ratio)));
    }
    const auto signed_width = delta.x() < 0 ? -width : width;
    const auto signed_height = delta.y() < 0 ? -height : height;
    rect = QRect(anchor, anchor + QPoint(signed_width, signed_height)).normalized();
  } else if (crop_square_constrained_) {
    const auto delta = current - anchor;
    const auto side = std::max(1, std::min(std::abs(delta.x()), std::abs(delta.y())));
    const auto x = delta.x() < 0 ? anchor.x() - side : anchor.x();
    const auto y = delta.y() < 0 ? anchor.y() - side : anchor.y();
    rect = QRect(x, y, side, side);
  } else {
    rect = normalized_rect(anchor, current);
  }
  // Keep at least 1px per axis (QRect::normalized() collapses to 0-size when
  // the cursor lands exactly 1px before the anchor).
  if (rect.width() < 1) {
    rect.setWidth(1);
  }
  if (rect.height() < 1) {
    rect.setHeight(1);
  }
  return rect;
}

CanvasWidget::TransformHandle CanvasWidget::crop_handle_at(QPoint widget_point) const {
  if (!crop_session_active_) {
    return TransformHandle::None;
  }
  const auto handle =
      transform_handle_at(widget_point, QRectF(crop_rect_), crop_angle_, TransformHitZones::EdgesAndCorners);
  // The shared hit-test reports the rotate stem hanging above the top edge and
  // the bands just outside the corners; crop rotates by dragging anywhere
  // outside the box instead, so those clicks must not grab a phantom handle.
  // The edge bands resize like on the Move tool's box (GitHub issue 176).
  return handle == TransformHandle::Rotate ? TransformHandle::None : handle;
}

void CanvasWidget::begin_crop_drag_out(QMouseEvent* event, QPoint document_point) {
  const auto snapped = snapped_document_point(document_point);
  crop_dragging_out_ = true;
  spacebar_repositioning_drag_rect_ = false;
  crop_press_widget_point_ = event->pos();
  crop_anchor_document_ = snapped;
  crop_current_document_ = snapped;
  crop_angle_ = 0.0;
  crop_square_constrained_ = (event->modifiers() & Qt::ShiftModifier) != 0;
  // Alt mirrors the box about the press point (Photoshop's draw-from-center);
  // moves and key events keep both latches current mid-drag (GitHub issue 78).
  crop_from_center_ = (event->modifiers() & Qt::AltModifier) != 0;
  update();
}

void CanvasWidget::handle_crop_session_press(QMouseEvent* event) {
  if (crop_straighten_armed_ || event->modifiers().testFlag(Qt::ControlModifier)) {
    // Straighten (GitHub issue 702): the press starts a line wherever it lands, the
    // handles and the inside included. Ctrl held is the same gesture for one line.
    begin_crop_straighten_line(event);
    return;
  }
  const auto handle = crop_handle_at(event->pos());
  const auto on_handle = handle != TransformHandle::None && handle != TransformHandle::Move;
  if (crop_box_is_default_ && !on_handle &&
      canvas_document_rect().contains(document_position(event->pos()))) {
    // The automatic canvas frame has no interior to move: a press on the
    // canvas (inside the frame or beside a ratio-fitted one) lays out a new
    // box; a mere click keeps the frame. The handles work as on any box.
    begin_crop_drag_out(event, document_position(event->pos()));
    return;
  }
  if (handle == TransformHandle::None) {
    // A press off the box starts the rotate gesture (Photoshop's straighten):
    // the box pivots about its center to follow the drag. A plain click (no
    // travel) leaves the angle untouched. Esc resets to the canvas frame, where
    // a drag lays out a new rect.
    crop_rotating_ = true;
    crop_press_widget_point_ = event->pos();
    crop_rotate_start_angle_ = crop_angle_;
    setCursor(transform_rotate_cursor_at(event->pos(), QRectF(crop_rect_), crop_angle_, false));
    if (crop_view_follows_box()) {
      // The view turns during this drag, so the pointer's angle is taken on screen, about
      // the point the box's center holds.
      crop_hold_center_widget_ = view_transform().map_to_widget(crop_box_center_document());
      const auto vector = event->position() - crop_hold_center_widget_;
      crop_rotate_start_vector_degrees_ = std::atan2(vector.y(), vector.x()) * kDegreesPerRadian;
      return;
    }
    const auto document_point = document_position_f(event->position());
    const auto center_x = static_cast<double>(crop_rect_.x()) + crop_rect_.width() / 2.0;
    const auto center_y = static_cast<double>(crop_rect_.y()) + crop_rect_.height() / 2.0;
    crop_rotate_start_vector_degrees_ =
        std::atan2(document_point.y() - center_y, document_point.x() - center_x) * kDegreesPerRadian;
    return;
  }
  crop_drag_handle_ = handle;
  crop_drag_start_rect_ = crop_rect_;
  crop_press_widget_point_ = event->pos();
  crop_hold_center_widget_ = view_transform().map_to_widget(crop_box_center_document());
  if (handle == TransformHandle::Move && crop_view_follows_box()) {
    set_crop_handle_cursor(handle, true);
  }
  crop_drag_start_point_ = document_position_f(event->position());
  crop_drag_grab_offset_ =
      transform_handle_grab_offset(handle, QRectF(crop_rect_), crop_angle_, crop_drag_start_point_);
}

void CanvasWidget::update_crop_rotate_drag(QPointF widget_point, Qt::KeyboardModifiers modifiers) {
  if (!crop_rotating_) {
    return;
  }
  if (crop_view_follows_box()) {
    // The image follows the pointer around the box's center, so the box's angle over
    // the document goes the other way and the view takes the difference: the box stays
    // upright where it is. The box keeps its size and nothing looks at the canvas
    // edges (docs/legal-constraints.md, "Crop interaction").
    const auto vector = widget_point - crop_hold_center_widget_;
    const auto vector_degrees = std::atan2(vector.y(), vector.x()) * kDegreesPerRadian;
    auto degrees =
        normalized_degrees(crop_rotate_start_angle_ - (vector_degrees - crop_rotate_start_vector_degrees_));
    if ((modifiers & Qt::ShiftModifier) != 0) {
      degrees = normalized_degrees(std::round(degrees / 15.0) * 15.0);
    }
    place_crop_box(crop_rect_, degrees, crop_hold_center_widget_);
    return;
  }
  const auto document_point = document_position_f(widget_point);
  const auto center_x = static_cast<double>(crop_rect_.x()) + crop_rect_.width() / 2.0;
  const auto center_y = static_cast<double>(crop_rect_.y()) + crop_rect_.height() / 2.0;
  const auto vector_degrees =
      std::atan2(document_point.y() - center_y, document_point.x() - center_x) * kDegreesPerRadian;
  auto degrees =
      normalized_degrees(crop_rotate_start_angle_ + vector_degrees - crop_rotate_start_vector_degrees_);
  // Shift snaps to 15 degrees, matching the transform rotate handle.
  if ((modifiers & Qt::ShiftModifier) != 0) {
    degrees = normalized_degrees(std::round(degrees / 15.0) * 15.0);
  }
  crop_angle_ = degrees;
  update();
}

void CanvasWidget::update_crop_drag_out(QPoint document_point, Qt::KeyboardModifiers modifiers) {
  if (spacebar_repositioning_drag_rect_) {
    const auto delta = document_point - spacebar_reposition_last_document_position_;
    crop_anchor_document_ += delta;
    crop_current_document_ += delta;
    spacebar_reposition_last_document_position_ = document_point;
  } else {
    crop_square_constrained_ = (modifiers & Qt::ShiftModifier) != 0;
    crop_from_center_ = (modifiers & Qt::AltModifier) != 0;
    crop_current_document_ = snapped_marquee_current_point(crop_anchor_document_, document_point);
  }
  update();
}

void CanvasWidget::update_crop_adjust_drag(QPointF widget_point, Qt::KeyboardModifiers modifiers) {
  if (crop_drag_handle_ == TransformHandle::None) {
    return;
  }

  auto document_point = document_position_f(widget_point);
  const auto rotated = crop_angle_ != 0.0;
  if (crop_drag_handle_ == TransformHandle::Move && crop_view_follows_box() &&
      !spacebar_repositioning_drag_rect_) {
    // Move Image Under Crop Box: the image follows the pointer, so the box travels the
    // other way over the document, in whole pixels, and the view pans to hold the box
    // where it is on screen.
    const auto travel =
        view_transform().map_vector_to_document(widget_point - QPointF(crop_press_widget_point_));
    const QPoint raw_delta(static_cast<int>(std::lround(-travel.x())), static_cast<int>(std::lround(-travel.y())));
    const auto delta = rotated ? raw_delta : snapped_rect_delta(crop_drag_start_rect_, raw_delta);
    const auto target = crop_drag_start_rect_.translated(delta);
    if (target != crop_rect_ && !place_crop_box(target, crop_angle_, crop_hold_center_widget_)) {
      // Past the pan limits: as far toward the target as they allow.
      const auto from = crop_rect_.topLeft();
      const QPointF step = target.topLeft() - from;
      double low = 0.0;
      double high = 1.0;
      for (int i = 0; i < 16; ++i) {
        const auto middle = (low + high) / 2.0;
        const QPoint candidate(from.x() + static_cast<int>(std::lround(step.x() * middle)),
                               from.y() + static_cast<int>(std::lround(step.y() * middle)));
        if (candidate == crop_rect_.topLeft() ||
            place_crop_box(QRect(candidate, crop_rect_.size()), crop_angle_, crop_hold_center_widget_)) {
          low = middle;
        } else {
          high = middle;
        }
      }
    }
    return;
  }
  if (spacebar_repositioning_drag_rect_) {
    // Space held mid-drag slides the whole box (the marquee handle rule). The
    // drag-start rect follows so releasing Space resumes the resize where the
    // box now is; a rotated box moves raw (snapping assumes axis-aligned edges).
    const QPoint rounded(static_cast<int>(std::lround(document_point.x())),
                         static_cast<int>(std::lround(document_point.y())));
    const auto raw_delta = rounded - spacebar_reposition_origin_document_position_;
    const auto delta = rotated ? raw_delta : snapped_rect_delta(spacebar_reposition_start_marquee_rect_, raw_delta);
    crop_drag_start_rect_ = spacebar_reposition_start_marquee_start_rect_.translated(delta);
    crop_rect_ = spacebar_reposition_start_marquee_rect_.translated(delta);
    update();
    return;
  }
  if (crop_drag_handle_ == TransformHandle::Move) {
    const QPoint raw_delta(static_cast<int>(std::lround(document_point.x() - crop_drag_start_point_.x())),
                           static_cast<int>(std::lround(document_point.y() - crop_drag_start_point_.y())));
    // Guide/grid snapping assumes axis-aligned edges, so a rotated box moves raw.
    crop_rect_ = crop_drag_start_rect_.translated(
        rotated ? raw_delta : snapped_rect_delta(crop_drag_start_rect_, raw_delta));
    update();
    return;
  }

  // A handle grabbed beside its edge keeps the edge under the grab instead of
  // jumping it to the pointer (GitHub issue 176's edge bands).
  document_point += crop_drag_grab_offset_;
  // Handle drags on a rotated box work in the box's own frame: map the cursor
  // through the inverse rotation about the drag-start center, then run the
  // axis-aligned edge math there (the free-transform approach).
  auto tracked = rotated ? document_point : snapped_document_point_f(document_point);
  if (rotated) {
    const auto radians = crop_angle_ / kDegreesPerRadian;
    const auto cos_theta = std::cos(radians);
    const auto sin_theta = std::sin(radians);
    const auto center_x =
        static_cast<double>(crop_drag_start_rect_.x()) + crop_drag_start_rect_.width() / 2.0;
    const auto center_y =
        static_cast<double>(crop_drag_start_rect_.y()) + crop_drag_start_rect_.height() / 2.0;
    const auto dx = document_point.x() - center_x;
    const auto dy = document_point.y() - center_y;
    tracked = QPointF(center_x + cos_theta * dx + sin_theta * dy,
                      center_y - sin_theta * dx + cos_theta * dy);
  }
  const QPoint point(static_cast<int>(std::lround(tracked.x())), static_cast<int>(std::lround(tracked.y())));
  const auto start = crop_drag_start_rect_;
  const auto moves_left = crop_drag_handle_ == TransformHandle::TopLeft ||
                          crop_drag_handle_ == TransformHandle::Left ||
                          crop_drag_handle_ == TransformHandle::BottomLeft;
  const auto moves_right = crop_drag_handle_ == TransformHandle::TopRight ||
                           crop_drag_handle_ == TransformHandle::Right ||
                           crop_drag_handle_ == TransformHandle::BottomRight;
  const auto moves_top = crop_drag_handle_ == TransformHandle::TopLeft ||
                         crop_drag_handle_ == TransformHandle::Top ||
                         crop_drag_handle_ == TransformHandle::TopRight;
  const auto moves_bottom = crop_drag_handle_ == TransformHandle::BottomLeft ||
                            crop_drag_handle_ == TransformHandle::Bottom ||
                            crop_drag_handle_ == TransformHandle::BottomRight;
  const auto corner = (moves_left || moves_right) && (moves_top || moves_bottom);

  // A set ratio always constrains; otherwise Size style's link (any handle) or
  // Shift on a corner holds the drag-start aspect (GitHub issue 332). (This is a
  // drag-rect tool, not a transform session, so transform_drag_keeps_aspect
  // deliberately does not apply.)
  double target_ratio = 0.0;
  if (crop_ratio_w_ > 0.0 && crop_ratio_h_ > 0.0) {
    target_ratio = crop_ratio_w_ / crop_ratio_h_;
  } else if (crop_keep_proportions_ && start.height() > 0) {
    target_ratio = static_cast<double>(start.width()) / start.height();
  } else if (corner && (modifiers & Qt::ShiftModifier) != 0 && start.height() > 0) {
    target_ratio = static_cast<double>(start.width()) / start.height();
  }

  QRect rect;
  if ((modifiers & Qt::AltModifier) != 0) {
    // Alt resizes about the center: the opposite side mirrors the dragged one
    // (GitHub issue 66; read mid-drag, so it works with the Alt pressed after the
    // grab too). A set ratio or Shift still constrains the result.
    rect = center_anchored_handle_rect(start, moves_left || moves_right, moves_top || moves_bottom, point,
                                       target_ratio);
  } else if (target_ratio > 0.0 && corner) {
    const auto anchor_x = moves_left ? start.x() + start.width() : start.x();
    const auto anchor_y = moves_top ? start.y() + start.height() : start.y();
    auto width = std::max(1, std::abs(point.x() - anchor_x));
    auto height = std::max(1, std::abs(point.y() - anchor_y));
    if (static_cast<double>(width) / static_cast<double>(height) > target_ratio) {
      width = std::max(1, static_cast<int>(std::round(height * target_ratio)));
    } else {
      height = std::max(1, static_cast<int>(std::round(width / target_ratio)));
    }
    const auto x = point.x() < anchor_x ? anchor_x - width : anchor_x;
    const auto y = point.y() < anchor_y ? anchor_y - height : anchor_y;
    rect = QRect(x, y, width, height);
  } else if (target_ratio > 0.0) {
    // Ratio-locked edge drag: the dragged axis follows the cursor, the other is
    // derived and re-centered on the start rect.
    if (moves_left || moves_right) {
      const auto anchor_x = moves_left ? start.x() + start.width() : start.x();
      const auto width = std::max(1, std::abs(point.x() - anchor_x));
      const auto height = std::max(1, static_cast<int>(std::round(width / target_ratio)));
      const auto x = point.x() < anchor_x ? anchor_x - width : anchor_x;
      rect = QRect(x, start.y() + start.height() / 2 - height / 2, width, height);
    } else {
      const auto anchor_y = moves_top ? start.y() + start.height() : start.y();
      const auto height = std::max(1, std::abs(point.y() - anchor_y));
      const auto width = std::max(1, static_cast<int>(std::round(height * target_ratio)));
      const auto y = point.y() < anchor_y ? anchor_y - height : anchor_y;
      rect = QRect(start.x() + start.width() / 2 - width / 2, y, width, height);
    }
  } else {
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
    // Dragging through the opposite edge flips cleanly.
    const auto x0 = std::min(left, right);
    const auto y0 = std::min(top, bottom);
    rect = QRect(x0, y0, std::max(1, std::abs(right - left)), std::max(1, std::abs(bottom - top)));
  }

  crop_rect_ = rect;
  update();
}

void CanvasWidget::finish_crop_mouse_release(QMouseEvent* event) {
  if (crop_dragging_out_) {
    const auto was_repositioning = spacebar_repositioning_drag_rect_;
    crop_dragging_out_ = false;
    spacebar_repositioning_drag_rect_ = false;
    const auto widget_delta = event->pos() - crop_press_widget_point_;
    const auto was_click = !was_repositioning &&
                           widget_delta.manhattanLength() < QApplication::startDragDistance();
    if (!was_click) {
      if (!was_repositioning) {
        crop_current_document_ =
            snapped_marquee_current_point(crop_anchor_document_, document_position(event->pos()));
      }
      crop_rect_ = crop_drag_rect(crop_anchor_document_, crop_current_document_);
      crop_session_active_ = true;
      crop_box_is_default_ = false;
      crop_box_from_selection_ = false;
      if (crop_view_follows_box()) {
        center_crop_box_in_view();
      }
      notify_crop_session_changed();
    }
    // A plain click leaves any pending rect (and the session) untouched.
    crop_square_constrained_ = false;
    crop_from_center_ = false;
    update_tool_cursor();
    update();
    return;
  }

  if (crop_rotating_) {
    update_crop_rotate_drag(event->position(), event->modifiers());
    crop_rotating_ = false;
    if (crop_angle_ != 0.0) {
      crop_box_is_default_ = false;
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
    return;
  }

  if (crop_drag_handle_ != TransformHandle::None) {
    update_crop_adjust_drag(event->position(), event->modifiers());
    // A handle drag moved the box on screen: with the image moving under the box, the
    // view pans so the box is centered again. A pan only, at the zoom the user has
    // (docs/legal-constraints.md, "Crop interaction"). The inside drag held the box in
    // place already.
    const auto recenter =
        crop_view_follows_box() &&
        QLineF(view_transform().map_to_widget(crop_box_center_document()), crop_hold_center_widget_).length() > 0.5;
    crop_drag_handle_ = TransformHandle::None;
    spacebar_repositioning_drag_rect_ = false;
    crop_box_is_default_ = false;
    if (recenter) {
      center_crop_box_in_view();
    }
    update_tool_cursor();
    update();
    notify_crop_session_changed();
  }
}

void CanvasWidget::set_crop_straighten_armed(bool armed) {
  armed = armed && crop_session_active_ && tool_ == CanvasTool::Crop;
  if (armed == crop_straighten_armed_) {
    return;
  }
  crop_straighten_armed_ = armed;
  if (!armed && crop_straightening_ && crop_straighten_from_button_) {
    cancel_crop_straighten_line();
  }
  update_tool_cursor();
  notify_crop_session_changed();
}

bool CanvasWidget::crop_straighten_armed() const noexcept {
  return crop_straighten_armed_;
}

bool CanvasWidget::crop_straighten_line_active() const noexcept {
  return crop_straightening_;
}

void CanvasWidget::begin_crop_straighten_line(QMouseEvent* event) {
  crop_straightening_ = true;
  crop_straighten_from_button_ = crop_straighten_armed_;
  crop_straighten_press_widget_ = event->pos();
  // Document coordinates through the view transform, so the line means the same at any
  // zoom and view rotation, Move Image Under Crop Box's turned view included.
  crop_straighten_start_ = document_position_f(event->position());
  crop_straighten_end_ = crop_straighten_start_;
  setCursor(Qt::CrossCursor);
  update();
}

void CanvasWidget::finish_crop_straighten_line(QMouseEvent* event) {
  crop_straighten_end_ = document_position_f(event->position());
  crop_straightening_ = false;
  const auto travel = (event->pos() - crop_straighten_press_widget_).manhattanLength();
  if (travel < QApplication::startDragDistance() || crop_straighten_start_ == crop_straighten_end_) {
    // A click draws no line: nothing turns, and the button stays on.
    crop_straighten_from_button_ = false;
    update_tool_cursor();
    update();
    return;
  }
  auto degrees = crop_straighten_degrees(crop_straighten_start_, crop_straighten_end_, crop_angle_);
  if (std::abs(degrees) < 1e-9) {
    degrees = 0.0;
  }
  // The box turns once, here, about its own center: never while the line is drawn, and
  // with no zoom, no change of size and no test of where the canvas edges fall
  // (docs/legal-constraints.md, "Crop interaction"). With the image moving under the
  // box, the view takes the turn the way the rotate drag shows it.
  if (degrees != crop_angle_) {
    if (crop_view_follows_box()) {
      place_crop_box(crop_rect_, degrees, view_transform().map_to_widget(crop_box_center_document()));
    } else {
      crop_angle_ = degrees;
    }
    if (crop_angle_ != 0.0) {
      crop_box_is_default_ = false;
    }
  }
  if (crop_straighten_from_button_) {
    crop_straighten_armed_ = false;
  }
  crop_straighten_from_button_ = false;
  update_tool_cursor();
  update();
  notify_crop_session_changed();
}

void CanvasWidget::cancel_crop_straighten_line() {
  if (!crop_straightening_) {
    return;
  }
  crop_straightening_ = false;
  crop_straighten_from_button_ = false;
  update_tool_cursor();
  update();
}

void CanvasWidget::draw_crop_straighten_line(QPainter& painter) const {
  if (tool_ != CanvasTool::Crop || !crop_straightening_ || crop_straighten_start_ == crop_straighten_end_) {
    return;
  }
  // The Ruler's line: cursor ink (dark under light) with a tick across each end, exempt
  // from theme roles like every tool overlay (docs/ui-conventions.md).
  const auto start = widget_position_f(crop_straighten_start_);
  const auto end = widget_position_f(crop_straighten_end_);
  const auto along = end - start;
  const auto length = std::hypot(along.x(), along.y());
  const QPointF normal = length > 0.0 ? QPointF(-along.y() / length, along.x() / length) : QPointF(0.0, 1.0);
  constexpr double kTick = 5.0;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(Qt::NoBrush);
  const QPen dark_ink(QColor(10, 14, 20), 3.0, Qt::SolidLine, Qt::RoundCap);    // color-literal-ok: cursor ink
  const QPen light_ink(QColor(245, 248, 252), 1.0, Qt::SolidLine, Qt::FlatCap);  // color-literal-ok: cursor ink
  for (int pass = 0; pass < 2; ++pass) {
    painter.setPen(pass == 0 ? dark_ink : light_ink);
    painter.drawLine(QLineF(start, end));
    painter.drawLine(QLineF(start - normal * kTick, start + normal * kTick));
    painter.drawLine(QLineF(end - normal * kTick, end + normal * kTick));
  }
  painter.restore();
}

void CanvasWidget::nudge_crop_rect(QPoint delta) {
  if (!crop_session_active_ || delta.isNull()) {
    return;
  }
  if (crop_view_follows_box()) {
    // The arrows nudge the image under the box, along the document's axes: the box
    // goes the other way over the document and keeps its place on screen.
    const auto center_widget = view_transform().map_to_widget(crop_box_center_document());
    if (!place_crop_box(crop_rect_.translated(-delta), crop_angle_, center_widget)) {
      return;
    }
  } else {
    crop_rect_.translate(delta);
  }
  crop_box_is_default_ = false;
  update();
  notify_crop_session_changed();
}

void CanvasWidget::set_crop_overlay_options(const CropOverlayOptions& options) {
  auto clamped = options;
  clamped.shield_opacity_percent = std::clamp(clamped.shield_opacity_percent, 0, 100);
  if (!clamped.shield_color.isValid()) {
    clamped.shield_color = QColor(0, 0, 0);
  }
  clamped.shield_color.setAlpha(255);
  if (clamped == crop_overlay_options_) {
    return;
  }
  const auto move_image_changed = clamped.move_image != crop_overlay_options_.move_image;
  crop_overlay_options_ = clamped;
  if (move_image_changed) {
    apply_crop_move_image_change();
  }
  if (crop_session_active_ || crop_dragging_out_) {
    update();
  }
}

const CropOverlayOptions& CanvasWidget::crop_overlay_options() const noexcept {
  return crop_overlay_options_;
}

void CanvasWidget::draw_crop_overlay(QPainter& painter) const {
  std::optional<QRect> document_rect;
  double angle = 0.0;
  if (crop_dragging_out_) {
    document_rect = crop_drag_rect(crop_anchor_document_, crop_current_document_);
  } else if (crop_session_active_) {
    document_rect = crop_rect_;
    angle = crop_angle_;
  }
  if (!document_rect.has_value() || document_rect->isEmpty()) {
    return;
  }

  // Exact widget-space box (the +1 doc-rect convention is internal to the
  // QRectF overload); rotation happens about its center in widget space.
  const auto widget_rect = widget_rect_for_document_rect(QRectF(*document_rect));
  auto center = widget_rect.center();
  // Move Image Under Crop Box: the view has taken the box's angle, so on screen the two
  // turns cancel. Drawing through both would antialias an upright box and leave rounding
  // dust that shifts its lines between frames, so the box is drawn straight in widget
  // space, its center on a 1/64 pixel grid. The overlay itself is the same one, drawn
  // the same way, in every gesture and after it (docs/legal-constraints.md, "Crop
  // interaction").
  const bool straight = crop_view_turn_degrees_ != 0.0 && upright_overlay_depth_ > 0 && !crop_dragging_out_;
  if (straight) {
    center = upright_overlay_to_widget().map(center);
    center = QPointF(std::round(center.x() * 64.0) / 64.0, std::round(center.y() * 64.0) / 64.0);
    angle = CanvasViewTransform::normalized_angle(angle + view_rotation_degrees_);
    painter.save();
    painter.setWorldTransform(QTransform());
  }
  QTransform box_to_widget;
  box_to_widget.translate(center.x(), center.y());
  box_to_widget.rotate(angle);
  const QRectF local_rect(-widget_rect.width() / 2.0, -widget_rect.height() / 2.0, widget_rect.width(),
                          widget_rect.height());
  const auto corners = box_to_widget.map(QPolygonF(local_rect));

  // Shield: darken everything outside the pending crop, in the user's shield
  // color and opacity (a user setting, unthemed like the grid and guide colors).
  // Show Cropped Area off hides the outside behind the backdrop instead.
  const auto& options = crop_overlay_options_;
  std::optional<QColor> outside_fill;
  if (!options.show_cropped_area) {
    outside_fill = backdrop_color();
  } else if (options.shield_enabled && options.shield_opacity_percent > 0) {
    QColor shield_color = options.shield_color;
    shield_color.setAlpha(
        static_cast<int>(std::lround(std::clamp(options.shield_opacity_percent, 0, 100) * 255.0 / 100.0)));
    outside_fill = shield_color;
  }
  if (outside_fill.has_value()) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPainterPath shield;
    shield.addRect(straight ? QRectF(rect()) : QRectF(overlay_view_rect()));
    QPainterPath box_path;
    box_path.addPolygon(corners);
    box_path.closeSubpath();
    painter.fillPath(shield.subtracted(box_path), *outside_fill);
    painter.restore();
  }

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, angle != 0.0);
  painter.setTransform(box_to_widget, true);

  // Composition guides (GitHub issue 325; rule of thirds by default), as a
  // dark+light pair so they read on any artwork; skipped when the rect is too
  // small for them to help. "While dragging" shows them only during a gesture.
  const bool dragging =
      crop_dragging_out_ || crop_rotating_ || crop_straightening_ || crop_drag_handle_ != TransformHandle::None;
  if (crop_overlay_ != CropOverlay::None && (!crop_overlay_only_while_dragging_ || dragging) &&
      widget_rect.width() >= 24.0 && widget_rect.height() >= 24.0) {
    const auto guides = crop_overlay_guides(crop_overlay_, crop_overlay_orientation_, local_rect);
    QPen dark_pen(QColor(10, 14, 20, 110), 1.0);
    dark_pen.setCosmetic(true);
    QPen light_pen(QColor(245, 248, 252, 110), 1.0);
    light_pen.setCosmetic(true);
    for (const auto& group : guides.axis_groups) {
      painter.setPen(dark_pen);
      for (const auto& line : group) {
        painter.drawLine(dark_twin(line));
      }
      painter.setPen(light_pen);
      for (const auto& line : group) {
        painter.drawLine(line);
      }
    }
    if (!guides.oblique.empty() || !guides.arcs.isEmpty()) {
      painter.save();
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setClipRect(local_rect, Qt::IntersectClip);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(dark_pen);
      for (const auto& line : guides.oblique) {
        painter.drawLine(dark_twin(line));
      }
      // The spiral's curve has no single "below": a wider dark stroke under it.
      QPen dark_curve_pen(dark_pen);
      dark_curve_pen.setWidthF(2.0);
      painter.setPen(dark_curve_pen);
      painter.drawPath(guides.arcs);
      painter.setPen(light_pen);
      for (const auto& line : guides.oblique) {
        painter.drawLine(line);
      }
      painter.drawPath(guides.arcs);
      painter.restore();
    }
  }

  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(theme().canvas_layer_selection_border, 1.0, Qt::DashLine));
  painter.drawRect(local_rect);
  painter.restore();

  // Handles appear once a rect is pending; a drag-out in flight shows only the
  // rect and shield.
  if (!crop_dragging_out_) {
    constexpr double kHandleSize = 8.0;
    const std::array<TransformHandle, 8> handles = {
        TransformHandle::TopLeft,    TransformHandle::Top,    TransformHandle::TopRight,
        TransformHandle::Right,      TransformHandle::BottomRight, TransformHandle::Bottom,
        TransformHandle::BottomLeft, TransformHandle::Left};
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, angle != 0.0);
    painter.setPen(QPen(QColor(10, 14, 20), 1.0));
    painter.setBrush(QColor(245, 248, 252));
    // The same eight points in the straight frame: the box's corners and edge middles.
    const std::array<QPointF, 8> straight_points = {
        local_rect.topLeft(),     QPointF(0.0, local_rect.top()),    local_rect.topRight(),
        QPointF(local_rect.right(), 0.0), local_rect.bottomRight(), QPointF(0.0, local_rect.bottom()),
        local_rect.bottomLeft(),  QPointF(local_rect.left(), 0.0)};
    for (std::size_t i = 0; i < handles.size(); ++i) {
      const auto point = straight ? box_to_widget.map(straight_points[i])
                                  : transform_handle_position(handles[i], QRectF(*document_rect), angle);
      painter.drawRect(
          QRectF(point.x() - kHandleSize / 2.0, point.y() - kHandleSize / 2.0, kHandleSize, kHandleSize));
    }
    painter.restore();
  }
  if (straight) {
    painter.restore();
  }
}

}  // namespace patchy::ui
