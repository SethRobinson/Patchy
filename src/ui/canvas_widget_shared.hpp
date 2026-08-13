#pragma once

// Helpers shared by the canvas_widget_*.cpp translation units. CanvasWidget's
// implementation is being split across several files following the same rules
// as the MainWindow split (see docs/code-organization.md); helpers used by more
// than one of those files are
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

#include <array>
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

// Document-sized RGBA8888 render of one layer alone, folding the layer mask
// and layer opacity into alpha. The active-layer branch of every
// "Sample All Layers" option: shared by the magic-wand / quick-select engines
// and the retouch tools' source snapshots (Clone, Healing, Spot Healing,
// Patch).
QImage active_layer_sample_image(const Layer& layer, QSize document_size);

// Procedural round-brush coverage falloff (smoothstep edge over the softness
// band); shared by the brush TU's stamp shapes and the spot-healing footprint
// stamper.
float brush_coverage(double distance_squared, int radius, int softness);

// Alpha-weighted average of 8 ring samples at `radius` around `center` over an
// RGBA8888 snapshot (center-pixel fallback when the ring is fully
// transparent); the low-frequency "tone" term of the healing math. Shared by
// the brush TU (Healing Brush) and the spot-healing / patch commit loops.
std::array<double, 3> healing_ring_tone(const QImage& snapshot, QPoint center, int radius);

// Classic frequency-separation healing sample: destination ring tone plus the
// source pixel's difference from its own ring tone (see the constraint comment
// at the definition). Shared by the brush TU (Healing Brush) and the
// spot-healing / patch commit loops.
std::array<std::uint8_t, 4> healing_sample(const QImage& snapshot, QPoint source, QPoint destination,
                                           int tone_radius);

// Straight-alpha RGBA mix of `src` over `dst` by `amount`; the write primitive
// of the clone/heal family. Shared by the brush TU and the spot-healing /
// patch commit loops.
void blend_straight_rgba(std::uint8_t* dst, const std::uint8_t* src, float amount);

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
// Mip level for PREVIEW-ONLY display-resolution compositing (move drags): the
// display mip level clamped to [0, 3].
int preview_composite_level_for_zoom(double zoom) noexcept;
// Expand a (non-negative) document rect outward so its edges land on the
// 2^level mip-block grid. Patches aligned this way downscale to exactly the
// same pixels the full-image mip chain produces for that area.
QRect rect_aligned_to_mip_grid(QRect rect, int level) noexcept;
// Map a grid-aligned, canvas-clipped full-res rect into the preview-scaled
// document's coordinates (floor origin, ceil extent - the partial edge blocks
// of a ceil-halved canvas stay covered).
QRect preview_scaled_document_rect(QRect rect, int level) noexcept;
// A live preview frame (move patches, transform composited preview) slower
// than this latches the drag onto its proxy on the next move: the area gates
// cannot price the stack a drag crosses. Env override PATCHY_MOVE_LIVE_LATCH_MS
// (historic name; it governs move AND transform drags; 0 latches after any
// live frame - the test hook).
int live_preview_frame_latch_ms() noexcept;

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

// Paint/retouch tools that show the round Size/Soft footprint cursor; past the
// ~155px display cap they switch to a crosshair plus the canvas-overlay outline
// (OS/browser cursor pixmaps cannot grow unbounded). Shared by
// update_tool_cursor in the cursors TU and the hover-outline gates in the brush
// TU. Quick Select joins the overlay through its own size/cursor path.
bool tool_uses_brush_footprint_cursor(CanvasTool tool) noexcept;

// Tools whose strokes stamp the active bitmap brush tip; only their cursor and
// overlay outline trace the tip shape. Every other footprint tool strokes
// procedurally, so its outline stays the procedural circle even while a tip is
// selected. Shared by the cursors and brush TUs.
bool tool_paints_with_brush_tip(CanvasTool tool) noexcept;

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
