#pragma once

// Helpers shared by the canvas_widget_*.cpp translation units. CanvasWidget's
// implementation is being split across several files following the same rules
// as the MainWindow split (see AGENTS.md "MainWindow's implementation is split
// across main_window_*.cpp"); helpers used by more than one of those files are
// promoted out of the per-file anonymous namespaces into this header. Internal
// to the CanvasWidget implementation - do not include this from outside the
// canvas_widget_*.cpp family.

#include "core/layer.hpp"
#include "core/pixel_tools.hpp"
#include "ui/canvas_widget.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRegion>

#include <cstdint>
#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

namespace patchy::ui {

class CanvasWidget;

constexpr double kPi = 3.14159265358979323846;

// Whether the Move tool / free transform can pick up this layer's pixels,
// shared by the move-tool code in canvas_widget.cpp and the transform TU.
bool layer_has_movable_pixels(const Layer& layer);

// A supported Smart Filter stack re-renders from its source on move/transform
// instead of translating the cached pixels.
bool move_layer_requires_smart_filter_rerender(const Layer& layer);

// Local-space bounding rect of the layer's visible (non-transparent) pixels.
std::optional<QRect> opaque_pixel_local_rect(const Layer& layer);

// Whether the layer's visible pixels (source alpha x layer mask) cover the
// document point; shared by the topmost-pixel-layer hit test still in
// canvas_widget.cpp and the move-layer hit test below.
bool pixel_layer_contains_document_point(const Layer& layer, QPoint document_point, bool require_visible_pixel);

// Whether the Move tool's hit test picks this layer at the document point;
// shared by the topmost-move-layer hit test still in canvas_widget.cpp and
// the auto-select press handling in the events TU.
bool move_layer_contains_document_point(const Layer& layer, QPoint document_point);

// Document-space bounds for a moving layer's outline preview; shared by the
// move-gesture setup in the events TU and the outline painters in the move TU.
std::optional<Rect> move_layer_outline_bounds(const Layer& layer);

// Grayscale mask value for painting a color onto a layer mask; shared by the
// brush TU (mask brush segments) and the mask shape/fill members still in
// canvas_widget.cpp.
std::uint8_t mask_value_from_color(QColor color);

// Coverage-weighted blend of a mask value over the current one.
std::uint8_t blend_mask_value(std::uint8_t current, std::uint8_t value, float coverage);

// Normalized QRect spanned by two corner points; shared by the selection TU
// and the shape/draw and event code still in canvas_widget.cpp.
QRect normalized_rect(QPoint a, QPoint b);

// Row-run QRegion built from a byte mask (non-zero = selected) restricted to
// [min_x,max_x] x [min_y,max_y]; shared by the selection TU and the magic-wand
// engine still in canvas_widget.cpp.
QRegion region_from_mask(const std::vector<std::uint8_t>& selected, int width, int height,
                         int min_x, int min_y, int max_x, int max_y);

// Hard-edged grayscale coverage mask (255 inside the region) over bounds;
// shared by the selection TU and the magic-wand engine still in
// canvas_widget.cpp.
QImage hard_mask_from_region(const QRegion& region, QRect bounds);

// Padding a mask's bounds need before feathering so the blur has room to
// falloff; shared by the selection TU and the magic-wand / quick-select
// engines still in canvas_widget.cpp.
int feather_mask_padding(int feather_radius);

// Triple box blur approximating the gaussian selection feather; shared by the
// selection TU and the magic-wand / quick-select engines still in
// canvas_widget.cpp.
QImage feather_blur_mask(QImage mask, int feather_radius);

// Baseline EditOptions for the pixel-editing paths: bakes the brush settings,
// palette snap, and the active selection into the options. Shared by the brush
// TU and the shape/fill/line members still in canvas_widget.cpp.
EditOptions edit_options(QColor primary, QColor secondary, int brush_size, int brush_opacity, int brush_softness,
                         bool fill_shapes, bool lock_transparent_pixels, const CanvasWidget& canvas,
                         int brush_roundness = 100, double brush_angle_degrees = 0.0);

// Hash key for a document pixel touched by the current stroke; shared by the
// per-stroke dedup/accumulator maps in the brush TU and the shape tools.
std::uint64_t stroke_pixel_key(std::int32_t x, std::int32_t y) noexcept;

// Ruler geometry, shared by the ruler painter in the render TU and the
// guide-drag hit tests still in canvas_widget.cpp.
constexpr int kTopRulerHeight = 24;
constexpr int kLeftRulerWidth = 32;

// Pixel width of one document grid cycle, shared by the grid overlay painter
// in the render TU and the snapping code still in canvas_widget.cpp.
double grid_cycle_pixels(std::int32_t cycle_32) noexcept;

// Zoom-level display policy helpers, shared by the render TU's paint/display
// cache code and the view/event/shape-preview code still in canvas_widget.cpp.
bool uses_pixel_aligned_view(double zoom) noexcept;
bool uses_deep_zoom_pixel_renderer(double zoom) noexcept;
bool uses_smooth_display_scaling(double zoom, bool deep_pixel_renderer) noexcept;
int display_mip_level_for_zoom(double zoom) noexcept;

// Selection crosshair / badge styling: a pure-black stroke (drawn antialiased so
// the edges stay soft) over a white halo that extends ~1px on every side, so the
// cursor stays visible even hovering over solid black.
const QColor kSelectionCursorInk(0, 0, 0);
const QColor kSelectionCursorHalo(255, 255, 255);
constexpr double kSelectionCursorWidth = 1.5;
constexpr double kSelectionCursorHaloWidth = kSelectionCursorWidth + 2.0;  // +1px per side

// Draws the +/-/x badge for the active combine mode on a selection-tool cursor.
// Replace draws nothing. Stroked twice: white halo first, then black on top.
// Shared by the selection/magic-wand cursor builders in the cursors TU and
// quick_select_cursor in canvas_widget.cpp.
void paint_selection_mode_badge(QPainter& painter, CanvasWidget::SelectionMode mode, QPointF center);

// Whether Alt+Left temporarily turns the tool into the color picker; shared by
// the event code in canvas_widget.cpp and update_tool_cursor in the cursors TU.
bool tool_uses_alt_left_for_color_pick(CanvasTool tool) noexcept;

// Clamps a document-space point onto the canvas; shared by the event/lasso/
// zoom code in canvas_widget.cpp and pop_magnetic_anchor in the
// selection-engines TU.
QPoint clamped_document_point(const Document& document, QPoint point);

// Whether the tool accepts the Alt+Right-drag brush size/softness gesture;
// shared by the mouse event code in canvas_widget.cpp and
// dispatch_tablet_as_mouse in the pen TU.
bool tool_supports_brush_adjust_drag(CanvasTool tool) noexcept;

// PATCHY_ZOOM_TRACE=1 prints paint/zoom phase timings over 2 ms to stderr (the
// PATCHY_REV_TRACE pattern): run the real app with it set to attribute slow
// zoom/pan/paint steps to a phase instead of guessing.
bool zoom_trace_enabled();

class ZoomTraceScope {
 public:
  ZoomTraceScope(const char* label, double zoom) : label_(label), zoom_(zoom), enabled_(zoom_trace_enabled()) {
    if (enabled_) {
      started_ = std::chrono::steady_clock::now();
    }
  }
  ZoomTraceScope(const ZoomTraceScope&) = delete;
  ZoomTraceScope& operator=(const ZoomTraceScope&) = delete;
  ~ZoomTraceScope() {
    if (!enabled_) {
      return;
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();
    if (elapsed >= 2.0) {
      std::fprintf(stderr, "[ZOOMTRACE] %s ms=%.2f zoom=%.4f\n", label_, elapsed, zoom_);
      std::fflush(stderr);
    }
  }

 private:
  const char* label_;
  double zoom_;
  bool enabled_;
  std::chrono::steady_clock::time_point started_;
};

}  // namespace patchy::ui
