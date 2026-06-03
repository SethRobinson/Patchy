#pragma once

#include "core/document.hpp"
#include "core/pixel_tools.hpp"
#include "ui/image_document_io.hpp"

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
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QPainter;
class QEvent;
class QResizeEvent;

namespace patchy::ui {

enum class CanvasTool {
  Move,
  Marquee,
  EllipticalMarquee,
  Lasso,
  MagicWand,
  Brush,
  Clone,
  Smudge,
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
  Q_OBJECT

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

  enum class LayerEditTarget {
    Content,
    Mask
  };

  enum class DocumentChangeReason {
    Immediate,
    BrushStrokePreview,
    BrushStrokeFinished
  };

  enum class TransformInterpolation {
    NearestNeighbor,
    Bilinear,
    Bicubic
  };

  struct TransformControlsState {
    bool active{false};
    CanvasAnchor reference_point{CanvasAnchor::Center};
    QPointF reference_position{};
    double scale_x_percent{100.0};
    double scale_y_percent{100.0};
    double rotation_degrees{0.0};
    TransformInterpolation interpolation{TransformInterpolation::Bicubic};
  };

  struct RenderCacheDiagnostics {
    int full_refreshes{0};
    int partial_patches{0};
    int move_precommit_patches{0};
    int forced_refreshes{0};
    int dirty_region_batches{0};
    int dirty_region_rects{0};
    std::uint64_t dirty_region_pixels{0};
    int move_preview_patch_reuses{0};
    int processing_overlays_shown{0};
    int processing_overlay_frames{0};
  };

  explicit CanvasWidget(QWidget* parent = nullptr);

