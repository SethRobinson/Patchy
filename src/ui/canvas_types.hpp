#pragma once

// Canvas values shared by tools, document sessions and window controllers.
// Persisted enum values are append-only. CanvasWidget keeps aliases for callers.
#include "core/layer.hpp"
#include "core/document_channel.hpp"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace patchy::ui {

inline constexpr std::size_t kSelectionToolCount = 10;

inline constexpr int kMaxBrushSize = 1024;
inline constexpr int kMaxQuickSelectSize = 512;

// The pixel grid's threshold (GitHub issue 600): the view zoom, in percent, from which View >
// Show > Pixel Grid draws. Preferences > Units & Grids offers these choices; the setting is
// view/pixelGridMinZoom. Below 600% the grid brings the deep-zoom renderer with it, so its
// lines land on the raster's cell edges (CanvasWidget::deep_zoom_pixels_active).
inline constexpr int kDefaultPixelGridMinZoomPercent = 600;
inline constexpr std::array<int, 7> kPixelGridMinZoomPercentChoices{400, 500, 600, 800, 1200, 1600, 3200};

// Edit > Transform modes for a Free Transform session (GitHub issue 225): plain corner
// and edge drags skew, distort, or apply perspective. Scale and Rotate (GitHub issue
// 299) narrow the box to one kind of drag: handles only scale, or every handle
// rotates. None is plain Free Transform. docs/free-transform.md.
enum class TransformDistortMode { None, Skew, Distort, Perspective, Scale, Rotate };

enum class CanvasTool {
  Move,
  Marquee,
  EllipticalMarquee,
  Lasso,
  MagneticLasso,
  MagicWand,
  QuickSelect,
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
  Zoom,
  Healing,
  Dodge,
  Burn,
  Sponge,
  BlurBrush,
  SharpenBrush,
  PatternStamp,
  MixerBrush,
  // July 2026 vector tools (append-only: values ride persisted settings).
  Pen,
  PathSelect,
  DirectSelect,
  Polygon,
  CustomShape,
  // August 2026 healing group (append-only: values ride persisted settings).
  SpotHealing,
  PatchTool,
  // August 2026 Crop tool (append-only: values ride persisted settings).
  Crop,
  // August 2026 anchor tools (append-only: values ride persisted settings).
  // They share the Pen's handlers with a fixed edit action each.
  AddAnchor,
  DeleteAnchor,
  ConvertPoint,
  // October 2026 Object Selection (append-only: values ride persisted settings).
  ObjectSelect,
  // October 2026 Artboard tool (append-only: values ride persisted settings).
  Artboard,
  // October 2026 Polygonal Lasso (append-only: values ride persisted settings).
  PolygonLasso,
  // October 2026 Perspective Crop (append-only: values ride persisted settings).
  PerspectiveCrop,
  // October 2026 Red Eye (append-only: values ride persisted settings).
  RedEye,
  // October 2026 Ruler (append-only: values ride persisted settings).
  Ruler,
  // November 2026 Content-Aware Move (append-only: values ride persisted settings).
  ContentAwareMove,
  // November 2026 Rotate View (append-only: values ride persisted settings).
  RotateView,
  // November 2026 Type Mask (append-only: values ride persisted settings). Shares the Type
  // tool's editor; its commit makes a selection from the letters (docs/type-mask.md).
  TypeMask
};

// The tools that open the inline text editor on a click: Type and Type Mask.
[[nodiscard]] constexpr bool is_type_tool(CanvasTool tool) noexcept {
  return tool == CanvasTool::Text || tool == CanvasTool::TypeMask;
}

// The tools that draw a lasso outline and drag the selected region (the Patch tool's
// machinery in canvas_widget_patch_tool.cpp): Patch and Content-Aware Move.
[[nodiscard]] constexpr bool uses_patch_drag(CanvasTool tool) noexcept {
  return tool == CanvasTool::PatchTool || tool == CanvasTool::ContentAwareMove;
}

