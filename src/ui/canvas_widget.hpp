#pragma once

#include "core/document.hpp"

#include <QBasicTimer>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QPolygon>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

class QPainter;

namespace photoslop::ui {

enum class CanvasTool {
  Move,
  Marquee,
  Lasso,
  MagicWand,
  Brush,
  Clone,
  Eraser,
  Gradient,
  Line,
  Rectangle,
  Ellipse,
  Fill,
  Eyedropper,
  Text,
  Pan,
  Zoom
};

struct CanvasInfoState {
  bool inside_document{false};
  QPoint document_point{};
  QColor color{Qt::transparent};
  std::optional<QRect> active_rect{};
  QString active_rect_label{};
};

class CanvasWidget final : public QWidget {
public:
  enum class SelectionMode {
    Replace,
    Add,
    Subtract,
    Intersect
  };

  enum class MarqueeStyle {
    Normal,
    FixedRatio,
    FixedSize
  };

  explicit CanvasWidget(QWidget* parent = nullptr);

  void set_document(Document* document);
  [[nodiscard]] double zoom() const noexcept;
  void set_zoom(double zoom);
  void fit_to_view();
  void zoom_to_document_rect(QRect document_rect);
  void set_tool(CanvasTool tool) noexcept;
  [[nodiscard]] CanvasTool tool() const noexcept;
  void set_auto_select_layer(bool enabled) noexcept;
  [[nodiscard]] bool auto_select_layer() const noexcept;
  void set_primary_color(QColor color);
  [[nodiscard]] QColor primary_color() const noexcept;
  void set_secondary_color(QColor color);
  [[nodiscard]] QColor secondary_color() const noexcept;
  void set_brush_size(int size);
  [[nodiscard]] int brush_size() const noexcept;
  void set_brush_opacity(int opacity);
  [[nodiscard]] int brush_opacity() const noexcept;
  void set_brush_softness(int softness);
  [[nodiscard]] int brush_softness() const noexcept;
  void set_clone_aligned(bool aligned) noexcept;
  [[nodiscard]] bool clone_aligned() const noexcept;
  void set_wand_tolerance(int tolerance);
  [[nodiscard]] int wand_tolerance() const noexcept;
  void set_fill_shapes(bool fill_shapes) noexcept;
  void set_selection_mode(SelectionMode mode) noexcept;
  [[nodiscard]] SelectionMode selection_mode() const noexcept;
  void set_marquee_style(MarqueeStyle style) noexcept;
  [[nodiscard]] MarqueeStyle marquee_style() const noexcept;
  void set_marquee_fixed_size(int width, int height) noexcept;
  [[nodiscard]] QSize marquee_fixed_size() const noexcept;
  bool begin_free_transform();
  void cancel_free_transform();
  [[nodiscard]] bool free_transform_active() const noexcept;
  [[nodiscard]] std::optional<QRect> active_layer_document_rect() const noexcept;
  void document_changed();
  void document_changed(QRect document_rect);
  void select_all();
  void invert_selection();
  void clear_selection();
  void reselect();
  void set_selection_edges_visible(bool visible) noexcept;
  [[nodiscard]] bool selection_edges_visible() const noexcept;
  void toggle_selection_edges_visible();
  void expand_selection(int pixels);
  void contract_selection(int pixels);
  void border_selection(int pixels);
  void select_layer_opaque_pixels(LayerId layer_id);
  void select_active_layer_opaque_pixels();
  void grow_selection();
  void select_similar_to_selection();
  [[nodiscard]] std::optional<QRect> selected_document_rect() const noexcept;
  [[nodiscard]] const QRegion& selected_document_region() const noexcept;
  [[nodiscard]] bool has_selection() const noexcept;
  [[nodiscard]] bool selection_contains(QPoint point) const noexcept;
  [[nodiscard]] QPoint widget_position_for_document_point(QPoint document_position) const;
  void set_before_edit_callback(std::function<void(QString)> callback);
  void set_color_picked_callback(std::function<void(QColor)> callback);
  void set_text_requested_callback(std::function<void(QPoint)> callback);
  void set_active_layer_changed_callback(std::function<void(LayerId)> callback);
  void set_status_callback(std::function<void(QString)> callback);
  void set_info_callback(std::function<void(CanvasInfoState)> callback);
  void set_selected_layer_ids(std::vector<LayerId> layer_ids);

protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void timerEvent(QTimerEvent* event) override;

private:
  enum class TransformHandle {
    None,
    Move,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
    Rotate
  };

  struct MovingLayer {
    LayerId id{};
    Rect original_bounds{};
  };