  void set_document(Document* document);
  [[nodiscard]] double zoom() const noexcept;
  void set_zoom(double zoom);
  void zoom_at_widget_point(QPointF widget_position, double factor);
  void fit_to_view();
  void zoom_to_document_rect(QRect document_rect);
  void set_tool(CanvasTool tool);
  [[nodiscard]] CanvasTool tool() const noexcept;
  void set_layer_edit_target(LayerEditTarget target) noexcept;
  [[nodiscard]] LayerEditTarget layer_edit_target() const noexcept;
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
  void set_brush_build_up(bool build_up) noexcept;
  [[nodiscard]] bool brush_build_up() const noexcept;
  void set_gradient_method(GradientMethod method) noexcept;
  [[nodiscard]] GradientMethod gradient_method() const noexcept;
  void set_gradient_reverse(bool reverse) noexcept;
  [[nodiscard]] bool gradient_reverse() const noexcept;
  void set_gradient_opacity(int opacity) noexcept;
  [[nodiscard]] int gradient_opacity() const noexcept;
  void set_gradient_stops(std::optional<std::vector<GradientStop>> stops);
  [[nodiscard]] const std::optional<std::vector<GradientStop>>& gradient_stops() const noexcept;
  [[nodiscard]] std::vector<GradientStop> effective_gradient_stops() const;
  void set_clone_aligned(bool aligned) noexcept;
  [[nodiscard]] bool clone_aligned() const noexcept;
  void set_wand_tolerance(int tolerance);
  [[nodiscard]] int wand_tolerance() const noexcept;
  void set_wand_contiguous(bool enabled) noexcept;
  [[nodiscard]] bool wand_contiguous() const noexcept;
  void set_wand_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool wand_sample_all_layers() const noexcept;
  void set_show_transform_controls(bool enabled) noexcept;
  [[nodiscard]] bool show_transform_controls() const noexcept;
  void set_fill_shapes(bool fill_shapes) noexcept;
  void set_selection_mode(SelectionMode mode) noexcept;
  [[nodiscard]] SelectionMode selection_mode() const noexcept;
  void set_marquee_style(MarqueeStyle style) noexcept;
  [[nodiscard]] MarqueeStyle marquee_style() const noexcept;
  void set_marquee_fixed_size(int width, int height) noexcept;
  [[nodiscard]] QSize marquee_fixed_size() const noexcept;
  void set_selection_feather_radius(int pixels) noexcept;
  [[nodiscard]] int selection_feather_radius() const noexcept;
  void set_selection_antialias(bool enabled) noexcept;
  [[nodiscard]] bool selection_antialias() const noexcept;
  bool begin_free_transform();
  void finish_free_transform();
  void cancel_free_transform();
  [[nodiscard]] bool free_transform_active() const noexcept;
  void set_transform_interpolation(TransformInterpolation interpolation) noexcept;
  [[nodiscard]] TransformInterpolation transform_interpolation() const noexcept;
  void set_transform_reference_point(CanvasAnchor anchor) noexcept;
  [[nodiscard]] CanvasAnchor transform_reference_point() const noexcept;
  [[nodiscard]] std::optional<TransformControlsState> transform_controls_state() const;
  bool set_transform_controls_state(QPointF reference_position, double scale_x_percent,
                                    double scale_y_percent, double rotation_degrees);
  [[nodiscard]] std::optional<QRect> active_layer_document_rect() const noexcept;
  [[nodiscard]] RenderCacheDiagnostics render_cache_diagnostics() const noexcept;
  [[nodiscard]] bool processing_overlay_visible() const noexcept;
  [[nodiscard]] bool processing_operation_active() const noexcept;
  void begin_processing_operation(QString message = {});
  void tick_processing_operation();
  void end_processing_operation();
  bool wait_for_processing_operation(std::function<bool()> operation_ready, bool allow_overlay = true);
  void force_refresh();
  void document_changed();
  void document_changed(QRect document_rect);
  void document_changed(QRegion document_region);
  void document_changed_effect_bounds(QRect document_rect);
  void document_changed_effect_bounds(QRegion document_region);
  void select_all();
  void invert_selection();
  void clear_selection();
  void reselect();
  void set_selection_edges_visible(bool visible) noexcept;
  [[nodiscard]] bool selection_edges_visible() const noexcept;
  void toggle_selection_edges_visible();
  void set_rulers_visible(bool visible) noexcept;
  [[nodiscard]] bool rulers_visible() const noexcept;
  void set_grid_visible(bool visible) noexcept;
  [[nodiscard]] bool grid_visible() const noexcept;
  void set_guides_visible(bool visible) noexcept;
  [[nodiscard]] bool guides_visible() const noexcept;
  void set_guides_locked(bool locked) noexcept;
  [[nodiscard]] bool guides_locked() const noexcept;
  void set_snap_enabled(bool enabled) noexcept;
  [[nodiscard]] bool snap_enabled() const noexcept;
  void set_snap_to_guides(bool enabled) noexcept;
  [[nodiscard]] bool snap_to_guides() const noexcept;
  void set_snap_to_grid(bool enabled) noexcept;
  [[nodiscard]] bool snap_to_grid() const noexcept;
  void set_snap_to_document(bool enabled) noexcept;
  [[nodiscard]] bool snap_to_document() const noexcept;
  void set_snap_to_layers(bool enabled) noexcept;
  [[nodiscard]] bool snap_to_layers() const noexcept;
  void set_snap_to_selection(bool enabled) noexcept;
  [[nodiscard]] bool snap_to_selection() const noexcept;
  void set_grid_subdivisions(int subdivisions) noexcept;
  [[nodiscard]] int grid_subdivisions() const noexcept;
  void set_grid_style(int style) noexcept;
  [[nodiscard]] int grid_style() const noexcept;
  void set_grid_color(QColor color) noexcept;
  [[nodiscard]] QColor grid_color() const noexcept;
  void set_guide_color(QColor color) noexcept;
  [[nodiscard]] QColor guide_color() const noexcept;
  void add_guide(GuideOrientation orientation, std::int32_t position_32);
  void clear_guides();
  void clear_selected_guides();
  [[nodiscard]] bool has_selected_guides() const noexcept;
  void expand_selection(int pixels);
  void contract_selection(int pixels);
  void border_selection(int pixels);
  void select_layer_opaque_pixels(LayerId layer_id);
  void select_layer_mask_pixels(LayerId layer_id);
  void select_active_layer_opaque_pixels();
  [[nodiscard]] QRect fill_active_layer_mask(QColor color);
  [[nodiscard]] QRect clear_active_layer_mask();
  void grow_selection();
  void select_similar_to_selection();
  [[nodiscard]] std::optional<QRect> selected_document_rect() const noexcept;
  [[nodiscard]] const QRegion& selected_document_region() const noexcept;
  [[nodiscard]] std::uint8_t selection_alpha_at(QPoint point) const noexcept;
  [[nodiscard]] bool selection_has_partial_alpha() const noexcept;
  [[nodiscard]] bool has_selection() const noexcept;
  [[nodiscard]] bool selection_contains(QPoint point) const noexcept;
  [[nodiscard]] QPoint widget_position_for_document_point(QPoint document_position) const;
  void set_before_edit_callback(std::function<void(QString)> callback);
  void set_color_picked_callback(std::function<void(QColor)> callback);
  void set_text_requested_callback(std::function<void(QPoint, QRect)> callback);
  void set_active_layer_changed_callback(std::function<void(LayerId)> callback);
  void set_status_callback(std::function<void(QString)> callback);
  void set_info_callback(std::function<void(CanvasInfoState)> callback);
  void set_document_changed_callback(std::function<void()> callback);
  void set_document_changed_callback(std::function<void(DocumentChangeReason)> callback);
  void set_view_changed_callback(std::function<void()> callback);
  void set_transform_controls_changed_callback(std::function<void()> callback);
  void set_selected_layer_ids(std::vector<LayerId> layer_ids);

protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
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

