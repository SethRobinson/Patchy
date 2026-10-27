#include "ui/perspective_planes_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/color_panel.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/measurement_units.hpp"
#include "ui/modifier_names.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <utility>

namespace patchy::ui {

namespace {

enum class WorkspaceTool { CreatePlane, EditPlane, Clone, Paint, Hand, Zoom };

constexpr std::size_t kHistoryLimit = 60;
constexpr double kHandleRadius = 6.0;
constexpr double kPullTabOffset = 20.0;
constexpr double kPullTabRadius = 8.0;
constexpr double kRotateHandleOffset = 28.0;

QString quad_problem_text(PlaneQuadProblem problem) {
  switch (problem) {
    case PlaneQuadProblem::TooSmall:
      return QObject::tr("the corners are too close together.");
    case PlaneQuadProblem::Crossing:
      return QObject::tr("two sides cross. Click the corners in order around the surface.");
    case PlaneQuadProblem::Concave:
      return QObject::tr("a corner points inward, or three corners are in a line.");
    case PlaneQuadProblem::BehindView:
      return QObject::tr("it would bend behind the view.");
    case PlaneQuadProblem::None:
      break;
  }
  return {};
}

QPointF to_qpoint(PlanePoint p) { return {p.x, p.y}; }
PlanePoint to_plane_point(QPointF p) { return {p.x(), p.y()}; }

double screen_distance(QPointF a, QPointF b) {
  const auto d = a - b;
  return std::sqrt(d.x() * d.x() + d.y() * d.y());
}

// The dark-and-light line pair tool overlays use so they read over any artwork.
void draw_pair(QPainter& painter, const QPainterPath& path, const QColor& ink, double width = 1.5) {
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(0, 0, 0, 150), width + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path);
  painter.setPen(QPen(ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path);
}

struct HistoryEntry {
  std::vector<PerspectivePlane> planes_before;
  std::vector<PerspectivePlane> planes_after;
  PerspectiveCanvas::Change pixels;
  // Grid and angle edits of one plane merge into one step.
  int merge_key{0};
  // Place Layer: the layer's own content taken out of it.
  std::optional<PerspectiveCanvas::Lift> lift;
};

struct Floating {
  std::size_t source{0};
  PixelBuffer pixels;
  PerspectiveSampler sampler;
  std::optional<int> plane_id;
  PerspectivePlacement placement;
  QPointF loose_center;  // document point while not on a plane
  // Place Layer: the layer's content was taken out of the layer for this image; dropping
  // the image puts it back.
  std::optional<PerspectiveCanvas::Lift> lift;
};

class PerspectivePlanesView final : public QWidget {
public:
  PerspectivePlanesView(PerspectiveWorkspaceInput& input, QWidget* parent)
      : QWidget(parent),
        canvas_(input.layer, input.layer_bounds, input.document_width, input.document_height),
        backdrop_(std::move(input.backdrop)),
        selection_(std::move(input.selection)),
        can_place_layer_(input.can_place_layer),
        planes_(std::move(input.planes)),
        sources_(std::move(input.paste_sources)),
        paint_color_(input.foreground) {
    setObjectName(QStringLiteral("perspectivePlanesView"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(560, 400);
    for (const auto& plane : planes_) {
      next_id_ = std::max(next_id_, plane.id + 1);
    }
    composite_image_ = QImage(canvas_.composite().data().data(), canvas_.width(), canvas_.height(),
                              static_cast<qsizetype>(canvas_.composite().stride_bytes()), QImage::Format_RGBA8888);
    canvas_.set_sample_context(backdrop_, std::move(input.above));
    canvas_.set_sample_layers(input.sample_layers);
    // The layers below, drawn behind: built once by the caller, never per repaint.
    if (backdrop_.width() == canvas_.width() && backdrop_.height() == canvas_.height() &&
        backdrop_.format() == PixelFormat::rgba8()) {
      const auto& backdrop = std::as_const(backdrop_);
      backdrop_image_ = QImage(backdrop.data().data(), backdrop.width(), backdrop.height(),
                               static_cast<qsizetype>(backdrop.stride_bytes()), QImage::Format_RGBA8888);
    }
    if (selection_.width() != canvas_.width() || selection_.height() != canvas_.height() ||
        selection_.format().channels != 1) {
      selection_ = PixelBuffer();
    }
    // A layer with see-through parts over other layers is usually something to put on a
    // surface of the picture below: the opening hint suggests Place Layer.
    bool transparent = false;
    bool opaque = false;
    const auto& layer = canvas_.original();
    for (std::int32_t y = 0; y < layer.height() && !(transparent && opaque); ++y) {
      const auto row = layer.row(y);
      for (std::int32_t x = 0; x < layer.width(); ++x) {
        const auto alpha = row[static_cast<std::size_t>(x) * 4 + 3];
        transparent = transparent || alpha < 255;
        opaque = opaque || alpha > 0;
      }
    }
    layer_has_content_ = opaque;
    suggest_place_layer_ = can_place_layer_ && opaque && transparent && !backdrop_image_.isNull();
    planes_changed();
    if (!planes_.empty()) {
      selected_id_ = planes_.front().id;
    }
  }

  std::function<void()> on_state_changed;

  // --- Queries the dialog reads -------------------------------------------------------
  [[nodiscard]] WorkspaceTool tool() const noexcept { return tool_; }
  [[nodiscard]] const std::vector<PerspectivePlane>& planes() const noexcept { return planes_; }
  [[nodiscard]] const PerspectiveCanvas& canvas() const noexcept { return canvas_; }
  [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty() || floating_.has_value() || !points_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] bool has_floating() const noexcept { return floating_.has_value(); }
  [[nodiscard]] const std::vector<PerspectivePasteSource>& sources() const noexcept { return sources_; }
  [[nodiscard]] bool has_backdrop() const noexcept { return !backdrop_image_.isNull(); }
  [[nodiscard]] bool show_backdrop() const noexcept { return show_backdrop_; }
  void set_show_backdrop(bool show) {
    show_backdrop_ = show;
    update();
  }
  // Place Layer needs a layer that can give up its content and a usable plane.
  [[nodiscard]] bool can_place_layer() const noexcept { return can_place_layer_ && layer_has_content_; }
  [[nodiscard]] bool has_usable_plane() const {
    return std::any_of(planes_.begin(), planes_.end(),
                       [this](const PerspectivePlane& plane) { return geometry_for(plane.id).has_value(); });
  }
  // Something was written or placed (the plane is no longer only a guide).
  [[nodiscard]] bool has_content_edits() const {
    return std::any_of(undo_.begin(), undo_.end(),
                       [](const HistoryEntry& entry) { return !entry.pixels.empty() || entry.lift.has_value(); });
  }
  std::function<void()> on_layer_placed;
  [[nodiscard]] double zoom() const noexcept { return zoom_; }
  [[nodiscard]] const PerspectivePlane* selected_plane() const {
    const auto found = std::find_if(planes_.begin(), planes_.end(),
                                    [this](const PerspectivePlane& plane) { return plane.id == selected_id_; });
    return found == planes_.end() ? nullptr : &*found;
  }
  [[nodiscard]] std::optional<PlaneGeometry> geometry_for(int id) const {
    const auto found = geometries_.find(id);
    return found == geometries_.end() ? std::nullopt : found->second;
  }
  [[nodiscard]] double plane_grid(const PerspectivePlane& plane) const {
    const auto geometry = geometry_for(plane.id);
    return geometry.has_value() ? plane_grid_size(plane, *geometry) : plane.grid_size;
  }
  [[nodiscard]] PerspectiveBrush brush() const noexcept { return brush_; }
  [[nodiscard]] QColor paint_color() const noexcept { return paint_color_; }
  [[nodiscard]] const std::optional<PerspectivePlacement> floating_placement() const {
    if (!floating_.has_value() || !floating_->plane_id.has_value()) {
      return std::nullopt;
    }
    return floating_->placement;
  }

  [[nodiscard]] QString hint() const {
    if (floating_.has_value()) {
      if (floating_->lift.has_value()) {
        return QObject::tr("The layer now fills the plane. Drag it to move it, drag a corner to scale it, or drag the "
                           "round handle to turn it. Enter places it; Esc puts it back.");
      }
      return floating_->plane_id.has_value()
                 ? QObject::tr("Drag the image to move it on the plane, drag a corner to scale it, or drag the round "
                               "handle to turn it. Enter places it; Esc removes it.")
                 : QObject::tr("Drag the image onto a plane. It takes on that plane's perspective.");
    }
    switch (tool_) {
      case WorkspaceTool::CreatePlane:
        if (create_problem_ != PlaneQuadProblem::None && points_.size() >= 3) {
          return QObject::tr("This shape cannot be a plane: %1").arg(quad_problem_text(create_problem_));
        }
        if (!has_usable_plane()) {
          // Step 1: nothing can be edited until there is a target plane.
          if (points_.empty()) {
            return suggest_place_layer_
                       ? QObject::tr("Step 1 of 2: set the target plane. Click the four corners of the surface where "
                                     "this layer should go.")
                       : QObject::tr("Step 1 of 2: set the target plane. Click the four corners of a flat surface, such "
                                     "as a wall, a floor or the side of a box.");
          }
          if (points_.size() < 3) {
            return QObject::tr("Step 1 of 2: click the next corner, going around the surface in order. Backspace "
                               "removes the last corner.");
          }
          return QObject::tr("Step 1 of 2: click the last corner to finish the target plane.");
        }
        if (points_.empty()) {
          return QObject::tr("Click the first corner of another flat surface, such as a wall, a floor or the side of a "
                             "box.");
        }
        if (points_.size() < 3) {
          return QObject::tr("Click the next corner, going around the surface in order. Backspace removes the last "
                             "corner.");
        }
        return QObject::tr("Click the last corner to finish the plane.");
      case WorkspaceTool::EditPlane: {
        if (planes_.empty()) {
          return QObject::tr("Draw a plane first with Create Plane.");
        }
        const auto* plane = selected_plane();
        if (plane == nullptr) {
          return QObject::tr("Click a plane to select it.");
        }
        if (!geometry_for(plane->id).has_value()) {
          return QObject::tr("This plane cannot be used: %1")
              .arg(quad_problem_text(check_plane_quad(plane->corners)));
        }
        if (!has_content_edits()) {
          // A plane alone changes nothing: say so, and name the next step.
          const auto step = can_place_layer()
                                ? QObject::tr("Step 2 of 2: put something on the target plane. Press Place Layer to put "
                                              "this layer on it, or Paste onto Plane, Clone or Paint.")
                                : QObject::tr("Step 2 of 2: put something on the target plane. Press Paste onto Plane, "
                                              "Clone or Paint.");
          return planes_.size() > 1 ? step + QLatin1Char(' ') + choose_target_text() : step;
        }
        if (planes_.size() > 1 && !plane->link.has_value()) {
          return QObject::tr("Drag a corner or an edge to fit the surface.") + QLatin1Char(' ') + choose_target_text();
        }
        if (plane->link.has_value()) {
          return QObject::tr("This plane is joined to an edge of another. Set its Angle, or drag the diamond on its far "
                             "edge to set its depth. Dragging a corner unjoins it.");
        }
        return QObject::tr("Drag a corner or an edge to fit the surface. Drag a + tab to pull out a joined plane. "
                           "Delete removes the plane.");
      }
      case WorkspaceTool::Clone:
        if (!clone_source_.has_value()) {
          return resolve_modifier_names(QObject::tr("%ALT%-click on a plane to set where to clone from."));
        }
        return resolve_modifier_names(
            QObject::tr("Paint on a plane to clone. The brush shrinks with distance. %ALT%-click to pick a new source."));
      case WorkspaceTool::Paint:
        return QObject::tr("Paint on a plane with the color. The brush shrinks with distance.");
      case WorkspaceTool::Hand:
        return QObject::tr("Drag to scroll the view.");
      case WorkspaceTool::Zoom:
        return resolve_modifier_names(QObject::tr("Click to zoom in, %ALT%-click to zoom out."));
    }
    return {};
  }
  // Steps 1 and 2 show as a banner, so setting the target plane first cannot be missed.
  [[nodiscard]] bool hint_is_step() const {
    if (floating_.has_value() || hint_is_problem()) {
      return false;
    }
    if (tool_ == WorkspaceTool::CreatePlane) {
      return !has_usable_plane();
    }
    const auto* plane = selected_plane();
    return tool_ == WorkspaceTool::EditPlane && plane != nullptr && geometry_for(plane->id).has_value() &&
           !has_content_edits();
  }
  [[nodiscard]] static QString choose_target_text() {
    return QObject::tr("Click inside another plane to make it the target.");
  }
  // 1-based position of the target plane among the planes, 0 for none.
  [[nodiscard]] int target_number() const {
    for (std::size_t i = 0; i < planes_.size(); ++i) {
      if (planes_[i].id == selected_id_) {
        return static_cast<int>(i) + 1;
      }
    }
    return 0;
  }
  [[nodiscard]] SampleLayers sample_layers() const noexcept { return canvas_.sample_layers(); }
  void set_sample_layers(SampleLayers mode) { canvas_.set_sample_layers(mode); }
  [[nodiscard]] bool hint_is_problem() const {
    if (tool_ == WorkspaceTool::CreatePlane && points_.size() >= 3 && create_problem_ != PlaneQuadProblem::None) {
      return true;
    }
    const auto* plane = selected_plane();
    return tool_ == WorkspaceTool::EditPlane && plane != nullptr && !geometry_for(plane->id).has_value();
  }

  // --- Commands ------------------------------------------------------------------------
  void set_tool(WorkspaceTool tool) {
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
    if (tool != WorkspaceTool::CreatePlane) {
      points_.clear();
    }
    tool_ = tool;
    updateCursor();
    notify();
  }
  void set_brush(PerspectiveBrush brush) {
    brush_ = brush;
    update();
  }
  void set_paint_color(QColor color) { paint_color_ = color; }

  void set_selected_grid(double grid) {
    auto* plane = mutable_selected();
    if (plane == nullptr || plane->grid_size == grid) {
      return;
    }
    const auto before = planes_;
    plane->grid_size = grid;
    record_planes(before, 1000000 + plane->id);
  }
  void set_selected_angle(double angle) {
    auto* plane = mutable_selected();
    if (plane == nullptr || !plane->link.has_value() || plane->link->angle_degrees == angle) {
      return;
    }
    const auto before = planes_;
    plane->link->angle_degrees = angle;
    planes_changed();
    record_planes(before, 2000000 + plane->id);
  }
  void delete_selected_plane() {
    const auto found = std::find_if(planes_.begin(), planes_.end(),
                                    [this](const PerspectivePlane& plane) { return plane.id == selected_id_; });
    if (found == planes_.end()) {
      return;
    }
    const auto before = planes_;
    planes_.erase(found);
    selected_id_ = planes_.empty() ? 0 : planes_.back().id;
    if (clone_source_.has_value() && !geometry_for(clone_source_->first).has_value()) {
      clone_source_.reset();
    }
    planes_changed();
    record_planes(before);
  }
  // Backspace: the last corner while placing, else the selected plane.
  void remove_last() {
    if (!points_.empty()) {
      points_.pop_back();
      update_create_problem();
      update();
      notify();
      return;
    }
    if (floating_.has_value()) {
      drop_floating();
      return;
    }
    delete_selected_plane();
  }
  // Removes the floating image without placing it; a lifted layer's content goes back.
  void drop_floating() {
    if (!floating_.has_value()) {
      return;
    }
    if (floating_->lift.has_value()) {
      composite_changed(canvas_.set_original_region(floating_->lift->rect, floating_->lift->before));
    }
    floating_.reset();
    update();
    notify();
  }
  // Esc: drop what is pending. Returns false when there was nothing, so the dialog closes.
  bool cancel_pending() {
    if (canvas_.stroke_active()) {
      finish_stroke();
      return true;
    }
    if (floating_.has_value()) {
      drop_floating();
      return true;
    }
    if (!points_.empty()) {
      points_.clear();
      update();
      notify();
      return true;
    }
    return false;
  }

  void start_paste(std::size_t source) {
    if (source >= sources_.size() || sources_[source].pixels.empty()) {
      return;
    }
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
    commit_floating();
    Floating floating;
    floating.source = source;
    floating.pixels = sources_[source].pixels;
    floating.sampler = PerspectiveSampler(floating.pixels);
    // Onto the selected plane when there is one, fitted and turned upright.
    const auto* plane = selected_plane();
    std::optional<PlaneGeometry> geometry = plane != nullptr ? geometry_for(plane->id) : std::nullopt;
    if (!geometry.has_value()) {
      for (const auto& candidate : planes_) {
        geometry = geometry_for(candidate.id);
        if (geometry.has_value()) {
          plane = &candidate;
          break;
        }
      }
    }
    if (geometry.has_value()) {
      floating.plane_id = plane->id;
      floating.placement = fit_placement(*geometry, floating.pixels.width(), floating.pixels.height());
    } else {
      floating.loose_center = to_qpoint(screen_to_document(rect().center()));
    }
    floating_ = std::move(floating);
    if (tool_ == WorkspaceTool::CreatePlane) {
      tool_ = WorkspaceTool::EditPlane;
      points_.clear();
    }
    update();
    notify();
  }
  // Place Layer: the layer's own content (or the selected part) comes out of the layer and
  // floats on the selected plane, filling it corner to corner.
  void start_place_layer() {
    if (!can_place_layer()) {
      return;
    }
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
    commit_floating();
    const auto* plane = selected_plane();
    std::optional<PlaneGeometry> geometry = plane != nullptr ? geometry_for(plane->id) : std::nullopt;
    if (!geometry.has_value()) {
      for (const auto& candidate : planes_) {
        geometry = geometry_for(candidate.id);
        if (geometry.has_value()) {
          plane = &candidate;
          break;
        }
      }
    }
    if (!geometry.has_value()) {
      notify();
      return;
    }
    auto lifted = canvas_.lift(selection_.empty() ? nullptr : &selection_);
    if (!lifted.has_value()) {
      notify();
      return;
    }
    Floating floating;
    floating.source = sources_.size();
    floating.pixels = std::move(lifted->first);
    floating.sampler = PerspectiveSampler(floating.pixels);
    floating.plane_id = plane->id;
    floating.placement = fill_placement(*geometry, floating.pixels.width(), floating.pixels.height());
    composite_changed(lifted->second.rect);
    floating.lift = std::move(lifted->second);
    floating_ = std::move(floating);
    selected_id_ = plane->id;
    if (tool_ != WorkspaceTool::EditPlane) {
      tool_ = WorkspaceTool::EditPlane;
      points_.clear();
      updateCursor();
    }
    if (on_layer_placed) {
      on_layer_placed();
    }
    update();
    notify();
  }
  void set_floating_scale(double scale) {
    if (floating_.has_value() && scale > 0.0) {
      floating_->placement.scale = scale;
      update();
    }
  }
  void set_floating_rotation(double degrees) {
    if (floating_.has_value()) {
      floating_->placement.rotation_degrees = degrees;
      update();
    }
  }
  // Writes the floating image onto its plane as one workspace step.
  void commit_floating() {
    if (!floating_.has_value()) {
      return;
    }
    auto floating = std::move(*floating_);
    floating_.reset();
    if (floating.plane_id.has_value()) {
      const auto geometry = geometry_for(*floating.plane_id);
      if (geometry.has_value()) {
        auto change = canvas_.paste(*geometry, floating.pixels,
                                    placement_to_plane(floating.placement, floating.pixels.width(),
                                                       floating.pixels.height()));
        if (!change.empty() || floating.lift.has_value()) {
          composite_changed(change.bounds);
          push_history({planes_, planes_, std::move(change), 0, std::move(floating.lift)});
        }
        update();
        notify();
        return;
      }
    }
    // Not on a plane: nothing is written, and a lifted layer gets its content back.
    if (floating.lift.has_value()) {
      composite_changed(canvas_.set_original_region(floating.lift->rect, floating.lift->before));
    }
    update();
    notify();
  }
  void finish_pending() {
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
    commit_floating();
    points_.clear();
  }

  void undo() {
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
    if (floating_.has_value() || !points_.empty()) {
      cancel_pending();
      return;
    }
    if (undo_.empty()) {
      return;
    }
    auto entry = std::move(undo_.back());
    undo_.pop_back();
    planes_ = entry.planes_before;
    planes_changed();
    if (!entry.pixels.empty()) {
      composite_changed(canvas_.revert(entry.pixels));
    }
    if (entry.lift.has_value()) {
      composite_changed(canvas_.set_original_region(entry.lift->rect, entry.lift->before));
    }
    redo_.push_back(std::move(entry));
    keep_selection_valid();
    notify();
  }
  void redo() {
    if (redo_.empty()) {
      return;
    }
    finish_pending();
    auto entry = std::move(redo_.back());
    redo_.pop_back();
    planes_ = entry.planes_after;
    planes_changed();
    if (entry.lift.has_value()) {
      composite_changed(canvas_.set_original_region(entry.lift->rect, entry.lift->after));
    }
    if (!entry.pixels.empty()) {
      composite_changed(canvas_.reapply(entry.pixels));
    }
    undo_.push_back(std::move(entry));
    keep_selection_valid();
    notify();
  }

  void fit_view() {
    const double margin = 24.0;
    const double sx = (width() - 2.0 * margin) / std::max(1, canvas_.width());
    const double sy = (height() - 2.0 * margin) / std::max(1, canvas_.height());
    zoom_ = std::clamp(std::min(sx, sy), 0.01, 32.0);
    offset_ = QPointF((width() - canvas_.width() * zoom_) * 0.5, (height() - canvas_.height() * zoom_) * 0.5);
    fitted_ = true;
    update();
    notify();
  }
  void zoom_by(double factor, QPointF anchor) {
    const auto document = screen_to_document(anchor);
    zoom_ = std::clamp(zoom_ * factor, 0.01, 32.0);
    offset_ = anchor - QPointF(document.x * zoom_, document.y * zoom_);
    fitted_ = false;
    update();
    notify();
  }
  void set_zoom(double zoom) { zoom_by(zoom / zoom_, rect().center()); }

  // --- Scripted driving (tests): document-space input without the mouse ----------------
  void add_plane_for_test(const PlaneQuad& quad) {
    const auto before = planes_;
    planes_.push_back({next_id_++, quad, 0.0, std::nullopt});
    selected_id_ = planes_.back().id;
    planes_changed();
    record_planes(before);
  }

protected:
  void resizeEvent(QResizeEvent*) override {
    if (fitted_) {
      fit_view();
    }
  }
  void showEvent(QShowEvent*) override { fit_view(); }

  void paintEvent(QPaintEvent* event) override {
    QPainter painter(this);
    painter.fillRect(rect(), theme().canvas_backdrop);
    const QRectF document_rect(offset_, QSizeF(canvas_.width() * zoom_, canvas_.height() * zoom_));
    // The transparency checkerboard, then the layer with the workspace's edits.
    const int square = 8;
    QPixmap tile(square * 2, square * 2);
    tile.fill(checkerboard_light_square());
    {
      QPainter tile_painter(&tile);
      tile_painter.fillRect(0, 0, square, square, checkerboard_dark_square());
      tile_painter.fillRect(square, square, square, square, checkerboard_dark_square());
    }
    painter.fillRect(document_rect.intersected(QRectF(event->rect())), QBrush(tile));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom_ < 1.0);
    // Only the visible part of the document is drawn, so a repaint costs the view's
    // size, never the layer's.
    const QRectF visible = document_rect.intersected(QRectF(rect()));
    if (!visible.isEmpty()) {
      const QRectF source((visible.left() - offset_.x()) / zoom_, (visible.top() - offset_.y()) / zoom_,
                          visible.width() / zoom_, visible.height() / zoom_);
      if (show_backdrop_ && !backdrop_image_.isNull()) {
        painter.drawImage(visible, backdrop_image_, source);
      }
      painter.drawImage(visible, composite_image_, source);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint_planes(painter);
    paint_floating(painter);
    paint_create(painter);
    paint_brush(painter);
  }

  void mousePressEvent(QMouseEvent* event) override {
    setFocus();
    const auto position = event->position();
    last_mouse_ = position;
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (space_held_ || tool_ == WorkspaceTool::Hand))) {
      panning_ = true;
      setCursor(Qt::ClosedHandCursor);
      return;
    }
    if (event->button() != Qt::LeftButton) {
      return;
    }
    const auto document = screen_to_document(position);
    if (floating_.has_value() && press_floating(position, document)) {
      return;
    }
    switch (tool_) {
      case WorkspaceTool::CreatePlane:
        press_create(document);
        break;
      case WorkspaceTool::EditPlane:
        press_edit(position, document);
        break;
      case WorkspaceTool::Clone:
      case WorkspaceTool::Paint:
        press_brush(document, event->modifiers());
        break;
      case WorkspaceTool::Zoom:
        zoom_by((event->modifiers() & Qt::AltModifier) ? 0.5 : 2.0, position);
        break;
      case WorkspaceTool::Hand:
        break;
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const auto position = event->position();
    const auto delta = position - last_mouse_;
    last_mouse_ = position;
    hover_ = position;
    hovering_ = true;
    if (panning_) {
      offset_ += delta;
      fitted_ = false;
      update();
      return;
    }
    const auto document = screen_to_document(position);
    if (floating_drag_ != FloatingDrag::None) {
      drag_floating(document);
      return;
    }
    if (edit_drag_.kind != EditDrag::Kind::None) {
      drag_edit(document);
      return;
    }
    if (canvas_.stroke_active()) {
      stroke_to(document);
      return;
    }
    if (tool_ == WorkspaceTool::CreatePlane) {
      update_create_problem();
      notify();
    }
    update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
      panning_ = false;
      updateCursor();
      return;
    }
    if (event->button() != Qt::LeftButton) {
      return;
    }
    if (floating_drag_ != FloatingDrag::None) {
      floating_drag_ = FloatingDrag::None;
      notify();
      return;
    }
    if (edit_drag_.kind != EditDrag::Kind::None) {
      release_edit();
      return;
    }
    if (canvas_.stroke_active()) {
      finish_stroke();
    }
  }