// What the Artboard tool (and the artboard labels any tool can click) asks MainWindow to do;
// the canvas never edits the document itself (docs/artboards.md).
struct ArtboardEditRequest {
  enum class Kind {
    Select,       // make `artboard_id` the active layer
    Create,       // a new empty artboard at `rect`
    Move,         // move `artboard_id` and its content by `delta` (a copy when `duplicate`)
    Resize,       // give `artboard_id` the rect `rect`, content untouched
    AddAdjacent,  // a new artboard the size of `artboard_id` on `side` (0 right, 1 bottom, 2 left, 3 top)
    Rename        // ask for a new name for `artboard_id`
  };
  Kind kind{Kind::Select};
  std::optional<LayerId> artboard_id;
  QRect rect;
  QPoint delta;
  int side{0};
  bool duplicate{false};
  // A Move from an arrow key: a run of them is one undo step.
  bool nudge{false};
};

// Which tool produced a committed vector path; MainWindow picks the layer
// name pattern (Pen/Polygon/CustomShape all route the same way otherwise).
enum class VectorPathSource {
  Pen,
  Polygon,
  CustomShape
};

// What the Line/Rectangle/Ellipse draw tools produce: a vector shape layer
// (Photoshop's default), subpaths on the document work path, or the legacy
// raster pixels. Shape and Path drags hand the geometry to MainWindow through
// vector_shape_drawn_callback_ instead of painting.
enum class VectorToolMode {
  Shape,
  Path,
  Pixels
};

// What a Gradient tool drag produces (GitHub issue 580): painted pixels, or a
// Gradient Fill layer (a new one, or the active one rewritten). FillLayer drags
// hand their end points to MainWindow through gradient_fill_drawn_callback_.
enum class GradientToolMode {
  Pixels,
  FillLayer
};

// A Gradient tool handle gesture on an existing Gradient Fill (GitHub issue 663): live
// previews while it drags, then one Commit (one undo step) or a Cancel that restores it.
enum class GradientFillEditPhase {
  Preview,
  Commit,
  Cancel
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
  // 32-bit documents: the composite's linear-light RGB (1.0 = full), unclamped.
  std::optional<std::array<float, 3>> linear_color{};
  std::optional<QRect> active_rect{};
  QString active_rect_label{};
};

enum class CanvasReadPhase {
  Press,
  Drag,
  Release,
  Cancel,
  Dismiss
};

// Read-only, dialog-owned canvas gesture. It is intentionally separate from
// the Eyedropper tool: consumers can inspect document coordinates without
// changing the foreground color, palette state, layer revisions, or history.
struct CanvasReadGesture {
  QPoint document_position{};
  QPoint global_position{};
  Qt::KeyboardModifiers modifiers{};
  CanvasReadPhase phase{CanvasReadPhase::Press};
};

enum class LocalToneRange {
  Shadows,
  Midtones,
  Highlights
};

enum class SpongeMode {
  Saturate,
  Desaturate
};

enum class SelectionMode {
  Replace,
  Add,
  Subtract,
  Intersect
};

enum class PenHoverAction {
  Draw = 0,
  Add,
  Delete,
  Convert,
  Close,
  Continue  // an open path's end point: a click continues that path
};

// How the Crop tool draws the area outside its pending box (the Crop options
// popup, GitHub issue 326). The defaults are the original look: a black shield
// at 47% (alpha 120). Show Cropped Area off paints the outside as the backdrop.
struct CropOverlayOptions {
  bool show_cropped_area{true};
  bool shield_enabled{true};
  QColor shield_color{0, 0, 0};
  int shield_opacity_percent{47};
  friend bool operator==(const CropOverlayOptions&, const CropOverlayOptions&) = default;
};

enum class MarqueeStyle {
  Normal,
  FixedRatio,
  FixedSize
};

enum class PatchToolMode {
  Source,
  Destination
};

