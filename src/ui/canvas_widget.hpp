#pragma once

#include "core/document.hpp"
#include "core/pixel_tools.hpp"
#include "ui/image_document_io.hpp"
#include "ui/selection_outline.hpp"

#include <QBasicTimer>
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygon>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QPainter;
class QEvent;
class QResizeEvent;
class QTabletEvent;

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

// Action that can be bound to a pen barrel/side button. These are the actions
// that are useful to trigger directly from the pen while painting. Tablet pad
// "express keys" are intentionally not represented here: no desktop tablet
// driver delivers pad buttons to the application, so those are configured in
// the vendor driver as keyboard shortcuts instead.
enum class PenButtonAction {
  None,
  PanCanvas,
  ZoomCanvas,
  PickColor,
  SetCloneSource,
  SwapColors,
  Undo,
  Redo,
  ToggleEraser,
  IncreaseBrushSize,
  DecreaseBrushSize
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

  // Full selection state, captured so selection edits (marquee/lasso/wand drags,
  // Select All, Deselect, Invert, ...) can participate in the undo/redo history.
  struct SelectionSnapshot {
    QRegion selection;
    QRegion display_region;
    QRect mask_bounds;
    QImage mask_alpha;
  };

  // Tools whose combine mode (New/Add/Subtract/Intersect) is tracked separately,
  // so switching between e.g. Lasso and Marquee preserves each tool's own mode.
  static constexpr std::size_t kSelectionToolCount = 4;
  [[nodiscard]] static int selection_tool_index(CanvasTool tool) noexcept;

  enum class LayerEditTarget {
    Content,
    Mask
  };

  enum class MaskDisplayMode {
    None,
    Overlay,
    Grayscale
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

  struct PenInputSettings {
    bool enabled{true};
    bool pressure_size{true};
    int pressure_size_min_percent{20};
    bool pressure_opacity{true};
    int pressure_opacity_min_percent{15};
    bool use_eraser_tip{true};
    PenButtonAction primary_button_action{PenButtonAction::PanCanvas};
    PenButtonAction secondary_button_action{PenButtonAction::PickColor};
    bool tilt_shape{false};
    int tilt_min_roundness_percent{35};
  };

  struct PenInputSample {
    enum class PointerType {
      Unknown,
      Pen,
      Eraser,
      Cursor
    };

    QPointF widget_position{};
    QPointF document_position{};
    float pressure{1.0F};
    bool pressure_available{false};
    float x_tilt{0.0F};
    float y_tilt{0.0F};
    bool tilt_available{false};
    float tangential_pressure{0.0F};
    bool tangential_pressure_available{false};
    double rotation_degrees{0.0};
    bool rotation_available{false};
    float z{0.0F};
    bool z_available{false};
    PointerType pointer_type{PointerType::Unknown};
    qint64 device_id{-1};
    Qt::MouseButton button{Qt::NoButton};
    Qt::MouseButtons buttons{Qt::NoButton};
    Qt::KeyboardModifiers modifiers{Qt::NoModifier};
  };

  explicit CanvasWidget(QWidget* parent = nullptr);