  void wheelEvent(QWheelEvent* event) override {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0) {
      zoom_by(std::pow(1.25, steps), event->position());
    }
    event->accept();
  }

  void leaveEvent(QEvent*) override {
    hovering_ = false;
    update();
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
      space_held_ = true;
      setCursor(Qt::OpenHandCursor);
      return;
    }
    QWidget::keyPressEvent(event);
  }
  void keyReleaseEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
      space_held_ = false;
      updateCursor();
      return;
    }
    QWidget::keyReleaseEvent(event);
  }

private:
  enum class FloatingDrag { None, Move, Scale, Rotate };
  struct EditDrag {
    enum class Kind { None, Corner, Edge, Move, Pull, Depth };
    Kind kind{Kind::None};
    int plane_id{0};
    int index{0};
    PlanePoint start{};
    std::vector<PerspectivePlane> before;
  };

  void notify() {
    if (on_state_changed) {
      on_state_changed();
    }
  }
  void updateCursor() {
    if (tool_ == WorkspaceTool::Hand) {
      setCursor(Qt::OpenHandCursor);
    } else if (tool_ == WorkspaceTool::Zoom) {
      setCursor(Qt::PointingHandCursor);
    } else {
      setCursor(Qt::CrossCursor);
    }
  }

  [[nodiscard]] PlanePoint screen_to_document(QPointF screen) const {
    return {(screen.x() - offset_.x()) / zoom_, (screen.y() - offset_.y()) / zoom_};
  }
  [[nodiscard]] QPointF document_to_screen(PlanePoint document) const {
    return {offset_.x() + document.x * zoom_, offset_.y() + document.y * zoom_};
  }
  [[nodiscard]] Homography document_to_view() const {
    return homography_scale_translate(zoom_, zoom_, offset_.x(), offset_.y());
  }
  [[nodiscard]] std::optional<QPointF> plane_to_screen(const PlaneGeometry& geometry, PlanePoint p) const {
    const auto mapped = geometry.to_image.map(p);
    if (!mapped.has_value()) {
      return std::nullopt;
    }
    return document_to_screen(*mapped);
  }

  PerspectivePlane* mutable_selected() {
    const auto found = std::find_if(planes_.begin(), planes_.end(),
                                    [this](const PerspectivePlane& plane) { return plane.id == selected_id_; });
    return found == planes_.end() ? nullptr : &*found;
  }
  void keep_selection_valid() {
    if (selected_plane() == nullptr) {
      selected_id_ = planes_.empty() ? 0 : planes_.back().id;
    }
  }

  void planes_changed() {
    camera_ = perspective_camera_for(planes_, canvas_.width(), canvas_.height());
    update_connected_planes(planes_, camera_);
    geometries_.clear();
    for (const auto& plane : planes_) {
      geometries_[plane.id] = plane_geometry(plane.corners, camera_);
    }
    if (floating_.has_value() && floating_->plane_id.has_value() && !geometry_for(*floating_->plane_id).has_value()) {
      floating_->plane_id.reset();
    }
    update();
  }

  void push_history(HistoryEntry entry) {
    redo_.clear();
    undo_.push_back(std::move(entry));
    while (undo_.size() > kHistoryLimit) {
      undo_.pop_front();
    }
  }
  void record_planes(const std::vector<PerspectivePlane>& before, int merge_key = 0) {
    planes_changed();
    if (before == planes_) {
      notify();
      return;
    }
    if (merge_key != 0 && !undo_.empty() && undo_.back().merge_key == merge_key && redo_.empty()) {
      undo_.back().planes_after = planes_;
    } else {
      push_history({before, planes_, {}, merge_key});
    }
    notify();
  }

  void composite_changed(Rect rect) {
    if (!rect.empty()) {
      update();
    }
  }

  // The plane under a document point: the selected one first, then the newest.
  [[nodiscard]] std::optional<int> plane_at(PlanePoint document) const {
    std::vector<int> order;
    if (selected_plane() != nullptr) {
      order.push_back(selected_id_);
    }
    for (auto it = planes_.rbegin(); it != planes_.rend(); ++it) {
      order.push_back(it->id);
    }
    for (const auto id : order) {
      const auto geometry = geometry_for(id);
      if (!geometry.has_value()) {
        continue;
      }
      const auto p = geometry->to_plane.map(document);
      if (p.has_value() && geometry->contains(*p)) {
        return id;
      }
    }
    return std::nullopt;
  }

  // --- Create Plane ------------------------------------------------------------------
  void update_create_problem() {
    create_problem_ = PlaneQuadProblem::None;
    if (points_.size() == 3 && hovering_) {
      const PlaneQuad quad{points_[0], points_[1], points_[2], screen_to_document(hover_)};
      create_problem_ = check_plane_quad(quad);
    }
  }
  void press_create(PlanePoint document) {
    if (points_.size() < 3) {
      points_.push_back(document);
      update();
      notify();
      return;
    }
    const PlaneQuad quad{points_[0], points_[1], points_[2], document};
    create_problem_ = check_plane_quad(quad);
    if (create_problem_ != PlaneQuadProblem::None) {
      notify();
      return;
    }
    const auto before = planes_;
    planes_.push_back({next_id_++, quad, 0.0, std::nullopt});
    selected_id_ = planes_.back().id;
    points_.clear();
    record_planes(before);
    // A new plane is usually adjusted next.
    tool_ = WorkspaceTool::EditPlane;
    updateCursor();
    notify();
  }

  // --- Edit Plane --------------------------------------------------------------------
  [[nodiscard]] std::optional<QPointF> pull_tab_position(const PerspectivePlane& plane, int edge) const {
    std::array<QPointF, 4> screen{};
    QPointF centroid;
    for (std::size_t i = 0; i < 4; ++i) {
      screen[i] = document_to_screen(plane.corners[i]);
      centroid += screen[i] / 4.0;
    }
    const auto a = screen[static_cast<std::size_t>(edge)];
    const auto b = screen[static_cast<std::size_t>((edge + 1) % 4)];
    const auto middle = (a + b) / 2.0;
    QPointF normal(-(b - a).y(), (b - a).x());
    const double length = std::sqrt(normal.x() * normal.x() + normal.y() * normal.y());
    if (!(length > 0.0)) {
      return std::nullopt;
    }
    normal /= length;
    const auto out = middle - centroid;
    if (normal.x() * out.x() + normal.y() * out.y() < 0.0) {
      normal = -normal;
    }
    return middle + normal * kPullTabOffset;
  }
  [[nodiscard]] QPointF depth_handle_position(const PerspectivePlane& plane) const {
    return (document_to_screen(plane.corners[2]) + document_to_screen(plane.corners[3])) / 2.0;
  }

  void press_edit(QPointF screen, PlanePoint document) {
    const auto* selected = selected_plane();
    if (selected != nullptr) {
      // Handles of the selected plane first: corners, the depth diamond, edges, + tabs.
      for (int i = 0; i < 4; ++i) {
        if (screen_distance(document_to_screen(selected->corners[static_cast<std::size_t>(i)]), screen) <=
            kHandleRadius + 3.0) {
          begin_edit(EditDrag::Kind::Corner, selected->id, i, document);
          return;
        }
      }
      if (selected->link.has_value() && screen_distance(depth_handle_position(*selected), screen) <= kHandleRadius + 3.0) {
        begin_edit(EditDrag::Kind::Depth, selected->id, 0, document);
        return;
      }
      for (int edge = 0; edge < 4; ++edge) {
        const auto middle = (document_to_screen(selected->corners[static_cast<std::size_t>(edge)]) +
                             document_to_screen(selected->corners[static_cast<std::size_t>((edge + 1) % 4)])) /
                            2.0;
        if (screen_distance(middle, screen) <= kHandleRadius + 3.0) {
          begin_edit(EditDrag::Kind::Edge, selected->id, edge, document);
          return;
        }
        if (geometry_for(selected->id).has_value()) {
          const auto tab = pull_tab_position(*selected, edge);
          if (tab.has_value() && screen_distance(*tab, screen) <= kPullTabRadius + 2.0) {
            begin_pull(*selected, edge, document);
            return;
          }
        }
      }
    }
    const auto hit = plane_at(document);
    if (hit.has_value()) {
      selected_id_ = *hit;
      begin_edit(EditDrag::Kind::Move, *hit, 0, document);
      notify();
      return;
    }
    update();
    notify();
  }
  void begin_edit(EditDrag::Kind kind, int plane_id, int index, PlanePoint document) {
    edit_drag_ = {kind, plane_id, index, document, planes_};
  }
  void begin_pull(const PerspectivePlane& parent, int edge, PlanePoint document) {
    edit_drag_ = {EditDrag::Kind::Pull, 0, edge, document, planes_};
    PerspectivePlane child;
    child.id = next_id_++;
    child.link = PlaneLink{parent.id, edge, 90.0, 0.05};
    const auto depth = connected_plane_depth_at(parent.corners, edge, 90.0, camera_, document);
    child.link->depth = std::max(0.05, depth.value_or(0.05));
    const auto quad = connected_plane_quad(parent.corners, edge, 90.0, child.link->depth, camera_);
    if (!quad.has_value()) {
      edit_drag_ = {};
      return;
    }
    child.corners = *quad;
    planes_.push_back(child);
    edit_drag_.plane_id = child.id;
    selected_id_ = child.id;
    planes_changed();
    notify();
  }
  void drag_edit(PlanePoint document) {
    auto found = std::find_if(planes_.begin(), planes_.end(),
                              [this](const PerspectivePlane& plane) { return plane.id == edit_drag_.plane_id; });
    if (found == planes_.end()) {
      return;
    }
    auto& plane = *found;
    const double dx = document.x - edit_drag_.start.x;
    const double dy = document.y - edit_drag_.start.y;
    const auto original_it = std::find_if(edit_drag_.before.begin(), edit_drag_.before.end(),
                                          [this](const PerspectivePlane& candidate) {
                                            return candidate.id == edit_drag_.plane_id;
                                          });
    const bool reshaping = edit_drag_.kind == EditDrag::Kind::Corner || edit_drag_.kind == EditDrag::Kind::Edge ||
                           edit_drag_.kind == EditDrag::Kind::Move;
    if (reshaping && original_it == edit_drag_.before.end()) {
      return;
    }
    const PerspectivePlane* original = reshaping ? &*original_it : nullptr;
    switch (edit_drag_.kind) {
      case EditDrag::Kind::Corner:
        plane.link.reset();
        plane.corners = original->corners;
        plane.corners[static_cast<std::size_t>(edit_drag_.index)] = document;
        break;
      case EditDrag::Kind::Edge: {
        plane.link.reset();
        plane.corners = original->corners;
        for (const int k : {edit_drag_.index, (edit_drag_.index + 1) % 4}) {
          plane.corners[static_cast<std::size_t>(k)].x += dx;
          plane.corners[static_cast<std::size_t>(k)].y += dy;
        }
        break;
      }
      case EditDrag::Kind::Move:
        plane.link.reset();
        plane.corners = original->corners;
        for (auto& corner : plane.corners) {
          corner.x += dx;
          corner.y += dy;
        }
        break;
      case EditDrag::Kind::Pull:
      case EditDrag::Kind::Depth: {
        if (!plane.link.has_value()) {
          break;
        }
        const auto parent = std::find_if(planes_.begin(), planes_.end(), [&plane](const PerspectivePlane& other) {
          return other.id == plane.link->parent_id;
        });
        if (parent == planes_.end()) {
          break;
        }
        const auto depth =
            connected_plane_depth_at(parent->corners, plane.link->edge, plane.link->angle_degrees, camera_, document);
        if (depth.has_value() &&
            connected_plane_quad(parent->corners, plane.link->edge, plane.link->angle_degrees, *depth, camera_)
                .has_value()) {
          plane.link->depth = *depth;
        }
        break;
      }
      case EditDrag::Kind::None:
        break;
    }
    planes_changed();
    notify();
  }
  void release_edit() {
    auto before = std::move(edit_drag_.before);
    edit_drag_ = {};
    record_planes(before);
  }

  // --- Floating paste ----------------------------------------------------------------
  // The pasted image's corners on screen (when on a plane) in image order.
  [[nodiscard]] std::optional<std::array<QPointF, 4>> floating_corners() const {
    if (!floating_.has_value() || !floating_->plane_id.has_value()) {
      return std::nullopt;
    }
    const auto geometry = geometry_for(*floating_->plane_id);
    if (!geometry.has_value()) {
      return std::nullopt;
    }
    const auto image_to_plane =
        placement_to_plane(floating_->placement, floating_->pixels.width(), floating_->pixels.height());
    const double w = floating_->pixels.width();
    const double h = floating_->pixels.height();
    std::array<QPointF, 4> corners{};
    const std::array<PlanePoint, 4> image{{{0.0, 0.0}, {w, 0.0}, {w, h}, {0.0, h}}};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto on_plane = image_to_plane.map(image[i]);
      const auto screen = on_plane.has_value() ? plane_to_screen(*geometry, *on_plane) : std::nullopt;
      if (!screen.has_value()) {
        return std::nullopt;
      }
      corners[i] = *screen;
    }
    return corners;
  }
  [[nodiscard]] std::optional<QPointF> rotate_handle_position() const {
    const auto corners = floating_corners();
    if (!corners.has_value()) {
      return std::nullopt;
    }
    const auto top = ((*corners)[0] + (*corners)[1]) / 2.0;
    const auto center = ((*corners)[0] + (*corners)[1] + (*corners)[2] + (*corners)[3]) / 4.0;
    auto direction = top - center;
    const double length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
    if (!(length > 0.0)) {
      return std::nullopt;
    }
    return top + direction / length * kRotateHandleOffset;
  }
  [[nodiscard]] bool floating_contains(QPointF screen) const {
    const auto corners = floating_corners();
    if (corners.has_value()) {
      QPolygonF polygon;
      for (const auto& corner : *corners) {
        polygon << corner;
      }
      return polygon.containsPoint(screen, Qt::OddEvenFill);
    }
    if (floating_.has_value() && !floating_->plane_id.has_value()) {
      return loose_rect().contains(screen);
    }
    return false;
  }
  [[nodiscard]] QRectF loose_rect() const {
    const double scale = std::min(1.0, 240.0 / std::max(1, std::max(floating_->pixels.width(), floating_->pixels.height())));
    const QSizeF size(floating_->pixels.width() * scale, floating_->pixels.height() * scale);
    const auto center = document_to_screen(to_plane_point(floating_->loose_center));
    return {center - QPointF(size.width() / 2.0, size.height() / 2.0), size};
  }
  bool press_floating(QPointF screen, PlanePoint document) {
    const auto rotate = rotate_handle_position();
    if (rotate.has_value() && screen_distance(*rotate, screen) <= kHandleRadius + 3.0) {
      floating_drag_ = FloatingDrag::Rotate;
      return true;
    }
    const auto corners = floating_corners();
    if (corners.has_value()) {
      for (const auto& corner : *corners) {
        if (screen_distance(corner, screen) <= kHandleRadius + 3.0) {
          floating_drag_ = FloatingDrag::Scale;
          scale_start_ = floating_->placement.scale;
          scale_start_distance_ = std::max(1.0e-6, plane_distance_to_center(document));
          return true;
        }
      }
    }
    if (floating_contains(screen)) {
      floating_drag_ = FloatingDrag::Move;
      grab_offset_ = {};
      if (floating_->plane_id.has_value()) {
        // Keep the spot that was grabbed under the pointer.
        const auto geometry = geometry_for(*floating_->plane_id);
        const auto p = geometry.has_value() ? geometry->to_plane.map(document) : std::nullopt;
        if (p.has_value()) {
          grab_offset_ = {floating_->placement.center_x - p->x, floating_->placement.center_y - p->y};
        }
      }
      return true;
    }
    // A click elsewhere places the image and goes on with the current tool.
    commit_floating();
    return false;
  }
  [[nodiscard]] double plane_distance_to_center(PlanePoint document) const {
    const auto geometry = geometry_for(floating_->plane_id.value_or(-1));
    if (!geometry.has_value()) {
      return 0.0;
    }
    const auto p = geometry->to_plane.map(document);
    if (!p.has_value()) {
      return 0.0;
    }
    const double dx = p->x - floating_->placement.center_x;
    const double dy = p->y - floating_->placement.center_y;
    return std::sqrt(dx * dx + dy * dy);
  }
  void drag_floating(PlanePoint document) {
    auto& floating = *floating_;
    switch (floating_drag_) {
      case FloatingDrag::Move: {
        // Over a plane the image takes on that plane's perspective, centered under the
        // pointer; it is always on exactly one plane, never split across two.
        const auto hit = plane_at(document);
        if (hit.has_value()) {
          const auto geometry = geometry_for(*hit);
          const auto p = geometry->to_plane.map(document);
          if (p.has_value()) {
            if (floating.plane_id != hit) {
              // Onto another plane: fitted to it and centered under the pointer.
              floating.plane_id = *hit;
              const auto fitted = fit_placement(*geometry, floating.pixels.width(), floating.pixels.height());
              floating.placement.scale = fitted.scale;
              floating.placement.rotation_degrees = fitted.rotation_degrees;
              grab_offset_ = {};
            }
            floating.placement.center_x = p->x + grab_offset_.x;
            floating.placement.center_y = p->y + grab_offset_.y;
          }
        } else {
          floating.plane_id.reset();
          floating.loose_center = to_qpoint(document);
        }
        break;
      }
      case FloatingDrag::Scale: {
        const double reach = plane_distance_to_center(document);
        if (reach > 0.0) {
          floating.placement.scale = std::clamp(scale_start_ * reach / scale_start_distance_, 0.01, 100.0);
        }
        break;
      }
      case FloatingDrag::Rotate: {
        const auto geometry = geometry_for(floating.plane_id.value_or(-1));
        const auto p = geometry.has_value() ? geometry->to_plane.map(document) : std::nullopt;
        if (p.has_value()) {
          const double dx = p->x - floating.placement.center_x;
          const double dy = p->y - floating.placement.center_y;
          if (dx != 0.0 || dy != 0.0) {
            // The handle sits above the image's top edge: straight up is 0 degrees.
            double degrees = std::atan2(dx, -dy) * 180.0 / 3.14159265358979323846;
            degrees = std::round(degrees * 10.0) / 10.0;
            if (degrees < 0.0) {
              degrees += 360.0;
            }
            floating.placement.rotation_degrees = degrees;
          }
        }
        break;
      }
      case FloatingDrag::None:
        break;
    }
    update();
    notify();
  }

  // --- Clone and Paint ---------------------------------------------------------------
  void press_brush(PlanePoint document, Qt::KeyboardModifiers modifiers) {
    const auto hit = plane_at(document);
    if (tool_ == WorkspaceTool::Clone && (modifiers & Qt::AltModifier)) {
      if (hit.has_value()) {
        const auto p = geometry_for(*hit)->to_plane.map(document);
        if (p.has_value()) {
          clone_source_ = std::make_pair(*hit, *p);
          clone_offset_.reset();
        }
      }
      update();
      notify();
      return;
    }
    if (!hit.has_value()) {
      return;
    }
    if (tool_ == WorkspaceTool::Clone && !clone_source_.has_value()) {
      notify();
      return;
    }
    const auto geometry = *geometry_for(*hit);
    const auto p = geometry.to_plane.map(document);
    if (!p.has_value()) {
      return;
    }
    PerspectiveStrokeSetup setup;
    setup.kind = tool_ == WorkspaceTool::Clone ? PerspectiveStrokeKind::Clone : PerspectiveStrokeKind::Paint;
    setup.plane = geometry;
    setup.brush = brush_;
    setup.color = {static_cast<std::uint8_t>(paint_color_.red()), static_cast<std::uint8_t>(paint_color_.green()),
                   static_cast<std::uint8_t>(paint_color_.blue()), 255};
    if (setup.kind == PerspectiveStrokeKind::Clone) {
      const auto source_geometry = geometry_for(clone_source_->first);
      if (!source_geometry.has_value()) {
        clone_source_.reset();
        notify();
        return;
      }
      setup.source_plane = *source_geometry;
      // Aligned: the first stroke after picking a source fixes the offset.
      if (!clone_offset_.has_value()) {
        clone_offset_ = PlanePoint{clone_source_->second.x - p->x, clone_source_->second.y - p->y};
      }
      setup.source_offset = *clone_offset_;
    }
    stroke_plane_ = *hit;
    if (selected_id_ != *hit) {
      selected_id_ = *hit;
      notify();
    }
    canvas_.begin_stroke(setup);
    composite_changed(canvas_.stroke_to(*p));
    update();
  }
  void stroke_to(PlanePoint document) {
    const auto geometry = geometry_for(stroke_plane_);
    if (!geometry.has_value()) {
      return;
    }
    const auto p = geometry->to_plane.map(document);
    if (p.has_value()) {
      composite_changed(canvas_.stroke_to(*p));
    }
    update();
  }
  void finish_stroke() {
    auto change = canvas_.end_stroke();
    if (!change.empty()) {
      push_history({planes_, planes_, std::move(change), 0});
    }
    notify();
  }

  // --- Painting ----------------------------------------------------------------------
  void paint_planes(QPainter& painter) const {
    const auto accent = theme().accent_bright;
    const auto problem = theme().status_error_mark;
    for (const auto& plane : planes_) {
      const bool selected = plane.id == selected_id_;
      const auto geometry = geometry_for(plane.id);
      QPolygonF outline;
      for (const auto& corner : plane.corners) {
        outline << document_to_screen(corner);
      }
      outline << outline.front();
      if (geometry.has_value()) {
        QColor fill = accent;
        fill.setAlpha(selected ? 56 : 14);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(outline);
        paint_grid(painter, plane, *geometry);
      }
      QPainterPath path;
      path.addPolygon(outline);
      draw_pair(painter, path, !geometry.has_value() ? problem : (selected ? accent : QColor(255, 255, 255, 170)),
                selected ? 3.0 : 1.0);
      if (selected && tool_ == WorkspaceTool::EditPlane && !floating_.has_value()) {
        paint_handles(painter, plane, geometry.has_value());
      }
    }
    // The tag goes last so no other plane covers it.
    for (const auto& plane : planes_) {
      if (plane.id == selected_id_ && geometry_for(plane.id).has_value()) {
        paint_target_tag(painter, plane);
      }
    }
  }
  void paint_target_tag(QPainter& painter, const PerspectivePlane& plane) const {
    // Beside the corner nearest the top left of the screen, inside the plane.
    QPointF anchor = document_to_screen(plane.corners[0]);
    QPointF centroid;
    for (const auto& corner : plane.corners) {
      const auto point = document_to_screen(corner);
      centroid += point / 4.0;
      if (point.x() + point.y() < anchor.x() + anchor.y()) {
        anchor = point;
      }
    }
    const auto text = QObject::tr("Target");
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const QSizeF size(metrics.horizontalAdvance(text) + 12.0, metrics.height() + 4.0);
    auto inward = centroid - anchor;
    const double length = std::sqrt(inward.x() * inward.x() + inward.y() * inward.y());
    if (length > 0.0) {
      inward /= length;
    }
    const QPointF place = anchor + inward * 18.0;
    const QRectF box(place, size);
    painter.setPen(QPen(QColor(0, 0, 0, 150), 1.0));
    painter.setBrush(theme().accent_bright);
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(theme().text_on_accent_bright);
    painter.drawText(box, Qt::AlignCenter, text);
  }
  void paint_grid(QPainter& painter, const PerspectivePlane& plane, const PlaneGeometry& geometry) const {
    double step = plane_grid_size(plane, geometry);
    while (geometry.width / step + geometry.height / step > 400.0) {
      step *= 2.0;
    }
    QPainterPath path;
    const auto line = [&](PlanePoint a, PlanePoint b) {
      const auto sa = plane_to_screen(geometry, a);
      const auto sb = plane_to_screen(geometry, b);
      if (sa.has_value() && sb.has_value()) {
        path.moveTo(*sa);
        path.lineTo(*sb);
      }
    };
    for (double s = step; s < geometry.width - 1.0e-6; s += step) {
      line({s, 0.0}, {s, geometry.height});
    }
    for (double t = step; t < geometry.height - 1.0e-6; t += step) {
      line({0.0, t}, {geometry.width, t});
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 70), 2.0));
    painter.drawPath(path);
    painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
    painter.drawPath(path);
  }
  void paint_handles(QPainter& painter, const PerspectivePlane& plane, bool valid) const {
    const auto accent = theme().accent_bright;
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
    for (int edge = 0; edge < 4; ++edge) {
      const auto a = document_to_screen(plane.corners[static_cast<std::size_t>(edge)]);
      const auto b = document_to_screen(plane.corners[static_cast<std::size_t>((edge + 1) % 4)]);
      painter.setBrush(QColor(255, 255, 255, 230));
      painter.drawEllipse((a + b) / 2.0, kHandleRadius - 1.5, kHandleRadius - 1.5);
      if (!valid) {
        continue;
      }
      const auto tab = pull_tab_position(plane, edge);
      if (tab.has_value()) {
        painter.setBrush(accent);
        painter.drawEllipse(*tab, kPullTabRadius, kPullTabRadius);
        painter.setPen(QPen(QColor(255, 255, 255, 240), 2.0));
        painter.drawLine(*tab - QPointF(4.0, 0.0), *tab + QPointF(4.0, 0.0));
        painter.drawLine(*tab - QPointF(0.0, 4.0), *tab + QPointF(0.0, 4.0));
        painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
      }
    }
    painter.setBrush(QColor(255, 255, 255, 240));
    for (const auto& corner : plane.corners) {
      const auto point = document_to_screen(corner);
      painter.drawRect(QRectF(point - QPointF(kHandleRadius, kHandleRadius), QSizeF(2.0 * kHandleRadius, 2.0 * kHandleRadius)));
    }
    if (plane.link.has_value()) {
      const auto point = depth_handle_position(plane);
      QPolygonF diamond;
      diamond << point + QPointF(0.0, -kHandleRadius - 2.0) << point + QPointF(kHandleRadius + 2.0, 0.0)
              << point + QPointF(0.0, kHandleRadius + 2.0) << point + QPointF(-kHandleRadius - 2.0, 0.0);
      painter.setBrush(accent);
      painter.drawPolygon(diamond);
    }
  }
  void paint_create(QPainter& painter) const {
    if (tool_ != WorkspaceTool::CreatePlane || points_.empty()) {
      return;
    }
    QPolygonF line;
    for (const auto& point : points_) {
      line << document_to_screen(point);
    }
    const auto accent = theme().accent_bright;
    if (points_.size() == 3 && hovering_) {
      const PlaneQuad quad{points_[0], points_[1], points_[2], screen_to_document(hover_)};
      const auto geometry = plane_geometry(quad, perspective_camera_for(planes_, canvas_.width(), canvas_.height()));
      line << hover_ << line.front();
      if (geometry.has_value()) {
        QColor fill = accent;
        fill.setAlpha(40);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPolygon(line);
        paint_grid(painter, PerspectivePlane{0, quad, 0.0, std::nullopt}, *geometry);
      }
      QPainterPath path;
      path.addPolygon(line);
      draw_pair(painter, path, geometry.has_value() ? accent : theme().status_error_mark, 2.0);
    } else {
      if (hovering_) {
        line << hover_;
      }
      QPainterPath path;
      path.addPolygon(line);
      draw_pair(painter, path, accent, 1.5);
    }
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
    painter.setBrush(QColor(255, 255, 255, 240));
    for (const auto& point : points_) {
      const auto screen = document_to_screen(point);
      painter.drawRect(QRectF(screen - QPointF(kHandleRadius, kHandleRadius), QSizeF(2.0 * kHandleRadius, 2.0 * kHandleRadius)));
    }
  }
  void paint_floating(QPainter& painter) {
    if (!floating_.has_value()) {
      return;
    }
    if (!floating_->plane_id.has_value()) {
      painter.setOpacity(0.75);
      const auto target = loose_rect();
      const auto& pixels = std::as_const(floating_->pixels);
      painter.drawImage(target, QImage(pixels.data().data(), pixels.width(),
                                       pixels.height(), static_cast<qsizetype>(pixels.stride_bytes()),
                                       QImage::Format_RGBA8888));
      painter.setOpacity(1.0);
      QPainterPath path;
      path.addRect(target);
      draw_pair(painter, path, theme().accent_bright, 1.5);
      return;
    }
    const auto geometry = geometry_for(*floating_->plane_id);
    if (!geometry.has_value()) {
      return;
    }
    // The pasted image rendered at screen resolution through the same plane-clipped
    // mapping the final write uses; only the visible part of the plane is computed.
    if (overlay_.width() != width() || overlay_.height() != height()) {
      overlay_ = PixelBuffer(std::max(1, width()), std::max(1, height()), PixelFormat::rgba8());
      overlay_.clear(0);
    } else if (!overlay_dirty_.empty()) {
      (void)overlay_.data();
      for (std::int32_t y = overlay_dirty_.y; y < overlay_dirty_.y + overlay_dirty_.height; ++y) {
        std::fill_n(overlay_.pixel(overlay_dirty_.x, y), static_cast<std::size_t>(overlay_dirty_.width) * 4, 0);
      }
    }
    const auto image_to_plane =
        placement_to_plane(floating_->placement, floating_->pixels.width(), floating_->pixels.height());
    overlay_dirty_ = render_perspective_image(overlay_, document_to_view(), Rect{0, 0, width(), height()}, *geometry,
                                              floating_->sampler, image_to_plane);
    if (!overlay_dirty_.empty()) {
      const QImage image(overlay_.data().data(), overlay_.width(), overlay_.height(),
                         static_cast<qsizetype>(overlay_.stride_bytes()), QImage::Format_RGBA8888);
      const QRect area(overlay_dirty_.x, overlay_dirty_.y, overlay_dirty_.width, overlay_dirty_.height);
      painter.drawImage(area, image, area);
    }
    const auto corners = floating_corners();
    if (!corners.has_value()) {
      return;
    }
    QPolygonF outline;
    for (const auto& corner : *corners) {
      outline << corner;
    }
    outline << outline.front();
    QPainterPath path;
    path.addPolygon(outline);
    draw_pair(painter, path, theme().accent_bright, 1.5);
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
    painter.setBrush(QColor(255, 255, 255, 240));
    for (const auto& corner : *corners) {
      painter.drawRect(QRectF(corner - QPointF(kHandleRadius, kHandleRadius), QSizeF(2.0 * kHandleRadius, 2.0 * kHandleRadius)));
    }
    const auto rotate = rotate_handle_position();
    if (rotate.has_value()) {
      const auto top = ((*corners)[0] + (*corners)[1]) / 2.0;
      QPainterPath stem;
      stem.moveTo(top);
      stem.lineTo(*rotate);
      draw_pair(painter, stem, theme().accent_bright, 1.5);
      painter.setPen(QPen(QColor(0, 0, 0, 200), 1.5));
      painter.setBrush(theme().accent_bright);
      painter.drawEllipse(*rotate, kHandleRadius, kHandleRadius);
    }
  }
  void paint_brush(QPainter& painter) const {
    if ((tool_ != WorkspaceTool::Clone && tool_ != WorkspaceTool::Paint) || floating_.has_value()) {
      return;
    }
    // The clone source: a crosshair where the next sample comes from.
    if (tool_ == WorkspaceTool::Clone && clone_source_.has_value()) {
      const auto source_geometry = geometry_for(clone_source_->first);
      std::optional<PlanePoint> source_point = clone_source_->second;
      if (clone_offset_.has_value() && hovering_) {
        const auto hit = canvas_.stroke_active() ? std::optional<int>(stroke_plane_) : plane_at(screen_to_document(hover_));
        const auto geometry = hit.has_value() ? geometry_for(*hit) : std::nullopt;
        const auto p = geometry.has_value() ? geometry->to_plane.map(screen_to_document(hover_)) : std::nullopt;
        source_point = p.has_value() ? std::optional<PlanePoint>(PlanePoint{p->x + clone_offset_->x, p->y + clone_offset_->y})
                                     : std::nullopt;
      }
      if (source_geometry.has_value() && source_point.has_value()) {
        const auto screen = plane_to_screen(*source_geometry, *source_point);
        if (screen.has_value()) {
          QPainterPath cross;
          cross.moveTo(*screen - QPointF(9.0, 0.0));
          cross.lineTo(*screen + QPointF(9.0, 0.0));
          cross.moveTo(*screen - QPointF(0.0, 9.0));
          cross.lineTo(*screen + QPointF(0.0, 9.0));
          cross.addEllipse(*screen, 5.0, 5.0);
          draw_pair(painter, cross, QColor(255, 255, 255, 230), 1.0);
        }
      }
    }
    if (!hovering_) {
      return;
    }
    const auto document = screen_to_document(hover_);
    const auto hit = canvas_.stroke_active() ? std::optional<int>(stroke_plane_) : plane_at(document);
    const auto geometry = hit.has_value() ? geometry_for(*hit) : std::nullopt;
    const auto center = geometry.has_value() ? geometry->to_plane.map(document) : std::nullopt;
    if (!center.has_value()) {
      // Off every plane: a small crossed ring says the brush cannot paint here.
      QPainterPath ring;
      ring.addEllipse(hover_, 7.0, 7.0);
      ring.moveTo(hover_ + QPointF(-5.0, -5.0));
      ring.lineTo(hover_ + QPointF(5.0, 5.0));
      draw_pair(painter, ring, QColor(255, 255, 255, 220), 1.0);
      return;
    }
    // The brush as it lands on the plane: a circle in plane space, foreshortened.
    const double radius = brush_.size * 0.5;
    QPainterPath outline;
    bool started = false;
    for (int i = 0; i <= 64; ++i) {
      const auto [s, c] = perspective_sin_cos_degrees(i * 360.0 / 64.0);
      const auto screen = plane_to_screen(*geometry, {center->x + radius * c, center->y + radius * s});
      if (!screen.has_value()) {
        started = false;
        continue;
      }
      if (!started) {
        outline.moveTo(*screen);
        started = true;
      } else {
        outline.lineTo(*screen);
      }
    }
    draw_pair(painter, outline, QColor(255, 255, 255, 230), 1.0);
  }

  PerspectiveCanvas canvas_;
  QImage composite_image_;
  PixelBuffer backdrop_;
  QImage backdrop_image_;
  bool show_backdrop_{true};
  PixelBuffer selection_;
  bool can_place_layer_{false};
  bool layer_has_content_{false};
  bool suggest_place_layer_{false};
  std::vector<PerspectivePlane> planes_;
  std::map<int, std::optional<PlaneGeometry>> geometries_;
  PerspectiveCamera camera_{};
  std::vector<PerspectivePasteSource> sources_;
  int next_id_{1};
  int selected_id_{0};
  WorkspaceTool tool_{WorkspaceTool::CreatePlane};
  std::vector<PlanePoint> points_;
  PlaneQuadProblem create_problem_{PlaneQuadProblem::None};
  EditDrag edit_drag_{};
  std::optional<Floating> floating_;
  FloatingDrag floating_drag_{FloatingDrag::None};
  PlanePoint grab_offset_{};
  double scale_start_{1.0};
  double scale_start_distance_{1.0};
  PixelBuffer overlay_;
  Rect overlay_dirty_{};
  PerspectiveBrush brush_{};
  QColor paint_color_;
  std::optional<std::pair<int, PlanePoint>> clone_source_;
  std::optional<PlanePoint> clone_offset_;
  int stroke_plane_{0};
  std::deque<HistoryEntry> undo_;
  std::vector<HistoryEntry> redo_;
  double zoom_{1.0};
  QPointF offset_;
  bool fitted_{true};
  bool panning_{false};
  bool space_held_{false};
  bool hovering_{false};
  QPointF hover_;
  QPointF last_mouse_;
};