// Content-Aware Move (docs/content-aware-move.md): Move fills the spot the region left,
// Extend keeps it; both heal the copy into the drop point.
enum class ContentAwareMoveMode {
  Move,
  Extend
};

// Where Content-Aware Move writes (GitHub issue 599 follow-up): the active layer, or a new
// layer above it holding only the pixels the move changed (the original stays as it was).
// Values are stored as tokens (tools/contentAwareMoveDestination); append only.
enum class ContentAwareMoveDestination {
  CurrentLayer,
  NewLayer
};

// The Healing Brush's Mode (GitHub issue 381; docs/healing.md). Normal is the classic
// heal; Replace writes the source texture as sampled, without the tone transfer (Clone's
// write, keeping grain); the rest blend the healed color into the layer with that layer
// blend mode. Values index kHealingModeTokens; append only.
enum class HealingMode : std::uint8_t { Normal, Replace, Multiply, Screen, Darken, Lighten, Color, Luminosity };

// Settings tokens (`tools/healingMode`); permanent identifiers.
inline constexpr std::array<std::string_view, 8> kHealingModeTokens{
    "normal", "replace", "multiply", "screen", "darken", "lighten", "color", "luminosity"};

[[nodiscard]] constexpr std::string_view healing_mode_token(HealingMode mode) noexcept {
  return kHealingModeTokens[static_cast<std::size_t>(mode)];
}

[[nodiscard]] constexpr std::optional<HealingMode> healing_mode_from_token(std::string_view token) noexcept {
  for (std::size_t index = 0; index < kHealingModeTokens.size(); ++index) {
    if (kHealingModeTokens[index] == token) {
      return static_cast<HealingMode>(index);
    }
  }
  return std::nullopt;
}

// The layer blend mode a blending Healing mode applies; Normal for Normal and Replace.
[[nodiscard]] constexpr BlendMode healing_mode_blend(HealingMode mode) noexcept {
  switch (mode) {
    case HealingMode::Multiply:
      return BlendMode::Multiply;
    case HealingMode::Screen:
      return BlendMode::Screen;
    case HealingMode::Darken:
      return BlendMode::Darken;
    case HealingMode::Lighten:
      return BlendMode::Lighten;
    case HealingMode::Color:
      return BlendMode::Color;
    case HealingMode::Luminosity:
      return BlendMode::Luminosity;
    case HealingMode::Normal:
    case HealingMode::Replace:
      break;
  }
  return BlendMode::Normal;
}

struct MarqueeShape {
  QRect rect;             // document space, deliberately not clipped to the canvas
  bool ellipse{false};
  int corner_radius{0};   // Radius option at creation (rectangle only)
  int feather{0};
  bool antialias{true};
  bool operator==(const MarqueeShape&) const = default;
};

struct SelectionSnapshot {
  QRegion selection;
  QRegion display_region;
  QRect mask_bounds;
  QImage mask_alpha;
  std::optional<MarqueeShape> marquee_shape;
  // Quick Mask is temporary canvas state rather than document data. History
  // snapshots carry its COW buffer so a gesture can undo without copying or
  // serializing the document, and can still restore the resulting selection
  // after Quick Mask has been exited.
  std::optional<PixelBuffer> quick_mask_pixels;
};