  enum class StrokeConstraintAxis {
    None,
    Horizontal,
    Vertical
  };

  struct MovingLayer {
    LayerId id{};
    Rect original_bounds{};
    std::optional<Rect> original_opaque_bounds{};
    bool expensive_style{false};
  };

  [[nodiscard]] QImage render_document_image() const;
  void ensure_render_cache();
  [[nodiscard]] QImage render_document_image_with_processing();
  [[nodiscard]] std::vector<RenderedDocumentPatch> render_document_patches_with_processing(
      const QRegion& document_region, const std::vector<std::pair<LayerId, Rect>>& layer_bounds,
      bool force_processing_wait);
  [[nodiscard]] bool dirty_region_should_use_processing_wait(const QRegion& document_region) const noexcept;
  void refresh_render_cache_rect(QRect document_rect);
  void refresh_render_cache_region(const QRegion& document_region);
  bool patch_render_cache_rect(QRect document_rect, const QImage& partial);
  bool patch_render_cache_patches(const std::vector<RenderedDocumentPatch>& patches);
  void invalidate_display_mip_cache() noexcept;
  [[nodiscard]] const QImage& display_image_for_zoom();
  [[nodiscard]] QColor compose_document_pixel(std::int32_t x, std::int32_t y) const;
  void draw_checkerboard(QPainter& painter, const QRectF& rect, QRect exposed_rect) const;
  void draw_deep_zoom_image(QPainter& painter, const QImage& image, QRect exposed_rect) const;
  void draw_shape_preview(QPainter& painter) const;
  void draw_text_rect_preview(QPainter& painter) const;
  void draw_zoom_preview(QPainter& painter) const;
  void draw_selection_overlay(QPainter& painter) const;
  void draw_free_transform(QPainter& painter) const;
  void draw_transform_controls(QPainter& painter, QRectF document_rect, double angle_degrees) const;
  void draw_move_transform_controls(QPainter& painter) const;
  void draw_grid_overlay(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const;
  void draw_guides_overlay(QPainter& painter) const;
  void draw_rulers(QPainter& painter) const;
  void draw_processing_overlay(QPainter& painter) const;
  void show_processing_overlay(QString message = {});
  void hide_processing_overlay();
  void update_tool_cursor();
  [[nodiscard]] QPoint document_position(const QPoint& widget_position) const;
  [[nodiscard]] QPointF document_position_f(QPointF widget_position) const;
  [[nodiscard]] QPoint widget_position(const QPoint& document_position) const;
  [[nodiscard]] QPointF widget_position_f(QPointF document_position) const;
  [[nodiscard]] QPoint snapped_document_point(QPoint point) const;
  [[nodiscard]] QPointF snapped_document_point_f(QPointF point) const;
  void append_snap_target_candidates(std::vector<double>& x_candidates,
                                     std::vector<double>& y_candidates) const;
  [[nodiscard]] QPoint snapped_rect_delta(QRect source_rect, QPoint raw_delta) const;
  [[nodiscard]] QPoint snapped_marquee_current_point(QPoint anchor, QPoint current) const;
  [[nodiscard]] QPoint snapped_move_delta(QPoint raw_delta) const;
  [[nodiscard]] int guide_at_widget_position(QPoint widget_position) const;
  [[nodiscard]] GuideOrientation guide_orientation_from_ruler(QPoint widget_position) const noexcept;
  [[nodiscard]] bool widget_position_in_ruler(QPoint widget_position) const noexcept;
  void clear_guide_selection() noexcept;
  void begin_guide_drag(int guide_index, QPoint widget_position);
  void begin_new_guide_drag(QPoint widget_position);
  void update_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  void finish_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  void cancel_guide_drag();
  [[nodiscard]] bool document_contains(QPoint point) const noexcept;
  [[nodiscard]] bool selection_allows(QPoint point) const noexcept;
  [[nodiscard]] Layer* active_pixel_layer() const noexcept;
  [[nodiscard]] LayerMask* active_layer_mask() const noexcept;
  [[nodiscard]] bool editing_layer_mask() const noexcept;
  [[nodiscard]] bool active_layer_locks_transparent_pixels() const noexcept;
  [[nodiscard]] bool active_layer_is_locked() const noexcept;
  [[nodiscard]] bool layer_is_effectively_locked(const Layer& layer) const noexcept;
  void show_locked_layer_message() const;
  [[nodiscard]] Layer* topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel,
                                              bool skip_locked) const noexcept;
  [[nodiscard]] Layer* topmost_move_layer_at(QPoint document_point, bool skip_locked) const noexcept;
  [[nodiscard]] Layer* topmost_text_layer_at(QPoint document_point) const noexcept;
  void activate_layer(Layer& layer);
  [[nodiscard]] QPoint layer_position(const Layer& layer, QPoint document_point) const noexcept;
  [[nodiscard]] QRect widget_rect_for_document_rect(QRect document_rect) const;
  [[nodiscard]] QRectF widget_rect_for_document_rect(QRectF document_rect) const;
  bool begin_edit(QString label);
  void clear_brush_stroke_tracking() noexcept;
  void begin_axis_constrained_stroke(QPointF document_point) noexcept;
  void reset_axis_constrained_stroke() noexcept;
  [[nodiscard]] QPointF axis_constrained_stroke_point(QPointF document_point,
                                                      Qt::KeyboardModifiers modifiers) noexcept;
  [[nodiscard]] QPoint axis_constrained_stroke_point(QPoint document_point,
                                                     Qt::KeyboardModifiers modifiers) noexcept;
  [[nodiscard]] QPoint axis_constrained_move_delta(QPoint raw_delta,
                                                   Qt::KeyboardModifiers modifiers) noexcept;
  void begin_brush_smoothing(QPointF document_point) noexcept;
  void reset_brush_smoothing() noexcept;
  [[nodiscard]] QRect advance_smoothed_brush_stroke(QPointF document_point, bool erase);
  [[nodiscard]] QRect finish_smoothed_brush_stroke(QPointF document_point, bool erase);
  [[nodiscard]] QRect draw_smoothed_brush_curve(QPointF start, QPointF control, QPointF end, bool erase);
  [[nodiscard]] float capped_stroke_coverage(std::int32_t x, std::int32_t y, float coverage,
                                             float source_alpha);
  void install_brush_stroke_coverage_cap(EditOptions& options);
  [[nodiscard]] QRect draw_brush_segment(QPointF from, QPointF to, bool erase);
  [[nodiscard]] QRect draw_brush_segment(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_brush_at(QPoint point, bool erase);
  [[nodiscard]] QRect draw_mask_brush_segment(QPointF from, QPointF to, bool erase);
  [[nodiscard]] QRect draw_mask_brush_segment(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_brush_at(QPoint point, bool erase);
  [[nodiscard]] QRect smudge_brush_segment(QPoint from, QPoint to);
  void set_clone_source(QPoint point);
  [[nodiscard]] QRect clone_brush_segment(QPoint from, QPoint to);
  [[nodiscard]] QRect clone_brush_at(QPoint point);
  void draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase);
  [[nodiscard]] QRect draw_line(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_gradient(QPoint from, QPoint to);
  [[nodiscard]] GradientOptions current_gradient_options() const;
  [[nodiscard]] QRect draw_rectangle(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_ellipse(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect flood_fill(QPoint start);
  [[nodiscard]] QRect draw_mask_line(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_gradient(QPoint from, QPoint to);
  [[nodiscard]] QRect draw_mask_rectangle(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_ellipse(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect flood_fill_mask(QPoint start);
  void pick_color(QPoint point);
  void magic_wand_select(QPoint start);
  [[nodiscard]] QRegion marquee_selection_region(QPoint anchor, QPoint current) const;
  [[nodiscard]] QRect marquee_selection_rect(QPoint anchor, QPoint current) const;
  [[nodiscard]] QImage marquee_selection_mask(QPoint anchor, QPoint current, QRect& bounds) const;
  [[nodiscard]] QImage lasso_selection_mask(const QPolygon& polygon, QRect& bounds) const;
  void set_selection_from_region(QRegion selection);
  void set_selection_from_mask(QRegion selection, QRect mask_bounds, QImage mask_alpha);
  void restore_selection_before_edit();
  [[nodiscard]] SelectionMode selection_operation(Qt::KeyboardModifiers modifiers) const noexcept;
  [[nodiscard]] QRegion combine_selection(const QRegion& candidate) const;
  void combine_selection_from_region(const QRegion& candidate);
  void combine_selection_from_mask(QRegion candidate, QRect candidate_bounds, QImage candidate_alpha);
  [[nodiscard]] std::vector<LayerId> movable_layer_ids() const;
  [[nodiscard]] std::optional<QRect> move_hover_outline_rect_at(QPoint widget_position,
                                                                Qt::KeyboardModifiers modifiers) const;
  void update_move_hover_outline(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  void clear_move_hover_outline();
  [[nodiscard]] QRect moving_layer_outline_rect(const MovingLayer& moving_layer, QPoint delta) const;
  [[nodiscard]] std::vector<std::pair<LayerId, Rect>> moving_layer_bounds(QPoint delta) const;
  [[nodiscard]] QRect moving_layers_dirty_rect(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRegion moving_layers_dirty_region(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRect moving_layers_outline_dirty_rect(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] bool moving_layers_should_use_outline_preview(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRegion move_active_layer_by(QPoint delta);
  void document_changed_impl(QRegion document_region, bool includes_effect_bounds,
                             DocumentChangeReason reason = DocumentChangeReason::Immediate);
  void notify_document_changed(DocumentChangeReason reason = DocumentChangeReason::Immediate);
  void set_transform_cursor_for_handle(TransformHandle handle);
  void update_move_transform_controls_dirty(std::optional<QRectF> old_rect);
  [[nodiscard]] std::optional<QRectF> transform_controls_rect_for_layer(const Layer& layer) const;
  [[nodiscard]] std::optional<QRectF> move_transform_controls_rect() const;
  void set_move_transform_controls_layer(std::optional<LayerId> layer_id);
  void notify_transform_controls_changed();
  [[nodiscard]] QPointF transform_reference_position(QRectF document_rect, double angle_degrees) const;
  bool prepare_free_transform_source();
  void refresh_transform_composited_preview_cache();
  [[nodiscard]] TransformHandle transform_handle_at(QPoint widget_point) const;
  [[nodiscard]] TransformHandle transform_handle_at(QPoint widget_point, QRectF document_rect,
                                                    double angle_degrees) const;
  [[nodiscard]] QPointF transform_handle_position(TransformHandle handle) const;
  [[nodiscard]] QPointF transform_handle_position(TransformHandle handle, QRectF document_rect,
                                                  double angle_degrees) const;
  void update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void commit_free_transform();
  bool constrain_pan() noexcept;
  void notify_view_changed();
  void emit_info_for_widget_position(QPoint widget_position) const;

  Document* document_{nullptr};
  double zoom_{1.0};
  QPointF pan_{40.0, 40.0};
  QImage render_cache_{};
  bool render_cache_dirty_{true};
  RenderCacheDiagnostics render_cache_diagnostics_{};
  std::vector<QImage> display_mip_cache_{};
  QSize display_mip_source_size_{};
  QPoint last_mouse_position_{};
  QPoint last_document_position_{};
  QPointF last_document_position_f_{};
  QPointF stroke_constraint_start_{};
  StrokeConstraintAxis stroke_constraint_axis_{StrokeConstraintAxis::None};
  QPointF brush_smoothing_last_input_position_{};
  QPointF brush_smoothing_last_rendered_position_{};
  bool brush_smoothing_active_{false};
  QPoint clone_source_point_{};
  QPoint clone_source_offset_{};
  QPoint shape_start_{};
  QPoint shape_current_{};
  QPoint move_start_{};
  QPoint selection_start_{};
  QPoint selection_current_{};
  CanvasTool tool_{CanvasTool::Brush};
  LayerEditTarget layer_edit_target_{LayerEditTarget::Content};
  QColor primary_color_{Qt::black};
  QColor secondary_color_{Qt::white};
  int brush_size_{12};
  int brush_opacity_{100};
  int brush_softness_{75};
  bool brush_build_up_{false};
  GradientMethod gradient_method_{GradientMethod::Linear};
  bool gradient_reverse_{false};
  int gradient_opacity_{100};
  std::optional<std::vector<GradientStop>> gradient_stops_;
  int wand_tolerance_{24};
  bool wand_contiguous_{true};
  bool wand_sample_all_layers_{false};
  bool show_transform_controls_{true};
  bool fill_shapes_{false};
  bool auto_select_layer_{true};
  SelectionMode selection_mode_{SelectionMode::Replace};
  MarqueeStyle marquee_style_{MarqueeStyle::Normal};
  QSize marquee_fixed_size_{1024, 768};
  int selection_feather_radius_{0};
  bool selection_antialias_{true};
  bool panning_{false};
  bool spacebar_panning_{false};
  bool spacebar_repositioning_drag_rect_{false};
  QPoint spacebar_reposition_last_document_position_{};
  QPoint spacebar_reposition_origin_document_position_{};
  QPoint spacebar_reposition_start_selection_start_{};
  QPoint spacebar_reposition_start_selection_current_{};
  bool painting_{false};
  bool drawing_shape_{false};
  bool dragging_text_rect_{false};
  bool move_drag_pending_{false};
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
  QRegion selection_display_region_;
  QRect selection_mask_bounds_;
  QImage selection_mask_alpha_;
  QRegion last_cleared_selection_;
  QRegion last_cleared_selection_display_region_;
  QRect last_cleared_selection_mask_bounds_;
  QImage last_cleared_selection_mask_alpha_;
  QRegion selection_before_edit_;
  QRegion selection_display_region_before_edit_;
  QRect selection_mask_before_edit_bounds_;
  QImage selection_mask_before_edit_alpha_;
  bool selection_edges_visible_{true};
  bool rulers_visible_{false};
  bool grid_visible_{false};
  bool guides_visible_{true};
  bool guides_locked_{false};
  bool snap_enabled_{true};
  bool snap_to_guides_{true};
  bool snap_to_grid_{true};
  bool snap_to_document_{true};
  bool snap_to_layers_{true};
  bool snap_to_selection_{true};
  int grid_subdivisions_{4};
  int grid_style_{0};
  QColor grid_color_{78, 154, 255, 105};
  QColor guide_color_{255, 70, 180, 230};
  int selected_guide_index_{-1};
  bool dragging_guide_{false};
  bool creating_guide_{false};
  bool guide_drag_remove_{false};
  GuideOrientation guide_drag_orientation_{GuideOrientation::Vertical};
  std::int32_t guide_drag_position_32_{0};
  GuideOrientation guide_drag_original_orientation_{GuideOrientation::Vertical};
  std::int32_t guide_drag_original_position_32_{0};
  SelectionMode selection_operation_{SelectionMode::Replace};
  QBasicTimer selection_timer_;
  int selection_dash_offset_{0};
  QBasicTimer processing_animation_timer_;
  bool processing_overlay_visible_{false};
  bool processing_render_wait_active_{false};
  int processing_operation_depth_{0};
  std::chrono::steady_clock::time_point processing_operation_started_{};
  bool processing_operation_owns_overlay_{false};
  QString processing_overlay_message_{};
  int processing_animation_frame_{0};
  std::unordered_set<std::uint64_t> brush_stroke_pixels_;
  std::unordered_map<std::uint64_t, float> brush_stroke_alpha_caps_;
  patchy::SmudgeState smudge_state_;
  QImage clone_source_cache_{};
  bool clone_source_set_{false};
  bool clone_aligned_{true};
  bool clone_aligned_offset_set_{false};
  std::vector<MovingLayer> moving_layers_;
  QPoint text_rect_start_{};
  QPoint text_rect_current_{};
  std::vector<LayerId> selected_layer_ids_;
  std::optional<QRect> move_hover_outline_rect_;
  QPoint move_press_widget_position_{};
  QPoint move_preview_delta_{};
  std::vector<RenderedDocumentPatch> move_preview_patches_;
  std::optional<QPoint> move_preview_patches_delta_{};
  bool moving_layers_use_outline_preview_{false};
  std::optional<LayerId> transform_layer_id_;
  QRectF transform_original_rect_{};
  QRectF transform_current_rect_{};
  QRectF transform_drag_start_rect_{};
  QPointF transform_drag_start_point_{};
  TransformHandle transform_drag_handle_{TransformHandle::None};
  double transform_angle_{0.0};
  double transform_start_angle_{0.0};
  double transform_scale_x_sign_{1.0};
  double transform_scale_y_sign_{1.0};
  double transform_drag_start_scale_x_sign_{1.0};
  double transform_drag_start_scale_y_sign_{1.0};
  TransformInterpolation transform_interpolation_{TransformInterpolation::Bicubic};
  CanvasAnchor transform_reference_point_{CanvasAnchor::Center};
  QImage transform_base_cache_{};
  QImage transform_source_image_{};
  QImage transform_composited_preview_cache_{};
  QRect transform_source_local_rect_{};
  bool transform_requires_composited_preview_{false};
  std::optional<LayerId> move_transform_controls_layer_id_{};
  std::function<void(QString)> before_edit_callback_;
  std::function<void(QColor)> color_picked_callback_;
  std::function<void(QPoint, QRect)> text_requested_callback_;
  std::function<void(LayerId)> active_layer_changed_callback_;
  std::function<void(QString)> status_callback_;
  std::function<void(CanvasInfoState)> info_callback_;
  std::function<void()> document_changed_callback_;
  std::function<void(DocumentChangeReason)> document_changed_reason_callback_;
  std::function<void()> view_changed_callback_;
  std::function<void()> transform_controls_changed_callback_;
};

}  // namespace patchy::ui