  void set_document(Document* document);
  [[nodiscard]] double zoom() const noexcept;
  void set_zoom(double zoom);
  void zoom_at_widget_point(QPointF widget_position, double factor);
  void set_wheel_zooms(bool enabled) noexcept;
  [[nodiscard]] bool wheel_zooms() const noexcept;
  void refresh_tool_cursor();
  void fit_to_view();
  void zoom_to_document_rect(QRect document_rect);
  void set_spacebar_panning(bool enabled);
  [[nodiscard]] bool begin_pan_at_global_position(QPoint global_position);
  [[nodiscard]] bool pan_to_global_position(QPoint global_position);
  [[nodiscard]] bool end_pan();
  void set_tool(CanvasTool tool);
  [[nodiscard]] CanvasTool tool() const noexcept;
  void set_edit_locked(bool locked) noexcept;
  [[nodiscard]] bool edit_locked() const noexcept;
  void set_layer_edit_target(LayerEditTarget target) noexcept;
  [[nodiscard]] LayerEditTarget layer_edit_target() const noexcept;
  void set_mask_display_mode(MaskDisplayMode mode);
  [[nodiscard]] MaskDisplayMode mask_display_mode() const noexcept;
  void invalidate_mask_display();
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
  // Bitmap brush tip for the Brush and Eraser tools; null tip = procedural round/soft brush.
  // The id is an opaque library key kept here so the options bar and settings stay in sync.
  void set_brush_tip(std::shared_ptr<const patchy::BrushTip> tip, const QString& tip_id);
  [[nodiscard]] const QString& brush_tip_id() const noexcept;
  [[nodiscard]] bool has_brush_tip() const noexcept;
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
  void set_pen_input_settings(PenInputSettings settings) noexcept;
  [[nodiscard]] const PenInputSettings& pen_input_settings() const noexcept;
  [[nodiscard]] std::optional<PenInputSample> last_pen_input_sample() const;
  void set_wand_tolerance(int tolerance);
  [[nodiscard]] int wand_tolerance() const noexcept;
  void set_wand_contiguous(bool enabled) noexcept;
  [[nodiscard]] bool wand_contiguous() const noexcept;
  void set_wand_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool wand_sample_all_layers() const noexcept;
  void set_show_transform_controls(bool enabled) noexcept;
  [[nodiscard]] bool show_transform_controls() const noexcept;
  void set_fill_shapes(bool fill_shapes) noexcept;
  void set_shape_corner_radius(int radius) noexcept;
  [[nodiscard]] int shape_corner_radius() const noexcept;
  void set_fill_opacity(int opacity) noexcept;
  [[nodiscard]] int fill_opacity() const noexcept;
  void set_fill_softness(int softness) noexcept;
  [[nodiscard]] int fill_softness() const noexcept;
  void set_selection_mode(SelectionMode mode) noexcept;
  [[nodiscard]] SelectionMode selection_mode() const noexcept;
  // Combine mode actually in effect right now, folding in any held Shift/Alt and
  // the "no override without an existing selection" rule (used by the cursor
  // badge and the Options-bar mode buttons).
  [[nodiscard]] SelectionMode effective_selection_mode() const noexcept;
  [[nodiscard]] SelectionMode selection_mode_for_tool(CanvasTool tool) const noexcept;
  void set_selection_mode_for_tool(CanvasTool tool, SelectionMode mode) noexcept;
  [[nodiscard]] SelectionSnapshot capture_selection_snapshot() const;
  void apply_selection_snapshot(const SelectionSnapshot& snapshot);
  // Pins the marching-ants dash phase and stops its animation timer so tests
  // can grab deterministic frames at chosen phases.
  void set_selection_dash_offset_for_testing(int offset);
  // Run a selection-changing command (Select All, Deselect, Invert, ...) and push
  // an undo entry for it if the selection actually changed.
  void run_selection_command(QString label, const std::function<void()>& command);
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
  void document_changed_async_preview();
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
  // Invoked when a selection-only edit completes and actually changed the
  // selection, so the host can push an undo entry holding the pre-edit state.
  // `coalesce` marks a continuation of a move sequence (drag/nudge): consecutive
  // coalescing edits collapse into the single entry holding the pre-sequence
  // state, so a run of moves is one undo step.
  void set_selection_history_callback(std::function<void(QString, SelectionSnapshot, bool coalesce)> callback);
  // Invoked when the effective combine mode changes (tool switch, mode button,
  // or live Shift/Alt) so the Options-bar mode buttons can follow.
  void set_selection_mode_changed_callback(std::function<void(SelectionMode)> callback);
  void set_color_picked_callback(std::function<void(QColor)> callback);
  // Invoked when the canvas itself changes brush/gradient parameters (size
  // drag gesture, opacity digit keys) so the options bar can resync.
  void set_brush_settings_changed_callback(std::function<void()> callback);
  void set_pen_button_action_callback(std::function<void(PenButtonAction)> callback);
  void set_text_requested_callback(std::function<void(QPoint, QRect)> callback);
  void set_active_layer_changed_callback(std::function<void(LayerId)> callback);
  void set_status_callback(std::function<void(QString)> callback);
  void set_info_callback(std::function<void(CanvasInfoState)> callback);
  // Re-emit the info state (cursor position, color, selection rect) from the current cursor
  // position, for refreshes that are not driven by a canvas mouse event (selection edits via
  // keyboard or menus, document tab switches).
  void refresh_info_display() const;
  void set_document_changed_callback(std::function<void()> callback);
  void set_document_changed_callback(std::function<void(DocumentChangeReason)> callback);
  void set_view_changed_callback(std::function<void()> callback);
  void set_transform_controls_changed_callback(std::function<void()> callback);
  // Re-render a just-transformed text layer's glyphs crisply through its stored transform.  Returns
  // true if it replaced the layer's pixels/bounds (so the resampled bitmap is overridden).
  void set_text_layer_transform_render_callback(std::function<bool(LayerId)> callback);
  void set_selected_layer_ids(std::vector<LayerId> layer_ids);

protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void tabletEvent(QTabletEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void timerEvent(QTimerEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

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

  struct EffectiveBrushInput {
    int size{12};
    int opacity{100};
    int softness{75};
    int roundness{100};
    double angle_degrees{0.0};
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
  void start_async_render_cache_refresh();
  void cancel_async_render_cache_refresh() noexcept;
  [[nodiscard]] std::vector<RenderedDocumentPatch> render_document_patches_with_processing(
      const QRegion& document_region, const std::vector<std::pair<LayerId, Rect>>& layer_bounds,
      bool force_processing_wait);
  [[nodiscard]] bool dirty_region_should_use_processing_wait(const QRegion& document_region) const noexcept;
  void refresh_render_cache_rect(QRect document_rect);
  void refresh_render_cache_region(const QRegion& document_region);
  bool patch_render_cache_rect(QRect document_rect, const QImage& partial);
  bool patch_render_cache_patches(const std::vector<RenderedDocumentPatch>& patches);
  void invalidate_display_mip_cache() noexcept;
  void ensure_move_base_cache();
  void clear_move_base_cache() noexcept;
  [[nodiscard]] const QImage& display_image_for_zoom();
  [[nodiscard]] const QImage& move_base_display_image_for_zoom();
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
  // Applies the mode-badged cursor for the active selection tool; returns false
  // for non-selection tools so the caller can fall through.
  bool apply_selection_cursor_for_mode(SelectionMode mode);
  // Applies the magnifier cursor for the zoom tool, badged with a + (zoom in)
  // or - (zoom out, when Alt is held).
  void apply_zoom_cursor(bool zoom_out);
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
  void refresh_mask_display_image(QRegion document_region);
  void draw_mask_display_overlay(QPainter& painter, const QRectF& target_rect, bool pixel_aligned_view,
                                 QRect pixel_aligned_target_rect);
  [[nodiscard]] bool active_layer_locks_transparent_pixels() const noexcept;
  [[nodiscard]] bool active_layer_locks_image_pixels() const noexcept;
  [[nodiscard]] bool active_layer_locks_position() const noexcept;
  [[nodiscard]] bool layer_effectively_locks_image_pixels(const Layer& layer) const noexcept;
  [[nodiscard]] bool layer_effectively_locks_position(const Layer& layer) const noexcept;
  void show_layer_pixels_locked_message() const;
  void show_layer_position_locked_message() const;
  void show_edit_locked_message() const;
  [[nodiscard]] Layer* topmost_pixel_layer_at(QPoint document_point, bool require_visible_pixel,
                                              bool skip_locked) const noexcept;
  [[nodiscard]] Layer* topmost_move_layer_at(QPoint document_point, bool skip_locked) const noexcept;
  [[nodiscard]] Layer* topmost_text_layer_at(QPoint document_point) const noexcept;
  void activate_layer(Layer& layer);
  [[nodiscard]] QPoint layer_position(const Layer& layer, QPoint document_point) const noexcept;
  [[nodiscard]] QRect widget_rect_for_document_rect(QRect document_rect) const;
  [[nodiscard]] QRectF widget_rect_for_document_rect(QRectF document_rect) const;
  bool begin_edit(QString label);
  [[nodiscard]] CanvasTool effective_tool_for_input() const noexcept;
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
  [[nodiscard]] EffectiveBrushInput effective_brush_input() const noexcept;
  [[nodiscard]] EditOptions current_brush_edit_options(const EffectiveBrushInput& brush) const;
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
  [[nodiscard]] QPoint shape_constrained_current() const;
  [[nodiscard]] QRect draw_mask_line(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_gradient(QPoint from, QPoint to);
  [[nodiscard]] QRect draw_mask_rectangle(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_ellipse(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect render_mask_shape(QRect rect, bool erase, patchy::ShapeKind kind);
  [[nodiscard]] QRect flood_fill_mask(QPoint start);
  void begin_color_pick(QPoint widget_position, QPoint global_position);
  void update_color_pick(QPoint widget_position, QPoint global_position);
  void end_color_pick();
  void set_picked_color(QColor color);
  void pick_color(QPoint point);
  void magic_wand_select(QPoint start);
  [[nodiscard]] QRegion marquee_selection_region(QPoint anchor, QPoint current) const;
  [[nodiscard]] QRect marquee_selection_rect(QPoint anchor, QPoint current) const;
  [[nodiscard]] QImage marquee_selection_mask(QPoint anchor, QPoint current, QRect& bounds) const;
  [[nodiscard]] QImage lasso_selection_mask(const QPolygon& polygon, QRect& bounds) const;
  void set_selection_from_region(QRegion selection);
  void set_selection_from_mask(QRegion selection, QRect mask_bounds, QImage mask_alpha);
  void restore_selection_before_edit();
  // Marks the cached marching-ants outline stale. Must be called by any code
  // that writes selection_ / selection_display_region_ directly instead of
  // going through the setters above.
  void invalidate_selection_outline() noexcept;
  // Lazily retraces the outline loops after a selection change and refreshes
  // the cached device-space path when zoom/pan/viewport differ from the key it
  // was built for; animation ticks then only restroke the cached path.
  void ensure_selection_outline_screen_path() const;
  // Push an undo entry for a selection change whose pre-edit state is `before`,
  // unless the selection is unchanged.
  void record_selection_history(QString label, const SelectionSnapshot& before, bool coalesce = false);
  [[nodiscard]] SelectionSnapshot selection_snapshot_before_edit() const;
  void notify_selection_mode_changed();
  void update_selection_square_constraint(Qt::KeyboardModifiers modifiers);
  void refresh_active_marquee_selection();
  [[nodiscard]] bool can_move_selection_at(QPoint document_point, Qt::KeyboardModifiers modifiers) const;
  void apply_selection_move(QPoint delta);
  void nudge_selection(QPoint delta);
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
  void refresh_free_transform_preview_caches();
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
  [[nodiscard]] PenInputSample pen_input_sample_from_tablet_event(const QTabletEvent& event) const;
  [[nodiscard]] PenButtonAction pen_action_for_button(Qt::MouseButton button) const noexcept;
  [[nodiscard]] bool pen_recently_in_proximity() const;
  [[nodiscard]] bool tablet_event_should_pan(const PenInputSample& sample, QEvent::Type event_type) const noexcept;
  [[nodiscard]] bool tablet_event_should_zoom(const PenInputSample& sample, QEvent::Type event_type) const noexcept;
  void begin_zoom_drag(QPointF widget_position);
  void update_zoom_drag(QPointF widget_position);
  void end_zoom_drag();
  void begin_brush_adjust_drag(QPoint widget_position, bool from_tablet = false);
  void update_brush_adjust_drag(QPoint widget_position);
  void end_brush_adjust_drag(bool commit);
  void draw_brush_adjust_overlay(QPainter& painter) const;
  void draw_brush_adjust_readout(QPainter& painter, QPointF center, double radius) const;
  void notify_brush_settings_changed();
  // Returns the active tip pre-scaled for `size` and feathered for `softness` (the brush Soft
  // setting), from the per-tip cache; null when no tip is set.
  [[nodiscard]] std::shared_ptr<const patchy::ScaledBrushTip> scaled_brush_tip_for(int size,
                                                                                   int softness) const;
  void apply_brush_tip_to_options(EditOptions& options, int brush_size, int brush_softness) const;
  [[nodiscard]] QImage brush_tip_stamp_image(int size, int softness) const;
  // Sets a cursor tracing the active tip's outline; false when there is no usable tip shape.
  bool apply_brush_tip_cursor();
  // Brushes whose on-screen footprint exceeds the OS-cursor cap draw their outline as a canvas
  // overlay that follows the pointer instead (the cursor becomes a plain crosshair).
  [[nodiscard]] QSize brush_outline_display_size() const;
  [[nodiscard]] bool brush_outline_uses_overlay() const;
  [[nodiscard]] QRect brush_hover_outline_rect() const;
  void track_brush_hover_position(QPoint widget_position);
  void draw_brush_hover_outline(QPainter& painter) const;
  bool handle_opacity_digit_key(int key, Qt::KeyboardModifiers modifiers, bool auto_repeat);
  bool perform_pen_button_action(PenButtonAction action, const PenInputSample& sample);
  bool dispatch_tablet_as_mouse(QTabletEvent* event, const PenInputSample& sample);

  Document* document_{nullptr};
  double zoom_{1.0};
  QPointF pan_{40.0, 40.0};
  bool wheel_zooms_{true};
  QImage render_cache_{};
  bool render_cache_dirty_{true};
  RenderCacheDiagnostics render_cache_diagnostics_{};
  bool async_render_cache_in_flight_{false};
  bool async_render_cache_pending_{false};
  std::uint64_t async_render_cache_generation_{0};
  std::vector<QImage> display_mip_cache_{};
  QSize display_mip_source_size_{};
  QPoint last_mouse_position_{};
  QPoint last_document_position_{};
  QPointF last_document_position_f_{};
  QPointF stroke_constraint_start_{};
  StrokeConstraintAxis stroke_constraint_axis_{StrokeConstraintAxis::None};
  bool edit_locked_{false};
  QPointF brush_smoothing_last_input_position_{};
  QPointF brush_smoothing_last_rendered_position_{};
  bool brush_smoothing_active_{false};
  QPoint clone_source_point_{};
  QPoint clone_source_offset_{};
  QPoint shape_start_{};
  QPoint shape_current_{};
  bool shape_square_constrained_{false};
  QPoint move_start_{};
  QPoint selection_start_{};
  QPoint selection_current_{};
  QPoint selection_press_widget_position_{};
  QPoint selection_move_origin_document_{};
  bool selection_shift_at_press_{false};
  bool selection_shift_released_since_press_{false};
  bool selection_square_constrained_{false};
  CanvasTool tool_{CanvasTool::Brush};
  LayerEditTarget layer_edit_target_{LayerEditTarget::Content};
  MaskDisplayMode mask_display_mode_{MaskDisplayMode::None};
  QImage mask_display_image_;
  LayerId mask_display_image_layer_{0};
  QColor primary_color_{Qt::black};
  QColor secondary_color_{Qt::white};
  int brush_size_{12};
  int brush_opacity_{100};
  int brush_softness_{75};
  bool brush_build_up_{false};
  std::shared_ptr<const patchy::BrushTip> brush_tip_;
  QString brush_tip_id_;
  patchy::BrushTipMipChain brush_tip_mips_;
  // Most-recently-used scaled stamps keyed by (target size, softness); pressure-driven size
  // changes hit this instead of rescaling the tip on every dab.
  mutable std::vector<std::pair<std::pair<int, int>, std::shared_ptr<const patchy::ScaledBrushTip>>>
      brush_tip_scaled_cache_;
  patchy::BrushTipStrokeState brush_tip_stroke_state_;
  QPoint brush_hover_widget_position_{};
  bool brush_hover_position_valid_{false};
  mutable QImage brush_outline_overlay_image_;  // cached tip outline at display scale
  mutable QString brush_outline_overlay_key_;
  GradientMethod gradient_method_{GradientMethod::Linear};
  bool gradient_reverse_{false};
  int gradient_opacity_{100};
  std::optional<std::vector<GradientStop>> gradient_stops_;
  int wand_tolerance_{24};
  bool wand_contiguous_{true};
  bool wand_sample_all_layers_{false};
  bool show_transform_controls_{true};
  bool fill_shapes_{false};
  int shape_corner_radius_{0};
  int fill_opacity_{100};
  int fill_softness_{0};
  bool auto_select_layer_{true};
  SelectionMode selection_mode_{SelectionMode::Replace};
  // Per-tool combine modes; selection_mode_ mirrors the active selection tool's
  // entry. Indexed by selection_tool_index().
  std::array<SelectionMode, kSelectionToolCount> selection_modes_per_tool_{
      SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace};
  // Set when a marquee drag begins with Alt held and no existing selection: the
  // press point is the center and the rectangle grows symmetrically.
  bool marquee_from_center_{false};
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
  bool brush_adjust_dragging_{false};
  QPoint brush_adjust_origin_widget_{};
  QPoint brush_adjust_current_widget_{};
  bool brush_adjust_from_tablet_{false};
  int brush_adjust_start_size_{12};
  int brush_adjust_start_softness_{75};
  std::optional<QPointF> last_stroke_end_document_{};
  QElapsedTimer opacity_digit_timer_{};
  int opacity_pending_digit_{-1};
  bool drawing_shape_{false};
  bool dragging_text_rect_{false};
  bool move_drag_pending_{false};
  bool moving_layer_{false};
  bool transforming_layer_{false};
  bool dragging_transform_{false};
  bool color_picking_{false};
  bool selecting_{false};
  bool lassoing_{false};
  bool moving_selection_{false};
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
  // Marching-ants caches, rebuilt lazily inside const paint code (same pattern
  // as the mutable brush-outline caches above): document-space contour loops
  // (stale when selection_outline_dirty_; only used at zoom >= 1 — below that
  // the outline is retraced at device resolution) and the device-space paths
  // built for the zoom/pan/viewport key stored alongside them.
  mutable std::vector<OutlineLoop> selection_outline_loops_;
  mutable bool selection_outline_dirty_{true};
  mutable SelectionOutlineScreenPaths selection_outline_screen_paths_;
  mutable bool selection_outline_screen_valid_{false};
  mutable double selection_outline_screen_zoom_{0.0};
  mutable QPointF selection_outline_screen_pan_;
  mutable QRect selection_outline_screen_viewport_;
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
  PenInputSettings pen_input_settings_{};
  std::optional<PenInputSample> active_pen_input_sample_{};
  std::optional<PenInputSample> last_pen_input_sample_{};
  QElapsedTimer pen_proximity_clock_{};
  qint64 last_tablet_event_ms_{-1};
  Qt::MouseButton mouse_pen_action_button_{Qt::NoButton};
  bool handling_tablet_event_{false};
  bool pen_button_suppressing_paint_{false};
  bool pen_zoom_dragging_{false};
  QPointF zoom_drag_anchor_widget_{};
  QPointF zoom_drag_last_pos_{};
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
  QImage move_base_cache_{};
  std::vector<QImage> move_base_display_mip_cache_{};
  qint64 move_base_display_mip_source_key_{0};
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
  std::function<void(QString, SelectionSnapshot, bool)> selection_history_callback_;
  std::function<void(SelectionMode)> selection_mode_changed_callback_;
  std::function<void(QColor)> color_picked_callback_;
  std::function<void()> brush_settings_changed_callback_;
  std::function<void(PenButtonAction)> pen_button_action_callback_;
  std::function<void(QPoint, QRect)> text_requested_callback_;
  std::function<void(LayerId)> active_layer_changed_callback_;
  std::function<void(QString)> status_callback_;
  std::function<void(CanvasInfoState)> info_callback_;
  std::function<void()> document_changed_callback_;
  std::function<void(DocumentChangeReason)> document_changed_reason_callback_;
  std::function<void()> view_changed_callback_;
  std::function<void()> transform_controls_changed_callback_;
  std::function<bool(LayerId)> text_layer_transform_render_callback_;
};

}  // namespace patchy::ui