// Esc and Enter go to the workspace first: they drop or place what is pending before
// they reach the dialog's Cancel and OK.
class PerspectivePlanesDialog final : public QDialog {
public:
  using QDialog::QDialog;
  PerspectivePlanesView* view{nullptr};

  void done(int result) override {
    if (view != nullptr && result == QDialog::Accepted) {
      view->finish_pending();
    }
    QDialog::done(result);
  }

protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (view != nullptr && event->key() == Qt::Key_Escape && view->cancel_pending()) {
      event->accept();
      return;
    }
    if (view != nullptr && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && view->has_floating()) {
      view->commit_floating();
      event->accept();
      return;
    }
    QDialog::keyPressEvent(event);
  }
};

struct ToolEntry {
  WorkspaceTool tool;
  const char* name;
  const char* tip;
  const char* object_name;
  Qt::Key key;
};

constexpr std::array<ToolEntry, 6> kTools{{
    {WorkspaceTool::CreatePlane, QT_TRANSLATE_NOOP("QObject", "Create Plane"),
     QT_TRANSLATE_NOOP("QObject", "Click four corners of a flat surface"), "perspectiveCreatePlaneTool", Qt::Key_C},
    {WorkspaceTool::EditPlane, QT_TRANSLATE_NOOP("QObject", "Edit Plane"),
     QT_TRANSLATE_NOOP("QObject", "Select, move and reshape planes, or pull out a joined plane"),
     "perspectiveEditPlaneTool", Qt::Key_V},
    {WorkspaceTool::Clone, QT_TRANSLATE_NOOP("QObject", "Clone"),
     QT_TRANSLATE_NOOP("QObject", "Copy one part of a plane onto another, in perspective"), "perspectiveCloneTool",
     Qt::Key_S},
    {WorkspaceTool::Paint, QT_TRANSLATE_NOOP("QObject", "Paint"),
     QT_TRANSLATE_NOOP("QObject", "Paint a color onto a plane, in perspective"), "perspectivePaintTool", Qt::Key_B},
    {WorkspaceTool::Hand, QT_TRANSLATE_NOOP("QObject", "Hand"), QT_TRANSLATE_NOOP("QObject", "Scroll the view"),
     "perspectiveHandTool", Qt::Key_H},
    {WorkspaceTool::Zoom, QT_TRANSLATE_NOOP("QObject", "Zoom"), QT_TRANSLATE_NOOP("QObject", "Zoom the view"),
     "perspectiveZoomTool", Qt::Key_Z},
}};

