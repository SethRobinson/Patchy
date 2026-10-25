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
#include <QFontMetrics>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <chrono>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

namespace patchy::ui {

class CanvasWidget;

constexpr double kPi = 3.14159265358979323846;

// The transparency checkerboard's two squares. Deliberately theme-exempt (the
// color-scheme section of docs/ui-conventions.md): it depicts alpha, not chrome. The
// artboard label ink reads them as the backing where the composite is clear.
inline QColor checkerboard_dark_square() { return QColor(188, 188, 188); }
inline QColor checkerboard_light_square() { return QColor(236, 236, 236); }

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

// Whether the document point lies inside the rect the Move tool draws for the
// layer (move_layer_outline_bounds: opaque raster extent or text frame),
// transparent pixels included. The rect pass behind the pixel-precise hit
// test above, so a press anywhere inside a layer's outline grabs it.
bool move_layer_rect_contains_document_point(const Layer& layer, QPoint document_point);

// Document-space bounds for a moving layer's outline preview; shared by the
// move-gesture setup in the events TU and the outline painters in the move TU.
std::optional<Rect> move_layer_outline_bounds(const Layer& layer);

// Grayscale mask value for painting a color onto a layer mask; shared by the
// brush TU (mask brush segments) and the mask shape/fill members still in
// canvas_widget.cpp.
std::uint8_t mask_value_from_color(QColor color);

// Coverage-weighted blend of a mask value over the current one.
std::uint8_t blend_mask_value(std::uint8_t current, std::uint8_t value, float coverage);
// Runs an 8-bit retouch (Spot Healing, Remove Object, Patch) on a 16/32-bit layer
// (docs/high-bit-depth.md): construction swaps in the layer narrowed to 8 bits;
// destruction writes back, at the layer's depth, only the pixels the edit changed, so
// every other pixel keeps its full precision, then calls `changed` once.
class NarrowedLayerEdit {
public:
  NarrowedLayerEdit(Layer& layer, std::function<void()> changed);
  ~NarrowedLayerEdit();
  NarrowedLayerEdit(const NarrowedLayerEdit&) = delete;
  NarrowedLayerEdit& operator=(const NarrowedLayerEdit&) = delete;

private:
  Layer& layer_;
  std::function<void()> changed_;
  PixelBuffer deep_;
  PixelBuffer narrowed_before_;
  Rect bounds_before_{};
};
// blend_mask_value on the mask sample at `px` in a gray buffer of `format`, at its depth
// (16/32-bit masks blend in float, docs/high-bit-depth.md).
void blend_mask_at(PixelFormat format, std::uint8_t* px, std::uint8_t value, float coverage);
// The mask sample at `px` as 0..255 (any depth).
[[nodiscard]] float mask_sample_at(PixelFormat format, const std::uint8_t* px);

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
// The same render limited to `region` (document pixels): an image of region's
// size whose (0, 0) is region's top-left, transparent outside the layer. The
// eyedropper's Current Layer sample reads a few pixels through it instead of
// rendering the whole document.
QImage active_layer_sample_region(const Layer& layer, QRect region);
// What automatic selection analyzes with Sample All Layers off (docs/ai-selection.md): the
// layer rendered on its own, whatever its kind (pixel, shape, text, Smart Object, or a group
// with its contents), with its opacity, mask and effects. Every other layer is hidden except
// its ancestor groups. Null with `error` set when the layer is missing, hidden, or renders
// nothing (an adjustment layer on its own). RGBA8888, document-sized.
QImage automatic_selection_layer_image(const Document& document, LayerId layer_id, QString* error);
// The status/script message for an automatic selection command (`command`, translated)
// whose model ran out of memory (patchy::ai::is_out_of_memory), in place of the raw
// ONNX Runtime diagnostic.
QString automatic_selection_out_of_memory_message(const QString& command);

// The round-brush coverage falloff shared by the brush TU's stamp shapes and the
// spot-healing footprint stamper is patchy::brush_coverage (core/pixel_tools.hpp),
// the same function the brush engine stamps with.

// Straight-alpha RGBA mix of `src` over `dst` by `amount`; the write primitive
// of the clone/heal family. Shared by the brush TU and the spot-healing /
// patch commit loops.
void blend_straight_rgba(std::uint8_t* dst, const std::uint8_t* src, float amount);

// Baseline EditOptions for the pixel-editing paths: bakes the brush settings,
// palette snap, and the active selection into the options. Shared by the brush
// TU and the shape/fill/line members still in canvas_widget.cpp.
// Applies the Fill tool / Fill command settings (options bar: Opacity, Soft, Tol, Contiguous)
// to `options`: Opacity scales primary.a, Soft becomes the inward feather band (up to 50 px),
// and Tol / Contiguous drive flood_fill. fill_rect ignores the last two.
void apply_fill_settings(EditOptions& options, const CanvasWidget& canvas);
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
// Whether the canvas interpolates at this view zoom: the one display smoothing rule
// (docs/view-navigation.md, "Display interpolation").
bool uses_smooth_display_scaling(double zoom, bool deep_pixel_renderer) noexcept;
// The pixel grid's line color for one pixel (premultiplied ARGB in, opaque out): the
// pixel's own color pushed toward black or white, so the line reads over light and dark
// pixels alike (GitHub issue 239). Document-view drawing, like the checkerboard.
QRgb pixel_grid_line_color(QRgb pixel) noexcept;
// The deep-zoom raster: `source` pixel (i, j) is document pixel document_origin + (i, j)
// and covers device columns [qRound(edges_x[i] * ratio), qRound(edges_x[i + 1] * ratio)),
// the cover an aliased fillRect of its logical widget rect gets; rows likewise. edges_x and
// edges_y hold one more entry than the columns and rows drawn. With `pixel_grid`, each
// cell's first device column and row take pixel_grid_line_color (none along document
// column or row 0). With `grid_only` as well, every other device pixel is transparent:
// the lines alone, for drawing over a frame that covered the raster (the Dynamic Vector
// Preview blit). Returns premultiplied ARGB at device resolution and stores the device
// position of its top-left pixel in device_origin; cost is one write per device pixel.
QImage deep_zoom_raster(const QImage& source, QPoint document_origin, std::span<const int> edges_x,
                        std::span<const int> edges_y, double device_pixel_ratio, bool pixel_grid,
                        QPoint& device_origin, bool grid_only = false);
int display_mip_level_for_zoom(double zoom) noexcept;
// "Layer N", one past the top-level count and not taken anywhere in the tree (New Layer,
// and the layer a paint press adds to an empty artboard).
[[nodiscard]] QString default_new_layer_name(const Document& document);
// Mip level for PREVIEW-ONLY display-resolution compositing (move drags): the
// display mip level clamped to [0, 3].
int preview_composite_level_for_zoom(double zoom) noexcept;
// One step of every zoomed-out display mip chain: a (w+1)/2 x (h+1)/2 image
// whose pixels each average their own 2x2 source block (alpha-weighted for
// straight-alpha formats; an odd last row or column averages what it has).
// Each output pixel depends on its block alone, so a changed source rect can
// be patched in place (GitHub issue 165: rebuilding the chain on every brush
// frame cost the whole composite per repaint).
QImage halved_display_mip(const QImage& source);
// The rect of a halved_display_mip output that a change to source_rect touches.
QRect halved_display_mip_rect(QRect source_rect) noexcept;
// Recompute target_rect of target (halved_display_mip(source)'s size and
// format) from source, bit-identical to rebuilding the whole level.
void refresh_halved_display_mip(const QImage& source, QImage& target, QRect target_rect);
// Expand a (non-negative) document rect outward so its edges land on the
// 2^level mip-block grid. Patches aligned this way downscale to exactly the
// same pixels the full-image mip chain produces for that area.
QRect rect_aligned_to_mip_grid(QRect rect, int level) noexcept;
// Map a grid-aligned, canvas-clipped full-res rect into the preview-scaled
// document's coordinates (floor origin, ceil extent - the partial edge blocks
// of a ceil-halved canvas stay covered).
QRect preview_scaled_document_rect(QRect rect, int level) noexcept;
// Handle-drag rect anchored at the start rect's center (Alt held; GitHub issue
// 66): the dragged edge or corner follows `point` and the opposite side mirrors
// it, so the box resizes evenly about its center. `moves_x`/`moves_y` say which
// axes the handle drives; a `target_ratio` > 0 constrains width:height, fitting
// the larger axis to the smaller as the anchored branches do. Never below 1 px.
QRect center_anchored_handle_rect(QRect start, bool moves_x, bool moves_y, QPoint point,
                                  double target_ratio) noexcept;
// The next rung of Photoshop's zoom ladder (0.56% ... 100% ... 12800%) above or
// below `view_zoom` (document pixels per device pixel; GitHub issue 77). A view
// already on a rung, within half a percent, steps to the neighbouring rung, so
// 46.39% goes to 50% in and 33.33% out. Clamped to the ladder's ends; the
// caller's zoom range clamp still applies.
double next_zoom_ladder_step(double view_zoom, bool zoom_in) noexcept;
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

// Painting tools where Ctrl (Command on macOS) is the temporary Move (GitHub issues
// 380, 421, 423): Ctrl+drag moves the selected layers, a Ctrl+click that never drags
// selects the layer under the pointer. The footprint-cursor tools.
bool tool_has_ctrl_temporary_move(CanvasTool tool) noexcept;

// The small + at the center of the brush cursors and the overlay outline: a dark
// outline around a light core, crisp on whole pixels, so it reads on any artwork
// (GitHub issue 425). Tool cursor colors, exempt from the theme roles.
void paint_brush_center_cross(QPainter& painter, QPoint center);

// The canvas HUD pill (the drag size readout's look, GitHub issues 142 and 422):
// one line of text on the HUD fill and border, theme roles only. The pill is the
// text plus its padding; the caller places it.
QSize canvas_hud_pill_size(const QFontMetrics& metrics, const QString& text);
void paint_canvas_hud_pill(QPainter& painter, const QRect& pill, const QString& text);

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