  [[nodiscard]] QImage render_document_image() const;
  void ensure_render_cache();
  void refresh_render_cache_rect(QRect document_rect);
  [[nodiscard]] QColor compose_document_pixel(std::int32_t x, std::int32_t y) const;
  void draw_checkerboard(QPainter& painter, const QRectF& rect) const;
  void draw_shape_preview(QPainter& painter) const;
  void draw_zoom_preview(QPainter& painter) const;
  void draw_selection_overlay(QPainter& painter) const;
  void draw_free_transform(QPainter& painter) const;
  void update_tool_cursor();
  [[nodiscard]] QPoint document_position(const QPoint& widget_position) const;
  [[nodiscard]] QPointF document_position_f(QPointF widget_position) const;
  [[nodiscard]] QPoint widget_position(const QPoint& document_position) const;
  [[nodiscard]] QPointF widget_position_f(QPointF document_position) const;
  [[nodiscard]] bool document_contains(QPoint point) const noexcept;
  [[nodiscard]] bool selection_allows(QPoint point) const noexcept;
  [[nodiscard]] Layer* active_pixel_layer() const noexcept;
  [[nodiscard]] bool active_layer_locks_transparent_pixels() const noexcept;
  [[nodiscard]] Layer* topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel) const noexcept;
  [[nodiscard]] Layer* topmost_text_layer_at(QPoint document_point) const noexcept;
  void activate_layer(Layer& layer);
  [[nodiscard]] QPoint layer_position(const Layer& layer, QPoint document_point) const noexcept;
  [[nodiscard]] QRect widget_rect_for_document_rect(QRect document_rect) const;
  [[nodiscard]] QRectF widget_rect_for_document_rect(QRectF document_rect) const;
  bool begin_edit(QString label);
  [[nodiscard]] QRect draw_brush_segment(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_brush_at(QPoint point, bool erase);
  void set_clone_source(QPoint point);
  [[nodiscard]] QRect clone_brush_segment(QPoint from, QPoint to);
  [[nodiscard]] QRect clone_brush_at(QPoint point);
  void draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase);
  [[nodiscard]] QRect draw_line(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_gradient(QPoint from, QPoint to);
  [[nodiscard]] QRect draw_rectangle(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_ellipse(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect flood_fill(QPoint start);
  void pick_color(QPoint point);
  void magic_wand_select(QPoint start);
  [[nodiscard]] QRegion marquee_selection_region(QPoint anchor, QPoint current) const;
  [[nodiscard]] SelectionMode selection_operation(Qt::KeyboardModifiers modifiers) const noexcept;
  [[nodiscard]] QRegion combine_selection(const QRegion& candidate) const;
  [[nodiscard]] std::vector<LayerId> movable_layer_ids() const;
  [[nodiscard]] std::vector<std::pair<LayerId, Rect>> moving_layer_bounds(QPoint delta) const;
  [[nodiscard]] QRect moving_layers_dirty_rect(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRect move_active_layer_by(QPoint delta);
  [[nodiscard]] TransformHandle transform_handle_at(QPoint widget_point) const;
  [[nodiscard]] QPointF transform_handle_position(TransformHandle handle) const;
  void update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void commit_free_transform();
  void emit_info_for_widget_position(QPoint widget_position) const;

  Document* document_{nullptr};
  double zoom_{1.0};
  QPointF pan_{40.0, 40.0};
  QImage render_cache_{};
  bool render_cache_dirty_{true};
  QPoint last_mouse_position_{};
  QPoint last_document_position_{};
  QPoint clone_source_point_{};
  QPoint clone_source_offset_{};
  QPoint shape_start_{};
  QPoint shape_current_{};
  QPoint move_start_{};
  QPoint selection_start_{};
  CanvasTool tool_{CanvasTool::Brush};
  QColor primary_color_{Qt::black};
  QColor secondary_color_{Qt::white};
  int brush_size_{12};
  int brush_opacity_{100};
  int brush_softness_{0};
  int wand_tolerance_{24};
  bool fill_shapes_{false};
  bool auto_select_layer_{true};
  SelectionMode selection_mode_{SelectionMode::Replace};
  MarqueeStyle marquee_style_{MarqueeStyle::Normal};
  QSize marquee_fixed_size_{1024, 768};
  bool panning_{false};
  bool spacebar_panning_{false};
  bool spacebar_repositioning_drag_rect_{false};
  QPoint spacebar_reposition_last_document_position_{};
  bool painting_{false};
  bool drawing_shape_{false};
  bool moving_layer_{false};
  bool transforming_layer_{false};
  bool dragging_transform_{false};
  bool selecting_{false};
  bool lassoing_{false};
  bool zooming_{false};
  QPoint zoom_start_{};
  QPoint zoom_current_{};
  QPolygon lasso_points_;
  QRegion selection_;
  QRegion last_cleared_selection_;
  QRegion selection_before_edit_;
  bool selection_edges_visible_{true};
  SelectionMode selection_operation_{SelectionMode::Replace};
  QBasicTimer selection_timer_;
  int selection_dash_offset_{0};
  std::unordered_set<std::uint64_t> brush_stroke_pixels_;
  QImage clone_source_cache_{};
  bool clone_source_set_{false};
  bool clone_aligned_{true};
  bool clone_aligned_offset_set_{false};
  std::vector<MovingLayer> moving_layers_;
  std::vector<LayerId> selected_layer_ids_;
  QPoint move_preview_delta_{};
  QImage move_preview_cache_{};
  std::optional<LayerId> transform_layer_id_;
  QRectF transform_original_rect_{};
  QRectF transform_current_rect_{};
  QRectF transform_drag_start_rect_{};
  QPointF transform_drag_start_point_{};
  TransformHandle transform_drag_handle_{TransformHandle::None};
  double transform_angle_{0.0};
  double transform_start_angle_{0.0};
  QImage transform_base_cache_{};
  QImage transform_source_image_{};
  std::function<void(QString)> before_edit_callback_;
  std::function<void(QColor)> color_picked_callback_;
  std::function<void(QPoint)> text_requested_callback_;
  std::function<void(LayerId)> active_layer_changed_callback_;
  std::function<void(QString)> status_callback_;
  std::function<void(CanvasInfoState)> info_callback_;
};

}  // namespace photoslop::ui