enum class LayerEditTarget {
  Content,
  Mask,
  DocumentChannel,
  ComponentRed,
  ComponentGreen,
  ComponentBlue,
  // Temporary, full-document grayscale buffer owned by the canvas while a
  // native Smart Filter mask is being edited. The document model is updated
  // only through smart_filter_mask_committed_callback_.
  SmartFilterMask,
  // The active layer's vector mask path: the pen/shape/path tools edit it,
  // raster edits refuse (the path is the source of truth).
  VectorMask,
  // The ink plates of the working CMYK separation (docs/cmyk.md), shown while View >
  // Proof Colors is on. Read-only views like the color components.
  ComponentCyan,
  ComponentMagenta,
  ComponentYellow,
  ComponentBlack
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
  Bicubic,
  BicubicSmoother,
  BicubicSharper,
  Automatic
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

struct PaintStrokeHistoryEvent {
  enum class Phase {
    Begin,
    Finish
  };
  Phase phase{Phase::Begin};
  LayerEditTarget target{LayerEditTarget::Content};
  LayerId layer_id{0};
  ChannelId channel_id{0};
  QRect document_dirty{};
};

// Cache and interaction counters shared with stress reports without the widget class.
struct CanvasRenderCacheDiagnostics {
  int vector_preview_renders{0};
  std::uint64_t vector_preview_peak_raster_bytes{0};
  double vector_preview_elapsed_ms{0.0};
  int full_refreshes{0};
  // Background refreshes that finished while a newer one was already pending and
  // were shown as an intermediate frame (the cache stays dirty; GitHub #601).
  int intermediate_refreshes{0};
  // Document changes under an open preview dialog that refreshed in the
  // background instead of patching the canvas on the GUI thread (GitHub #601).
  int live_preview_async_refreshes{0};
  // Background preview refreshes that rendered only the region that changed
  // since the frame on screen, over a copy of it (GitHub #601 follow-up).
  int region_preview_refreshes{0};
  // Background refreshes whose display mip chain was built on the worker, so a
  // zoomed-out canvas installs the frame without a GUI-thread rebuild.
  int background_display_mips{0};
  int partial_patches{0};
  // Times a zoomed-out paint built the composite's display mip chain from the
  // full-resolution image. Partial patches update the chain in place, so a
  // brush stroke adds none (GitHub issue 165).
  int display_mip_rebuilds{0};
  int move_precommit_patches{0};
  // Move releases whose accurate patches rendered on a worker behind a hold.
  int move_deferred_commits{0};
  int forced_refreshes{0};
  int dirty_region_batches{0};
  int dirty_region_rects{0};
  std::uint64_t dirty_region_pixels{0};
  int move_preview_patch_reuses{0};
  int processing_overlays_shown{0};
  // Renders the GUI thread waited on (wait_for_processing_operation). An interactive preview
  // such as a Layer Style slider drag must add none.
  int awaited_renders{0};
  int processing_overlay_frames{0};
  // Times a move drag latched onto the translated-snapshot proxy preview
  // because its summed per-layer work area crossed the
  // kMoveOutlineDirtyAreaThreshold / kStyledMoveOutlineDirtyAreaThreshold
  // limits. At most once per drag.
  int move_proxy_previews{0};
  // Times a move drag composited its previews from the preview-scaled
  // document (display-resolution compositing at zoom <= 50%). At most once
  // per drag.
  int move_scaled_previews{0};
  // Times a move press reused the base cache + proxy snapshot retained from
  // the previous commit of the same selection (skipping both rebuilds). At
  // most once per drag.
  int move_preview_cache_reuses{0};
  // Times a move drag fell back to the dashed-outline preview: the work area
  // crossed the same limits but the proxy snapshot could not be built (or
  // would exceed kMoveProxyLastResortSnapshotArea). At most once per drag.
  int move_outline_previews{0};
  // Times a free-transform drag latched onto the low-res proxy preview
  // because its transformed area crossed the kTransformProxyAreaThreshold /
  // kStyledTransformProxyAreaThreshold limits, or a live composited-preview
  // frame ran over the live_preview_frame_latch_ms escape hatch. At most
  // once per drag.
  int transform_proxy_previews{0};
  // Times a free-transform drag of a clipping base drew the clip-group blit
  // (the transformed base plus the cached clipped members, GitHub #398)
  // instead of compositing patches per mouse-move. At most once per drag.
  int transform_clip_group_previews{0};
  // Times a transform session built its base cache from the preview-scaled
  // document (display-resolution compositing at zoom <= 50%).
  int transform_scaled_bases{0};
  // Warp Transform preview renders composited from the preview-scaled document
  // (zoom <= 50%), and the output pixel count of the latest preview render at its
  // own level (GitHub #200: bounded by the view, never the whole layer).
  int warp_scaled_previews{0};
  std::uint64_t warp_preview_last_pixels{0};
  // The latest preview render's (pixel, cell) inverse tests (TransformedImage::
  // cell_pixel_tests): bounded by a small multiple of its output pixels however far a
  // handle stretches the cells (GitHub issue 299).
  std::uint64_t warp_preview_last_cell_tests{0};
  // Every Warp Transform preview render. A drag renders once per painted frame,
  // however many mouse moves queued up before it (GitHub #298).
  int warp_preview_renders{0};
  // Every Path Select / Direct Select drag step that rewrites a path (and re-bakes a
  // shape or vector mask). A drag applies once per batch of queued mouse moves at
  // the latest pointer position (GitHub #314).
  int path_drag_applies{0};
  // Shape-layer re-rasterizations made by write_shape_layer_path (a Path Select or
  // Direct Select drag step, or any other path edit of a shape layer). A Path Select
  // drag that only translates whole shapes re-rasterizes once, at release (GitHub #678).
  int path_drag_rerasters{0};
  // Move-tool and Free Transform drag moves that arrived while the drag had already
  // been applied in the current event-loop pass, so only their latest position is
  // applied when the pass ends (GitHub #207, #443, #444).
  int drag_moves_coalesced{0};
  // Retouch presses (Clone, Healing, Spot Healing, Patch) whose All Layers snapshot
  // shared the settled render cache instead of rendering the document again (GitHub #597).
  int retouch_snapshot_cache_reuses{0};
  // Times a render-cache patch had to copy the whole cache first because another image
  // shared it, and the time those copies took (GitHub #597 follow-up 3).
  int render_cache_patch_detaches{0};
  double render_cache_patch_detach_ms{0.0};
  // Source tiles a Clone or Healing stroke copied from the shared render cache before
  // it read them or a patch overwrote them (GitHub #597 follow-up 3).
  int retouch_source_tiles_filled{0};
  // Toning and detail brush segments (Dodge, Burn, Sponge, Blur, Sharpen, Smudge) painted, the
  // pixels they visited and the time they took (patchy_perf_tests toningbig, GitHub #660).
  int toning_segments{0};
  std::int64_t toning_segment_pixels{0};
  double toning_segment_ms{0.0};
  // Spaced Brush dabs (reduced Flow, Airbrush) and Airbrush build-up ticks painted, with the
  // time they took (patchy_perf_tests airbrushbig, GitHub #677).
  int brush_dabs{0};
  double brush_dab_ms{0.0};
  int airbrush_ticks{0};
  double airbrush_tick_ms{0.0};
};

// Where the latest Clone or Healing press spent its time, through its first dab and the
// first frame's refresh request (patchy_perf_tests clonepress, GitHub #597 follow-up 3).
struct RetouchPressTimings {
  double begin_edit_ms{0.0};       // begin_edit: the undo snapshot
  double history_ms{0.0};          // arm_paint_stroke_history: growth reserve, stroke history
  double source_ms{0.0};           // the source snapshot (8-bit and deep)
  double setup_ms{0.0};            // stroke state: mapping, tracking reset, healing originals
  double first_dab_ms{0.0};        // the first stamp, the layer detach included
  double layer_detach_ms{0.0};     // the first mutable access to the layer's pixels
  double preview_ms{0.0};          // active_edit_target_changed_impl for the first dab
};

// A resampled image in document space: the result of CanvasWidget's `resample_transformed_rgba8`
// and `resample_warped_rgba8` (ui/canvas_widget.hpp) and of the smart-object renderer.
struct TransformedImage {
  QImage image;
  Rect bounds{};
  // resample_warped_rgba8 only: (pixel, cell) inverse tests the walk ran, the warp
  // renderer's work measure (GitHub issue 299: a stretched cell must not test its
  // whole bounding box).
  std::uint64_t cell_pixel_tests{0};
};

}  // namespace patchy::ui