QString with_key(const QString& text, const QKeySequence& keys) {
  return QStringLiteral("%1 (%2)").arg(text, keys.toString(QKeySequence::NativeText));
}

}  // namespace

PerspectiveWorkspaceResult run_perspective_planes_workspace(QWidget* parent, PerspectiveWorkspaceInput input) {
  PerspectiveWorkspaceResult result;
  PerspectivePlanesDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("perspectivePlanesDialog"));
  dialog.setWindowTitle(QObject::tr("Perspective Planes"));
  dialog.resize(1100, 740);

  auto* root = new QVBoxLayout(&dialog);

  // Options strip: what the current tool or the pasted image can change.
  auto* strip = new QStackedWidget(&dialog);
  strip->setObjectName(QStringLiteral("perspectiveOptionsStrip"));
  // Beside the strip: which plane edits go to, and which layer.
  auto* strip_row = new QHBoxLayout();
  strip_row->addWidget(strip, 1);
  auto* target_label = new QLabel(&dialog);
  target_label->setObjectName(QStringLiteral("perspectiveTargetLabel"));
  strip_row->addWidget(target_label);
  root->addLayout(strip_row);

  auto* plane_page = new QWidget(strip);
  auto* plane_row = new QHBoxLayout(plane_page);
  plane_row->setContentsMargins(0, 0, 0, 0);
  plane_row->addWidget(new QLabel(QObject::tr("Grid:"), plane_page));
  auto* grid = new QSpinBox(plane_page);
  grid->setObjectName(QStringLiteral("perspectiveGridSpin"));
  grid->setRange(0, 5000);
  grid->setSpecialValueText(QObject::tr("Auto"));
  grid->setSuffix(QObject::tr(" px"));
  grid->setToolTip(QObject::tr("The grid cell size on the selected plane, measured along its nearest edge"));
  plane_row->addWidget(grid);
  plane_row->addSpacing(12);
  auto* angle_label = new QLabel(QObject::tr("Angle:"), plane_page);
  plane_row->addWidget(angle_label);
  auto* angle = new QSpinBox(plane_page);
  angle->setObjectName(QStringLiteral("perspectiveAngleSpin"));
  angle->setRange(10, 350);
  angle->setValue(90);
  angle->setSuffix(QObject::tr(" deg"));
  angle->setToolTip(QObject::tr("The angle between a joined plane and the plane it was pulled from: 90 is a square "
                                "corner toward you, 180 is flat, 270 a corner away from you"));
  plane_row->addWidget(angle);
  plane_row->addSpacing(12);
  auto* delete_plane = new QPushButton(QObject::tr("Delete Plane"), plane_page);
  delete_plane->setObjectName(QStringLiteral("perspectiveDeletePlaneButton"));
  plane_row->addWidget(delete_plane);
  // What to do with a plane: these name the next step once one exists.
  plane_row->addSpacing(18);
  auto* next_label = new QLabel(QObject::tr("Then:"), plane_page);
  next_label->setObjectName(QStringLiteral("perspectiveNextStepLabel"));
  plane_row->addWidget(next_label);
  auto* place_layer = new QPushButton(QObject::tr("Place Layer"), plane_page);
  place_layer->setObjectName(QStringLiteral("perspectivePlaceLayerButton"));
  place_layer->setToolTip(QObject::tr("Put this layer on the plane, filling it corner to corner (only the selected part "
                                      "when there is a selection); OK replaces the layer with the result"));
  plane_row->addWidget(place_layer);
  auto* paste_onto = new QPushButton(QObject::tr("Paste onto Plane"), plane_page);
  paste_onto->setObjectName(QStringLiteral("perspectivePasteOntoPlaneButton"));
  plane_row->addWidget(paste_onto);
  auto* next_clone = new QPushButton(QObject::tr("Clone"), plane_page);
  next_clone->setObjectName(QStringLiteral("perspectiveNextCloneButton"));
  plane_row->addWidget(next_clone);
  auto* next_paint = new QPushButton(QObject::tr("Paint"), plane_page);
  next_paint->setObjectName(QStringLiteral("perspectiveNextPaintButton"));
  plane_row->addWidget(next_paint);
  plane_row->addStretch(1);
  strip->addWidget(plane_page);

  auto* brush_page = new QWidget(strip);
  auto* brush_row = new QHBoxLayout(brush_page);
  brush_row->setContentsMargins(0, 0, 0, 0);
  const auto add_spin = [brush_page, brush_row](const QString& label, const char* object_name, int minimum, int maximum,
                                                int value, const QString& suffix) {
    brush_row->addWidget(new QLabel(label, brush_page));
    auto* spin = new QSpinBox(brush_page);
    spin->setObjectName(QLatin1String(object_name));
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setSuffix(suffix);
    brush_row->addWidget(spin);
    brush_row->addSpacing(12);
    return spin;
  };
  auto* size = add_spin(QObject::tr("Size:"), "perspectiveBrushSizeSpin", 1, 2000, 60, QObject::tr(" px"));
  size->setToolTip(QObject::tr("Brush diameter where the plane is nearest; it shrinks with distance"));
  auto* hardness = add_spin(QObject::tr("Hardness:"), "perspectiveBrushHardnessSpin", 0, 100, 50, percent_suffix());
  auto* opacity = add_spin(QObject::tr("Opacity:"), "perspectiveBrushOpacitySpin", 1, 100, 100, percent_suffix());
  auto* color_button = new QPushButton(QObject::tr("Color..."), brush_page);
  color_button->setObjectName(QStringLiteral("perspectivePaintColorButton"));
  color_button->setToolTip(QObject::tr("The color Paint uses (starts as the foreground color)"));
  brush_row->addWidget(color_button);
  // Clone's Sample: the retouch tools' shared choice and texts.
  auto* sample_label = new QLabel(QObject::tr("Sample:"), brush_page);
  brush_row->addWidget(sample_label);
  auto* sample = new QComboBox(brush_page);
  sample->setObjectName(QStringLiteral("perspectiveCloneSampleCombo"));
  sample->addItem(QCoreApplication::translate("patchy::ui::MainWindow", "Current Layer"),
                  static_cast<int>(SampleLayers::CurrentLayer));
  sample->addItem(QCoreApplication::translate("patchy::ui::MainWindow", "Current & Below"),
                  static_cast<int>(SampleLayers::CurrentAndBelow));
  sample->addItem(QCoreApplication::translate("patchy::ui::MainWindow", "All Layers"),
                  static_cast<int>(SampleLayers::AllLayers));
  sample->setToolTip(QObject::tr("Which layers Clone reads: this layer, it and the layers below, or everything you "
                                 "see. A pasted image is read once it is placed. Shared with the Clone tool."));
  brush_row->addWidget(sample);
  brush_row->addStretch(1);
  strip->addWidget(brush_page);

  auto* paste_page = new QWidget(strip);
  auto* paste_row = new QHBoxLayout(paste_page);
  paste_row->setContentsMargins(0, 0, 0, 0);
  paste_row->addWidget(new QLabel(QObject::tr("Scale:"), paste_page));
  auto* scale = new QSpinBox(paste_page);
  scale->setObjectName(QStringLiteral("perspectivePasteScaleSpin"));
  scale->setRange(1, 10000);
  scale->setSuffix(percent_suffix());
  paste_row->addWidget(scale);
  paste_row->addSpacing(12);
  paste_row->addWidget(new QLabel(QObject::tr("Rotation:"), paste_page));
  auto* rotation = new QSpinBox(paste_page);
  rotation->setObjectName(QStringLiteral("perspectivePasteRotationSpin"));
  rotation->setRange(-360, 360);
  rotation->setWrapping(true);
  rotation->setSuffix(QObject::tr(" deg"));
  paste_row->addWidget(rotation);
  paste_row->addSpacing(12);
  auto* place = new QPushButton(QObject::tr("Place"), paste_page);
  place->setObjectName(QStringLiteral("perspectivePlaceButton"));
  place->setToolTip(with_key(QObject::tr("Write the image onto its plane"), QKeySequence(Qt::Key_Return)));
  paste_row->addWidget(place);
  auto* remove = new QPushButton(QObject::tr("Remove"), paste_page);
  remove->setObjectName(QStringLiteral("perspectiveRemovePasteButton"));
  remove->setToolTip(with_key(QObject::tr("Remove the image without placing it"), QKeySequence(Qt::Key_Escape)));
  paste_row->addWidget(remove);
  paste_row->addStretch(1);
  strip->addWidget(paste_page);

  auto* empty_page = new QWidget(strip);
  strip->addWidget(empty_page);

  auto* content = new QHBoxLayout();
  root->addLayout(content, 1);

  auto* tools = new QWidget(&dialog);
  tools->setObjectName(QStringLiteral("perspectiveToolColumn"));
  auto* tools_layout = new QVBoxLayout(tools);
  tools_layout->setContentsMargins(0, 0, 0, 0);
  tools_layout->setSpacing(4);
  int tool_width = 110;
  for (const auto& entry : kTools) {
    tool_width = std::max(tool_width, tools->fontMetrics().horizontalAdvance(QObject::tr(entry.name)) + 24);
  }
  tools->setFixedWidth(tool_width);
  // Tools that need a plane first read as unavailable until there is one.
  append_themed_style(*tools, QStringLiteral("QToolButton:disabled { color: @text_disabled; }"));
  auto* tool_group = new QButtonGroup(&dialog);
  tool_group->setExclusive(true);
  std::vector<QToolButton*> tool_buttons;
  for (std::size_t index = 0; index < kTools.size(); ++index) {
    const auto& entry = kTools[index];
    auto* button = new QToolButton(tools);
    button->setObjectName(QLatin1String(entry.object_name));
    button->setText(QObject::tr(entry.name));
    button->setToolTip(with_key(QObject::tr(entry.tip), QKeySequence(entry.key)));
    button->setCheckable(true);
    button->setFixedWidth(tool_width);
    tool_group->addButton(button, static_cast<int>(index));
    tools_layout->addWidget(button);
    tool_buttons.push_back(button);
    if (index == 3) {
      tools_layout->addSpacing(8);
    }
  }
  tools_layout->addSpacing(8);
  auto* paste = new QToolButton(tools);
  paste->setObjectName(QStringLiteral("perspectivePasteButton"));
  paste->setText(QObject::tr("Paste..."));
  paste->setFixedWidth(tool_width);
  paste->setPopupMode(QToolButton::InstantPopup);
  tools_layout->addWidget(paste);
  auto* place_layer_tool = new QToolButton(tools);
  place_layer_tool->setObjectName(QStringLiteral("perspectivePlaceLayerTool"));
  place_layer_tool->setText(QObject::tr("Place Layer"));
  place_layer_tool->setToolTip(place_layer->toolTip());
  place_layer_tool->setFixedWidth(tool_width);
  tools_layout->addWidget(place_layer_tool);
  tools_layout->addStretch(1);
  content->addWidget(tools);

  auto* view = new PerspectivePlanesView(input, &dialog);
  dialog.view = view;
  content->addWidget(view, 1);

  auto* paste_menu = new QMenu(paste);
  paste_menu->setObjectName(QStringLiteral("perspectivePasteMenu"));
  for (std::size_t index = 0; index < view->sources().size(); ++index) {
    auto* action = paste_menu->addAction(view->sources()[index].name);
    action->setObjectName(QStringLiteral("perspectivePasteSource%1").arg(index));
    QObject::connect(action, &QAction::triggered, view, [view, index] { view->start_paste(index); });
  }
  paste->setMenu(paste_menu);
  paste_onto->setMenu(paste_menu);
  paste->setEnabled(!view->sources().empty());
  if (view->sources().empty()) {
    paste_onto->setEnabled(false);
  }
  paste->setToolTip(view->sources().empty()
                        ? QObject::tr("Copy an image, or make a selection, before opening Perspective Planes to paste it")
                        : with_key(QObject::tr("Paste an image onto a plane"), QKeySequence(QKeySequence::Paste)));

  // What the plane-gated buttons say when they can be used.
  const auto clone_tip = tool_buttons[static_cast<std::size_t>(WorkspaceTool::Clone)]->toolTip();
  const auto paint_tip = tool_buttons[static_cast<std::size_t>(WorkspaceTool::Paint)]->toolTip();
  const auto paste_tip = paste->toolTip();
  tool_buttons[static_cast<std::size_t>(WorkspaceTool::Clone)]->setProperty("patchy.available", true);
  tool_buttons[static_cast<std::size_t>(WorkspaceTool::Paint)]->setProperty("patchy.available", true);
  paste->setProperty("patchy.available", !view->sources().empty());
  const auto layer_name = input.layer_name.isEmpty() ? QObject::tr("this layer") : input.layer_name;

  // Bottom row: the hint for the current step, zoom, where the result goes, history, OK.
  auto* bottom = new QHBoxLayout();
  auto* hint = new QLabel(&dialog);
  hint->setObjectName(QStringLiteral("perspectivePlanesHint"));
  hint->setWordWrap(true);
  hint->setMinimumWidth(320);
  hint->setMargin(4);
  bottom->addWidget(hint, 1);
  auto* fit = new QPushButton(QObject::tr("Fit"), &dialog);
  fit->setObjectName(QStringLiteral("perspectiveFitButton"));
  fit->setToolTip(with_key(QObject::tr("Fit the image in the view"), QKeySequence(Qt::CTRL | Qt::Key_0)));
  bottom->addWidget(fit);
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("perspectiveZoomLabel"));
  zoom_label->setMinimumWidth(48);
  bottom->addWidget(zoom_label);
  bottom->addSpacing(12);
  auto* show_others = new QCheckBox(QObject::tr("Show other layers"), &dialog);
  show_others->setObjectName(QStringLiteral("perspectiveShowOtherLayersCheck"));
  show_others->setToolTip(QObject::tr("Show the document's other layers behind this one; edits still go only to this "
                                      "layer"));
  show_others->setChecked(view->show_backdrop());
  show_others->setVisible(view->has_backdrop());
  bottom->addWidget(show_others);
  bottom->addSpacing(12);
  bottom->addWidget(new QLabel(QObject::tr("Result:"), &dialog));
  auto* output = new QComboBox(&dialog);
  output->setObjectName(QStringLiteral("perspectiveOutputCombo"));
  output->addItem(QObject::tr("New layer above"));
  output->addItem(QObject::tr("Replace this layer"));
  output->setToolTip(QObject::tr("New layer above keeps this layer as it is and puts the result above it; Replace this "
                                 "layer puts the result into this layer itself"));
  if (!input.can_write_onto_layer) {
    output->setEnabled(false);
  } else if (input.write_onto_layer) {
    output->setCurrentIndex(1);
  }
  bottom->addWidget(output);
  auto* undo = new QPushButton(QObject::tr("Undo"), &dialog);
  undo->setObjectName(QStringLiteral("perspectiveUndoButton"));
  undo->setToolTip(with_key(undo->text(), QKeySequence(QKeySequence::Undo)));
  undo->setAutoDefault(false);
  auto* redo = new QPushButton(QObject::tr("Redo"), &dialog);
  redo->setObjectName(QStringLiteral("perspectiveRedoButton"));
  redo->setToolTip(with_key(redo->text(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)));
  redo->setAutoDefault(false);
  bottom->addWidget(undo);
  bottom->addWidget(redo);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("perspectiveButtonBox"));
  bottom->addWidget(buttons);
  root->addLayout(bottom);
  for (auto* button : {fit, delete_plane, place, remove, color_button, place_layer, paste_onto, next_clone, next_paint}) {
    button->setAutoDefault(false);
  }

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  bool syncing = false;
  const auto refresh = [&] {
    syncing = true;
    const auto tool = view->tool();
    tool_buttons[static_cast<std::size_t>(tool)]->setChecked(true);
    if (view->has_floating()) {
      strip->setCurrentWidget(paste_page);
      const auto placement = view->floating_placement();
      scale->setEnabled(placement.has_value());
      rotation->setEnabled(placement.has_value());
      if (placement.has_value()) {
        scale->setValue(static_cast<int>(std::lround(placement->scale * 100.0)));
        rotation->setValue(static_cast<int>(std::lround(placement->rotation_degrees)) % 360);
      }
    } else if (tool == WorkspaceTool::CreatePlane || tool == WorkspaceTool::EditPlane) {
      strip->setCurrentWidget(plane_page);
      const auto* plane = view->selected_plane();
      grid->setEnabled(plane != nullptr);
      delete_plane->setEnabled(plane != nullptr);
      const bool usable = plane != nullptr && view->geometry_for(plane->id).has_value();
      for (QWidget* next : {static_cast<QWidget*>(next_label), static_cast<QWidget*>(place_layer),
                            static_cast<QWidget*>(paste_onto), static_cast<QWidget*>(next_clone),
                            static_cast<QWidget*>(next_paint)}) {
        next->setVisible(usable);
      }
      place_layer->setVisible(usable && view->can_place_layer());
      paste_onto->setEnabled(!view->sources().empty());
      const bool joined = plane != nullptr && plane->link.has_value();
      angle->setEnabled(joined);
      angle_label->setEnabled(joined);
      if (plane != nullptr) {
        grid->setValue(plane->grid_size > 0.0 ? static_cast<int>(std::lround(plane->grid_size)) : 0);
        if (joined) {
          angle->setValue(static_cast<int>(std::lround(plane->link->angle_degrees)));
        }
      }
    } else if (tool == WorkspaceTool::Clone || tool == WorkspaceTool::Paint) {
      strip->setCurrentWidget(brush_page);
      color_button->setVisible(tool == WorkspaceTool::Paint);
      sample_label->setVisible(tool == WorkspaceTool::Clone);
      sample->setVisible(tool == WorkspaceTool::Clone);
      sample->setCurrentIndex(std::max(0, sample->findData(static_cast<int>(view->sample_layers()))));
    } else {
      strip->setCurrentWidget(empty_page);
    }
    hint->setText(view->hint());
    // Steps 1 and 2 read as a banner (info banner roles, bold); other hints as plain text.
    const bool step = view->hint_is_step();
    QPalette palette = hint->palette();
    palette.setColor(QPalette::WindowText, view->hint_is_problem() ? theme().status_error_text
                                           : step                   ? theme().info_banner_text
                                                                    : theme().text_primary);
    palette.setColor(QPalette::Window, theme().info_banner_bg);
    hint->setPalette(palette);
    hint->setAutoFillBackground(step);
    QFont hint_font = hint->font();
    hint_font.setBold(step);
    hint->setFont(hint_font);
    hint->setProperty("patchy.step", step);
    // Nothing can go anywhere before a target plane exists.
    const bool has_plane = view->has_usable_plane();
    const auto gate = [has_plane](QAbstractButton* button, const QString& tip) {
      button->setEnabled(has_plane && button->property("patchy.available").toBool());
      button->setToolTip(has_plane ? tip
                                   : QObject::tr("Draw a plane first: click the four corners of a flat surface "
                                                 "with Create Plane."));
    };
    gate(tool_buttons[static_cast<std::size_t>(WorkspaceTool::Clone)], clone_tip);
    gate(tool_buttons[static_cast<std::size_t>(WorkspaceTool::Paint)], paint_tip);
    gate(paste, paste_tip);
    const auto* target = view->selected_plane();
    const auto editing = QObject::tr("Editing: %1").arg(layer_name);
    target_label->setText(target != nullptr && has_plane
                              ? QObject::tr("Target: plane %1 of %2").arg(view->target_number()).arg(
                                    static_cast<int>(view->planes().size())) +
                                    QStringLiteral("    ") + editing
                              : QObject::tr("Target: no plane yet") + QStringLiteral("    ") + editing);
    zoom_label->setText(QStringLiteral("%1%").arg(std::lround(view->zoom() * 100.0)));
    undo->setEnabled(view->can_undo());
    redo->setEnabled(view->can_redo());
    place_layer_tool->setVisible(view->can_place_layer());
    place_layer_tool->setEnabled(has_plane && !view->has_floating());
    place_layer_tool->setToolTip(has_plane ? place_layer->toolTip()
                                           : QObject::tr("Draw a plane first: click the four corners of a flat "
                                                         "surface with Create Plane."));
    syncing = false;
  };
  view->on_state_changed = refresh;

  QObject::connect(tool_group, &QButtonGroup::idClicked, view, [view](int id) {
    if (id >= 0 && id < static_cast<int>(kTools.size())) {
      view->set_tool(kTools[static_cast<std::size_t>(id)].tool);
    }
  });
  QObject::connect(grid, &QSpinBox::valueChanged, view, [view, &syncing](int value) {
    if (!syncing) {
      view->set_selected_grid(static_cast<double>(value));
    }
  });
  QObject::connect(angle, &QSpinBox::valueChanged, view, [view, &syncing](int value) {
    if (!syncing) {
      view->set_selected_angle(static_cast<double>(value));
    }
  });
  QObject::connect(delete_plane, &QPushButton::clicked, view, [view] { view->delete_selected_plane(); });
  QObject::connect(place_layer, &QPushButton::clicked, view, [view] { view->start_place_layer(); });
  QObject::connect(sample, &QComboBox::currentIndexChanged, view, [view, sample, &syncing](int) {
    if (!syncing) {
      view->set_sample_layers(static_cast<SampleLayers>(sample->currentData().toInt()));
    }
  });
  QObject::connect(place_layer_tool, &QToolButton::clicked, view, [view] { view->start_place_layer(); });
  QObject::connect(next_clone, &QPushButton::clicked, view, [view] { view->set_tool(WorkspaceTool::Clone); });
  QObject::connect(next_paint, &QPushButton::clicked, view, [view] { view->set_tool(WorkspaceTool::Paint); });
  QObject::connect(show_others, &QCheckBox::toggled, view, [view](bool checked) { view->set_show_backdrop(checked); });
  // Placing the layer itself means the layer becomes the result.
  view->on_layer_placed = [output] {
    if (output->isEnabled()) {
      output->setCurrentIndex(1);
    }
  };
  const auto brush_changed = [view, size, hardness, opacity] {
    view->set_brush({static_cast<double>(size->value()), hardness->value(), opacity->value()});
  };
  for (auto* spin : {size, hardness, opacity}) {
    QObject::connect(spin, &QSpinBox::valueChanged, view, brush_changed);
  }
  brush_changed();
  QObject::connect(color_button, &QPushButton::clicked, view, [view, &dialog] {
    const auto picked = request_patchy_color(&dialog, view->paint_color(), QObject::tr("Paint Color"));
    if (picked.has_value()) {
      view->set_paint_color(*picked);
    }
  });
  QObject::connect(scale, &QSpinBox::valueChanged, view, [view, &syncing](int value) {
    if (!syncing) {
      view->set_floating_scale(value / 100.0);
    }
  });
  QObject::connect(rotation, &QSpinBox::valueChanged, view, [view, &syncing](int value) {
    if (!syncing) {
      view->set_floating_rotation(static_cast<double>(value));
    }
  });
  QObject::connect(place, &QPushButton::clicked, view, [view] { view->commit_floating(); });
  QObject::connect(remove, &QPushButton::clicked, view, [view] { view->cancel_pending(); });
  QObject::connect(fit, &QPushButton::clicked, view, [view] { view->fit_view(); });
  QObject::connect(undo, &QPushButton::clicked, view, [view] { view->undo(); });
  QObject::connect(redo, &QPushButton::clicked, view, [view] { view->redo(); });

  // Window shortcuts on the modal dialog: the main window's shortcuts are blocked while
  // it is open, so none of these keys reaches the document.
  const auto add_shortcut = [&dialog](const QKeySequence& keys, auto&& slot) {
    auto* shortcut = new QShortcut(keys, &dialog);
    QObject::connect(shortcut, &QShortcut::activated, &dialog, std::forward<decltype(slot)>(slot));
  };
  add_shortcut(QKeySequence(QKeySequence::Undo), [view] { view->undo(); });
  for (const auto& keys : {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)}) {
    add_shortcut(keys, [view] { view->redo(); });
  }
  add_shortcut(QKeySequence(QKeySequence::Paste), [view] {
    if (!view->sources().empty() && view->has_usable_plane()) {
      view->start_paste(0);
    }
  });
  add_shortcut(QKeySequence(Qt::Key_Delete), [view] { view->remove_last(); });
  add_shortcut(QKeySequence(Qt::Key_Backspace), [view] { view->remove_last(); });
  add_shortcut(QKeySequence(Qt::CTRL | Qt::Key_0), [view] { view->fit_view(); });
  add_shortcut(QKeySequence(Qt::CTRL | Qt::Key_1), [view] { view->set_zoom(1.0); });
  for (const auto& keys : {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)}) {
    add_shortcut(keys, [view] { view->zoom_by(1.25, view->rect().center()); });
  }
  add_shortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), [view] { view->zoom_by(0.8, view->rect().center()); });
  for (std::size_t index = 0; index < kTools.size(); ++index) {
    add_shortcut(QKeySequence(kTools[index].key), [button = tool_buttons[index]] { button->click(); });
  }
  const auto add_size_step = [&add_shortcut, size](const QKeySequence& keys, int direction, bool coarse) {
    add_shortcut(keys, [size, direction, coarse] {
      const int value = size->value();
      size->setValue(std::clamp(value + direction * proportional_brush_step(value, direction, coarse), size->minimum(),
                                size->maximum()));
    });
  };
  add_size_step(QKeySequence(Qt::Key_BracketLeft), -1, false);
  add_size_step(QKeySequence(Qt::Key_BracketRight), 1, false);
  add_size_step(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), -1, true);
  add_size_step(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), 1, true);

  // Planes the document already has open in Edit Plane; a first visit starts with Create.
  view->set_tool(view->planes().empty() ? WorkspaceTool::CreatePlane : WorkspaceTool::EditPlane);
  refresh();
  view->setFocus();

  append_themed_style(dialog, dialog_spinbox_button_style());
  remember_dialog_position(dialog);
  const bool accepted = exec_dialog(dialog) == QDialog::Accepted;
  result.accepted = accepted;
  result.planes = view->planes();
  result.write_onto_layer = output->currentIndex() == 1 && input.can_write_onto_layer;
  result.sample_layers = view->sample_layers();
  if (accepted) {
    result.edits = view->canvas().cropped_edits();
    if (view->canvas().original_changed()) {
      result.lifted_layer = view->canvas().original();
    }
  }
  view->on_state_changed = nullptr;
  view->on_layer_placed = nullptr;
  return result;
}

}  // namespace patchy::ui
