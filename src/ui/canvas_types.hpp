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

namespace patchy::ui {

inline constexpr std::size_t kSelectionToolCount = 9;

inline constexpr int kMaxBrushSize = 1024;

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
  PolygonLasso
};

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

enum class MarqueeStyle {
  Normal,
  FixedRatio,
  FixedSize
};

enum class PatchToolMode {
  Source,
  Destination
};

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
  // Times a transform session built its base cache from the preview-scaled
  // document (display-resolution compositing at zoom <= 50%).
  int transform_scaled_bases{0};
  // Warp Transform preview renders composited from the preview-scaled document
  // (zoom <= 50%), and the output pixel count of the latest preview render at its
  // own level (GitHub #200: bounded by the view, never the whole layer).
  int warp_scaled_previews{0};
  std::uint64_t warp_preview_last_pixels{0};
  // Every Warp Transform preview render. A drag renders once per painted frame,
  // however many mouse moves queued up before it (GitHub #298).
  int warp_preview_renders{0};
};

}  // namespace patchy::ui
