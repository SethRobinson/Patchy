#pragma once

#include "ui/script_stroke.hpp"
#include "ui/canvas_types.hpp"
#include "ui/coalesced_pointer_input.hpp"

#include "color/color_management.hpp"

#include "core/artboard.hpp"
#include "core/document.hpp"
#include "core/exemplar_inpaint.hpp"
#include "core/layer_alignment.hpp"
#include "core/layer_tree.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/pattern_resource.hpp"
#include "core/pixel_tools.hpp"
#include "core/spot_heal.hpp"
#include "core/stroke_stabilizer.hpp"
#include "core/warp_mesh.hpp"
#include "ui/curves_clipping_preview.hpp"
#include "ui/image_document_io.hpp"
#include "ui/measurement_units.hpp"
#include "ui/selection_outline.hpp"
#include "ui/stroke_pixel_map.hpp"
#include "ui/vector_preview_renderer.hpp"

#include <QBasicTimer>
#include <QBrush>
#include <QColor>
#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPolygon>
#include <QRect>
#include <QRectF>
#include <QTransform>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QStringList>
#ifdef PATCHY_GPU_CANVAS
#include <QOpenGLWidget>
#endif
#include <QWidget>

#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <map>
#include <set>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <future>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QAction;
class QPainter;
class QMenu;
class QEvent;
class QResizeEvent;
class QScrollBar;
class QTabletEvent;

namespace patchy {
struct PreviewScaleCache;
enum class LiveShapeKind : std::uint8_t;
enum class PathCombineOp : std::uint8_t;
struct PathAnchor;
struct PathSubpath;
struct VectorPath;
}

namespace patchy::ai {
class InferenceCancel;
struct SamEmbedding;
}

namespace patchy::ui {

// The non-default part of a linked raster mask, cropped out once per layer
// content revision so a Free Transform preview frame resamples only the
// painted area and never rescans the stored buffer (canvas_widget_transform.cpp).
struct TransformLinkedMaskSource {
  std::uint64_t content_revision{0};
  Rect bounds{};  // document rect of `pixels`
  std::uint8_t default_color{255};
  PixelBuffer pixels{};  // copy-on-write; empty when the mask is uniformly its default
};

// What a Move-tool click or selection rectangle picks for the artwork under it
// (Photoshop's Auto-Select Layer/Group choice): the leaf itself, or the outermost
// group holding it (inside an artboard, the outermost group within it).
enum class MoveAutoSelectTarget : std::uint8_t { Layer, Group };
// Persisted under this settings key as "layer" or "group": permanent tokens. An
// unknown token reads as Layer.
inline constexpr char kMoveAutoSelectTargetSettingsKey[] = "tools/moveAutoSelectTarget";
[[nodiscard]] QString move_auto_select_target_token(MoveAutoSelectTarget target);
[[nodiscard]] MoveAutoSelectTarget move_auto_select_target_from_token(const QString& token);

#ifdef PATCHY_GPU_CANVAS
class CanvasWidget final : public QOpenGLWidget {
#else
class CanvasWidget final : public QWidget {
#endif
  Q_OBJECT

public:
  using LocalToneRange = patchy::ui::LocalToneRange;

  using SpongeMode = patchy::ui::SpongeMode;

  using SelectionMode = patchy::ui::SelectionMode;

  // Pen hover context: what a left click would do at the hovered point. The
  // values are contiguous; the cursors TU indexes a per-state cache by them.
  using PenHoverAction = patchy::ui::PenHoverAction;

  using MarqueeStyle = patchy::ui::MarqueeStyle;

  // Patch tool drag semantics (Photoshop parity). Source: the dragged-FROM
  // region is healed with the dragged-TO pixels. Destination: a copy of the
  // region is dragged to a new spot and healed in there.
  using PatchToolMode = patchy::ui::PatchToolMode;

  // The shape a Rectangular or Elliptical Marquee drag-out committed, kept so
  // its edges and corners can be dragged afterwards (the selection is then
  // re-rasterized from this rather than scaled). Set only by a Replace-mode
  // marquee commit; moves translate it; every other selection write clears it.
  using MarqueeShape = patchy::ui::MarqueeShape;

  // Full selection state, captured so selection edits (marquee/lasso/wand drags,
  // Select All, Deselect, Invert, ...) can participate in the undo/redo history.
  using SelectionSnapshot = patchy::ui::SelectionSnapshot;

  // Tools whose combine mode (New/Add/Subtract/Intersect) is tracked separately,
  // so switching between e.g. Lasso and Marquee preserves each tool's own mode.
  static constexpr std::size_t kSelectionToolCount = patchy::ui::kSelectionToolCount;
  [[nodiscard]] static int selection_tool_index(CanvasTool tool) noexcept;

  using LayerEditTarget = patchy::ui::LayerEditTarget;
  // A read-only grayscale view of one color component or ink plate of the composite.
  [[nodiscard]] static constexpr bool is_component_preview(LayerEditTarget target) noexcept {
    return target == LayerEditTarget::ComponentRed || target == LayerEditTarget::ComponentGreen ||
           target == LayerEditTarget::ComponentBlue || is_cmyk_plate(target);
  }
  [[nodiscard]] static constexpr bool is_cmyk_plate(LayerEditTarget target) noexcept {
    return target == LayerEditTarget::ComponentCyan || target == LayerEditTarget::ComponentMagenta ||
           target == LayerEditTarget::ComponentYellow || target == LayerEditTarget::ComponentBlack;
  }

  using MaskDisplayMode = patchy::ui::MaskDisplayMode;

  using DocumentChangeReason = patchy::ui::DocumentChangeReason;

  // Persisted as an int (`tools/transformInterpolation`): append only. Automatic
  // resolves per resample from the output area, like Image Size (docs/resampling.md).
  using TransformInterpolation = patchy::ui::TransformInterpolation;

  struct TransformControlsState {
    bool active{false};
    CanvasAnchor reference_point{CanvasAnchor::Center};
    QPointF reference_position{};
    double scale_x_percent{100.0};
    double scale_y_percent{100.0};
    double rotation_degrees{0.0};
    TransformInterpolation interpolation{TransformInterpolation::Bicubic};
    // Extent the scale percentages are relative to (the session's original rect, or
    // the passive box itself), so unit entry can convert "200 px" into a percent.
    QSizeF original_size{};
  };

  using RenderCacheDiagnostics = CanvasRenderCacheDiagnostics;

  using PenInputSettings = patchy::ui::PenInputSettings;

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
  // set_document for the same document after its canvas grew by an artboard edit
  // (docs/artboards.md): the old frame, moved by `document_shift`, stays on screen as the
  // stale frame the refresh replaces, instead of a blank canvas.
  void set_document_after_canvas_growth(Document* document, QPoint document_shift);
  [[nodiscard]] bool pointer_gesture_active() const noexcept;
  [[nodiscard]] double zoom() const noexcept;
  void set_zoom(double zoom);
  // The zoom the user sees: document pixels per DEVICE pixel, so 100% is one
  // document pixel per screen pixel on every display (Photoshop's rule; GitHub
  // issue 75). zoom() stays the logical, widget-unit scale that every geometry
  // caller uses; the two differ by devicePixelRatioF() on a scaled display.
  // Readouts, presets, the status box and scripting speak view zoom.
  [[nodiscard]] double view_zoom() const noexcept;
  void set_view_zoom(double view_zoom);
  void set_view_zoom_centered(double view_zoom);
  // Absolute zoom anchored at the viewport center, Photoshop-style: the anchor
  // is clamped to the document bounds (so a view left off-center never pins
  // grey margin), then per axis the document is centered when it fits the
  // viewport and clamped to show no grey past its edges when it overflows. UI
  // zoom presets (menu Zoom In/Out, Actual Pixels, the zoom-tool double-click,
  // the status-bar zoom box) must use this (through set_view_zoom_centered)
  // instead of set_zoom, which preserves pan and can leave the canvas mostly
  // off screen.
  void set_zoom_centered(double zoom);
  // In-canvas seamless tiling mode (View > Seamless Tiling in Window): paints wrap
  // copies of the committed composite around the document so tile seams are visible
  // and update live while painting. Overlays/gizmos and session previews stay on the
  // center tile by design. Per-canvas (per-document) state, default off.
  void set_tiling_preview_enabled(bool enabled);
  [[nodiscard]] bool tiling_preview_enabled() const noexcept;
  void zoom_at_widget_point(QPointF widget_position, double factor);
  void set_wheel_zooms(bool enabled) noexcept;
  [[nodiscard]] bool wheel_zooms() const noexcept;
  // Wheel input comes in two kinds (docs/view-navigation.md): a continuous two-finger
  // scroll (it carries a scroll phase) always pans in 2D, a stepped wheel follows
  // wheel_zooms(). wheel_zoom_factor is the zoom step for one event of either kind.
  [[nodiscard]] static bool wheel_event_is_continuous_scroll(const QWheelEvent& event) noexcept;
  [[nodiscard]] static double wheel_zoom_factor(const QWheelEvent& event) noexcept;
  // Scrubby Zoom (docs/view-navigation.md): a Zoom tool drag zooms live about
  // the press point instead of drawing a marquee. Persisted by MainWindow as
  // tools/zoomScrubby; default off.
  void set_zoom_scrubby(bool enabled) noexcept;
  [[nodiscard]] bool zoom_scrubby() const noexcept;
  // Zoom tool click direction (the options-bar Zoom In / Zoom Out toggle,
  // tools/zoomToolZoomsOut). Alt inverts whichever direction is active.
  void set_zoom_tool_zooms_out(bool enabled);
  [[nodiscard]] bool zoom_tool_zooms_out() const noexcept;
  // Zooms so the document covers the viewport (the larger axis ratio, where
  // fit_to_view uses the smaller) and centers it.
  void fill_to_view();
  void refresh_tool_cursor();
  // View > Fit on Screen: the whole document in the viewport, zooming in when it is small.
  void fit_to_view();
  // The view a document opens with (File > Open and every other opening or new-document
  // path): a document larger than the viewport is fitted, a smaller one shows at 100% view
  // zoom, centered, as in other editors (GitHub issue 247).
  void fit_to_view_without_enlarging();
  // Recenters the document in the viewport at the current zoom. Used after
  // operations that change document geometry (crop, image/canvas resize,
  // canvas rotate), where the stale pan could otherwise leave the remaining
  // image mostly off screen.
  void center_document_in_view();
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
  void set_document_channel_edit_target(ChannelId id, MaskDisplayMode mode = MaskDisplayMode::Grayscale);
  void set_component_channel_preview(LayerEditTarget component);
  [[nodiscard]] std::optional<ChannelId> active_document_channel_id() const noexcept;
  [[nodiscard]] bool editing_document_channel() const noexcept;
  [[nodiscard]] bool document_channel_is_editable() const noexcept;
  void set_mask_display_mode(MaskDisplayMode mode);
  [[nodiscard]] MaskDisplayMode mask_display_mode() const noexcept;
  void invalidate_mask_display();
  void set_auto_select_layer(bool enabled) noexcept;
  [[nodiscard]] bool auto_select_layer() const noexcept;
  void set_auto_select_target(MoveAutoSelectTarget target) noexcept;
  [[nodiscard]] MoveAutoSelectTarget auto_select_target() const noexcept;
  // The layers (or groups, under MoveAutoSelectTarget::Group) a Move-tool
  // selection rectangle would select if released now, in release order; empty
  // when no rectangle is being dragged.
  [[nodiscard]] std::vector<LayerId> move_layer_selection_preview_ids() const;
  void set_primary_color(QColor color);
  [[nodiscard]] QColor primary_color() const noexcept;
  void set_secondary_color(QColor color);
  [[nodiscard]] QColor secondary_color() const noexcept;
  // Palette-mode write constraint for the current document; null when palette mode
  // is off. palette_snap_for_edits() additionally returns null while a layer MASK
  // is the edit target (masks are grayscale coverage, never palette colors). The
  // cached LUT rebuilds when the document's palette_revision changes.
  [[nodiscard]] const PaletteSnapContext* palette_snap_context() const;
  [[nodiscard]] const PaletteSnapContext* palette_snap_for_edits() const;
  // Palette-mode WYSIWYG: snaps a display composite to the palette (hard 0/255
  // alpha) so the canvas shows exactly what indexed export produces, while layer
  // styles, text, and blend modes stay live in the document. No-op when palette
  // mode is off. Applied wherever images land in render_cache_.
  void quantize_image_for_palette_display(QImage& image) const;
  void set_brush_size(int size);
  // Uses the native brush renderer; the caller owns validation, undo, and refresh.
  QRect paint_script_stroke(const ScriptStroke& stroke, const std::function<bool(const QRect&)>& progress = {});
  [[nodiscard]] ScriptStroke current_script_brush() const;
  void apply_script_brush(const ScriptStroke& settings);
  [[nodiscard]] int brush_size() const noexcept;
  void set_brush_opacity(int opacity);
  [[nodiscard]] int brush_opacity() const noexcept;
  void set_brush_flow(int flow);
  [[nodiscard]] int brush_flow() const noexcept;
  void set_brush_softness(int softness);
  [[nodiscard]] int brush_softness() const noexcept;
  // Historical name retained for callers and tests. This is the Brush tool's
  // Airbrush toggle: while enabled, stationary timer dabs build toward Opacity
  // at the selected Flow rate.
  void set_brush_build_up(bool build_up) noexcept;
  [[nodiscard]] bool brush_build_up() const noexcept;
  void set_mixer_wet(int wet) noexcept;
  [[nodiscard]] int mixer_wet() const noexcept;
  void set_mixer_load(int load) noexcept;
  [[nodiscard]] int mixer_load() const noexcept;
  void set_mixer_mix(int mix) noexcept;
  [[nodiscard]] int mixer_mix() const noexcept;
  void set_mixer_flow(int flow) noexcept;
  [[nodiscard]] int mixer_flow() const noexcept;
  void set_mixer_sample_all_layers(bool sample_all_layers) noexcept;
  [[nodiscard]] bool mixer_sample_all_layers() const noexcept;
  // The Brush tool's Mode (core/paint_mode.hpp). Only Brush strokes on pixel layers
  // use it; every other tool and the mask/channel targets paint Normal.
  void set_brush_paint_mode(PaintMode mode) noexcept;
  [[nodiscard]] PaintMode brush_paint_mode() const noexcept;
  // Photoshop-style stroke Smoothing for Brush, Mixer Brush, and Eraser
  // strokes: the percent (0-100) becomes the stabilizer leash radius through
  // stroke_stabilizer_leash_radius(); 0 is exact pass-through.
  void set_brush_smoothing(int percent) noexcept;
  [[nodiscard]] int brush_smoothing() const noexcept;
  void set_brush_smoothing_pulled_string(bool enabled) noexcept;
  [[nodiscard]] bool brush_smoothing_pulled_string() const noexcept;
  void set_brush_smoothing_catch_up(bool enabled) noexcept;
  [[nodiscard]] bool brush_smoothing_catch_up() const noexcept;
  void set_brush_smoothing_catch_up_end(bool enabled) noexcept;
  [[nodiscard]] bool brush_smoothing_catch_up_end() const noexcept;
  void set_brush_smoothing_zoom_adjust(bool enabled) noexcept;
  [[nodiscard]] bool brush_smoothing_zoom_adjust() const noexcept;
  // Bitmap brush tip for Brush, Mixer Brush, Pattern Stamp, and Eraser;
  // null tip = procedural round/soft brush.
  // The id is an opaque library key kept here so the options bar and settings stay in sync.
  void set_brush_tip(std::shared_ptr<const patchy::BrushTip> tip, const QString& tip_id);
  [[nodiscard]] const QString& brush_tip_id() const noexcept;
  [[nodiscard]] bool has_brush_tip() const noexcept;
  // Procedural footprint painted while no bitmap tip is set (Brush, Eraser). A hard,
  // unrotated Square snaps to the pixel grid; see square_brush_coverage in core/pixel_tools.
  void set_brush_shape(patchy::BrushShape shape);
  [[nodiscard]] patchy::BrushShape brush_shape() const noexcept;
  // Per-dab tip dynamics + static tip shape, applied per tip by MainWindow (bitmap tips read
  // them from the library entry; the Round brush carries session-only values). Dynamics only
  // affect Brush strokes: erase strokes strip them, and a dynamics-active Round brush stamps
  // through a synthesized disc tip since the capsule renderer has no dab loop.
  void set_brush_dynamics(const patchy::BrushDynamics& dynamics) noexcept;
  [[nodiscard]] const patchy::BrushDynamics& brush_dynamics() const noexcept;
  void set_brush_base_shape(double angle_degrees, int roundness) noexcept;
  [[nodiscard]] double brush_base_angle_degrees() const noexcept;
  [[nodiscard]] int brush_base_roundness() const noexcept;
  // UI-test hook: fixes the per-stroke dynamics RNG seed so stroke artifacts are reproducible.
  void set_brush_dynamics_test_seed(std::optional<quint32> seed) noexcept;
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
  void set_pattern_stamp_pattern(std::optional<PatternResource> pattern);
  [[nodiscard]] const std::optional<PatternResource>& pattern_stamp_pattern() const noexcept;
  void set_pattern_stamp_aligned(bool aligned) noexcept;
  [[nodiscard]] bool pattern_stamp_aligned() const noexcept;
  void set_healing_diffusion(int diffusion) noexcept;
  [[nodiscard]] int healing_diffusion() const noexcept;
  void set_retouch_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool retouch_sample_all_layers() const noexcept;
  void set_patch_tool_mode(PatchToolMode mode) noexcept;
  [[nodiscard]] PatchToolMode patch_tool_mode() const noexcept;
  void set_patch_tool_transparent(bool enabled) noexcept;
  [[nodiscard]] bool patch_tool_transparent() const noexcept;
  void set_local_adjustment_strength(int strength) noexcept;
  [[nodiscard]] int local_adjustment_strength() const noexcept;
  void set_local_tone_range(LocalToneRange range) noexcept;
  [[nodiscard]] LocalToneRange local_tone_range() const noexcept;
  void set_local_protect_tones(bool enabled) noexcept;
  [[nodiscard]] bool local_protect_tones() const noexcept;
  void set_sponge_mode(SpongeMode mode) noexcept;
  [[nodiscard]] SpongeMode sponge_mode() const noexcept;
  void set_sponge_vibrance(bool enabled) noexcept;
  [[nodiscard]] bool sponge_vibrance() const noexcept;
  // Eyedropper options (GitHub issue 151), shared by the paint tools' Alt
  // eyedropper as in Photoshop: the side of the averaged square in document
  // pixels (1 = Point Sample; Photoshop's 3, 5, 11, 31, 51 and 101), and
  // which layers a pick reads (Sample; GitHub issue 321 added Current & Below
  // and the No Adjustments choices).
  void set_eyedropper_sample_size(int size) noexcept;
  [[nodiscard]] int eyedropper_sample_size() const noexcept;
  void set_eyedropper_sample_layers(SampleLayers mode) noexcept;
  [[nodiscard]] SampleLayers eyedropper_sample_layers() const noexcept;
  // The Eyedropper's Sets option: which color a click sets. Alt inverts it for
  // that click, as Alt inverts the Zoom tool's direction (GitHub issue 151).
  void set_eyedropper_picks_background(bool background);
  [[nodiscard]] bool eyedropper_picks_background() const noexcept;
  [[nodiscard]] bool eyedropper_target_is_background(Qt::KeyboardModifiers modifiers) const noexcept;
  // The Pick tool's sample-square outline at the pointer in widget coordinates
  // (GitHub issue 319), or nothing when it is hidden: Point Sample, a square under
  // kEyedropperSampleOutlineMinScreenPixels on screen, or the pointer off the document.
  [[nodiscard]] std::optional<QRect> eyedropper_sample_outline_widget_rect() const;
  // The sample loupe while a pick is held (GitHub issue 320; the options bar's
  // "Show Sample Ring", on by default): the Pick tool and the paint tools' Alt
  // eyedropper. Its painted area in widget coordinates, or nothing when hidden.
  void set_eyedropper_show_sample_ring(bool enabled);
  [[nodiscard]] bool eyedropper_show_sample_ring() const noexcept;
  [[nodiscard]] std::optional<QRect> sample_loupe_widget_rect() const;
  // The loupe's two arcs: the live sample and the target color when the press began.
  [[nodiscard]] std::optional<std::pair<QColor, QColor>> sample_loupe_colors() const;
  void set_pen_input_settings(PenInputSettings settings) noexcept;
  [[nodiscard]] const PenInputSettings& pen_input_settings() const noexcept;
  [[nodiscard]] std::optional<PenInputSample> last_pen_input_sample() const;
  void set_wand_tolerance(int tolerance);
  [[nodiscard]] int wand_tolerance() const noexcept;
  void set_wand_contiguous(bool enabled) noexcept;
  [[nodiscard]] bool wand_contiguous() const noexcept;
  void set_wand_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool wand_sample_all_layers() const noexcept;
  void set_quick_select_size(int size);
  [[nodiscard]] int quick_select_size() const noexcept;
  void set_quick_select_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool quick_select_sample_all_layers() const noexcept;
  void set_object_select_sample_all_layers(bool enabled) noexcept;
  [[nodiscard]] bool object_select_sample_all_layers() const noexcept;
  // Encoder runs the Object Selection tool has started (tests check the embedding is reused).
  [[nodiscard]] int object_select_encodes_started() const noexcept;
  // Combines an automatic-selection coverage mask (document coordinates, Grayscale8) with
  // the selection as one undo entry. `operation` is the plain boolean (Replace, Add,
  // Subtract, Intersect); the current Feather applies, and Anti-alias off thresholds the
  // mask at 50%. Used by Select > Subject, Object Selection and the script API.
  void apply_automatic_selection_mask(QRect bounds, QImage alpha, SelectionMode operation, QString history_label);
  // Select Object (selection right-click menu): the selection's bounding box as one Object
  // Selection box prompt; the object found replaces the selection. False on failure.
  bool select_object_in_selection();
  // What automatic selection analyzes (docs/ai-selection.md): the merged document with
  // `sample_all_layers`, otherwise the active layer rendered alone
  // (automatic_selection_layer_image). Null with `error` set when there is nothing to sample.
  [[nodiscard]] QImage automatic_selection_source(bool sample_all_layers, QString* error);
  void set_quick_select_enhance_edge(bool enabled) noexcept;
  [[nodiscard]] bool quick_select_enhance_edge() const noexcept;
  // Magnetic Lasso options (Photoshop parity): Width = edge search diameter in document px,
  // Edge Contrast = minimum gradient percentage that counts as an edge, Frequency = how
  // eagerly anchors drop while tracing.
  void set_magnetic_lasso_width(int width) noexcept;
  [[nodiscard]] int magnetic_lasso_width() const noexcept;
  void set_magnetic_lasso_edge_contrast(int contrast) noexcept;
  [[nodiscard]] int magnetic_lasso_edge_contrast() const noexcept;
  void set_magnetic_lasso_frequency(int frequency) noexcept;
  [[nodiscard]] int magnetic_lasso_frequency() const noexcept;
  // Live trace state (used by tests and the cancel sites).
  [[nodiscard]] bool magnetic_lasso_active() const noexcept;
  [[nodiscard]] int magnetic_lasso_anchor_count() const noexcept;
  void cancel_magnetic_lasso();
  // Polygonal Lasso session state (used by tests and the cancel sites). The Lasso's
  // Alt-release switch runs the same session, so these report it too.
  [[nodiscard]] bool polygon_lasso_active() const noexcept;
  [[nodiscard]] int polygon_lasso_vertex_count() const noexcept;
  [[nodiscard]] QPolygon polygon_lasso_outline() const;  // placed outline, no rubber band
  [[nodiscard]] QPoint polygon_lasso_rubber_band_end() const noexcept;
  void cancel_polygon_lasso();
  void set_show_transform_controls(bool enabled) noexcept;
  [[nodiscard]] bool show_transform_controls() const noexcept;
  // Live readout beside the pointer during Move and Free Transform drags
  // (Photoshop's "Show Transformation Values"): position of the reference point
  // plus the delta while moving, W x H plus percentages while scaling, the angle
  // plus its delta while rotating. Application preference, pushed in by
  // MainWindow like the Shift-aspect pairing.
  struct DragReadout {
    QStringList lines;         // full text: current values plus the change (status bar)
    QStringList canvas_lines;  // what the on-canvas panel shows: the change only
  };
  [[nodiscard]] std::optional<DragReadout> transform_drag_readout() const;
  // Widget-space panel rect of the readout; empty when nothing is shown.
  [[nodiscard]] QRect drag_readout_widget_rect() const;

  // One axis of a Move-drag snap: which target line the moving set landed on,
  // where it is in document space, and the two extents an alignment guide
  // bridges (docs/alignment.md). Guides draw themselves and grid snaps draw
  // nothing, so only Document / Selection / Layer matches paint a line.
  struct SnapMatch {
    enum class Kind { Guide, Grid, Document, Selection, Layer };
    Kind kind{Kind::Document};
    double position{0.0};   // document coordinate of the matched line
    QRectF source_span{};   // union of the moving rects carrying the matched feature
    QRectF target_span{};   // the matched layer rect, selection rect, or document rect
  };
  struct MoveSnapResult {
    QPoint delta{};
    std::optional<SnapMatch> x{};
    std::optional<SnapMatch> y{};
  };
  // One snap target line: its document position, the extent it belongs to
  // (for the alignment guide), and what kind of target it is.
  struct SnapCandidate {
    double position{0.0};
    QRectF span{};
    SnapMatch::Kind kind{SnapMatch::Kind::Document};
  };
  // The alignment guides currently shown for the live Move drag (x = vertical
  // line, y = horizontal line); both empty outside a snapped drag.
  [[nodiscard]] const std::optional<SnapMatch>& move_snap_match_x() const noexcept { return move_snap_x_; }
  [[nodiscard]] const std::optional<SnapMatch>& move_snap_match_y() const noexcept { return move_snap_y_; }

  // One Align/Distribute unit: a selected root (a folder moves as one block)
  // with the movable leaves under it and the union of their Move rects.
  struct AlignmentUnit {
    LayerId root{};
    std::vector<LayerId> leaf_ids{};
    Rect bounds{};
  };
  // Units for `root_ids` (normalized through root_drop_layer_ids; empty means
  // the canvas's layer selection with the movable_layer_ids fallbacks). Roots
  // with no movable leaf or no measurable rect are dropped.
  [[nodiscard]] std::vector<AlignmentUnit> alignment_units(const std::vector<LayerId>& root_ids) const;
  // How many units alignment_units(root_ids) would return, without measuring
  // rects (the enable-state refresh calls this on every selection change).
  [[nodiscard]] int alignment_unit_count(const std::vector<LayerId>& root_ids) const;
  struct LayerAlignmentResult {
    int unit_count{0};
    int moved_layers{0};
    QRegion dirty{};
  };
  // Layer > Arrange > Align: lines the units' `edge` up with the reference rect
  // (the selection bounds when one exists and `align_to_canvas` is false, the
  // document when `align_to_canvas` is set or only one unit exists, else the
  // units' union). One "Align Layers" history entry unless `record_history` is
  // false (the script route rides the run's snapshot). Callers repaint `dirty`.
  [[nodiscard]] LayerAlignmentResult align_layers(AlignEdge edge, bool align_to_canvas,
                                                  const std::vector<LayerId>& root_ids,
                                                  bool record_history = true);
  // Layer > Arrange > Distribute over three or more units; see
  // compute_distribute_deltas for the layout rules.
  [[nodiscard]] LayerAlignmentResult distribute_layers(DistributeMode mode, const std::vector<LayerId>& root_ids,
                                                       bool record_history = true);
  void set_show_transform_drag_values(bool enabled) noexcept;
  [[nodiscard]] bool show_transform_drag_values() const noexcept;
  // Numeric Free Transform entries land on whole pixels like Photoshop's "Snap Vector Tools
  // and Transforms to Pixel Grid"; off keeps the typed fraction and resamples sub-pixel.
  void set_snap_transforms_to_pixel_grid(bool enabled) noexcept;
  [[nodiscard]] bool snap_transforms_to_pixel_grid() const noexcept;
  void set_fill_shapes(bool fill_shapes) noexcept;
  [[nodiscard]] bool fill_shapes() const noexcept;
  void set_shape_corner_radius(int radius) noexcept;
  [[nodiscard]] int shape_corner_radius() const noexcept;
  void set_fill_opacity(int opacity) noexcept;
  [[nodiscard]] int fill_opacity() const noexcept;
  void set_fill_softness(int softness) noexcept;
  [[nodiscard]] int fill_softness() const noexcept;
  // Fill tool color tolerance (0..255, the Magic Wand's metric) and Contiguous; the Fill
  // command ignores both (it fills the whole selection).
  void set_fill_tolerance(int tolerance) noexcept;
  [[nodiscard]] int fill_tolerance() const noexcept;
  void set_fill_contiguous(bool enabled) noexcept;
  [[nodiscard]] bool fill_contiguous() const noexcept;
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
  // Built display mip levels (0 when the chain was dropped), and whether every built
  // level equals one rebuilt from the render cache now (patch_display_mip_cache exactness).
  [[nodiscard]] int display_mip_levels_for_testing() const noexcept;
  [[nodiscard]] bool display_mips_match_rebuild_for_testing() const;
  // Run a selection-changing command (Select All, Deselect, Invert, ...) and push
  // an undo entry for it if the selection actually changed.
  void run_selection_command(QString label, const std::function<void()>& command);
  // Edit > Remove Object: fill the current selection from its surroundings
  // (canvas_widget_spot_healing.cpp). ContentAware is the exhaustive exemplar
  // fill of core/exemplar_inpaint.hpp (deterministic per variation; falls
  // back to NearestEdge when no clean source patch is in reach). NearestEdge
  // is the selection form of Spot Healing: one shape-derived mirror or shift.
  enum class RemoveObjectMethod { ContentAware, NearestEdge };
  struct RemoveObjectOptions {
    RemoveObjectMethod method{RemoveObjectMethod::ContentAware};
    // NearestEdge: < 0 continues the canvas's own cycle (running again on the
    // same selection walks the geometry-only candidates), >= 0 forces that
    // candidate (wrapping) and becomes the cycle's position. ContentAware:
    // the variation, where 0 (or any negative value) is the byte-stable
    // best-match fill and N > 0 is variation N (a deterministic near-best
    // pick per patch; see core/exemplar_inpaint.hpp).
    int attempt{-1};
    // ContentAware only: strength of the tone match that follows the fill,
    // 0 (the raw exemplar fill, the default) to 100 (the full low-pass
    // replacement).
    int tone_match{0};
    // Extra edge feather in pixels, grown outward from the selection (a
    // gaussian of this sigma over the coverage) on top of the selection's own
    // feather: the fill covers the widened footprint and the soft skirt
    // blends its edge. 0 keeps the selection's coverage as is.
    int feather{0};
    // false runs the pixel-edit prechecks without the history push, for
    // callers that already own an undo snapshot (the script API) or that
    // preview into the layer and push on accept (the Remove Object dialog).
    bool record_history{true};
  };
  struct RemoveObjectResult {
    bool applied{false};
    RemoveObjectMethod method{RemoveObjectMethod::ContentAware};  // the method that ran (fallback included)
    int source_index{0};  // NearestEdge: 1-based candidate that was used
    int source_count{0};  // NearestEdge: candidates available
    int attempt{0};       // ContentAware: the variation that ran (0 = best match)
    std::int64_t patches{0};  // ContentAware: source patches copied
    QString error;  // the refusal (also reported to the status bar) when !applied
  };
  RemoveObjectResult remove_object_in_selection(const RemoveObjectOptions& options);
  // The same run in stages, so a host can keep the UI thread free: prepare
  // (UI thread: the prechecks, the padded coverage, the retouch snapshot),
  // compute (ANY thread: the exemplar fill and tone match over the job's own
  // copies, cancellable per patch; nothing to do for NearestEdge), commit
  // (UI thread: the source map, the history push, the heal write, the
  // status). remove_object_in_selection(options) is the three in turn under
  // the progress overlay. A job's snapshot must be the document as it should
  // look BEFORE the fill: a host that previews into the layer restores the
  // original before preparing the next job.
  struct RemoveObjectJob {
    RemoveObjectOptions options;
    QRect bounds;                     // padded write bounds (selection + ring + feather reach)
    std::vector<std::uint8_t> mask;   // coverage over bounds, 0 on the ring
    QImage snapshot;                  // RGBA8888 retouch snapshot
    bool valid{false};
    QString error;                    // the refusal (also reported to the status bar) when !valid
  };
  struct RemoveObjectComputed {
    bool cancelled{false};
    bool fell_back{false};            // ContentAware found no clean source patch: NearestEdge runs
    QImage filled;                    // ContentAware: the snapshot with the hole filled
    ExemplarInpaintResult inpaint;
  };
  [[nodiscard]] RemoveObjectJob prepare_remove_object(const RemoveObjectOptions& options);
  [[nodiscard]] static RemoveObjectComputed compute_remove_object(const RemoveObjectJob& job,
                                                                  const std::atomic<bool>* cancel,
                                                                  const std::function<void(int)>& progress_percent);
  RemoveObjectResult commit_remove_object(const RemoveObjectJob& job, const RemoveObjectComputed& computed);
  // The historical form: method, attempt, record_history, defaults otherwise.
  RemoveObjectResult remove_object_in_selection(RemoveObjectMethod method = RemoveObjectMethod::ContentAware,
                                                int attempt = -1, bool record_history = true);
  // Set by the host: the Patch tool's Enter with an outline and no drag asks
  // the host to run Remove Object (MainWindow opens its dialog). Without a
  // callback the canvas runs the default fill directly.
  void set_remove_object_requested_callback(std::function<void()> callback);
  // Read-only press-time precheck of begin_edit's pixel-layer branch (8-bit
  // pixel layer, pixel lock, text/smart-object/shape refusals) WITHOUT the
  // history push, for gestures that defer begin_edit to release (Spot Healing,
  // Patch). Reports the same status errors / rasterize prompt when `report`.
  bool can_begin_pixel_edit(bool report);
  void set_marquee_style(MarqueeStyle style) noexcept;
  [[nodiscard]] MarqueeStyle marquee_style() const noexcept;
  void set_marquee_fixed_size(int width, int height) noexcept;
  [[nodiscard]] QSize marquee_fixed_size() const noexcept;
  // Options-bar Style / Width / Height for the Rectangle and Ellipse draw tools
  // (session-only, mirroring the marquee's Normal / Fixed Ratio / Fixed Size).
  void set_shape_style(MarqueeStyle style) noexcept;
  [[nodiscard]] MarqueeStyle shape_style() const noexcept;
  void set_shape_fixed_size(int width, int height) noexcept;
  [[nodiscard]] QSize shape_fixed_size() const noexcept;
  // Shape | Path | Pixels for the Line/Rectangle/Ellipse draw tools. Vector
  // modes route the released drag to vector_shape_drawn_callback_ (falling
  // back to raster while a mask/channel/quick-mask target is being edited).
  void set_vector_tool_mode(VectorToolMode mode) noexcept;
  [[nodiscard]] VectorToolMode vector_tool_mode() const noexcept;
  void set_vector_shape_drawn_callback(
      std::function<void(patchy::LiveShapeKind, QRectF, QPointF, QPointF)> callback);
  // True while a shape drag pressed with Shift commits (GitHub issue 313): the host
  // adds that one shape to the targeted shape layer (Combine: Add) without
  // changing the options-bar Combine setting.
  [[nodiscard]] bool shape_drag_adds_to_shape() const noexcept { return shape_press_adds_to_shape_; }
  // A bare click (no drag) with Rectangle/Ellipse/Polygon/Custom Shape in a
  // vector mode asks the host for dimensions instead of committing a
  // degenerate shape (Photoshop's Create <Shape> dialog; the click point is
  // passed in document coordinates).
  void set_shape_create_requested_callback(std::function<void(CanvasTool, QPointF)> callback);
  // A Path Select / Direct Select double-click on the target shape layer's
  // geometry (anchor, segment, or a painted pixel) opens its appearance editor.
  void set_shape_appearance_requested_callback(std::function<void()> callback);
  // Pen tool (canvas_widget_vector_tools.cpp - the tablet-input TU is
  // canvas_widget_pen.cpp): a committed path arrives as one subpath.
  void set_vector_path_committed_callback(
      std::function<void(patchy::VectorPath, bool, VectorPathSource)> callback);
  // Shape-mode drags preview with the shape's ACTUAL appearance (options-bar
  // fill/stroke) instead of the raster-paint preview. Pulled through a
  // callback at draw time so the values can never go stale per canvas.
  struct ShapePreviewAppearance {
    // Document-space paints (Qt::NoBrush = no fill). Texture brushes carry the
    // pattern placement in their transform and get the widget-from-document
    // view composed on top at draw time; ObjectMode gradient brushes span the
    // painted shape's bounds and stay untransformed.
    QBrush fill{Qt::NoBrush};
    bool stroke_enabled{false};
    QBrush stroke{Qt::NoBrush};
    double stroke_width{1.0};
    // The committed stroke's geometry (GitHub issue 125): the preview once drew a
    // centered band with Qt's default bevel join, so a wide stroke jumped inward
    // and its corners changed shape on release.
    VectorStrokeAlignment stroke_alignment{VectorStrokeAlignment::Inside};
    VectorStrokeJoin stroke_join{VectorStrokeJoin::Miter};
    VectorStrokeCap stroke_cap{VectorStrokeCap::Butt};
    double stroke_miter_limit{100.0};
    int line_weight{1};
  };
  void set_shape_preview_appearance_callback(
      std::function<std::optional<ShapePreviewAppearance>()> callback);
  // Polygon tool options (sides, star inset percent 0 = plain polygon) and
  // the custom-shape stamp path (unit-box normalized).
  void set_polygon_sides(int sides) noexcept;
  [[nodiscard]] int polygon_sides() const noexcept;
  void set_polygon_star_inset(int percent) noexcept;
  [[nodiscard]] int polygon_star_inset() const noexcept;
  void set_custom_shape_path(std::shared_ptr<const patchy::VectorPath> path);
  [[nodiscard]] const patchy::VectorPath* custom_shape_path() const noexcept;
  // The options-bar W/H link: on, a Custom Shape drag keeps the shape's own
  // proportions, as Shift does.
  void set_custom_shape_keep_proportions(bool keep) noexcept;
  [[nodiscard]] bool custom_shape_keep_proportions() const noexcept;
  [[nodiscard]] bool pen_session_active() const noexcept;
  void commit_pen_path(bool closed);
  void cancel_pen_path();
  // Photoshop's Pen "Auto Add/Delete": off, the Pen only draws (a click on the
  // target path starts a new subpath instead of editing it). The dedicated
  // Add/Delete/Convert anchor tools ignore it.
  void set_pen_auto_add_delete(bool enabled);
  [[nodiscard]] bool pen_auto_add_delete() const noexcept;
  // Right-click menu of the path tools: add/delete/convert the hovered point,
  // delete or deselect the selected points, free-transform the path. Public
  // so tests can open it without the right-button release gesture.
  // Returns false when nothing was shown (no target path, a Pen session, a
  // path transform, or a non-path tool).
  bool show_path_context_menu(QPointF widget_point, QPoint global_position);
  // The canvas right-click menu (a right release within the drag distance of
  // its press): the Move tool's layers-under-the-pointer section, then the
  // host's selection commands when the click landed on the selection; path
  // tools get their own menu above. Public so tests can open it without the
  // gesture. Returns false when nothing was shown.
  bool show_canvas_context_menu(QPoint widget_point, QPoint global_position);
  // Path editing (PathSelect / DirectSelect / Pen add-delete) on the active
  // shape layer's path or the work path.
  [[nodiscard]] bool path_edit_has_selection() const noexcept;
  [[nodiscard]] int path_edit_selected_anchor_count() const noexcept;
  [[nodiscard]] std::vector<int> path_edit_selected_groups() const;
  void set_selected_subpaths_combine_op(patchy::PathCombineOp op);
  void clear_path_edit_selection();
  // Appends subpaths to the active layer's vector mask (undo + cache rebake);
  // used by pen commits and shape drags while the VectorMask target is active.
  void add_subpaths_to_vector_mask(std::vector<patchy::PathSubpath> subpaths, const QString& label);
  // Paths-panel targeting: an explicitly selected document path takes
  // precedence over the active shape layer / work-path fallback.
  void set_active_document_path(std::optional<DocumentPathId> id);
  [[nodiscard]] std::optional<DocumentPathId> active_document_path() const noexcept;
  // True while a Paths-panel row is selected: the targeted path's outline then
  // draws with ANY tool (Photoshop's target-path display); anchors and handles
  // still require a path tool.
  void set_panel_path_targeted(bool targeted);
  [[nodiscard]] bool panel_path_targeted() const noexcept;
  // Layers-panel selection, pushed on every selection change: under a path
  // tool the overlay also outlines every selected shape layer's path (hollow
  // anchors), not just the editing target.
  void set_panel_selected_layer_ids(std::vector<LayerId> ids);
  // View > Show > Target Path (Ctrl+Shift+H): false hides the whole path
  // overlay - including under path tools - without touching the targeting.
  void set_target_path_visible(bool visible);
  [[nodiscard]] bool target_path_visible() const noexcept;
  // Invoked when Escape (path tools, nothing else to dismiss) asks to hide the
  // targeted path; MainWindow clears the Paths-panel selection in response.
  void set_path_display_dismiss_callback(std::function<void()> callback);
  // Invoked on Ctrl+Enter while a Paths-panel row is targeted (Photoshop's
  // load-path-as-selection key). Canvas-scoped rather than an app shortcut:
  // the inline text editor owns a window-scoped Ctrl+Return while it exists.
  void set_path_load_selection_callback(std::function<void()> callback);
  // Invoked after any canvas-side path mutation (direct-select edits, path
  // transforms, vector-mask appends) so the Paths panel rows and thumbnails
  // never go stale; the panel's revision-keyed caches keep the refresh cheap.
  void set_path_edited_callback(std::function<void()> callback);
  // Invoked whenever the set of selected anchors changes (clicks, marquees,
  // deletes, prunes); MainWindow refreshes the options-bar selected-point
  // count from it.
  void set_path_selection_changed_callback(std::function<void()> callback);
  // Invoked when a Delete under the path tools removed the last subpath of one
  // or more shape layers (GitHub issue 315). The path edit's undo entry is still
  // open: MainWindow removes those layers inside it, so one Undo brings each
  // layer back with its path.
  void set_shape_layers_emptied_callback(std::function<void(const std::vector<LayerId>&)> callback);
  // The path the pen/path tools currently edit (panel > vector mask > shape
  // layer > work path); null when nothing is targetable.
  [[nodiscard]] const patchy::VectorPath* path_edit_target_path() const;
  [[nodiscard]] std::optional<patchy::VectorPath> text_area_at(QPoint document_point) const;
  [[nodiscard]] const patchy::VectorPath* text_area_edit_target_path() const;
  // Replaces the targeted path (same target rules) and re-rasterizes WITHOUT
  // arming an undo entry: preview dialogs apply through this and own the
  // snapshot. `touched_groups` lose their live-shape annotations; on a merged
  // shape layer each `group_copies` copy joins its source group's paint parts.
  void replace_path_edit_target(patchy::VectorPath path, const std::vector<int>& touched_groups,
                                const std::map<int, int>& group_copies = {});
  // Path free-transform session (Ctrl+T with a path tool active): a box over
  // the targeted path - or the Direct Select anchor subset - that moves,
  // scales, and rotates it, committed as ONE apply_path_edit undo entry.
  // Tool switches commit, document switches cancel (the pen-session rules).
  [[nodiscard]] bool path_transform_active() const noexcept;
  bool begin_path_transform();
  void commit_path_transform();
  void cancel_path_transform();
  // Rounded-corner radius for the rectangular marquee (0 = sharp corners).
  void set_marquee_corner_radius(int pixels) noexcept;
  [[nodiscard]] int marquee_corner_radius() const noexcept;
  void set_selection_feather_radius(int pixels) noexcept;
  [[nodiscard]] int selection_feather_radius() const noexcept;
  void set_selection_antialias(bool enabled) noexcept;
  [[nodiscard]] bool selection_antialias() const noexcept;
  bool begin_free_transform();
  void finish_free_transform();
  void cancel_free_transform();
  [[nodiscard]] bool free_transform_active() const noexcept;
  // Steps inside a pending Free Transform or Warp session (GitHub issue 225): every
  // handle drag, numeric edit, warp preset, and run of arrow-key nudges is one step.
  // Undo restores the geometry before the last step (the session stays open); Redo
  // re-applies it. Both return false when there is nothing to step (or no session).
  // A Free Transform <-> Warp mode switch starts the stack over.
  bool undo_transform_session_step();
  bool redo_transform_session_step();
  [[nodiscard]] int transform_session_undo_step_count() const noexcept;
  [[nodiscard]] int transform_session_redo_step_count() const noexcept;
  // True when a pending Free Transform or Warp session holds anything a cancel
  // would discard (a changed box or cage, or a stage carried over a mode switch).
  [[nodiscard]] bool transform_session_has_changes() const;
  // True while the session transforms a flattened target set (a selected folder
  // or a multi-selection) instead of one layer. Warp refuses such sessions.
  [[nodiscard]] bool free_transform_is_multi_target() const noexcept;
  // Warp Transform: a 4x4 Bezier control cage over the layer (Photoshop's warp
  // tool). Smart objects commit non-destructively (warp metadata + regenerated
  // SoLd); plain pixel layers bake destructively; text layers are refused.
  bool begin_warp_transform();
  void finish_warp_transform();
  void cancel_warp_transform();
  [[nodiscard]] bool warp_transform_active() const noexcept;
  // Single-session mode switch (Photoshop's options-bar warp toggle): leaves the
  // warp cage and enters free transform on the same pending session. The mesh
  // rides along uncommitted (the affine stage edits the baked hull box) and the
  // eventual commit composes both stages into ONE bake and ONE undo step; Esc
  // still cancels everything. The reverse switch is begin_warp_transform, which
  // carries a pending free-transform stage into the cage instead of discarding it.
  bool switch_warp_to_free_transform();
  // Skew, Distort and Perspective (GitHub issue 225; docs/free-transform.md). A Free
  // Transform session carries four corner offsets on top of its box, so the result
  // can be any quad. Edit > Transform picks a mode in which plain corner and edge
  // drags skew, distort or apply perspective; without one, Ctrl+drag a corner
  // distorts, Ctrl+drag an edge skews (Ctrl+Shift slides it along itself), and
  // Ctrl+Alt+Shift+drag a corner applies perspective.
  using TransformDistortMode = ::patchy::ui::TransformDistortMode;
  // Starts a Free Transform session if none is open, then sets the mode.
  bool begin_free_transform_mode(TransformDistortMode mode);
  [[nodiscard]] TransformDistortMode free_transform_distort_mode() const noexcept;
  // The session's corners in document space, in the order of the layer's own top
  // left, top right, bottom right and bottom left (a flip moves them on screen).
  [[nodiscard]] std::array<QPointF, 4> free_transform_quad() const;
  // Places the four corners (same order). Refused (false) for a non-affine quad on a
  // layer that cannot take one (see free_transform_accepts_non_affine).
  bool set_free_transform_quad(const std::array<QPointF, 4>& corners);
  // False when a target layer can only take an affine (parallelogram) result: type
  // layers, which must be rasterized first.
  [[nodiscard]] bool free_transform_accepts_non_affine() const;
  // Scripted Free Transform (layer.transform): one session on `layer_id` (a group
  // transforms its contents), corners placed where `place` maps the session box (the
  // layer's own TL, TR, BR, BL in document space), committed at once through the
  // interactive commit's per-type rules but without its own history entry (the script
  // run owns the undo snapshot) and without dialogs. Returns the refusal message, or an
  // empty string once committed.
  [[nodiscard]] QString apply_scripted_free_transform(
      LayerId layer_id, const std::function<std::array<QPointF, 4>(const std::array<QPointF, 4>& box)>& place,
      std::optional<TransformInterpolation> interpolation);
  // Options-bar preset: bakes a style (warpArc/.../warpRise, bend percent) into the
  // working cage; manual handle drags flip the reported style back to warpCustom.
  void apply_warp_style_preset(const QString& style, double value);
  [[nodiscard]] QString warp_style_preset() const;
  [[nodiscard]] double warp_style_preset_value() const noexcept;
  [[nodiscard]] int warp_handle_count() const noexcept;
  [[nodiscard]] QPointF warp_handle_document_position(int index) const;
  void set_warp_handle_document_position(int index, QPointF document_point);
  void set_transform_interpolation(TransformInterpolation interpolation) noexcept;
  [[nodiscard]] TransformInterpolation transform_interpolation() const noexcept;
  void set_transform_reference_point(CanvasAnchor anchor) noexcept;
  [[nodiscard]] CanvasAnchor transform_reference_point() const noexcept;
  // Which modifier state locks a corner-handle drag to the source aspect ratio.
  // Off (the default) matches current Photoshop: corners scale proportionally and
  // Shift releases the lock. On restores the older meaning, where a plain corner
  // drag distorts and Shift holds the ratio.
  void set_shift_keeps_transform_aspect(bool enabled) noexcept;
  [[nodiscard]] bool shift_keeps_transform_aspect() const noexcept;
  // True when a corner-handle drag under these modifiers must hold the source
  // aspect ratio. Shared by the pixel and path transform sessions so the two can
  // never disagree about what Shift means.
  [[nodiscard]] bool transform_drag_keeps_aspect(Qt::KeyboardModifiers modifiers) const noexcept;
  // Alt on a scale handle scales about the reference point instead of the
  // opposite edge (Photoshop). Shared predicate so every session agrees.
  [[nodiscard]] bool transform_drag_scales_about_reference(Qt::KeyboardModifiers modifiers) const noexcept;
  [[nodiscard]] std::optional<TransformControlsState> transform_controls_state() const;
  bool set_transform_controls_state(QPointF reference_position, double scale_x_percent,
                                    double scale_y_percent, double rotation_degrees);
  // Crop tool session (canvas_widget_crop.cpp). Picking the tool frames the
  // canvas (or the active selection) with handles; a drag inside that default
  // box lays out a new rect, handles adjust, the interior of a custom box moves
  // it, a drag off the box rotates it, Enter/Apply commits through the
  // crop-commit callback, Esc resets the box to the canvas. The rect lives in
  // document space and may extend past the canvas; the commit handler expands
  // the document. Switching tools cancels (never commits).
  [[nodiscard]] bool crop_session_active() const noexcept;
  [[nodiscard]] std::optional<QRect> crop_session_rect() const noexcept;
  // True once the box differs from the unrotated whole canvas: the Apply
  // button, the no-op commit guard, and the recovery busy check key on it.
  [[nodiscard]] bool crop_session_has_changes() const noexcept;
  // True when Esc or the X would change the box: it is not the automatic frame
  // (the canvas fitted to the ratio, GitHub issue 318).
  [[nodiscard]] bool crop_session_can_reset() const;
  // Box rotation in degrees about the rect center (0 until rotated).
  [[nodiscard]] double crop_session_angle() const noexcept;
  void commit_crop_session();
  void cancel_crop_session();
  // Puts the box back around the whole canvas (ratio-fitted), angle 0; Esc and
  // the options-bar X. Starts the session when the Crop tool has none.
  void reset_crop_session_to_canvas();
  // Resizes the pending box to `size` pixels about its center (the options-bar
  // Size fields); the box becomes custom. Ignored without a session.
  void set_crop_session_size(QSize size);
  // Aspect constraint for new drag-outs and corner-handle drags. Both values
  // must be > 0 to constrain; a change re-fits the pending box inside itself
  // about its center (GitHub issue 66).
  void set_crop_ratio(double width, double height) noexcept;
  [[nodiscard]] double crop_ratio_width() const noexcept;
  [[nodiscard]] double crop_ratio_height() const noexcept;
  // How the area outside the pending crop box draws (CropOverlayOptions,
  // canvas_types.hpp). Opacity clamps to 0..100; the color is stored opaque.
  void set_crop_overlay_options(const CropOverlayOptions& options);
  [[nodiscard]] const CropOverlayOptions& crop_overlay_options() const noexcept;
  // Size style's Width/Height link (GitHub issue 332): with no ratio set, every
  // handle drag keeps the box's drag-start width:height, through the same
  // constrained path a set ratio takes.
  void set_crop_keep_proportions(bool keep) noexcept;
  [[nodiscard]] bool crop_keep_proportions() const noexcept;
  // Composition guides drawn inside the crop box (GitHub issue 325). Orientation
  // flips the Triangle (0-1) and the Golden Spiral (0-3); the others ignore it.
  // only_while_dragging shows the guides only while the box is dragged, resized
  // or rotated (and while one is drawn out).
  enum class CropOverlay { None, Thirds, Grid, Diagonal, Triangle, GoldenRatio, GoldenSpiral };
  void set_crop_overlay(CropOverlay overlay, int orientation, bool only_while_dragging);
  [[nodiscard]] CropOverlay crop_overlay() const noexcept;
  [[nodiscard]] int crop_overlay_orientation() const noexcept;
  [[nodiscard]] bool crop_overlay_only_while_dragging() const noexcept;
  // Perspective Crop session (canvas_widget_perspective_crop.cpp, GitHub issue 328): a
  // drag lays out a box or four clicks place the corners; each corner then moves on
  // its own and the inside moves the quad. Enter/Apply hands the quad (document space,
  // output order TL, TR, BR, BL) to the commit callback; Esc/the X clear it; a tool
  // switch, edit lock or document swap cancel it. Session changes fire the crop
  // session callback.
  [[nodiscard]] bool perspective_crop_session_active() const noexcept;  // four corners placed
  [[nodiscard]] bool perspective_crop_session_pending() const noexcept;  // any corner or drag
  [[nodiscard]] std::optional<std::array<QPointF, 4>> perspective_crop_quad() const;
  // Places the four corners at once (scripts, tests); false for a folded quad or when
  // the Perspective Crop tool is not current. Corners are reordered upright.
  bool set_perspective_crop_quad(const std::array<QPointF, 4>& quad);
  void commit_perspective_crop_session();
  void cancel_perspective_crop_session();
  void set_perspective_crop_show_grid(bool show);
  // Makes the quad's left edge (in output terms) the result's top: the output turns a
  // quarter clockwise and the up marker moves with it. Ignored without a quad.
  void turn_perspective_crop_top();
  [[nodiscard]] bool perspective_crop_show_grid() const noexcept;
  [[nodiscard]] std::optional<QRect> active_layer_document_rect() const noexcept;
  [[nodiscard]] RenderCacheDiagnostics render_cache_diagnostics() const noexcept;
  // True when the displayed frame reflects the current document: no recomposite
  // pending and no fire-and-forget async refresh in flight. Used by the
  // profiling stress test's settle loop.
  [[nodiscard]] bool render_settled() const noexcept;
  // The full composite the canvas shows, when it is current (null otherwise): dialogs that
  // need a picture of the whole document (Image Size's preview) reuse it instead of
  // compositing again.
  [[nodiscard]] QImage current_composite() const;
  // A deferred Move commit is still rendering its accurate patches.
  [[nodiscard]] bool move_commit_job_pending() const noexcept;
  void set_vector_preview_enabled(bool enabled);
  [[nodiscard]] bool vector_preview_enabled() const noexcept;
  [[nodiscard]] QString vector_preview_status() const;
  void set_vector_preview_status_callback(std::function<void(QString, bool)> callback);
  // set_document for undo/redo restores of the SAME logical document: identical
  // interaction-state reset, but when the restored document has the same
  // dimensions the previous frame stays in the render cache (as a stale frame
  // for should_defer_full_refresh_to_async) and the render diagnostics keep
  // counting, so history steps on big documents swap frames instead of
  // flashing checkerboard.
  void set_document_for_history_restore(Document* document, bool normal_composite_unchanged = false);
  // Draggable position markers for a preview dialog (the G'MIC Filter Library's point
  // parameters, docs/gmic.md): plain markers at document positions, nothing drawn around
  // them. `moved(index, document_position, finished)` runs while one is dragged and once
  // more, with finished true, on release. Handles win the press over the active tool.
  void set_preview_point_handles(std::vector<QPointF> document_positions,
                                 std::function<void(int, QPointF, bool)> moved);
  void clear_preview_point_handles();
  [[nodiscard]] int preview_point_handle_count() const noexcept;
  [[nodiscard]] QPointF preview_point_handle(int index) const;                  // document space
  [[nodiscard]] QPointF preview_point_handle_widget_position(int index) const;  // for tests and input

  // Where a paint stroke gets the mouse positions the platform folded into a
  // move event (coalesced_mouse_positions by default). Tests install a
  // provider to feed a burst of samples through one event.
  using CoalescedMousePositionsProvider = std::function<std::vector<QPointF>(const QMouseEvent&)>;
  void set_coalesced_mouse_positions_provider(CoalescedMousePositionsProvider provider);

  [[nodiscard]] bool processing_overlay_visible() const noexcept;
  [[nodiscard]] bool processing_operation_active() const noexcept;
  // delay_ms_override < 0 uses the standard delay (processing_overlay_delay_ms,
  // default 1000 ms); callers that already waited out their own threshold pass
  // 0 so the first tick shows the overlay (the script busy indicator).
  void begin_processing_operation(QString message = {}, int delay_ms_override = -1);
  void tick_processing_operation();
  // Replace the running operation's overlay text (a percentage, say); shown
  // by the next tick or repaint.
  void set_processing_operation_message(QString message);
  void end_processing_operation();
  // Non-blocking counterpart for background preview renders (filter and
  // adjustment dialogs): while at least one preview worker is in flight and
  // the standard overlay delay has passed, the processing overlay shows
  // "Rendering preview..." so a slow preview reads as working, not broken.
  // Callers pair begin at worker spawn with end in the queued completion.
  void begin_preview_render();
  void end_preview_render();
  // The percentage (0..100) the preview in flight has reached, shown after the badge's
  // text; negative (the default, restored when the last preview ends) shows none.
  void set_preview_render_progress(int percent);
  [[nodiscard]] int preview_render_progress() const noexcept { return preview_render_progress_; }
  // True while the preview in flight replaces a draft already on screen: the badge reads
  // "Refining preview..." (reset when the last preview ends).
  void set_preview_render_refining(bool refining);
  // The badge's text for a preview render at its current progress.
  [[nodiscard]] QString preview_render_message() const;
  // True while a background full refresh (the deferred-async route that keeps
  // the previous frame on screen) or a deferred Move commit job has been
  // running longer than the standard overlay delay: the frame on screen is
  // then known to be out of date, and the overlay says "Processing..." so a
  // multi-second catch-up on a heavy document reads as working, not stuck.
  [[nodiscard]] bool background_refresh_overlay_visible() const noexcept;
  bool wait_for_processing_operation(std::function<bool()> operation_ready, bool allow_overlay = true);
  // True while a blocking processing wait is running. Input that arrives then is
  // wasm's re-entrant DOM delivery into the nested wait loop (docs/wasm.md); the
  // canvas drops presses and parks releases, and MainWindow's canvas event filter
  // must leave its click-swallow state untouched for such events.
  [[nodiscard]] bool processing_render_wait_active() const noexcept { return processing_render_wait_active_; }
  void force_refresh();
  void document_changed();
  void document_changed_async_preview();
  void layer_visibility_changed(LayerId id);
  void document_changed(QRect document_rect);
  void document_changed(QRegion document_region);
  void document_changed_effect_bounds(QRect document_rect);
  void document_changed_effect_bounds(QRegion document_region);
  // Invalidates the active grayscale target without invalidating the normal
  // compositor. Document channels are not part of the layer composite.
  void grayscale_target_changed(QRect document_rect,
                                DocumentChangeReason reason = DocumentChangeReason::Immediate);
  void select_all();
  void invert_selection();
  void clear_selection();
  void reselect();
  [[nodiscard]] bool quick_mask_active() const noexcept;
  void set_quick_mask_active(bool active);
  [[nodiscard]] const PixelBuffer& quick_mask_pixels() const noexcept;
  [[nodiscard]] std::uint64_t quick_mask_revision() const noexcept;
  [[nodiscard]] QRect fill_quick_mask(QColor color, QString history_label);
  // Smart Filter masks are edited in a canvas-owned gray8 buffer so a brush
  // gesture can update the overlay without regenerating the native filter
  // stack for every dab. `set` enters the target, `resync` replaces it after
  // undo/model changes without emitting a commit, and `clear` safely exits.
  [[nodiscard]] bool set_smart_filter_mask_edit_target(
      LayerId owner_id, PixelBuffer pixels, MaskDisplayMode mode = MaskDisplayMode::Overlay);
  [[nodiscard]] bool resync_smart_filter_mask_edit_target(LayerId owner_id, PixelBuffer pixels);
  void clear_smart_filter_mask_edit_target();
  [[nodiscard]] bool editing_smart_filter_mask() const noexcept;
  [[nodiscard]] std::optional<LayerId> smart_filter_mask_owner_id() const noexcept;
  [[nodiscard]] const PixelBuffer& smart_filter_mask_pixels() const noexcept;
  [[nodiscard]] std::uint64_t smart_filter_mask_revision() const noexcept;
  [[nodiscard]] QRect fill_smart_filter_mask(QColor color, QString history_label);
  [[nodiscard]] QRect invert_smart_filter_mask(QString history_label);
  void finish_smart_filter_mask_edit();
  void cancel_smart_filter_mask_edit();
  // View > Show Selection Edges (Ctrl+H), Photoshop's Extras toggle: false
  // hides the marching ants AND the path overlay (anchors, handles, outline;
  // a live pen session and a path-transform box still draw). Any selection
  // change resets it to true.
  void set_selection_edges_visible(bool visible) noexcept;
  [[nodiscard]] bool selection_edges_visible() const noexcept;
  void toggle_selection_edges_visible();
  void set_rulers_visible(bool visible) noexcept;
  [[nodiscard]] bool rulers_visible() const noexcept;
  // Display unit for the ruler tick marks (the app-wide "view/rulerUnits" preference,
  // pushed in by MainWindow). Physical units derive tick geometry from the document's
  // per-axis print PPI.
  void set_ruler_unit(MeasurementUnit unit) noexcept;
  [[nodiscard]] MeasurementUnit ruler_unit() const noexcept;
  // Fired when the user picks a unit from the ruler's right-click menu; the host owns
  // the preference and pushes it back into every canvas.
  void set_ruler_unit_change_requested_callback(std::function<void(MeasurementUnit)> callback);
  void set_grid_visible(bool visible) noexcept;
  [[nodiscard]] bool grid_visible() const noexcept;
  void set_guides_visible(bool visible) noexcept;
  [[nodiscard]] bool guides_visible() const noexcept;
  // View > Pixel Grid (GitHub issue 239): lines between pixels wherever the deep-zoom
  // renderer draws (600% and up), each tinted from its own pixel. On by default.
  void set_pixel_grid_visible(bool visible) noexcept;
  [[nodiscard]] bool pixel_grid_visible() const noexcept;
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
  // The pasteboard behind the document (GitHub issue 47). Unset, it is the theme's
  // `canvas_backdrop` role; a user color from the backdrop's right-click menu (a view
  // preference the host owns, `view/canvasBackdropColor`) replaces it in every window.
  void set_backdrop_color_override(std::optional<QColor> color);
  [[nodiscard]] std::optional<QColor> backdrop_color_override() const noexcept;
  [[nodiscard]] QColor backdrop_color() const;
  // Fired by the backdrop menu's preset entries (nullopt = Default) and by its Select
  // Custom Color... entry; unset, the presets apply to this canvas alone.
  void set_backdrop_color_change_requested_callback(std::function<void(std::optional<QColor>)> callback);
  void set_custom_backdrop_color_requested_callback(std::function<void()> callback);
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
  [[nodiscard]] PixelBuffer selection_as_grayscale() const;
  void replace_selection_from_grayscale(const PixelBuffer& pixels, QString history_label);
  // Apply under a caller-owned history transaction (native script batches).
  void apply_grayscale_to_selection(const PixelBuffer& pixels);
  void grow_selection();
  void select_similar_to_selection();
  [[nodiscard]] std::optional<QRect> selected_document_rect() const noexcept;
  [[nodiscard]] const QRegion& selected_document_region() const noexcept;
  [[nodiscard]] std::uint8_t selection_alpha_at(QPoint point) const noexcept;
  [[nodiscard]] bool selection_has_partial_alpha() const noexcept;
  [[nodiscard]] bool has_selection() const noexcept;
  [[nodiscard]] bool selection_contains(QPoint point) const noexcept;
  [[nodiscard]] QPoint widget_position_for_document_point(QPoint document_position) const;
  // Fractional counterpart: tests use it to land presses on exact document
  // coordinates whatever the centred pan is.
  [[nodiscard]] QPointF widget_position_f(QPointF document_position) const;
  // The document pixel under a widget-local point, for drop handlers outside
  // the widget (document_position itself stays private).
  [[nodiscard]] QPoint document_point_for_widget_position(QPoint widget_position) const {
    return document_position(widget_position);
  }
  [[nodiscard]] QPointF document_point_for_widget_position(QPointF widget_position) const {
    return document_position_f(widget_position);
  }
  void set_before_edit_callback(std::function<void(QString)> callback);
  // Paint-tool stroke lifecycle for the host's per-stroke history (docs/performance.md,
  // "History"): Begin fires right after the stroke's before-edit callback pushed its
  // snapshot and names the buffer the stroke will write (layer content, layer mask or
  // saved channel); Finish fires once the stroke is over with the union of every dirty
  // rectangle it reported, in document coordinates. Only the painting_ gestures (Brush,
  // Eraser, Clone, Healing, Smudge, the local adjustment brushes, Mixer Brush, Pattern
  // Stamp) emit it; Quick Mask and Smart Filter mask strokes never do.
  using PaintStrokeHistoryEvent = patchy::ui::PaintStrokeHistoryEvent;
  void set_paint_stroke_history_callback(std::function<void(const PaintStrokeHistoryEvent&)> callback);
  // Invoked when a selection-only edit completes and actually changed the
  // selection, so the host can push an undo entry holding the pre-edit state.
  // `coalesce` marks a continuation of a move sequence (drag/nudge): consecutive
  // coalescing edits collapse into the single entry holding the pre-sequence
  // state, so a run of moves is one undo step.
  void set_selection_history_callback(std::function<void(QString, SelectionSnapshot, bool coalesce)> callback);
  void set_quick_mask_changed_callback(std::function<void()> callback);
  // Receives one completed gesture/command. PixelBuffer copies are COW, so the
  // host may retain the result while rebuilding FEid and the filtered preview.
  void set_smart_filter_mask_committed_callback(
      std::function<bool(LayerId, QString, PixelBuffer, QRegion)> callback);
  // Invoked when the effective combine mode changes (tool switch, mode button,
  // or live Shift/Alt) so the Options-bar mode buttons can follow.
  void set_selection_mode_changed_callback(std::function<void(SelectionMode)> callback);
  void set_color_picked_callback(std::function<void(QColor)> callback);
  // Reports a pick the Eyedropper tool made with Alt held, which lands in the
  // background color (GitHub issue 150); the canvas has already stored it.
  void set_background_color_picked_callback(std::function<void(QColor)> callback) {
    background_color_picked_callback_ = std::move(callback);
  }
  void set_transient_read_interaction(std::function<void(const CanvasReadGesture&)> callback,
                                      QCursor cursor = Qt::CrossCursor);
  void clear_transient_read_interaction();
  [[nodiscard]] bool has_transient_read_interaction() const noexcept;
  void set_curves_clipping_preview(std::optional<CurvesClippingMode> mode,
                                   std::optional<CurvesChannel> channel = std::nullopt);
  [[nodiscard]] std::optional<CurvesClippingMode> curves_clipping_preview_mode() const noexcept;
  // Soft proof and gamut warning (docs/cmyk.md): a view of the canvas only. Null turns
  // it off. Document pixels, the render cache, color readouts and exports never see it.
  void set_proof_display_transform(std::shared_ptr<const CmykProofTransform> transform);
  [[nodiscard]] const std::shared_ptr<const CmykProofTransform>& proof_display_transform() const noexcept {
    return proof_display_transform_;
  }
  // What the canvas draws for `image` under the proof (the image itself when it is off).
  // The reference is valid until the next call.
  [[nodiscard]] const QImage& proof_display_image(const QImage& image);
  // Invoked when the canvas itself changes brush/gradient parameters (size
  // drag gesture, opacity/flow digit keys) so the options bar can resync.
  void set_brush_settings_changed_callback(std::function<void()> callback);
  void set_pen_button_action_callback(std::function<void(PenButtonAction)> callback);
  void set_text_requested_callback(std::function<void(QPoint, QRect)> callback);
  // The Type-tool press that opens a session on an existing layer keeps the rest of its gesture
  // on the canvas (Qt delivers a gesture to the widget that took the press), so the canvas hands
  // the drag to the host: begin = true right after the session opened (returns whether a live
  // editor is there to select in), then begin = false with each left-held move's widget point.
  void set_text_entry_selection_drag_callback(std::function<bool(QPointF, bool)> callback);
  // Type tool + Ctrl (GitHub issue 216): a press on a text layer drags it like the Move tool and
  // the Type tool stays active. text_layer_move_target_at says whether a Ctrl press at
  // widget_point has a text layer to move.
  [[nodiscard]] bool text_layer_move_target_at(QPoint widget_point) const;
  // Starts that drag of `id` from a press at widget_point (false, with the status message, when
  // the layer cannot move). `finished` runs once when the gesture ends (released, a click that
  // never dragged, or canceled). A host whose widget took the press (the inline editor, which
  // its commit removes) passes one to resume typing; the canvas then grabs the mouse so the rest
  // of the gesture reaches it.
  bool begin_text_layer_move(LayerId id, QPoint widget_point, std::function<void()> finished = {});
  void set_active_layer_changed_callback(std::function<void(LayerId)> callback);
  // The canvas asks the host (the layer panel is the selection's source of
  // truth) to make exactly these layers selected with active_id current; the
  // host pushes the result back through set_selected_layer_ids. Move-tool
  // modifier clicks and rectangle selection use this; unset, the canvas applies the selection to
  // itself directly. Empty layer_ids means "deselect every layer" (the host
  // also clears the document's active layer); active_id is unused then.
  void set_layer_selection_requested_callback(std::function<void(std::vector<LayerId>, LayerId)> callback);
  // Alt-drag with the Move tool (GitHub issue 69): asked once, when the press
  // turns into a drag, to duplicate these selection roots. The host duplicates
  // them, selects the copies (set_selected_layer_ids) and returns true; the
  // canvas then drags movable_layer_ids(), the copies. False keeps the
  // originals moving. Unset, Alt-drag is a plain move.
  void set_move_duplicate_requested_callback(std::function<bool(std::vector<LayerId>)> callback);
  // Commit of a pending crop rect + box angle (document geometry lives on
  // MainWindow).
  void set_crop_commit_requested_callback(std::function<void(QRect, double)> callback);
  // Fired when a crop session is established, cancelled, or committed so the
  // options bar and info panel can resync.
  void set_crop_session_changed_callback(std::function<void()> callback);
  // Commit of a pending Perspective Crop quad (document space, TL, TR, BR, BL).
  void set_perspective_crop_commit_requested_callback(std::function<void(std::array<QPointF, 4>)> callback);
  // Every artboard edit the canvas gestures ask for (canvas_widget_artboard.cpp,
  // docs/artboards.md): the Artboard tool's create/move/resize/add, a label click or
  // double-click. MainWindow applies them with an undo step.
  void set_artboard_edit_requested_callback(std::function<void(const ArtboardEditRequest&)> callback);
  // Artboard tool options the canvas reads: the size a click (no drag) or an add button
  // makes when there is nothing to copy the size from.
  void set_artboard_default_size(QSize size) noexcept;
  [[nodiscard]] bool artboard_drag_active() const noexcept;
  // A field scrub's live preview (the Properties and options-bar artboard X/Y/W/H): the
  // canvas shows artboard `id` at `rect` as an Artboard-tool drag does (a same-size rect
  // moves the artboard's pixels along, any other draws the resize outline), with any tool
  // and without touching the document. The caller commits once when the scrub ends, then
  // calls end_artboard_rect_preview (which leaves a mouse drag alone).
  void preview_artboard_rect(LayerId id, QRect rect);
  void end_artboard_rect_preview();
  // Keeps the artwork still on screen after an edit moved every document coordinate by
  // `document_shift` (an artboard edit that grew the canvas up or left).
  void shift_view_for_document_offset(QPoint document_shift);
  // Fired after a Move-tool drag re-nested layers into or out of artboards (the canvas
  // applies auto-nesting inside the move's undo step; MainWindow refreshes its panels).
  void set_layers_moved_callback(std::function<void(const std::vector<LayerId>&)> callback);
  // Artboard auto-nesting for Move-tool drags (the Auto-Nest Layers option): the drag
  // previews, and the release applies, the artboard each moved layer lands in.
  void set_artboard_auto_nest(bool enabled) noexcept;
  // The selected artboard when the Layers panel selection is exactly one artboard (the
  // Move tool then moves the artboard itself, rect and content together).
  [[nodiscard]] std::optional<LayerId> selected_root_artboard_id() const;
  // The topmost artboard whose name label is under a widget point, for every tool.
  [[nodiscard]] std::optional<LayerId> artboard_label_at(QPoint widget_point) const;
  void set_status_callback(std::function<void(QString)> callback);
  // The commands the host offers in the canvas context menu when a right-click
  // lands on the selection (Remove Object, Fill, Stroke, ...); a nullptr entry
  // is a separator. The actions stay owned by the host.
  void set_selection_context_actions_callback(std::function<QList<QAction*>()> callback);
  // Offered while a selection tool is active and the click is not inside a selection
  // (Select Subject, Select Background, Remove Background).
  void set_selection_tool_context_actions_callback(std::function<QList<QAction*>()> callback);
  // Offered first, anywhere on the canvas, while the Zoom tool is active (Zoom In, Zoom
  // Out, Fit on Screen, ...); same contract.
  void set_zoom_tool_context_actions_callback(std::function<QList<QAction*>()> callback);
  // The commands the host offers when a right-click lands on the active vector
  // shape layer (Shape Appearance, Free Transform, ...); same contract.
  void set_shape_context_actions_callback(std::function<QList<QAction*>()> callback);
  // The commands the host offers when a Move-tool right-click lands inside the
  // Move outline of the active layer that is not a shape or a group (Free
  // Transform); same contract.
  void set_layer_context_actions_callback(std::function<QList<QAction*>()> callback);
  // Blocking refusals (the tool action did NOT happen) report through this
  // callback so the host can present them as errors; unset, they fall back to
  // the plain status callback.
  void set_error_status_callback(std::function<void(QString)> callback);
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
  // Asked before a Free Transform that changes geometry commits, with every layer it would
  // transform. False cancels the whole transform (GitHub issue 185: the missing-font warning).
  void set_missing_font_transform_confirm_callback(std::function<bool(const std::vector<LayerId>&)> callback);
  void set_smart_object_transform_render_callback(std::function<bool(LayerId)> callback);
  // The natural-size decode of a smart object's source for the Warp cage: embedded
  // bytes, or a linked file resolved against the owning document's folder (the canvas
  // has no path). On failure `error` receives the reason to show (a missing linked
  // file names the file); an unset callback falls back to embedded bytes only.
  void set_smart_object_source_image_callback(
      std::function<std::optional<QImage>(LayerId, QString* error)> callback);
  // A paint tool pressed on a smart-object layer: the host offers to rasterize the
  // layer or open its contents. The triggering press is consumed either way (the
  // modal prompt swallows the release, so the stroke never starts).
  void set_smart_object_paint_prompt_callback(std::function<void(LayerId)> callback);
  void set_selected_layer_ids(std::vector<LayerId> layer_ids);

protected:
  // ShortcutOverride (canvas-owned Backspace/Delete during magnetic traces and guide
  // editing) + macOS trackpad pinch zoom (QNativeGestureEvent).
  bool event(QEvent* event) override;
#ifndef PATCHY_GPU_CANVAS
  void paintEvent(QPaintEvent* event) override;
#else
  void paintGL() override;
#endif
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void cancel_pointer_gestures();
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
  void paint_canvas(QPainter& painter, const QRect& exposed_rect);

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

  // Which areas of a transform box transform_handle_at reports (GitHub issue 176).
  // HandlesOnly: the eight handle squares, the Rotate stem handle, and the inside
  // (Move). EdgesAndCorners adds a band along each edge (that side's resize handle),
  // a band just outside each corner (Rotate), and a Move core that a small box keeps
  // ahead of its handles.
  enum class TransformHitZones { HandlesOnly, EdgesAndCorners };
  // What a Free Transform handle drag does (transform_drag_kind_for, chosen at press).
  enum class TransformDragKind { Scale, DistortCorner, SkewCorner, PerspectiveCorner, MoveEdge, SlideEdge };

  enum class StrokeConstraintAxis {
    None,
    Horizontal,
    Vertical
  };

  struct BrushStrokeLayerSnapshot {
    LayerId layer_id{0};
    Rect bounds{};
    PixelBuffer pixels{};
    bool background_extension{false};
  };

  struct EffectiveBrushInput {
    int size{12};
    int opacity{100};
    int softness{75};
    int roundness{100};
    double angle_degrees{0.0};
  };

  struct BrushCursorCache {
    CanvasTool tool{CanvasTool::Brush};
    int brush_size{0};
    int brush_softness{0};
    int diameter{0};
    int extent{0};
    bool one_pixel{false};
    QCursor cursor{};
  };

  struct MovingLayer {
    LayerId id{};
    Rect original_bounds{};
    std::optional<Rect> original_opaque_bounds{};
    // True when the layer's own style renders OR any ancestor group's style
    // does: a styled ancestor re-renders its silhouette and exterior effects
    // for every preview patch, so its children share the expensive path.
    bool expensive_style{false};
    // Summed padding of every styled ancestor group (the compositor outsets
    // per nesting level); the leaf's own style padding lives in
    // layer_bounds_with_effects, not here.
    int ancestor_effect_padding{0};
  };

  // One transformable leaf of a multi-target Free Transform session (a selected
  // folder flattens to its leaves, like the Move tool's movable_layer_ids).
  // expensive_style / ancestor_effect_padding mirror MovingLayer's fields.
  struct TransformTarget {
    LayerId id{};
    Rect original_bounds{};
    QRect source_local_rect{};
    QImage source_image{};
    bool expensive_style{false};
    int ancestor_effect_padding{0};
  };
  struct TransformTargetCollection {
    std::vector<TransformTarget> targets;
    // Layers whose linked masks ride along without pixel content: groups with
    // masks, adjustment leaves, empty-pixel leaves with linked masks.
    std::vector<LayerId> mask_only_ids;
    // Normalized panel roots (root_drop_layer_ids), sorted; the teardown
    // comparisons keep the session alive for an identical re-selection.
    std::vector<LayerId> root_ids;
    QString refusal;  // non-empty => refuse the session (already translated)
    // Refusal that must surface through show_layer_position_locked_message()
    // instead of a plain status string, matching the single-layer guard.
    bool position_lock_refusal{false};
    bool use_single_layer_path{false};
  };

  [[nodiscard]] QImage render_document_image() const;
  void ensure_render_cache();
  [[nodiscard]] QImage render_document_image_with_processing();
  // Stroke-start source snapshot for the retouch tools (Clone, Healing, Spot
  // Healing, Patch): the merged document when Sample All Layers is on, the
  // active pixel layer alone otherwise (null QImage when no pixel layer is
  // active). Captured once per gesture, never per move.
  [[nodiscard]] QImage retouch_source_snapshot();
  void set_document_internal(Document* document, bool preserve_frame_for_same_size,
                             bool normal_composite_unchanged = false);
  void start_async_render_cache_refresh();
  void document_changed_async_preview_impl(bool preserve_scaled_document);
  void cancel_async_render_cache_refresh() noexcept;
  void invalidate_vector_preview() noexcept;
  [[nodiscard]] bool vector_preview_available_for_view() const noexcept;
  [[nodiscard]] VectorPreviewView vector_preview_view() const noexcept;
  [[nodiscard]] bool vector_preview_settled() const noexcept;
  void prepare_vector_preview();
  // Draws the completed preview frame over the composite, then (at deep zoom, with View >
  // Pixel Grid on) the pixel grid alone from the document pixels, since the frame covered
  // the composite's raster, grid included (docs/vector-preview.md, "Overlay order").
  bool draw_vector_preview(QPainter& painter, QRect exposed_rect);
  void report_vector_preview_status(QString status, bool notice = false);
  // Dynamic Vector Preview through a Free Transform session (docs/vector-preview.md): a
  // single shape layer's session, without a pending warp or a raster mask, previews its
  // transformed path at view resolution between drags. The key (layer and the delta the
  // commit would apply) changes whenever the session does, which rebuilds the scene.
  [[nodiscard]] bool free_transform_vector_preview_possible() const;
  [[nodiscard]] std::optional<std::pair<LayerId, QTransform>> free_transform_vector_preview_key() const;
  // The document as the commit would leave it: the shape's vector data through the key's delta.
  [[nodiscard]] Document free_transform_vector_preview_document(const std::pair<LayerId, QTransform>& key) const;
  // The delta a commit applies to a shape layer's vector data (shape_free_transform_delta
  // fits an undistorted box to the path; a distorted one takes the session homography).
  [[nodiscard]] QTransform shape_layer_commit_delta(const Layer& layer) const;
  // True when a paint should keep showing the previous frame and let the async
  // refresh swap the new composite in, instead of blocking the paint on a full
  // recomposite: the cache is dirty, a same-size previous frame exists, no
  // explicit processing operation wants the overlay, and the document is at
  // overlay scale (kProcessingOverlayDirtyAreaThreshold - deliberately the
  // compile-time constant, so tests that force the overlay path via
  // PATCHY_PROCESSING_OVERLAY_MIN_PIXELS keep their blocking semantics).
  [[nodiscard]] bool should_defer_full_refresh_to_async() const noexcept;
  // First-paint variant for many-layer documents (kDeferFullRefreshMinLayers):
  // no same-size previous frame exists, so paint shows checkerboard plus the
  // processing spinner while the async composite runs, instead of blocking for
  // seconds. Pixel-huge documents keep their historical synchronous first paint.
  [[nodiscard]] bool should_defer_first_render_to_async() const noexcept;
  // Whether that first-render wait is currently on screen (drives the spinner
  // overlay and its animation timer without any stored state to go stale).
  [[nodiscard]] bool first_render_spinner_active() const noexcept;
  [[nodiscard]] std::vector<RenderedDocumentPatch> render_document_patches_with_processing(
      const QRegion& document_region, const std::vector<std::pair<LayerId, Rect>>& layer_bounds,
      bool force_processing_wait);
  [[nodiscard]] bool dirty_region_should_use_processing_wait(const QRegion& document_region) const noexcept;
  void refresh_render_cache_rect(QRect document_rect);
  void refresh_render_cache_region(const QRegion& document_region);
  bool patch_render_cache_rect(QRect document_rect, const QImage& partial);
  bool patch_render_cache_patches(const std::vector<RenderedDocumentPatch>& patches);
  void invalidate_display_mip_cache() noexcept;
  void patch_display_mip_cache(const std::vector<RenderedDocumentPatch>& patches);
  void refresh_curves_clipping_preview();
  void ensure_move_base_cache();
  bool request_move_preview();
  // Auto-nesting during a Move drag (docs/artboards.md, "Fast paths"): the re-parenting
  // the release will apply at `delta`, the document the live patches render from (the
  // re-nested copy, cached while the moves stay the same), the rect the proxy blit clips
  // to (the artboard the moving content lands in, nullopt for none or a mixed set), and
  // whether the proxy can stand in at all (every artboard composites plainly).
  [[nodiscard]] std::vector<ArtboardNestMove> move_nest_moves_for_delta(QPoint delta) const;
  [[nodiscard]] const Document& move_patch_source(const Document& base, int level, QPoint delta);
  [[nodiscard]] std::optional<QRect> move_proxy_clip_document_rect(QPoint delta) const;
  [[nodiscard]] bool move_proxy_allowed_for_artboards() const;
  // `source` with every artboard that owns a moving layer turned into a plain group: the
  // proxy snapshot then holds the moving content without paper or clip.
  [[nodiscard]] Document move_proxy_snapshot_source(const Document& source) const;
  void set_move_preview_requested(bool requested);
  bool should_prepare_move_preview_async() const noexcept;
  void cancel_move_preview() noexcept;
  // Deferred Move commit (see mouseReleaseEvent and the MoveCommitJob members).
  [[nodiscard]] bool can_hold_move_commit_preview(QPoint commit_delta) const noexcept;
  void arm_move_commit_hold(QPoint commit_delta);
  void clear_move_commit_hold() noexcept;
  void start_move_commit_job(const QRegion& document_region);
  void finish_move_commit_job(std::uint64_t generation);
  void cancel_move_commit_job() noexcept;
  void wait_for_move_commit_job();
  void clear_move_base_cache() noexcept;
  // Builds the once-per-drag snapshot of ONLY the moving subtree (intra-set
  // blending, clip runs, and styled ancestor folders baked in), downscaled to
  // kMoveProxyMaxPixels when needed. Returns false when no snapshot can back a
  // proxy preview (empty/oversized/failed render) so the caller falls back to
  // the dashed outline.
  [[nodiscard]] bool ensure_move_proxy_image();
  void clip_painter_to_transform_artboard(QPainter& painter) const;
  [[nodiscard]] QRect move_proxy_dirty_rect(QPoint old_delta, QPoint new_delta) const;
  void clear_move_proxy() noexcept;
  // Hash of the sorted moving-layer ids and the composite level; keys the
  // persistent slow-frame latch (see move_live_frame_slow_).
  [[nodiscard]] std::uint64_t move_live_latch_key() const;
  void reset_move_live_latch() noexcept;
  // Retention of the move base cache + proxy snapshot across consecutive
  // drags of the SAME selection: the base excludes the moving set entirely
  // (so its pure translation cannot invalidate it) and the proxy content
  // translates rigidly with the commit. retain_move_preview_caches runs on
  // the release routes that change nothing else (precommit-patch success or
  // zero delta); every external document change funnels through
  // invalidate_retained_move_caches, which defers to the release when a drag
  // is active (mid-drag clears would strand the in-flight proxy blit).
  void retain_move_preview_caches(const std::vector<LayerId>& committed_ids, QPoint committed_delta,
                                  bool proxy_content_complete);
  void clear_retained_move_caches() noexcept;
  void invalidate_retained_move_caches() noexcept;
  // PREVIEW-ONLY scaled document for display-resolution compositing: built
  // lazily per level, kept across drags, dropped on any document change.
  // Returns nullptr when level < 1 or the document is unavailable.
  [[nodiscard]] Document* preview_scaled_document_for_level(int level);
  void clear_preview_scaled_document() noexcept;
  // After a move commit whose release patched the render cache (keeping the
  // scaled document alive), refit the scaled copies of the committed layers to
  // their new positions; a later proxy snapshot renders them without bounds
  // overrides, so stale positions showed old content.
  void retarget_preview_scaled_for_committed_move(const std::vector<LayerId>& committed_ids);
  [[nodiscard]] const QImage& display_image_for_zoom();
  [[nodiscard]] const QImage& curves_clipping_display_image_for_zoom();
  [[nodiscard]] const QImage& move_base_display_image_for_zoom();
  [[nodiscard]] const QImage& warp_base_display_image_for_zoom();
  [[nodiscard]] QColor compose_document_pixel(std::int32_t x, std::int32_t y) const;
  // The displayed composite over `area` (inside the document), like compose_document_pixel.
  [[nodiscard]] QImage compose_document_rect(QRect area) const;
  void draw_checkerboard(QPainter& painter, const QRectF& rect, QRect exposed_rect) const;
  void draw_deep_zoom_image(QPainter& painter, const QImage& image, QRect exposed_rect) const;
  // The deep-zoom renderer for the document pixels of document_rect held in `image`:
  // one deep_zoom_raster blit of the exposed part, pixel grid included. With `grid_only`
  // it blits the grid lines alone (transparent elsewhere) over whatever is already drawn.
  void draw_deep_zoom_pixels(QPainter& painter, const QImage& image, QRect document_rect, QRect exposed_rect,
                             bool grid_only = false) const;
  // Seamless tiling mode internals (canvas_widget_render.cpp). tiling_ghosts_visible is
  // the cheap gate; tiling_direct_offsets returns the wrap offsets whose ghost tiles
  // intersect widget_rect, or an EMPTY vector when there are more than the direct-draw
  // limit (callers then use the textured-fill path / a full-widget update).
  [[nodiscard]] bool tiling_ghosts_visible(QRect widget_rect) const;
  [[nodiscard]] std::vector<QPoint> tiling_direct_offsets(QRect widget_rect) const;
  void draw_tiling_preview(QPainter& painter, const QRectF& target_rect, bool pixel_aligned_view,
                           QRect exposed_rect);
  [[nodiscard]] QPoint shape_constrained_current() const;
  void draw_shape_preview(QPainter& painter, QRect exposed_rect);
  // Shape-mode live preview with the actual fill/stroke appearance; returns
  // false when the tool/geometry has no appearance form (caller falls back).
  bool draw_shape_appearance_preview(QPainter& painter, const ShapePreviewAppearance& appearance);
  void draw_drag_size_readout(QPainter& painter) const;
  void draw_transform_drag_readout(QPainter& painter) const;
  // The readout sits outside the bounded drag repaints, so its old and new rects
  // join them explicitly; release/cancel clear the last rect.
  void update_drag_readout_region();
  void clear_drag_readout();
  [[nodiscard]] std::optional<QRectF> moving_layers_readout_base_rect() const;
  void draw_text_rect_preview(QPainter& painter) const;
  void draw_zoom_preview(QPainter& painter) const;
  void draw_selection_overlay(QPainter& painter) const;
  void draw_free_transform(QPainter& painter) const;
  void draw_transform_controls(QPainter& painter, QRectF document_rect, double angle_degrees) const;
  // The filled handle squares alone (no box, no rotate stem); shared by the
  // transform controls and the marquee resize handles.
  void draw_transform_handle_squares(QPainter& painter, QRectF document_rect, double angle_degrees,
                                     bool include_rotate) const;
  void draw_move_transform_controls(QPainter& painter) const;
  void draw_grid_overlay(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const;
  void draw_guides_overlay(QPainter& painter) const;
  void draw_rulers(QPainter& painter) const;
  // Document pixels per ruler unit along one axis (1 for Pixels; per-axis PPI through
  // the unit for physical units; extent/100 for Percent).
  [[nodiscard]] double ruler_pixels_per_unit(bool horizontal_axis) const noexcept;
  void show_ruler_unit_menu(QPoint global_position);
  void draw_processing_overlay(QPainter& painter) const;
  // canvas_widget_preview_points.cpp
  void draw_preview_point_handles(QPainter& painter) const;
  [[nodiscard]] bool press_preview_point_handle(QMouseEvent* event);
  [[nodiscard]] bool move_preview_point_handle(QMouseEvent* event, bool finished);
  // The marker a press at `widget_position` would grab (the topmost within reach), or -1.
  [[nodiscard]] int preview_point_handle_at(QPointF widget_position) const;
  // While markers are shown the canvas cursor is theirs: a hand over a marker or while
  // dragging one, an arrow elsewhere (the dialog locks the tool). False with no markers.
  bool apply_preview_point_cursor();
  [[nodiscard]] bool preview_render_overlay_visible() const;
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
  [[nodiscard]] QPoint snapped_document_point(QPoint point) const;
  // Layers a pending Free Transform session owns: their pre-session edges are
  // not snap targets for the session's own drags.
  [[nodiscard]] std::vector<LayerId> free_transform_snap_exclude_ids() const;
  [[nodiscard]] QPointF snapped_document_point_f(QPointF point) const;
  // Every enabled snap target except the grid, in the order guides, document,
  // selection, layers (the tie-break order every snap path shares). Layers
  // whose id is in `exclude_ids` (the moving set) contribute nothing.
  void collect_snap_candidates(const std::vector<LayerId>& exclude_ids, std::vector<SnapCandidate>& x_candidates,
                               std::vector<SnapCandidate>& y_candidates) const;
  void append_snap_target_candidates(std::vector<double>& x_candidates,
                                     std::vector<double>& y_candidates) const;
  [[nodiscard]] QPoint snapped_rect_delta(QRect source_rect, QPoint raw_delta) const;
  [[nodiscard]] QPoint snapped_marquee_current_point(QPoint anchor, QPoint current) const;
  [[nodiscard]] QPoint snapped_move_delta(QPoint raw_delta) const;
  [[nodiscard]] MoveSnapResult snapped_move_delta_with_matches(QPoint raw_delta) const;
  // Alignment-guide overlay bookkeeping (the drag-readout pattern: bounded
  // repaints over the previous and next widget rects, cleared on release).
  [[nodiscard]] QRect move_snap_guides_widget_rect() const;
  void update_move_snap_guides_region();
  void clear_move_snap_guides();
  void draw_move_snap_guides(QPainter& painter) const;
  [[nodiscard]] int guide_at_widget_position(QPoint widget_position) const;
  [[nodiscard]] GuideOrientation guide_orientation_from_ruler(QPoint widget_position) const noexcept;
  [[nodiscard]] bool widget_position_in_ruler(QPoint widget_position) const noexcept;
  // Over either ruler or their shared corner: guides are neither grabbed nor kept there.
  [[nodiscard]] bool over_ruler_bars(QPoint widget_position) const noexcept;
  void clear_guide_selection() noexcept;
  void begin_guide_drag(int guide_index, QPoint widget_position);
  void begin_new_guide_drag(QPoint widget_position);
  void update_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  void finish_guide_drag(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  void cancel_guide_drag();
  [[nodiscard]] bool document_contains(QPoint point) const noexcept;
  [[nodiscard]] bool selection_allows(QPoint point) const noexcept;
  [[nodiscard]] bool selection_clips_grayscale_edits() const noexcept;
  [[nodiscard]] Layer* active_pixel_layer() const noexcept;
  [[nodiscard]] LayerMask* active_layer_mask() const noexcept;
  [[nodiscard]] bool editing_layer_mask() const noexcept;
  [[nodiscard]] bool smart_filter_mask_pixels_are_valid(const PixelBuffer& pixels) const noexcept;
  [[nodiscard]] DocumentChannel* active_document_channel() const noexcept;
  [[nodiscard]] const DocumentChannel* active_document_channel_const() const noexcept;
  struct GrayscaleEditTarget {
    PixelBuffer* pixels{nullptr};
    QRect bounds;
  };
  [[nodiscard]] std::optional<GrayscaleEditTarget> active_grayscale_edit_target(QRect required_rect = {});
  [[nodiscard]] bool editing_grayscale_target() const noexcept;
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
  void request_layer_selection(std::vector<LayerId> layer_ids, LayerId active_id);
  // Escape with nothing to cancel, and Move-tool empty clicks/rectangles:
  // clear the layer selection and the active layer (no history entry).
  void request_layer_deselection();
  // For a tool that needs a layer: with no active layer, a one-layer
  // document's only layer becomes the selection instead of the gesture
  // being refused (MainWindow::select_only_layer_if_none_active's twin).
  void select_only_layer_if_none_active();
  // Move-tool section of the canvas context menu (canvas_widget_move.cpp):
  // the hit leaf layers under the pointer. Returns whether any entry was added.
  bool add_move_layer_menu_entries(QMenu& menu, QPoint widget_point);
  // Backdrop-color section of the canvas context menu, for a click outside the document.
  void add_backdrop_color_menu_entries(QMenu& menu);
  void close_canvas_context_menu();
  // Canvas context menus are never deleted while the click that picked an entry is still
  // being dispatched (see show_canvas_context_menu). A hidden menu is retired here and
  // reaped later, once its pick has finished or at the next menu at the same loop level.
  void retire_canvas_context_menu(QMenu* menu);
  void reap_retired_context_menus();
  // `duplicate_roots` non-empty: an Alt press (GitHub issue 69); the first
  // drag frame asks move_duplicate_requested_callback_ to copy those roots and
  // then drags the copies.
  void begin_move_drag(const std::vector<LayerId>& layer_ids, QPoint document_point, QPoint widget_point,
                       std::vector<LayerId> duplicate_roots = {});
  // kMinZoom/kMaxZoom bound the VIEW zoom; this clamps a logical scale to them.
  [[nodiscard]] double clamp_logical_zoom(double logical_zoom) const noexcept;
  void begin_move_layer_selection(QMouseEvent* event, const Layer* clicked_layer, bool rectangle_allowed);
  // The layer a Move-tool click on `leaf` selects under the Auto-Select target.
  [[nodiscard]] const Layer* auto_select_target_layer(const Layer* leaf) const;
  struct MoveSelectionCandidate {
    LayerId target{};
    QRectF bounds;  // the leaf's Move rect
  };
  // Every leaf a selection rectangle can pick, with the target it selects; one
  // walk of the tree on cached Move rects (no pixel work once they are cached).
  [[nodiscard]] std::vector<MoveSelectionCandidate> collect_move_selection_candidates() const;
  // The targets whose leaves intersect `box` (document space, already clipped to
  // the document), deduplicated in candidate order.
  [[nodiscard]] static std::vector<LayerId> move_selection_matches(
      const std::vector<MoveSelectionCandidate>& candidates, const QRectF& box);
  // The rectangle's document box clipped to the document (where the release
  // matches), empty when it lies wholly on the pasteboard.
  [[nodiscard]] QRectF move_layer_selection_box(QPointF anchor_document, QPointF current_document) const;
  void update_move_layer_selection_preview();
  // A leaf's Move rect, or for a group the union of its visible leaves' rects:
  // the dashed outline the hover and the rectangle pre-selection draw.
  [[nodiscard]] std::optional<QRect> move_target_outline_bounds(const Layer& layer) const;
  bool update_move_layer_selection(QMouseEvent* event);
  void finish_move_layer_selection(QMouseEvent* event);
  void cancel_move_layer_selection();
  void draw_move_layer_selection(QPainter& painter) const;
  [[nodiscard]] QRect move_layer_selection_widget_rect() const;
  [[nodiscard]] QPoint layer_position(const Layer& layer, QPoint document_point) const noexcept;
  [[nodiscard]] QRect widget_rect_for_document_rect(QRect document_rect) const;
  [[nodiscard]] QRectF widget_rect_for_document_rect(QRectF document_rect) const;
  // The widget rect to repaint after the displayed pixels of document_rect change: the
  // smooth display scale blends each pixel into its neighbours on screen.
  [[nodiscard]] QRect widget_repaint_rect_for_document_rect(QRect document_rect) const;
  bool begin_edit(QString label);
  // Called right after a successful begin_edit by the painting_ gestures: emits the
  // Begin history event and starts collecting the stroke's dirty rectangles.
  void arm_paint_stroke_history();
  // Emits the Finish event with the collected bounds (no-op when not armed).
  void finish_paint_stroke_history();
  // The stroke's LayerGrowthReserve (docs/performance.md): opened by arm_paint_stroke_history
  // for a pixel layer smaller than the canvas, closed (the layer trimmed to its exact
  // growth) on release and by finish_paint_stroke_history, before the stroke is reported.
  void open_stroke_growth_reserve();
  void close_stroke_growth_reserve();
  // Painting across artboards (docs/artboards.md): a paint press on an artboard that does
  // not own the active layer makes that artboard's topmost paintable pixel layer active
  // instead of painting where its own artboard clips the paint away. Returns the artboard
  // when it has no such layer: add_paint_layer_on_artboard, called right after begin_edit
  // (inside the stroke's undo step), gives it an empty one.
  [[nodiscard]] std::optional<LayerId> retarget_paint_to_clicked_artboard(QPoint document_point);
  void add_paint_layer_on_artboard(std::optional<LayerId> artboard_id);
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
  // One sample of a paint stroke (Brush, Eraser, Clone, Healing, Smudge, the
  // local adjustment brushes, Mixer): stamps from the previous sample to this
  // one and returns the document rect it wrote.
  QRect advance_paint_stroke(QPoint document_point, QPointF document_point_f, Qt::KeyboardModifiers modifiers,
                             CanvasTool effective_tool);
  [[nodiscard]] std::vector<QPointF> coalesced_stroke_positions(const QMouseEvent& event);
  // Keeps queued X11 motion uncompressed exactly while painting_ is set.
  void sync_stroke_input_compression();
  void begin_brush_smoothing(QPointF document_point) noexcept;
  void reset_brush_smoothing() noexcept;
  // Stroke Smoothing (the taut-leash stabilizer). It applies between the axis
  // constraint and the always-on midpoint smoother, for Brush/Mixer/Eraser
  // strokes only; smoothing 0 never consults it (exact pass-through).
  [[nodiscard]] static bool tool_uses_stroke_stabilizer(CanvasTool tool) noexcept;
  [[nodiscard]] double stroke_stabilizer_leash_radius() const noexcept;
  void begin_stroke_stabilizer(QPointF document_point, CanvasTool effective_tool);
  [[nodiscard]] QPointF stabilized_stroke_point(QPointF constrained_point, CanvasTool effective_tool);
  [[nodiscard]] QRect stroke_leash_overlay_rect() const;
  void invalidate_stroke_leash_overlay();
  void draw_stroke_leash_overlay(QPainter& painter) const;
  [[nodiscard]] QRect advance_smoothed_brush_stroke(QPointF document_point, bool erase);
  [[nodiscard]] QRect finish_smoothed_brush_stroke(QPointF document_point, bool erase);
  [[nodiscard]] QRect draw_smoothed_brush_curve(QPointF start, QPointF control, QPointF end, bool erase,
                                                bool stamp_endpoint = false);
  [[nodiscard]] double brush_stamp_spacing(const EffectiveBrushInput& brush) const noexcept;
  [[nodiscard]] bool brush_uses_dab_stroke(const EffectiveBrushInput& brush, bool erase) const noexcept;
  [[nodiscard]] QRect draw_brush_dab(QPointF point, bool erase, EditOptions& options);
  [[nodiscard]] QRect draw_brush_segment_with_dabs(QPointF from, QPointF to, bool erase,
                                                   const EffectiveBrushInput& brush,
                                                   bool stamp_endpoint);
  [[nodiscard]] float capped_stroke_coverage(std::int32_t x, std::int32_t y, float coverage,
                                             float source_alpha);
  [[nodiscard]] float accumulating_stroke_coverage(std::int32_t x, std::int32_t y,
                                                   float coverage, float opacity,
                                                   float flow);
  void install_brush_stroke_compositor(EditOptions& options, bool erase);
  void ensure_brush_stroke_layer_snapshot(LayerId layer_id, const Layer& layer);
  bool render_brush_stroke_pixel_deep(std::int32_t x, std::int32_t y, std::uint8_t* pixel, EditColor primary,
                                      EditColor secondary, bool lock_transparent_pixels, float target_alpha,
                                      bool erase) const;
  [[nodiscard]] std::array<std::uint8_t, 4> brush_stroke_original_pixel(std::int32_t x,
                                                                        std::int32_t y) const;
  [[nodiscard]] bool write_brush_stroke_pixel_from_snapshot(std::int32_t x, std::int32_t y,
                                                            std::uint8_t* pixel,
                                                            std::uint16_t channels,
                                                            EditColor primary,
                                                            EditColor secondary,
                                                            bool lock_transparent_pixels,
                                                            float coverage, bool erase,
                                                            const PaletteSnapContext* palette_snap);
  [[nodiscard]] bool write_brush_stroke_pixel_from_snapshot_blend(std::int32_t x, std::int32_t y,
                                                                  std::uint8_t* pixel,
                                                                  std::uint16_t channels,
                                                                  EditColor primary,
                                                                  EditColor secondary,
                                                                  bool lock_transparent_pixels,
                                                                  float coverage, bool erase,
                                                                  float flow);
  [[nodiscard]] bool render_brush_stroke_pixel_from_snapshot_target(
      std::int32_t x, std::int32_t y, std::uint8_t* pixel, std::uint16_t channels,
      EditColor primary, EditColor secondary, bool lock_transparent_pixels, float target_alpha,
      bool erase) const;
  void observe_wet_edge_coverage(std::int32_t x, std::int32_t y, float coverage,
                                 EditColor primary);
  [[nodiscard]] QRect finalize_pending_wet_edges(QRect dirty);
  void begin_mixer_brush_stroke();
  [[nodiscard]] EditColor sample_mixer_pickup(double x, double y, int brush_size) const;
  [[nodiscard]] EffectiveBrushInput effective_brush_input() const noexcept;
  [[nodiscard]] EditOptions current_brush_edit_options(const EffectiveBrushInput& brush) const;
  [[nodiscard]] QRect draw_brush_segment(QPointF from, QPointF to, bool erase,
                                         bool stamp_endpoint = false);
  [[nodiscard]] QRect draw_brush_segment(QPoint from, QPoint to, bool erase,
                                         bool stamp_endpoint = false);
  [[nodiscard]] QRect draw_brush_at(QPoint point, bool erase);
  [[nodiscard]] QRect draw_airbrush_dab(QPointF point);
  // Stamps the first dab a Direction-controlled stroke still holds at its end (a click).
  [[nodiscard]] QRect finish_brush_tip_stroke(bool erase);
  void remember_held_first_dab_pen_sample(bool was_holding);
  [[nodiscard]] QRect draw_mask_brush_segment(QPointF from, QPointF to, bool erase);
  [[nodiscard]] QRect draw_mask_brush_segment(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_mask_brush_at(QPoint point, bool erase);
  [[nodiscard]] QRect smudge_brush_segment(QPoint from, QPoint to);
  [[nodiscard]] QRect local_adjustment_brush_segment(QPoint from, QPoint to);
  [[nodiscard]] QRect local_adjustment_brush_segment_deep(QPoint from, QPoint to);
  [[nodiscard]] QRect clone_brush_segment_deep(QPoint from, QPoint to);
  [[nodiscard]] PixelBuffer retouch_source_deep();
  void set_clone_source(QPoint point);
  [[nodiscard]] bool begin_pattern_stamp_stroke(QPoint point);
  [[nodiscard]] QRect clone_brush_segment(QPoint from, QPoint to);
  [[nodiscard]] QRect clone_brush_at(QPoint point);
  void draw_pixel(Layer& layer, QPoint document_point, QColor color, bool erase);
  [[nodiscard]] QRect draw_line(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_gradient(QPoint from, QPoint to);
  [[nodiscard]] GradientOptions current_gradient_options() const;
  [[nodiscard]] QRect draw_rectangle(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect draw_ellipse(QPoint from, QPoint to, bool erase);
  [[nodiscard]] QRect flood_fill(QPoint start);
  [[nodiscard]] QRect shape_drag_rect(QPoint anchor, QPoint current) const;
  // Pen tool session (canvas_widget_vector_tools.cpp).
  [[nodiscard]] bool pen_click_closes_path(QPointF document_point) const;
  bool handle_pen_press(QMouseEvent* event, QPointF document_point);
  bool handle_pen_move(QMouseEvent* event, QPointF document_point);
  bool handle_pen_release(QMouseEvent* event);
  bool handle_pen_key(QKeyEvent* event);
  void draw_pen_overlay(QPainter& painter);
  // One hover classification shared by the pen's click editor and its cursor,
  // so what the cursor advertises and what a click does can never disagree.
  struct PenHoverHit {
    PenHoverAction action{PenHoverAction::Draw};
    std::pair<int, int> anchor{-1, -1};   // valid for Delete/Convert
    std::pair<int, int> segment{-1, -1};  // valid for Add
    double segment_t{0.5};                // valid for Add
  };
  [[nodiscard]] PenHoverHit pen_hover_hit(QPointF widget_point, QPointF document_point,
                                          Qt::KeyboardModifiers modifiers) const;
  // The unfiltered classification (what the point under the cursor IS);
  // pen_hover_hit narrows it by tool and the Auto Add/Delete option.
  [[nodiscard]] PenHoverHit pen_hover_hit_raw(QPointF widget_point, QPointF document_point,
                                              Qt::KeyboardModifiers modifiers) const;
  enum class PenEditMode { Auto, DrawOnly, AddOnly, DeleteOnly, ConvertOnly };
  [[nodiscard]] PenEditMode pen_edit_mode() const noexcept;
  [[nodiscard]] static PenHoverHit filter_pen_hit(PenHoverHit hit, PenEditMode mode) noexcept;
  // Applies the hit's Add/Delete/Convert edit to the target path; false for
  // Draw and Close. Shared by the pen click, the anchor tools, and the menu.
  bool apply_pen_hover_edit(const PenHoverHit& hit);
  // The Pen plus the Add/Delete/Convert anchor tools, which share its handlers.
  [[nodiscard]] bool pen_family_tool_active() const noexcept;
  // Returns the badge it applied (Draw while the arrow shows).
  PenHoverAction apply_pen_cursor(QPointF widget_point, Qt::KeyboardModifiers modifiers);
  // Status-bar hover hints. They emit only on the transition INTO an
  // actionable state (never back to plain hovering), so confirmations that
  // other code shows are not overwritten by mouse motion.
  void update_path_hover_hint(PenHoverAction action);
  enum class PathHoverTarget { None, Anchor, Handle, Segment, Fill };
  [[nodiscard]] PathHoverTarget path_hover_target_at(QPointF widget_point) const;
  void update_path_select_hover_hint(PathHoverTarget target);
  bool handle_pen_ctrl_press(QMouseEvent* event, QPointF document_point);
  // Ctrl with a shape tool acts as Path Select (GitHub issue 312).
  bool handle_shape_ctrl_press(QMouseEvent* event, QPointF document_point);
  void set_shape_ctrl_path_select(bool active);
  [[nodiscard]] int pen_session_anchor_at(QPointF widget_point) const;
  // Path editing (canvas_widget_vector_tools.cpp).
  [[nodiscard]] bool path_edit_tool_active() const noexcept;
  // Line, Rectangle, Ellipse, Polygon or Custom Shape.
  [[nodiscard]] bool shape_tool_active() const noexcept;
  // Whether the target path's outline draws with the current tool (GitHub issue 238): always
  // under a path tool; otherwise only while a Paths-panel row is targeted, and a shape layer's
  // own path only under a shape tool. Show Target Path, Ctrl+H and a pen session still apply.
  [[nodiscard]] bool target_path_outline_tool_allows() const;
  // The tool the path-edit handlers should behave as: DirectSelect while the
  // Pen's Ctrl latch is held, PathSelect while a shape tool's Ctrl is, the
  // actual tool otherwise.
  [[nodiscard]] CanvasTool path_edit_tool() const noexcept;
  [[nodiscard]] patchy::Layer* path_edit_target_layer() const;
  // What owns the target path (vector mask, document path, text area, shape layer, or the
  // work path) with its id. The point selection is (subpath, anchor) indices into that one
  // path, so it is dropped the moment the owner changes (GitHub issue 248).
  [[nodiscard]] std::pair<int, std::uint64_t> path_edit_target_owner_key() const;
  void drop_point_selection_if_target_changed();
  // Path Select works on whole groups: every group with a selected anchor becomes fully
  // selected (a switch from Direct Select or the Pen's Ctrl latch).
  void expand_path_selection_to_groups();
  // A Path Select press on `group`: a plain press keeps a selection that already holds the
  // group (a drag moves every selected shape) and otherwise selects just it; Shift adds it,
  // or deselects it when it is already selected. False when the press deselected it.
  bool path_select_press_group(const patchy::VectorPath& path, int group, bool additive);
  // The topmost visible, editable shape layer whose anchor, outline or fill is
  // under the pointer: Path/Direct Select pick it as their target (GitHub issue 248).
  [[nodiscard]] patchy::Layer* topmost_path_pick_layer_at(QPointF widget_point) const;
  // The open subpath end point under the pointer on the target path, if any: its
  // subpath index and whether it is the subpath's first anchor (GitHub issue 248: a Pen
  // click there continues the path).
  [[nodiscard]] std::optional<std::pair<int, bool>> pen_open_endpoint_at(QPointF widget_point) const;
  // Ctrl swaps Path Select and Direct Select while held (GitHub issue 248), the arrow
  // tools' version of the Pen's Ctrl latch; the cursor follows (apply_arrow_cursor).
  void set_arrow_swap(bool active);
  void apply_arrow_cursor();
  // Path transform session hover cursor: Free Transform's resize, move and per-corner
  // rotate cursors for the zone a press would grab (GitHub issue 248).
  void apply_path_transform_cursor(QPoint widget_point, Qt::KeyboardModifiers modifiers);
  mutable std::optional<patchy::VectorPath> text_area_path_cache_;
  mutable std::optional<patchy::LayerId> text_area_path_layer_;
  mutable std::uint64_t text_area_path_revision_{0};
  [[nodiscard]] patchy::Layer* vector_mask_target_layer() const;
  void apply_path_edit(patchy::VectorPath path, const QString& label,
                       const std::vector<int>& touched_groups, const std::map<int, int>& group_copies = {});
  [[nodiscard]] QPointF path_point_to_screen(double x, double y) const;
  [[nodiscard]] std::pair<int, int> path_anchor_at(QPointF widget_point) const;
  [[nodiscard]] int path_handle_at(QPointF widget_point, std::pair<int, int>& anchor) const;
  [[nodiscard]] bool path_segment_at(QPointF widget_point, std::pair<int, int>& segment,
                                     double& segment_t) const;
  // Cross-layer Direct Select: hit tests against an arbitrary path, the
  // eligibility filter for other panel-selected shape layers, and the shared
  // shape-layer write (re-bake + dirty flags + bounded repaint).
  [[nodiscard]] std::pair<int, int> anchor_hit_in(const patchy::VectorPath& path,
                                                  QPointF widget_point) const;
  [[nodiscard]] bool segment_hit_in(const patchy::VectorPath& path, QPointF widget_point,
                                    std::pair<int, int>& segment, double& segment_t) const;
  [[nodiscard]] const patchy::Layer* extra_edit_shape_layer(LayerId id) const;
  void write_shape_layer_path(patchy::Layer& layer, patchy::VectorPath path,
                              const std::vector<int>& touched_groups,
                              const std::map<int, int>& group_copies = {});
  void arm_path_edit_undo(const QString& label);
  void prune_extra_path_selection();
  bool handle_path_edit_press(QMouseEvent* event, QPointF document_point);
  bool handle_path_edit_move(QMouseEvent* event, QPointF document_point);
  // Applies the anchor/handle drag at document_point under the given modifier
  // state; split out of handle_path_edit_move so Shift press/release mid-drag
  // can replay it at the last raw pointer position.
  bool update_path_edit_drag(QPointF document_point, Qt::KeyboardModifiers modifiers);
  // Applies the drag at the latest pointer position if mouse moves arrived since the
  // last apply (handle_path_edit_move queues one apply per batch of moves).
  void flush_pending_path_edit_drag();
  // Photoshop's vertex constraint: snaps the total drag delta onto the nearest
  // horizontal, vertical, or 45-degree axis (re-evaluated per call, no latch).
  [[nodiscard]] static QPointF constrain_drag_to_axes(QPointF total_delta) noexcept;
  // Shift square constraint for the path-edit marquee (mirrors the selection
  // marquee: side = the smaller drag axis, signs preserved).
  [[nodiscard]] QPointF constrain_marquee_current(QPointF current,
                                                  Qt::KeyboardModifiers modifiers) const;
  void notify_path_selection_changed();
  bool handle_path_edit_release(QMouseEvent* event);
  bool handle_path_edit_key(QKeyEvent* event);
  bool pen_modifies_existing_path(QMouseEvent* event, QPointF document_point);
  // Drops selection keys that no longer index into the path: outside edits
  // (Make Work Path replacing the work path, undo shrinking a subpath) can
  // invalidate them between events; every mutating consumer prunes first.
  void prune_path_edit_selection(const patchy::VectorPath& path);
  void delete_selected_path_anchors();
  [[nodiscard]] QPointF path_transform_map_point(QPointF document_point) const;
  // The box's affine (path_transform_map_point as a QTransform), the original box's
  // corners (TL, TR, BR, BL), the corner quad in document space for some offsets (same
  // order), the box corner under an on-screen corner handle (flips swap them), and the
  // four on-screen corners in TL, TR, BR, BL handle order (GitHub issue 248).
  [[nodiscard]] QTransform path_transform_affine() const;
  [[nodiscard]] std::array<QPointF, 4> path_transform_original_corners() const;
  [[nodiscard]] std::array<QPointF, 4> path_transform_quad(const std::array<QPointF, 4>& offsets) const;
  [[nodiscard]] int path_transform_corner_index(TransformHandle corner) const;
  [[nodiscard]] std::array<QPointF, 4> path_transform_handle_corners() const;
  [[nodiscard]] bool path_transform_distorted() const noexcept;
  [[nodiscard]] patchy::VectorPath path_transform_preview_path() const;
  // The groups a transformed path touches (keyShapeInvalidated drops their live shapes).
  [[nodiscard]] std::vector<int> path_transform_touched_groups(const patchy::VectorPath& transformed) const;
  // Live fill (GitHub issue 248): a shape layer or vector mask target re-rasterizes with
  // the preview path on every change, un-armed; the restore puts the layer back as the
  // session found it, only while it still holds the session's last write.
  void refresh_path_transform_live_fill();
  void restore_path_transform_owner();
  [[nodiscard]] TransformHandle path_transform_handle_at(QPointF widget_point) const;
  bool handle_path_transform_press(QPointF document_point, QPointF widget_point, Qt::KeyboardModifiers modifiers);
  bool handle_path_transform_move(QMouseEvent* event, QPointF document_point);
  bool handle_path_transform_key(QKeyEvent* event);
  void draw_path_transform_overlay(QPainter& painter);
  void draw_path_edit_overlay(QPainter& painter);
  // Display-only mapping for a layer whose pixels are being previewed by a
  // Move drag or a Free Transform session: the path overlay follows the
  // preview instead of sitting at the committed position until release.
  // Hit-testing keeps document coordinates (the session owns the mouse).
  [[nodiscard]] QTransform free_transform_preview_delta() const;
  [[nodiscard]] bool transform_is_distorted() const noexcept;
  // The box's affine (free_transform_delta of the session); ignores corner offsets.
  [[nodiscard]] QTransform transform_affine_delta() const;
  // Original box -> document for the whole session: the affine when undistorted,
  // else the homography onto the corner quad.
  [[nodiscard]] QTransform transform_session_delta() const;
  // Source image (covering the original box) -> document, distortion included.
  [[nodiscard]] QTransform transform_session_source_to_document(QSize source_size) const;
  [[nodiscard]] QTransform transform_session_image_to_document(const QImage& image) const;
  [[nodiscard]] std::array<QPointF, 4> transform_original_corners() const;
  [[nodiscard]] std::array<QPointF, 4> transform_quad_for_offsets(const std::array<QPointF, 4>& offsets) const;
  // Original corner index (0 TL, 1 TR, 2 BR, 3 BL) a box handle sits on, flips included.
  [[nodiscard]] int transform_corner_index_for_handle(TransformHandle handle) const noexcept;
  // The two corner handles at the ends of an edge handle, clockwise.
  [[nodiscard]] static std::pair<TransformHandle, TransformHandle> transform_edge_corner_handles(TransformHandle edge);
  [[nodiscard]] static bool transform_handle_is_corner(TransformHandle handle) noexcept;
  [[nodiscard]] static bool transform_handle_is_edge(TransformHandle handle) noexcept;
  [[nodiscard]] QPointF distorted_transform_handle_position(TransformHandle handle) const;
  [[nodiscard]] TransformHandle distorted_transform_handle_at(QPoint widget_point) const;
  // The EdgesAndCorners hit zones (move-tool.md) on a convex quad given in widget space,
  // on-screen corner order TL, TR, BR, BL: the distorted Free Transform box and the
  // Perspective Crop quad share them. `rotate_stem` is the box's rotate handle, nullopt
  // when it has none (the rotate reach outside each corner still reports Rotate).
  [[nodiscard]] static TransformHandle quad_handle_at(const std::array<QPointF, 4>& corners,
                                                      std::optional<QPointF> rotate_stem, QPointF point);
  // A handle's widget position on such a quad (edges at their midpoints; Move at the centroid).
  [[nodiscard]] static QPointF quad_handle_position(const std::array<QPointF, 4>& corners, TransformHandle handle);
  // On-screen outward direction (degrees, y down) of a handle on the distorted quad.
  [[nodiscard]] double distorted_transform_handle_direction(TransformHandle handle) const;
  void draw_distorted_transform_controls(QPainter& painter) const;
  // Cursor for a pointer over the session box (distorted or not).
  // Session hover cursor. `modifiers` (folded from the current event) pick the gesture
  // a press would start, so Ctrl over a handle already shows the distort hint.
  void apply_session_transform_cursor(QPoint widget_point, Qt::KeyboardModifiers modifiers);
  // True when the point lies inside the session's (possibly distorted) box.
  [[nodiscard]] bool transform_session_box_contains(QPoint widget_point) const;
  // The pure Skew/Distort/Perspective pieces (GitHub issue 225), shared with the path
  // transform session (GitHub issue 248): the gesture a handle drag makes under the held
  // modifiers and an Edit > Transform mode; the corner offsets after a drag `delta` (in the
  // original box's frame; corners in the layer's TL, TR, BR, BL order, `edge_corners` the
  // grabbed edge's two corner indices); the convex-quad guard (+1/-1, 0 when concave,
  // self-crossing or collapsed); a handle's on-screen outward direction on a quad given in
  // on-screen TL, TR, BR, BL handle order; and the gesture cursor (false for Scale).
  [[nodiscard]] static TransformDragKind transform_drag_kind_for_mode(TransformHandle handle,
                                                                      Qt::KeyboardModifiers modifiers,
                                                                      TransformDistortMode mode) noexcept;
  [[nodiscard]] static std::array<QPointF, 4> transform_distorted_offsets(
      TransformDragKind kind, int corner, std::pair<int, int> edge_corners, QPointF delta, bool symmetric,
      const std::array<QPointF, 4>& start_offsets, const std::array<QPointF, 4>& original_corners);
  [[nodiscard]] static int transform_quad_winding(const std::array<QPointF, 4>& quad);
  [[nodiscard]] static double transform_quad_handle_direction(const std::array<QPointF, 4>& handle_corners,
                                                              TransformHandle handle);
  bool apply_transform_gesture_cursor(TransformDragKind kind);
  [[nodiscard]] TransformDragKind transform_drag_kind_for(TransformHandle handle,
                                                          Qt::KeyboardModifiers modifiers) const noexcept;
  // Sets up a Skew/Distort/Perspective drag at press; false (with a status message)
  // when the gesture would need a non-affine result the target cannot take.
  bool begin_transform_distort_drag(TransformHandle handle, Qt::KeyboardModifiers modifiers, QPointF document_press);
  void update_free_transform_distort(QPointF document_point, Qt::KeyboardModifiers modifiers);
  [[nodiscard]] QTransform layer_preview_transform(patchy::LayerId id) const;
  // Document-space bounds of the overlay-drawn layer paths that currently
  // ride a preview transform (mapped through it, padded for anchor squares);
  // empty when nothing is previewing. Unioned into the drag repaint rects.
  [[nodiscard]] QRectF path_overlay_preview_document_rect() const;
  [[nodiscard]] patchy::PathSubpath polygon_drag_subpath(QPointF center, QPointF radius_point) const;
  void commit_polygon_drag(QPointF center, QPointF radius_point);
  void commit_custom_shape_drag(QRectF bounds);
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
  // Quick Select stroke lifecycle. The drag only accumulates the brush footprint (seed mask +
  // overlay polyline); the segmentation runs ONCE in finish_quick_select_stroke() after the
  // gesture ends. Do not add live per-move classification before Nov 3, 2029: classify-and-
  // display while brush input is being received is claimed by Adobe's US 8050498.
  void begin_quick_select_stroke(QPoint document_point);
  void extend_quick_select_stroke(QPoint document_point);
  void finish_quick_select_stroke();
  void cancel_quick_select_stroke();
  void stamp_quick_select_segment(QPoint from, QPoint to);
  void draw_quick_select_stroke_overlay(QPainter& painter) const;
  // Object Selection gesture (canvas_widget_object_select.cpp, docs/ai-selection.md). The
  // drag only draws a plain rectangle outline; the network solves ONCE on release from ONE
  // prompt (the box, or the click point) and the result combines by plain selection
  // boolean. See the patent boundary in src/ai/segmentation_engine.hpp.
  void begin_object_select(QPoint widget_position, QPoint document_point, Qt::KeyboardModifiers modifiers);
  void extend_object_select(QPoint document_point);
  void finish_object_select(QPoint widget_position);
  void cancel_object_select();
  void draw_object_select_overlay(QPainter& painter) const;
  // The whole-image SAM embedding of the current object-selection source, computed on a
  // tracked worker the first time the tool is pressed and reused for clicks (and boxes too
  // big for zoom-to-box) until the document, the sampled layer, Sample All Layers, or the
  // sampled pixels change. Document change notifications only mark it for verification: the
  // next press hashes the source pixels and re-encodes only when they differ (a
  // selection-only undo keeps the embedding). A box with a zoom-to-box region encodes that
  // region at release instead (solve_object_select).
  void ensure_object_select_embedding();
  void invalidate_object_select_embedding() noexcept;
  bool solve_object_select(bool is_click, QPoint point, QRect box, SelectionMode operation, QString history_label);
  void mark_object_select_embedding_unverified() noexcept;
  // Spot Healing stroke lifecycle (canvas_widget_spot_healing.cpp). The drag
  // only accumulates the soft brush footprint (mask + overlay polyline); the
  // heal is computed ONCE in finish_spot_heal_stroke() after the gesture ends,
  // from one coherent rigid source mapping (core/spot_heal.hpp, footprint
  // shape only) plus the classic healing membrane of the expired US 6587592
  // (core/heal_membrane.hpp). No PatchMatch-style offset propagation or
  // perturbation, reshuffling, or gradient-domain compositing of source
  // gradients may be added here, and a content-driven source search must
  // follow the exhaustive exemplar boundary in docs/legal-constraints.md
  // (US 8285055/8340463/8355592, US 9058699, live-classification US 8050498).
  void begin_spot_heal_stroke(QPoint document_point, std::optional<QPointF> connect_from);
  void extend_spot_heal_stroke(QPoint document_point);
  void finish_spot_heal_stroke();
  // The release-time heal shared by the Spot Healing stroke and Remove Object:
  // `mask` (row-major coverage over `bounds`) is healed from `snapshot`
  // through `source_map` plus the membrane, written to `layer` and reported
  // to the edit target. `clip_to_selection` multiplies in the selection
  // coverage (a stroke); Remove Object's mask IS the selection. Returns the
  // written rect (empty when nothing was written).
  QRect heal_mask_from_surroundings(QRect bounds, const std::vector<std::uint8_t>& mask, const QImage& snapshot,
                                    const SpotHealSourceMap& source_map, Layer& layer, bool clip_to_selection);
  void cancel_spot_heal_stroke();
  void stamp_spot_heal_segment(QPoint from, QPoint to);
  void draw_spot_heal_stroke_overlay(QPainter& painter) const;
  // Crop tool session (canvas_widget_crop.cpp). crop_drag_rect is the third
  // deliberately-unmerged twin of marquee_selection_rect/shape_drag_rect.
  [[nodiscard]] QRect crop_drag_rect(QPoint anchor, QPoint current) const;
  [[nodiscard]] TransformHandle crop_handle_at(QPoint widget_point) const;
  void begin_crop_drag_out(QMouseEvent* event, QPoint document_point);
  void handle_crop_session_press(QMouseEvent* event);
  void update_crop_drag_out(QPoint document_point, Qt::KeyboardModifiers modifiers);
  void update_crop_adjust_drag(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void update_crop_rotate_drag(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void finish_crop_mouse_release(QMouseEvent* event);
  void nudge_crop_rect(QPoint delta);
  void notify_crop_session_changed();
  void reset_crop_session_state();
  // Frames the canvas, or the active selection's bounds (which override the
  // ratio), when the Crop tool is current and a document is set; a no-op
  // otherwise. Called on tool pick, document swap, and unlock.
  void begin_default_crop_session();
  // A selection set or cleared while the Crop tool still shows its automatic
  // frame (or the box it adopted from the previous selection) re-frames the
  // session so the box follows Select commands and scripts, not only the
  // selection that existed on tool pick. A custom box or an in-flight drag is
  // left alone.
  void follow_selection_into_crop_session();
  // The largest rect of the set ratio inside `within`, centered; `within`
  // itself when no ratio is set.
  [[nodiscard]] QRect ratio_fitted_crop_rect(QRect within) const;
  [[nodiscard]] QRect canvas_document_rect() const noexcept;
  void draw_crop_overlay(QPainter& painter) const;
  // Perspective Crop gestures (canvas_widget_perspective_crop.cpp).
  // What a press grabs on the finished quad: a corner 0-3 (output order), the inside,
  // an edge (Edge + 0 top, 1 right, 2 bottom, 3 left, in output terms) or the rotate
  // reach outside a corner; Free Transform's quad_handle_at zones.
  static constexpr int kPerspectiveCropPartNone = -1;
  static constexpr int kPerspectiveCropPartMove = 4;
  static constexpr int kPerspectiveCropPartEdge = 5;
  static constexpr int kPerspectiveCropPartRotate = 9;
  [[nodiscard]] std::array<QPointF, 4> perspective_crop_widget_quad() const;
  [[nodiscard]] int perspective_crop_part_at(QPoint widget_point) const;
  void handle_perspective_crop_press(QMouseEvent* event);
  void update_perspective_crop_drag(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void finish_perspective_crop_mouse_release(QMouseEvent* event);
  void update_perspective_crop_cursor(QPoint widget_point);
  void draw_perspective_crop_overlay(QPainter& painter) const;
  void draw_perspective_crop_up_marker(QPainter& painter, const QPolygonF& quad) const;
  // Artboards (canvas_widget_artboard.cpp): the canvas backdrop of an artboard document
  // (checkerboard only inside artboards), the outlines and name labels, and the Artboard
  // tool's handles, add buttons and drag previews.
  void draw_canvas_backdrop(QPainter& painter, const QRectF& target_rect, QRect exposed_rect) const;
  void draw_artboard_overlay(QPainter& painter) const;
  [[nodiscard]] std::optional<LayerId> selected_artboard_id() const;
  [[nodiscard]] QRectF artboard_label_widget_rect(const Layer& artboard) const;
  // What the rendered frame shows under a label (GitHub issue 260): a bounded grid of
  // samples over the pasteboard, another artboard's paper or its content, never work
  // proportional to the document. `light` picks the dark ink; `mixed` says the label
  // straddles light and dark (a label hanging over a neighbouring board's edge), so the
  // ink gets a halo in the other ink to read on both.
  struct ArtboardLabelBacking {
    bool light{false};
    bool mixed{false};
  };
  [[nodiscard]] ArtboardLabelBacking artboard_label_backing(const std::vector<const Layer*>& artboards,
                                                            const QRectF& label_widget_rect) const;
  // The artboard a Move-tool press at `widget_position` selects (and a drag moves), or
  // none (GitHub issue 259): its name label with any setting, and with Auto-Select or
  // Ctrl held its area where no layer is under the pointer. Hover, press and cursor
  // share it so the outline promises exactly what the click does.
  [[nodiscard]] std::optional<LayerId> move_artboard_target_at(QPoint widget_position,
                                                               Qt::KeyboardModifiers modifiers) const;
  [[nodiscard]] QRectF artboard_add_button_widget_rect(QRect artboard_rect, int side) const;
  [[nodiscard]] int artboard_add_button_at(QPoint widget_point) const;
  [[nodiscard]] TransformHandle artboard_handle_at(QPoint widget_point) const;
  [[nodiscard]] QRect artboard_create_rect(QPoint anchor, QPoint current, Qt::KeyboardModifiers modifiers) const;
  [[nodiscard]] QRect artboard_resize_rect(QPoint point, Qt::KeyboardModifiers modifiers) const;
  bool handle_artboard_press(QMouseEvent* event);
  void update_artboard_drag(QMouseEvent* event);
  // The moving artboard's pixels and the canvas without it, once per move.
  void capture_artboard_drag_ghost();
  void finish_artboard_drag(QMouseEvent* event);
  void cancel_artboard_drag();
  bool handle_artboard_label_press(QMouseEvent* event);
  bool handle_artboard_label_double_click(QMouseEvent* event);
  bool handle_move_tool_artboard_press(QMouseEvent* event);
  void begin_artboard_move_drag(const Layer& artboard, QMouseEvent* event);
  bool nudge_selected_artboard(QPoint delta);
  void request_artboard_edit(const ArtboardEditRequest& request);
  void update_artboard_tool_cursor(QPoint widget_point);
  // Patch tool drag lifecycle (canvas_widget_patch_tool.cpp). The drag shows
  // only a raw translated copy of the frozen snapshot; the heal is computed
  // ONCE in commit_patch_tool_drag() on release, with the user-dragged offset
  // as the only source choice, through the classic healing membrane of the
  // expired US 6587592 (core/heal_membrane.hpp). No PatchMatch-style offset
  // propagation or perturbation, reshuffling, gradient-domain compositing of
  // source gradients, or live per-move classification may be added here, and
  // a content-driven source search must follow the exhaustive exemplar
  // boundary in docs/legal-constraints.md (US 8285055/8340463/8355592,
  // US 9058699, live-classification US 8050498).
  [[nodiscard]] bool begin_patch_tool_drag(QPoint document_point);
  void update_patch_tool_drag(QPoint document_point);
  void release_patch_tool_drag(QPoint document_point);
  void cancel_patch_tool_drag();
  void commit_patch_tool_drag();
  void draw_patch_tool_drag_preview(QPainter& painter) const;
  void draw_patch_tool_drag_outline(QPainter& painter) const;
  [[nodiscard]] QCursor quick_select_cursor(SelectionMode mode) const;
  [[nodiscard]] QRegion marquee_selection_region(QPoint anchor, QPoint current) const;
  [[nodiscard]] QRect marquee_selection_rect(QPoint anchor, QPoint current) const;
  // Corner radius the rectangular marquee actually draws with: the user radius
  // clamped to half of `rect`, 0 for other tools or a zero setting.
  [[nodiscard]] double marquee_effective_corner_radius(QRect rect) const noexcept;
  [[nodiscard]] QImage marquee_selection_mask(QPoint anchor, QPoint current, QRect& bounds) const;
  // The live tool state (tool, Radius, Feather, Anti-alias) packed around `rect`.
  [[nodiscard]] MarqueeShape current_marquee_shape(QRect rect) const;
  // Rasterizers shared by the drag-out and the resize handles; they read only
  // the shape, never the live options, so a resize redraws what was drawn.
  [[nodiscard]] QRegion marquee_shape_region(const MarqueeShape& shape) const;
  [[nodiscard]] QImage marquee_shape_mask(const MarqueeShape& shape, QRect& bounds) const;
  // Replaces the selection with `shape` and remembers it as resizable.
  void apply_marquee_shape(const MarqueeShape& shape);
  // The remembered marquee rect while a marquee tool can resize it (not in
  // Quick Mask, no gesture in flight); nullopt hides the handles.
  [[nodiscard]] std::optional<QRect> resizable_marquee_rect() const;
  [[nodiscard]] TransformHandle marquee_resize_handle_at(QPoint widget_point) const;
  void update_marquee_resize_drag(QPoint document_point, Qt::KeyboardModifiers modifiers);
  void apply_marquee_resize_rect(QRect rect);
  // True while a gesture rewrites selection_ on every pointer move (a Replace
  // marquee drag-out or a handle resize); Add/Subtract/Intersect drag-outs keep
  // the existing selection until release, so it stays a snap target for them.
  [[nodiscard]] bool selection_is_live_gesture_output() const noexcept {
    return (selecting_ && selection_operation_ == SelectionMode::Replace) ||
           marquee_resize_handle_ != TransformHandle::None;
  }
  void draw_marquee_resize_handles(QPainter& painter) const;
  [[nodiscard]] QImage lasso_selection_mask(const QPolygon& polygon, QRect& bounds) const;
  [[nodiscard]] QImage lasso_selection_mask(const QPolygonF& polygon, QRect& bounds) const;
  // The one lasso commit (freehand, polygonal and magnetic): with Feather or Anti-alias the
  // outline rasterizes through lasso_selection_mask (mask_outline, which the Magnetic Lasso
  // smooths first), otherwise through a hard winding-fill QRegion clipped to the canvas, and
  // the result combines under selection_operation_. The caller owns the before-edit snapshot.
  void combine_selection_from_lasso(const QPolygon& outline);
  void combine_selection_from_lasso(const QPolygon& outline, const QPolygonF& mask_outline);
  // Polygonal Lasso session (docs/selection-tools.md). Clicks place vertices with the button
  // up between them and a rubber band follows the pointer; the region is built once, in
  // finish_polygon_lasso(). Shift keeps a segment at 45 degree steps, an Alt drag adds a
  // freehand run. from_lasso marks the Lasso's Alt-release switch, which closes on Alt up.
  // outline: the start click, or a freehand Lasso drag's points for the Alt-release switch.
  // The caller latches selection_operation_ first.
  void start_polygon_lasso(QPolygon outline, bool from_lasso);
  [[nodiscard]] QPoint polygon_lasso_target(QPoint document_point, Qt::KeyboardModifiers modifiers) const;
  void update_polygon_lasso_rubber_band(QPoint document_point, Qt::KeyboardModifiers modifiers);
  void add_polygon_lasso_vertex(QPoint document_point);
  void pop_polygon_lasso_vertex();  // Backspace/Delete: back to the previous vertex
  [[nodiscard]] bool polygon_lasso_start_hit(QPoint widget_point) const;
  void finish_polygon_lasso();
  // Magnetic Lasso trace lifecycle. The hover trace only maintains a snapped path polyline
  // (committed segments + the live segment to the cursor); the selection region is built once
  // in finish_magnetic_lasso().
  void start_magnetic_lasso(QPoint document_point, Qt::KeyboardModifiers modifiers);
  [[nodiscard]] QPoint magnetic_snap(QPoint document_point) const;
  // Shortest path from the current anchor to the point, no cooling. snap_target
  // is false for manual anchor clicks: a manual fastening point is the user's
  // correction tool and must land exactly where clicked (Photoshop semantics).
  void extract_magnetic_live_path(QPoint document_point, bool snap_target = true);
  void cool_magnetic_live_path();                          // auto-drop anchors along a long live path
  void add_magnetic_anchor();                              // manual anchor at the live path end
  void pop_magnetic_anchor();                              // Backspace: drop the newest anchor
  // Close the polygon and commit. The closing segment back to the start snaps
  // to edges like the rest of the trace (Photoshop parity); Alt-closes pass
  // false for the straight segment instead.
  void finish_magnetic_lasso(bool magnetic_close = true);
  [[nodiscard]] int magnetic_anchor_spacing() const noexcept;  // SCREEN px between auto anchors
  void set_selection_from_region(QRegion selection);
  void set_selection_from_mask(QRegion selection, QRect mask_bounds, QImage mask_alpha);
  // Snapshot / drop the pre-gesture selection (region, display region, mask,
  // marquee shape) that restore_selection_before_edit and the history entry use.
  void capture_selection_before_edit();
  void clear_selection_before_edit();
  void restore_selection_before_edit();
  void finish_quick_mask_edit();
  void invalidate_quick_mask_display() noexcept;
  // Marks the cached marching-ants outline stale. Must be called by any code
  // that writes selection_ / selection_display_region_ directly instead of
  // going through the setters above.
  void invalidate_selection_outline() noexcept;
  // The rasterized-selection lookup behind selection_alpha_at (see
  // selection_lookup_bits_): drops it, builds it, and answers one query.
  void invalidate_selection_lookup() noexcept;
  void build_selection_lookup() const;
  [[nodiscard]] bool selection_lookup_contains(QPoint point) const noexcept;
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
  void update_marquee_from_center(Qt::KeyboardModifiers modifiers);
  void refresh_active_marquee_selection();
  [[nodiscard]] bool can_move_selection_at(QPoint document_point, Qt::KeyboardModifiers modifiers) const;
  void apply_selection_move(QPoint delta);
  void nudge_selection(QPoint delta);
  [[nodiscard]] SelectionMode selection_operation(Qt::KeyboardModifiers modifiers) const noexcept;
  [[nodiscard]] QRegion combine_selection(const QRegion& candidate) const;
  void combine_selection_from_region(const QRegion& candidate);
  // Derives the candidate region from the mask itself. Use this overload when
  // the region would come from the same QImage being passed: computing it as a
  // sibling argument of std::move(mask) reads the image after the move has
  // already emptied it (argument evaluation order is unspecified; MSVC goes
  // right-to-left), which silently produced an empty selection.
  void combine_selection_from_mask(QRect candidate_bounds, QImage candidate_alpha);
  void combine_selection_from_mask(QRegion candidate, QRect candidate_bounds, QImage candidate_alpha);
  [[nodiscard]] std::vector<LayerId> movable_layer_ids() const;
  // Appends the movable leaves under `root` (position locks inherited from
  // `ancestor_flags`, no duplicates) to `ids`: the shared walk behind
  // movable_layer_ids and alignment_units.
  void collect_movable_leaf_ids(const Layer& root, LayerLockFlags ancestor_flags, std::vector<LayerId>& ids) const;
  // The selected roots alignment_units works from: `root_ids` normalized, or
  // the canvas selection (falling back to the active layer) when empty.
  [[nodiscard]] std::vector<LayerId> alignment_root_ids(const std::vector<LayerId>& root_ids) const;
  // Moves each listed layer by its own delta in one history entry: the shared
  // body of arrow-key nudges (move_active_layer_by) and Align/Distribute. Null
  // deltas are skipped; all-null pushes no history. Returns the dirty region
  // (partial when a Smart Filter re-render fails, mirroring the nudge path).
  [[nodiscard]] QRegion offset_layers(const std::vector<std::pair<LayerId, QPoint>>& deltas,
                                      const QString& undo_label, bool record_history = true);
  [[nodiscard]] std::optional<QRect> move_hover_outline_rect_at(QPoint widget_position,
                                                                Qt::KeyboardModifiers modifiers) const;
  void update_move_hover_outline(QPoint widget_position, Qt::KeyboardModifiers modifiers);
  // Whether a plain left press here with the Move tool would drag something (a layer,
  // the selected set, an artboard), mirroring the press handler (GitHub issue 170).
  [[nodiscard]] bool move_press_drags_at(QPoint widget_position) const;
  // The Move tool's idle cursor: transform-box zones, the four-way move cursor where
  // a press drags, the move pointer (arrow with a move badge) where it selects.
  void apply_move_tool_cursor(QPoint widget_position);
  void clear_move_hover_outline();
  [[nodiscard]] QRect moving_layer_outline_rect(const MovingLayer& moving_layer, QPoint delta) const;
  // The document rect the layer dirties at `delta`: bounds shifted, padded by
  // the layer's own effects and by its styled ancestors' summed padding.
  [[nodiscard]] QRect moving_layer_effect_rect(const Layer& layer, const MovingLayer& moving_layer,
                                               QPoint delta) const;
  [[nodiscard]] std::vector<std::pair<LayerId, Rect>> moving_layer_bounds(QPoint delta) const;
  [[nodiscard]] std::vector<std::pair<LayerId, Rect>> moving_layer_bounds(
      const std::vector<MovingLayer>& moving_layers, QPoint delta) const;
  [[nodiscard]] QRegion moving_layers_dirty_region(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRegion moving_layers_dirty_region(const std::vector<MovingLayer>& moving_layers,
                                                   QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRect moving_layers_outline_dirty_rect(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] bool moving_layers_should_use_outline_preview(QPoint old_delta, QPoint new_delta) const;
  [[nodiscard]] QRegion move_active_layer_by(QPoint delta);
  void document_changed_impl(QRegion document_region, bool includes_effect_bounds,
                             DocumentChangeReason reason = DocumentChangeReason::Immediate);
  void active_edit_target_changed_impl(QRegion document_region,
                                       DocumentChangeReason reason = DocumentChangeReason::Immediate);
  void notify_document_changed(DocumentChangeReason reason = DocumentChangeReason::Immediate);
  // Resize cursors follow the box's on-screen rotation (angle in degrees).
  void set_transform_cursor_for_handle(TransformHandle handle, double angle_degrees = 0.0);
  // As above, but a Rotate hit shows the rotate cursor of the corner the pointer
  // is beside (the stem handle keeps the plain rotate cursor).
  void set_transform_cursor_at(QPoint widget_point, TransformHandle handle, QRectF document_rect,
                               double angle_degrees);
  // The corner rotate cursor for a pointer off the box: the nearest corner's, by its
  // on-screen direction. `stem_counts` keeps the plain rotate cursor over the stem handle.
  [[nodiscard]] QCursor transform_rotate_cursor_at(QPoint widget_point, QRectF document_rect,
                                                   double angle_degrees, bool stem_counts) const;
  void update_move_transform_controls_dirty(std::optional<QRectF> old_rect);
  [[nodiscard]] std::optional<QRectF> transform_controls_rect_for_layer(const Layer& layer) const;
  [[nodiscard]] std::optional<QRectF> move_transform_target_rect() const;
  [[nodiscard]] std::optional<QRectF> move_transform_controls_rect() const;
  void set_move_transform_controls_layer(std::optional<LayerId> layer_id);
  void notify_transform_controls_changed();
  [[nodiscard]] QPointF transform_reference_position(QRectF document_rect, double angle_degrees) const;
  bool prepare_free_transform_source();
  [[nodiscard]] TransformTargetCollection collect_free_transform_targets() const;
  [[nodiscard]] static QRectF transform_targets_content_union(const std::vector<TransformTarget>& targets);
  bool begin_free_transform_multi(TransformTargetCollection collection);
  bool ensure_transform_multi_snapshot();
  void refresh_transform_multi_preview_cache(bool processing_wait);
  void commit_free_transform_multi();
  void refresh_transform_composited_preview_cache(bool processing_wait = false);
  [[nodiscard]] const TransformLinkedMaskSource* transform_linked_mask_source(const Layer& layer);
  void refresh_transform_preview_for_drag();
  [[nodiscard]] bool transform_drag_should_use_proxy_preview() const;
  void ensure_transform_proxy_image();
  void refresh_free_transform_preview_caches();
  void rebuild_transform_base_cache();
  [[nodiscard]] QRect transform_preview_document_rect() const;
  void update_transform_preview_region(QRect previous_document_rect);
  [[nodiscard]] const QImage& transform_base_display_image_for_zoom();
  [[nodiscard]] TransformHandle transform_handle_at(
      QPoint widget_point, TransformHitZones zones = TransformHitZones::EdgesAndCorners) const;
  // The session's Scale and Rotate modes (GitHub issue 299): Rotate turns every
  // corner and side into a rotate grip, Scale drops the rotate reach outside the box.
  [[nodiscard]] TransformHandle transform_handle_for_session_mode(TransformHandle handle) const noexcept;
  [[nodiscard]] TransformHandle transform_handle_at(QPoint widget_point, QRectF document_rect, double angle_degrees,
                                                    TransformHitZones zones = TransformHitZones::HandlesOnly) const;
  // Document offset that, added to the pointer, keeps the grabbed side exactly
  // where it was at the press (so a grab beside an edge never jumps it).
  [[nodiscard]] QPointF transform_handle_grab_offset(TransformHandle handle, QRectF document_rect,
                                                     double angle_degrees, QPointF document_press) const;
  [[nodiscard]] QPointF transform_handle_position(TransformHandle handle) const;
  [[nodiscard]] QPointF transform_handle_position(TransformHandle handle, QRectF document_rect,
                                                  double angle_degrees) const;
  void update_free_transform_preview(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void commit_free_transform();
  void commit_free_transform_with_pending_warp();
  // Committed-transform hold frame: the commit paths compose the session's
  // final preview (base + accurate patches, or the same approximate blit the
  // live preview drew) into one canvas image and keep painting it while the
  // render-cache refresh the commit triggered is still pending. Without it,
  // paint fell back to the stale pre-commit cache and the layer flashed at
  // its OLD geometry until the deferred recomposite landed.
  [[nodiscard]] QImage compose_transform_commit_hold_image() const;
  void arm_transform_commit_hold();
  void disarm_transform_commit_hold_if_settled();
  void clear_transform_commit_hold();
  // Quiet state teardown shared by cancel/commit and the warp mode switch: no
  // cursor/update/notify side effects.
  void reset_free_transform_session_state();
  void clear_pending_warp();
  bool resume_pending_warp_session();
  // Warp Transform internals: the working cage lives in CONTENT space and maps to
  // the document through warp_content_to_document_ (for smart objects the mesh
  // hull -> Trnf homography, for pixel layers the layer-bounds translation).
  bool prepare_warp_source();
  // Builds warp_base_cache_ (the document with the warped layer hidden) at the
  // current zoom's composite level when it is missing.
  void ensure_warp_base_cache();
  // Renders warp_preview_patches_ at view resolution (GitHub #200): the display's
  // composite level, only the on-screen document area (plus half a view per side
  // at rest), drag frames with bilinear samples and 16 px cells, resting frames
  // with the session's interpolation and the commit's 4 px cells.
  void refresh_warp_preview_cache();
  // After a zoom or pan: re-renders the resting preview when the view left the
  // rendered area or changed composite level (deferred to the event loop).
  void schedule_warp_preview_view_refresh();
  void refresh_warp_preview_for_view();
  // The document area on screen, grown by margin_fraction of its size per side and
  // clipped to the canvas (the whole canvas while the widget has no size).
  [[nodiscard]] QRect warp_preview_view_rect(double margin_fraction) const;
  // warp_source_image_ box-halved `level` times (cached per session); a null image
  // when the mip cannot be built.
  [[nodiscard]] const QImage& warp_source_for_level(int level);
  // Drops every per-session warp preview cache (base, its mips, patches, source mips).
  void clear_warp_preview_caches();
  [[nodiscard]] std::array<double, 8> warp_document_quad() const;
  // Whether `candidate` keeps the warp placeable (GitHub issue 299): its control hull
  // stays on the near side of the content->document map's horizon (a perspective
  // stage carried into the cage), and its document quad stays within the canvas grown
  // by twice its larger side (at least 4096 px), unless it is no worse than `current`.
  // A handle drag that fails keeps the last good position.
  [[nodiscard]] bool warp_mesh_placement_is_sane(const WarpMeshGrid& candidate,
                                                 const WarpMeshGrid& current) const;
  // Bakes (mesh, content->document map) into the layer at commit quality and, for
  // smart objects, writes the mesh + hull-quad placement metadata. Shared by the
  // warp commit and the free-transform commit of a pending warp session.
  bool bake_warp_into_layer(Layer& layer, const WarpMeshGrid& mesh,
                            const std::array<double, 9>& content_to_document, double content_width,
                            double content_height, const QImage& source_image, bool smart_object,
                            LayerId layer_id, Rect& new_bounds);
  [[nodiscard]] int warp_handle_at(QPoint widget_point) const;
  void draw_warp_transform(QPainter& painter) const;
  void commit_warp_transform();
  void reset_warp_state();
  // One pending-session state for the step history: the free-transform box (rect,
  // angle, flips) or the warp cage (mesh, style), whichever mode is active.
  struct TransformSessionStep {
    QRectF rect;
    double angle{0.0};
    double scale_x_sign{1.0};
    double scale_y_sign{1.0};
    std::array<QPointF, 4> corner_offsets{};
    WarpMeshGrid mesh;
    QString warp_style;
    double warp_style_value{0.0};
  };
  [[nodiscard]] TransformSessionStep current_transform_session_step() const;
  [[nodiscard]] static bool transform_session_steps_equal(const TransformSessionStep& a,
                                                          const TransformSessionStep& b);
  // Pushes `before` when the session state differs from it now. A nudge right after
  // a nudge joins that step, so holding an arrow key is one step.
  void record_transform_session_step(const TransformSessionStep& before, bool nudge = false);
  void restore_transform_session_step(const TransformSessionStep& step);
  void clear_transform_session_steps();
  bool constrain_pan() noexcept;
  // Re-anchors the view after the viewport changed from old_size (GitHub issue 241); true
  // when the pan moved. See docs/view-navigation.md, "Viewport resizes".
  bool keep_view_centered_across_resize(QSize old_size) noexcept;
  void notify_view_changed();
  void sync_scroll_bars();
  void handle_scroll_bar_value_changed(Qt::Orientation orientation, int value);
  void report_status_error(const QString& message) const;
  void emit_info_for_widget_position(QPoint widget_position) const;
  [[nodiscard]] PenInputSample pen_input_sample_from_tablet_event(const QTabletEvent& event) const;
  [[nodiscard]] PenButtonAction pen_action_for_button(Qt::MouseButton button) const noexcept;
  [[nodiscard]] bool pen_recently_in_proximity() const;
  // True while a press-drag (stroke, drag, pan, zoom drag) owns the pointer: trackpad
  // scroll and pinch must not move the view under it.
  [[nodiscard]] bool view_gesture_blocked_by_pointer() const noexcept;
  [[nodiscard]] bool tablet_event_should_pan(const PenInputSample& sample, QEvent::Type event_type) const noexcept;
  [[nodiscard]] bool tablet_event_should_zoom(const PenInputSample& sample, QEvent::Type event_type) const noexcept;
  void begin_zoom_drag(QPointF widget_position);
  void update_zoom_drag(QPointF widget_position);
  void end_zoom_drag();
  // Multiplies the zoom by kZoomDragFactorPerPixel^delta about zoom_drag_anchor_widget_.
  void apply_zoom_drag_step(double delta_pixels);
  // The press position clamped onto the document frame (margin presses zoom
  // toward the nearest document edge).
  [[nodiscard]] QPointF zoom_click_anchor(QPointF widget_position) const;
  // The click direction after Alt inverts the Zoom In / Zoom Out mode.
  [[nodiscard]] bool zoom_tool_zoom_out_active(Qt::KeyboardModifiers modifiers) const noexcept;
  // One Zoom-tool click: a step along the zoom ladder about zoom_click_anchor(widget_position).
  void zoom_click_step(QPointF widget_position, bool zoom_out);
  // Ctrl (Cmd on macOS) or Alt makes a Hand click zoom instead of pan (GitHub issue 304).
  [[nodiscard]] static bool hand_click_zooms(Qt::KeyboardModifiers modifiers) noexcept;
  // The open hand, or the Zoom tool's magnifier while hand_click_zooms (Alt shows the minus).
  void apply_hand_cursor(Qt::KeyboardModifiers modifiers);
  void begin_brush_adjust_drag(QPoint widget_position, bool from_tablet = false);
  void update_brush_adjust_drag(QPoint widget_position);
  void end_brush_adjust_drag(bool commit);
  void draw_brush_adjust_overlay(QPainter& painter) const;
  void draw_brush_adjust_readout(QPainter& painter, QPointF center, double radius) const;
  void notify_brush_settings_changed();
  // Returns the active tip pre-scaled for `size` and feathered for `softness` (the brush Soft
  // setting), from the per-tip cache. With no bitmap tip set this is null unless dynamics are
  // active, in which case the Round brush's synthesized disc stamp is returned.
  [[nodiscard]] std::shared_ptr<const patchy::ScaledBrushTip> scaled_brush_tip_for(int size,
                                                                                   int softness) const;
  void apply_brush_tip_to_options(EditOptions& options, int brush_size, int brush_softness) const;
  [[nodiscard]] QImage brush_tip_stamp_image(int size, int softness) const;
  // Sets a cursor tracing the active tip's outline; false when there is no usable tip shape.
  bool apply_brush_tip_cursor();
  // Brushes whose on-screen footprint exceeds the OS-cursor cap draw their outline as a canvas
  // overlay that follows the pointer instead (the cursor becomes a plain crosshair).
  // The size that drives the hover outline/cursor circle for the active tool (the Quick Select
  // brush has its own diameter, separate from the paint brush).
  [[nodiscard]] int active_outline_brush_size() const noexcept;
  [[nodiscard]] QSize brush_outline_display_size() const;
  [[nodiscard]] bool brush_outline_uses_overlay() const;
  [[nodiscard]] QRect brush_hover_outline_rect() const;
  void track_brush_hover_position(QPoint widget_position);
  void invalidate_brush_hover_outline(const QRect& previous_outline);
  void draw_brush_hover_outline(QPainter& painter) const;
  void track_eyedropper_sample_outline(QPoint widget_position);
  void hide_eyedropper_sample_outline();
  void draw_eyedropper_sample_outline(QPainter& painter) const;
  [[nodiscard]] QRect sample_loupe_viewport() const;
  void refresh_sample_loupe(QPoint widget_position, QPoint document_point);
  void clear_sample_loupe();
  void draw_sample_loupe(QPainter& painter) const;
  bool handle_opacity_digit_key(int key, Qt::KeyboardModifiers modifiers, bool auto_repeat);
  bool perform_pen_button_action(PenButtonAction action, const PenInputSample& sample);
  bool dispatch_tablet_as_mouse(QTabletEvent* event, const PenInputSample& sample);

  Document* document_{nullptr};
  double zoom_{1.0};
  QPointF pan_{40.0, 40.0};
  // The viewport size pan_ was last placed or re-anchored for; invalid until something
  // places the view. A hidden tab's resize arrives at show time without an old size, so
  // the re-anchor reads this instead.
  QSize view_size_;
  bool wheel_zooms_{true};
  // Set by a press, cleared by the next ScrollBegin: drops leftover flick momentum.
  bool swallow_scroll_momentum_{false};
  bool zoom_scrubby_{false};
  bool zoom_tool_zooms_out_{false};
  QScrollBar* horizontal_scroll_bar_{nullptr};
  QScrollBar* vertical_scroll_bar_{nullptr};
  bool syncing_scroll_bars_{false};
  QImage render_cache_{};
  bool render_cache_dirty_{true};
  bool tiling_preview_enabled_{false};
  // Cached scaled tile for the textured-fill ghost path; cleared alongside the display
  // mips (invalidate_display_mip_cache), which run at every render_cache_ content change.
  QPixmap tiling_tile_pixmap_{};
  QSize tiling_tile_pixmap_size_{};
  RenderCacheDiagnostics render_cache_diagnostics_{};
  bool async_render_cache_in_flight_{false};
  bool async_render_cache_pending_{false};
  bool async_render_cache_explicit_hold_{false};
  bool async_render_cache_start_queued_{false};
  std::uint64_t async_render_cache_generation_{0};
  // The generation the in-flight refresh snapshotted at. It trails
  // async_render_cache_generation_ once an invalidation outdated that
  // snapshot; only then does a repaint mark the refresh pending.
  std::uint64_t async_render_cache_in_flight_generation_{0};
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
  bool brush_smoothing_had_movement_{false};
  std::optional<BrushStrokeLayerSnapshot> brush_stroke_layer_snapshot_;
  QPoint clone_source_point_{};
  QPoint clone_source_offset_{};
  QPoint shape_start_{};
  QPoint shape_current_{};
  bool shape_square_constrained_{false};
  bool shape_from_center_{false};
  // Shift held at the shape press: set per press, cleared once the release commits.
  bool shape_press_adds_to_shape_{false};
  MarqueeStyle shape_style_{MarqueeStyle::Normal};
  QSize shape_fixed_size_{1024, 768};
  VectorToolMode vector_tool_mode_{VectorToolMode::Shape};
  std::function<void(patchy::LiveShapeKind, QRectF, QPointF, QPointF)> vector_shape_drawn_callback_;
  std::function<void(patchy::VectorPath, bool, VectorPathSource)> vector_path_committed_callback_;
  std::function<void(CanvasTool, QPointF)> shape_create_requested_callback_;
  // Path Select / Direct Select double-click on a shape layer's geometry.
  std::function<void()> shape_appearance_requested_callback_;
  std::function<std::optional<ShapePreviewAppearance>()> shape_preview_appearance_callback_;
  int polygon_sides_{5};
  int polygon_star_inset_{0};
  std::shared_ptr<const patchy::VectorPath> custom_shape_path_;
  bool custom_shape_keep_proportions_{false};
  std::vector<patchy::PathAnchor> pen_anchors_;
  bool pen_session_active_{false};
  bool pen_handle_dragging_{false};
  bool pen_handles_broken_{false};
  QPointF pen_hover_document_{};
  // Ctrl held at press latches the gesture onto the path-edit handlers with
  // DirectSelect semantics (press -> release; releasing Ctrl mid-drag keeps it).
  bool pen_temp_direct_select_{false};
  // Shape tools: Ctrl held while idle (cursor and overlay show Path Select), and the
  // gesture a Ctrl press latched onto the path-edit handlers (press -> release).
  bool shape_ctrl_path_select_{false};
  bool shape_temp_path_select_{false};
  bool pen_auto_add_delete_{true};
  PenHoverAction path_hover_hint_action_{PenHoverAction::Draw};
  PathHoverTarget path_hover_hint_target_{PathHoverTarget::None};
  // Ctrl-drag of an in-progress session anchor: index into pen_anchors_, -1 idle.
  int pen_session_drag_anchor_{-1};
  QPointF pen_session_drag_last_document_{};
  // Path-edit session state (selection keys are (subpath, anchor) indices).
  enum class PathEditDrag { None, Anchors, HandleIn, HandleOut, Marquee };
  std::set<std::pair<int, int>> path_selected_anchors_;
  // Set while a press that picked another shape layer replays on the new target.
  bool path_press_repicked_ = false;
  // Ctrl held over an arrow tool: path_edit_tool() reports the other arrow.
  bool arrow_swap_active_ = false;
  // Whether the point selection was made by Path Select (whole groups): a Direct
  // Select press starts fresh, a Path Select press expands Direct Select's points.
  bool path_selection_by_path_select_ = false;
  // Alt at a handle press: the drag breaks the smooth pair (Direct Select).
  bool path_drag_break_pair_ = false;
  // Alt at a Path Select press: the first drag move duplicates the selected shapes.
  bool path_drag_duplicate_pending_ = false;
  // A Pen session continuing an open subpath of the target from one of its ends: the
  // subpath, the owner it lives in, its anchors at the start, and whether the session
  // runs reversed (continuing from the first anchor).
  struct PenContinuation {
    int subpath{-1};
    std::pair<int, std::uint64_t> owner{0, 0};
    patchy::PathSubpath original;
    bool reversed{false};
  };
  std::optional<PenContinuation> pen_continuation_;
  // The target owner the point selection was made on (path_edit_target_owner_key).
  std::optional<std::pair<int, std::uint64_t>> path_selection_owner_;
  // Selected anchors on OTHER panel-selected shape layers (never the primary
  // target); drags, nudges, deletes, and the count span both structures.
  std::map<LayerId, std::set<std::pair<int, int>>> extra_selected_anchors_;
  PathEditDrag path_drag_mode_{PathEditDrag::None};
  std::pair<int, int> path_drag_anchor_{-1, -1};
  QPointF path_drag_last_document_{};
  // Anchor-drag Shift constraint: raw press origin, last raw pointer position
  // (replayed on Shift key transitions), and the total delta already applied.
  QPointF path_drag_origin_document_{};
  QPointF path_drag_raw_document_{};
  QPointF path_drag_applied_delta_{};
  // A drag move not applied yet: each apply re-bakes the shape (a gradient fill
  // costs a full re-render) and recomposites its region, and mouse moves can arrive
  // faster than that (Wayland's uncompressed motion), so one queued apply takes the
  // latest position however many moves came in before it (GitHub #314).
  bool path_drag_apply_pending_{false};
  Qt::KeyboardModifiers path_drag_pending_modifiers_{};
  QPointF path_marquee_start_{};
  QPointF path_marquee_current_{};
  // Last raw pointer position of the marquee drag; Shift key toggles replay
  // the square constraint against it without waiting for a mouse move.
  QPointF path_marquee_raw_current_{};
  bool path_edit_undo_armed_{false};
  bool path_edit_changed_{false};
  qint64 path_nudge_last_ms_{0};
  std::optional<DocumentPathId> active_document_path_;
  bool panel_path_targeted_{false};
  std::vector<LayerId> panel_selected_layer_ids_;
  bool target_path_visible_{true};
  std::function<void()> path_display_dismiss_callback_;
  std::function<void()> path_load_selection_callback_;
  std::function<void()> path_edited_callback_;
  std::function<void()> path_selection_changed_callback_;
  std::function<void(const std::vector<LayerId>&)> shape_layers_emptied_callback_;
  // Path free-transform session state. The affine maps the original rect onto
  // the (possibly negative-extent, i.e. flipped) current rect, then rotates
  // about the current center.
  bool path_transform_active_{false};
  patchy::VectorPath path_transform_original_;
  std::set<std::pair<int, int>> path_transform_subset_;  // empty = whole path
  QRectF path_transform_original_rect_;
  QRectF path_transform_current_rect_;
  double path_transform_angle_{0.0};  // radians
  TransformHandle path_transform_drag_handle_{TransformHandle::None};
  QRectF path_transform_drag_start_rect_;
  QPointF path_transform_drag_start_document_;
  double path_transform_drag_start_angle_{0.0};
  // Free Transform's corner offsets (original box frame, TL, TR, BR, BL), so Ctrl
  // distort, Ctrl+Shift skew and Ctrl+Alt+Shift perspective work on paths too.
  std::array<QPointF, 4> path_transform_corner_offsets_{};
  std::array<QPointF, 4> path_transform_drag_start_offsets_{};
  TransformDragKind path_transform_drag_kind_{TransformDragKind::Scale};
  QPointF path_transform_drag_grab_offset_;
  // The modifiers the session cursor last showed, for refreshes without an event.
  Qt::KeyboardModifiers path_transform_cursor_modifiers_{};
  // The live-fill target as the session found it (shape layer or vector-mask owner),
  // the document it lives in, and the path the live fill last wrote into it.
  std::optional<patchy::Layer> path_transform_owner_snapshot_;
  patchy::Document* path_transform_owner_document_{nullptr};
  bool path_transform_owner_is_mask_{false};
  std::optional<patchy::VectorPath> path_transform_live_path_;
  QPoint move_start_{};
  QPoint selection_start_{};
  QPoint selection_current_{};
  QPoint selection_press_widget_position_{};
  QPoint selection_move_origin_document_{};
  bool selection_shift_at_press_{false};
  bool selection_shift_released_since_press_{false};
  bool selection_alt_at_press_{false};
  bool selection_alt_released_since_press_{false};
  bool selection_square_constrained_{false};
  CanvasTool tool_{CanvasTool::Brush};
  LayerEditTarget layer_edit_target_{LayerEditTarget::Content};
  ChannelId active_document_channel_id_{0};
  MaskDisplayMode mask_display_mode_{MaskDisplayMode::None};
  QImage mask_display_image_;
  LayerId mask_display_image_layer_{0};
  ChannelId mask_display_image_channel_{0};
  std::uint64_t mask_display_image_revision_{0};
  // The composite separated through the working CMYK space for the ink plate views: four
  // inks per pixel (255 = full ink), from the render cache with that cacheKey.
  std::vector<std::uint8_t> plate_inks_;
  qint64 plate_inks_source_key_{0};
  std::shared_ptr<const CmykWorkingSpace> plate_inks_space_;
  QColor primary_color_{Qt::black};
  QColor secondary_color_{Qt::white};
  // Palette-mode snap cache; rebuilt lazily when the document palette changes
  // (revisions are app-globally unique, so equal revision = identical palette).
  mutable PaletteLut palette_lut_;
  mutable PaletteSnapContext palette_snap_context_{};
  mutable std::uint64_t palette_lut_revision_{std::numeric_limits<std::uint64_t>::max()};
  int brush_size_{12};
  int brush_opacity_{100};
  int brush_flow_{100};
  int brush_softness_{75};
  std::optional<BrushCursorCache> brush_cursor_cache_;
  bool brush_build_up_{false};
  PaintMode brush_paint_mode_{};
  // The mode the current stroke's pixel writer applies, chosen when the stroke
  // compositor is installed (Normal for every tool but Brush).
  PaintMode brush_stroke_paint_mode_{};
  int mixer_wet_{50};
  std::optional<double> script_brush_spacing_;
  std::function<bool(const QRect&)> script_brush_progress_;
  bool script_brush_cancelled_{false};
  int mixer_load_{50};
  int mixer_mix_{50};
  int mixer_flow_{100};
  bool mixer_sample_all_layers_{false};
  // Stroke Smoothing settings (Brush/Mixer/Eraser; see the setters above), the
  // per-stroke stabilizer, and its catch-up timer + on-canvas leash overlay.
  int brush_smoothing_{0};
  bool brush_smoothing_pulled_string_{false};
  bool brush_smoothing_catch_up_{true};
  bool brush_smoothing_catch_up_end_{true};
  bool brush_smoothing_zoom_adjust_{true};
  patchy::StrokeStabilizer stroke_stabilizer_;
  QBasicTimer stabilizer_timer_;
  QRect stroke_leash_overlay_rect_{};
  // The stationary catch-up cadence: the timer fires every 16 ms and each tick
  // advances the core stabilizer by this fixed dt (no clock reads in core, so
  // strokes stay deterministic; keep the two constants in step).
  static constexpr int kStrokeStabilizerTimerIntervalMs = 16;
  static constexpr double kStrokeStabilizerTickSeconds = 0.016;
  patchy::MixerBrushState mixer_brush_state_{};
  // Merged-document snapshot captured at mixer stroke start while Sample All
  // Layers is on; null when off. See begin_mixer_brush_stroke().
  QImage mixer_composite_snapshot_;
  std::shared_ptr<const patchy::BrushTip> brush_tip_;
  QString brush_tip_id_;
  patchy::BrushShape brush_shape_{patchy::BrushShape::Round};
  patchy::BrushTipMipChain brush_tip_mips_;
  // Most-recently-used scaled stamps keyed by (target size, softness); pressure-driven size
  // changes hit this instead of rescaling the tip on every dab.
  mutable std::vector<std::pair<std::pair<int, int>, std::shared_ptr<const patchy::ScaledBrushTip>>>
      brush_tip_scaled_cache_;
  patchy::BrushTipStrokeState brush_tip_stroke_state_;
  // The pen sample of a held first dab's press: a click stamps with it, not the release's.
  std::optional<PenInputSample> held_first_dab_pen_sample_{};
  patchy::BrushDynamics brush_dynamics_{};
  double brush_base_angle_degrees_{0.0};
  int brush_base_roundness_{100};
  std::optional<quint32> brush_dynamics_test_seed_;
  quint32 stroke_dynamics_seed_{0};
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
  int quick_select_size_{32};
  bool quick_select_sample_all_layers_{false};
  bool quick_select_enhance_edge_{false};
  bool show_transform_controls_{true};
  bool fill_shapes_{false};
  int shape_corner_radius_{0};
  int fill_opacity_{100};
  int fill_softness_{0};
  int fill_tolerance_{32};
  bool fill_contiguous_{true};
  bool auto_select_layer_{true};
  MoveAutoSelectTarget auto_select_target_{MoveAutoSelectTarget::Layer};
  SelectionMode selection_mode_{SelectionMode::Replace};
  // Per-tool combine modes; selection_mode_ mirrors the active selection tool's
  // entry. Indexed by selection_tool_index().
  std::array<SelectionMode, kSelectionToolCount> selection_modes_per_tool_{
      SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace,
      SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace, SelectionMode::Replace,
      SelectionMode::Replace};
  // Set when a marquee drag begins with Alt held and no existing selection: the
  // press point is the center and the rectangle grows symmetrically.
  bool marquee_from_center_{false};
  MarqueeStyle marquee_style_{MarqueeStyle::Normal};
  QSize marquee_fixed_size_{1024, 768};
  int marquee_corner_radius_{0};
  int selection_feather_radius_{0};
  bool selection_antialias_{true};
  bool panning_{false};
  // Right-button press position; a release without a drag opens the canvas
  // context menu (canvas_widget_move.cpp). The right button never pans.
  std::optional<QPoint> context_press_pos_;
  QPointer<QMenu> canvas_context_menu_;
  struct RetiredContextMenu {
    QPointer<QMenu> menu;
    int loop_level{0};  // QThread::loopLevel() when it hid; safe to delete at or below it
  };
  std::vector<RetiredContextMenu> retired_context_menus_;
  bool spacebar_panning_{false};
  bool spacebar_repositioning_drag_rect_{false};
  QPoint spacebar_reposition_last_document_position_{};
  QPoint spacebar_reposition_origin_document_position_{};
  QPoint spacebar_reposition_start_selection_start_{};
  QPoint spacebar_reposition_start_selection_current_{};
  bool painting_{false};
  CoalescedMouseState coalesced_mouse_state_{};
  CoalescedMousePositionsProvider coalesced_mouse_positions_provider_{};
  UncompressedPointerInputScope stroke_input_scope_{};
  QBasicTimer airbrush_timer_;
  bool brush_adjust_dragging_{false};
  QPoint brush_adjust_origin_widget_{};
  QPoint brush_adjust_current_widget_{};
  bool brush_adjust_from_tablet_{false};
  int brush_adjust_start_size_{12};
  int brush_adjust_start_softness_{75};
  std::optional<QPointF> last_stroke_end_document_{};
  QElapsedTimer opacity_digit_timer_{};
  int opacity_pending_digit_{-1};
  bool opacity_digit_targets_flow_{false};
  bool drawing_shape_{false};
  bool dragging_text_rect_{false};
  bool dragging_text_entry_selection_{false};
  bool move_drag_pending_{false};
  // Selection roots an Alt press asked to duplicate once the drag starts
  // (empty: a plain move). Consumed by the first drag frame.
  std::vector<LayerId> move_drag_duplicate_roots_;
  struct MoveLayerSelectionGesture {
    QPoint press_widget;
    QPointF anchor_document;
    QPointF current_document;
    std::vector<LayerId> selected_ids;
    std::optional<LayerId> active_id;
    std::optional<LayerId> clicked_id;
    std::vector<LayerId> duplicate_roots;
    bool rectangle_allowed{false};
    bool additive{false};
    bool dragging_rectangle{false};
    // Live pre-selection (GitHub issue 171): collected once when the rectangle
    // starts, then only rect tests per pointer move.
    bool candidates_collected{false};
    std::vector<MoveSelectionCandidate> candidates;
    std::unordered_map<LayerId, QRect> target_outlines;  // document rect per target
    std::vector<LayerId> preview_ids;
  };
  std::optional<MoveLayerSelectionGesture> move_layer_selection_gesture_;
  bool moving_layer_{false};
  bool transforming_layer_{false};
  bool dragging_transform_{false};
  bool color_picking_{false};
  // Set transiently by the Alt key handler so update_tool_cursor() uses the
  // event's authoritative (folded) Alt state instead of the global keyboard
  // state, which may not have refreshed yet when the app-level filter runs.
  std::optional<bool> alt_color_pick_cursor_override_;
  // Type-tool Ctrl+drag (begin_text_layer_move): the gesture is live, and Ctrl as the event
  // reported it for the cursor (the four-way cursor shows over a text layer it would move).
  bool text_layer_move_active_{false};
  std::function<void()> text_layer_move_finished_;
  std::optional<bool> text_ctrl_cursor_override_;
  void finish_text_layer_move();
  // Same rationale for the pen cursor's Alt/Ctrl badges: folded modifiers from
  // the key filter, never the (possibly stale) live keyboard state.
  std::optional<Qt::KeyboardModifiers> pen_cursor_modifier_override_;
  bool selecting_{false};
  bool lassoing_{false};
  bool quick_selecting_{false};
  // Brush footprint accumulated during a Quick Select drag: a doc-sized Grayscale8 stamp mask
  // for the release-time solve plus the raw stroke points for the on-canvas overlay.
  QImage quick_select_seed_mask_;
  QRect quick_select_seed_bounds_;
  QPolygonF quick_select_stroke_points_;
  QPoint quick_select_last_document_point_;
  bool object_selecting_{false};
  QPoint object_select_press_widget_{};
  QPoint object_select_release_widget_{};
  QPoint object_select_press_{};
  QPoint object_select_current_{};
  bool object_select_sample_all_layers_{true};
  // What was sampled; the pixels themselves are checked by object_select_source_hash_.
  struct ObjectSelectEmbeddingKey {
    bool sample_all_layers{true};
    bool high_quality{false};  // which model (patchy::ai::SamModel) made it
    LayerId layer{0};
    bool operator==(const ObjectSelectEmbeddingKey&) const = default;
  };
  std::optional<ObjectSelectEmbeddingKey> object_select_embedding_key_;
  std::shared_future<std::shared_ptr<const patchy::ai::SamEmbedding>> object_select_embedding_;
  // Stops the whole-image encode when a box release needs its own region encode instead.
  std::shared_ptr<patchy::ai::InferenceCancel> object_select_encode_cancel_;
  std::uint64_t object_select_source_hash_{0};
  QString object_select_source_error_;
  bool object_select_embedding_unverified_{false};
  int object_select_encodes_started_{0};
  // For the "Analyzing image... N%" estimate: the last encode's duration on this machine.
  QElapsedTimer object_select_encode_started_;
  bool object_select_encode_pending_{false};
  qint64 object_select_last_encode_ms_{800};
  // Spot Healing stroke state (canvas_widget_spot_healing.cpp): doc-sized
  // Grayscale8 soft footprint accumulated during the drag, healed once on
  // release from the press-time source snapshot.
  bool spot_healing_stroke_active_{false};
  QImage spot_heal_footprint_;
  QRect spot_heal_footprint_bounds_;
  QPolygonF spot_heal_stroke_points_;
  QPoint spot_heal_last_document_point_;
  QImage spot_heal_source_cache_;
  // Remove Object repeat cycle: the same selection (bounds plus a hash of its
  // coverage) run again advances to the next geometry-only source candidate.
  int remove_object_attempt_{0};
  bool remove_object_has_last_{false};
  QRect remove_object_last_bounds_;
  std::uint64_t remove_object_last_mask_hash_{0};
  std::function<void()> remove_object_requested_callback_;
  // Crop tool session state (canvas_widget_crop.cpp). All rects/points are in
  // document space; crop_rect_ may extend past the canvas (commit expands).
  bool crop_session_active_{false};
  // True while the box is the automatic canvas frame (ratio-fitted or not): a
  // press inside the canvas then lays out a new rect instead of moving the box.
  bool crop_box_is_default_{false};
  // True while the box descends from the selection it adopted on activation
  // (handles and moves keep it): the marching ants hide meanwhile.
  bool crop_box_from_selection_{false};
  bool crop_dragging_out_{false};
  bool crop_rotating_{false};
  TransformHandle crop_drag_handle_{TransformHandle::None};
  QPoint crop_anchor_document_;
  QPoint crop_current_document_;
  QPoint crop_press_widget_point_;
  QRect crop_rect_;
  QRect crop_drag_start_rect_;
  QPointF crop_drag_start_point_;
  QPointF crop_drag_grab_offset_;
  double crop_angle_{0.0};
  CropOverlayOptions crop_overlay_options_;
  double crop_rotate_start_angle_{0.0};
  double crop_rotate_start_vector_degrees_{0.0};
  bool crop_square_constrained_{false};
  bool crop_from_center_{false};
  double crop_ratio_w_{0.0};
  double crop_ratio_h_{0.0};
  bool crop_keep_proportions_{false};
  CropOverlay crop_overlay_{CropOverlay::Thirds};
  int crop_overlay_orientation_{0};
  bool crop_overlay_only_while_dragging_{false};
  std::function<void(QRect, double)> crop_commit_requested_callback_;
  std::function<void()> crop_session_changed_callback_;
  // Perspective Crop session state, document space. Points are the placed corners
  // (four once the quad is complete); drag corner 0-3 is a corner, 4 the whole quad.
  std::vector<QPointF> perspective_crop_points_;
  bool perspective_crop_dragging_out_{false};
  QPoint perspective_crop_press_widget_point_;
  QPointF perspective_crop_anchor_document_;
  QPointF perspective_crop_hover_document_;
  int perspective_crop_drag_part_{-1};
  QPointF perspective_crop_drag_start_point_;
  std::array<QPointF, 4> perspective_crop_drag_start_quad_{};
  QPointF perspective_crop_rotate_center_;
  double perspective_crop_rotate_start_degrees_{0.0};
  bool perspective_crop_show_grid_{true};
  std::function<void(std::array<QPointF, 4>)> perspective_crop_commit_requested_callback_;
  // Artboard tool gesture state (canvas_widget_artboard.cpp), document space.
  enum class ArtboardDragMode { None, Create, Move, Resize };
  ArtboardDragMode artboard_drag_mode_{ArtboardDragMode::None};
  std::optional<LayerId> artboard_drag_id_;
  TransformHandle artboard_drag_handle_{TransformHandle::None};
  QRect artboard_drag_start_rect_;
  QRect artboard_drag_rect_;
  QPoint artboard_drag_anchor_document_;
  QPointF artboard_drag_start_point_;
  QPoint artboard_press_widget_point_;
  bool artboard_drag_moved_{false};
  bool artboard_drag_duplicate_{false};
  // The drag state shows a field scrub's preview_artboard_rect, not a mouse drag.
  bool artboard_field_preview_{false};
  // The artboard's pixels at the start of a move, drawn at the moving rect.
  QImage artboard_drag_ghost_;
  // What the canvas shows where the moving artboard was (it, hidden): the drag shows the
  // artboard leaving its place, not a copy over the original.
  QImage artboard_drag_vacated_;
  QSize artboard_default_size_{1080, 1080};
  std::function<void(const ArtboardEditRequest&)> artboard_edit_requested_callback_;
  std::function<void(const std::vector<LayerId>&)> layers_moved_callback_;
  bool artboard_auto_nest_{true};
  // The Move drag's re-nesting roots (artboard_nest_roots, taken at drag start) and the
  // cached re-nested patch source (see move_patch_source).
  std::vector<LayerId> move_nest_root_ids_;
  std::vector<ArtboardNestMove> move_nest_preview_moves_;
  std::optional<Document> move_nest_preview_document_;
  int move_nest_preview_document_level_{-1};
  const Document* move_nest_preview_document_base_{nullptr};
  // Patch tool state (canvas_widget_patch_tool.cpp). The drag latches a frozen
  // snapshot, the selection's soft mask, and the doc-space outline path; the
  // preview blits the snapshot translated by the cumulative delta.
  PatchToolMode patch_tool_mode_{PatchToolMode::Source};
  bool patch_tool_transparent_{false};
  bool patch_tool_dragging_{false};
  QPoint patch_tool_drag_origin_;
  QPoint patch_tool_drag_delta_;
  QImage patch_tool_source_image_;
  QImage patch_tool_drag_mask_;
  QRect patch_tool_drag_mask_bounds_;
  QPainterPath patch_tool_outline_path_;
  QImage patch_tool_drag_proxy_image_;
  bool moving_selection_{false};
  bool zooming_{false};
  // Scrubby Zoom sub-state of zooming_: armed by the press when the option is
  // on, started once travel passes kZoomClickSlopPx (a shorter press stays a
  // click). Shares zoom_drag_anchor_widget_ / zoom_drag_last_pos_ with the pen
  // ZoomCanvas drag; the two gestures never overlap.
  bool zoom_scrubbing_{false};
  bool zoom_scrub_started_{false};
  // A Ctrl or Alt left press with the Hand (the tool, or Space held over another
  // tool): the release is one Zoom-tool click step, in for Ctrl, out for Alt,
  // and the drag before it does not pan (GitHub issue 304).
  bool hand_zoom_pending_{false};
  bool hand_zoom_out_{false};
  static constexpr int kZoomClickSlopPx = 8;
  QPoint zoom_start_{};
  QPoint zoom_current_{};
  QPolygon lasso_points_;
  // Magnetic Lasso hover-trace state. The trace runs with the mouse button UP (click to
  // start, click for a manual anchor, Backspace to pop, double-click/Enter/click-near-start
  // to close, Escape to cancel); magnetic_source_image_ keeps the trace-start composite
  // alive for the engine's non-owning buffer.
  bool magnetic_lassoing_{false};
  QVector<QPoint> magnetic_anchors_;
  QVector<int> magnetic_anchor_path_index_;  // index of each anchor in magnetic_committed_path_
  QPolygon magnetic_committed_path_;
  QPolygon magnetic_live_path_;
  QImage magnetic_source_image_;
  patchy::LiveWireEngine magnetic_engine_;
  int magnetic_lasso_width_{10};
  int magnetic_lasso_edge_contrast_{10};
  int magnetic_lasso_frequency_{57};
  // Polygonal Lasso session (also the Lasso's Alt-release switch). polygon_lasso_points_ is
  // the placed outline, vertices plus any Alt freehand runs; polygon_lasso_vertices_ indexes
  // each placed vertex in it so Backspace drops a vertex with its run. The rubber band
  // (last point to polygon_lasso_cursor_) is preview only.
  bool polygon_lassoing_{false};
  bool polygon_lasso_from_lasso_{false};
  bool polygon_lasso_button_down_{false};
  bool polygon_lasso_freehand_run_{false};  // the held button has added freehand points
  QPolygon polygon_lasso_points_;
  QVector<int> polygon_lasso_vertices_;
  QPoint polygon_lasso_cursor_;
  // Freehand Lasso: Alt counts as the polygon switch only once pressed after the drag began;
  // Alt held from the press is Subtract (the marquee's Alt latch).
  bool lasso_alt_at_press_{false};
  bool lasso_alt_released_since_press_{false};
  QRegion selection_;
  QRegion selection_display_region_;
  QRect selection_mask_bounds_;
  QImage selection_mask_alpha_;
  QRegion last_cleared_selection_;
  QRegion last_cleared_selection_display_region_;
  QRect last_cleared_selection_mask_bounds_;
  QImage last_cleared_selection_mask_alpha_;
  std::optional<MarqueeShape> last_cleared_marquee_shape_;
  QRegion selection_before_edit_;
  QRegion selection_display_region_before_edit_;
  QRect selection_mask_before_edit_bounds_;
  QImage selection_mask_before_edit_alpha_;
  std::optional<MarqueeShape> marquee_shape_before_edit_;
  std::optional<MarqueeShape> marquee_shape_;
  // A handle drag on the remembered marquee shape (None when idle).
  TransformHandle marquee_resize_handle_{TransformHandle::None};
  QRect marquee_resize_start_rect_;
  // The rect the drag last applied; Space repositions from here and moves
  // marquee_resize_start_rect_ along so the resize resumes in place.
  QRect marquee_resize_current_rect_;
  // Space held during a marquee or crop handle drag: the rect and the drag's
  // start rect as they were when Space went down (the two gestures never
  // overlap, so they share the storage).
  QRect spacebar_reposition_start_marquee_rect_;
  QRect spacebar_reposition_start_marquee_start_rect_;
  bool selection_edges_visible_{true};
  bool quick_mask_active_{false};
  PixelBuffer quick_mask_pixels_;
  std::uint64_t quick_mask_revision_{0};
  QColor quick_mask_saved_primary_{Qt::black};
  QColor quick_mask_saved_secondary_{Qt::white};
  std::optional<SelectionSnapshot> quick_mask_edit_before_;
  QString quick_mask_edit_label_;
  QRegion quick_mask_edit_dirty_;
  PixelBuffer smart_filter_mask_pixels_;
  LayerId smart_filter_mask_owner_id_{0};
  std::uint64_t smart_filter_mask_revision_{0};
  std::optional<PixelBuffer> smart_filter_mask_edit_before_;
  QString smart_filter_mask_edit_label_;
  QRegion smart_filter_mask_edit_dirty_;
  bool rulers_visible_{false};
  MeasurementUnit ruler_unit_{MeasurementUnit::Pixels};
  std::function<void(MeasurementUnit)> ruler_unit_change_requested_callback_;
  std::optional<QColor> backdrop_color_override_;
  std::function<void(std::optional<QColor>)> backdrop_color_change_requested_callback_;
  std::function<void()> custom_backdrop_color_requested_callback_;
  bool grid_visible_{false};
  bool pixel_grid_visible_{true};
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
  // Moving an existing guide: where the press landed on the guide's axis and how far the
  // guide sat from it, so the guide keeps its grab point instead of jumping to the pointer
  // and a press that never moves leaves a fractional (PSD-loaded) position alone.
  QPoint guide_drag_press_widget_;
  double guide_drag_grab_offset_{0.0};
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
  // Rasterized selection_ for the per-pixel lookups behind selection_alpha_at
  // when the region holds more than a few rectangles: QRegion::contains scans
  // every rectangle, so a wand selection of a background around a subject
  // (thousands of row spans) made a 1.5 Mpx fill take about 45 s (GitHub
  // issue 34). One bit per pixel over selection_lookup_bounds_, built on first
  // use under selection_lookup_mutex_ (render/filter workers may query it in
  // parallel), and dropped by invalidate_selection_outline() with the outline
  // caches, which every selection write already calls.
  mutable std::vector<std::uint8_t> selection_lookup_bits_;
  mutable QRect selection_lookup_bounds_;
  mutable std::atomic<bool> selection_lookup_valid_{false};
  mutable std::mutex selection_lookup_mutex_;
  QBasicTimer processing_animation_timer_;
  std::vector<QPointF> preview_points_;  // document space
  std::function<void(int, QPointF, bool)> preview_point_moved_;
  int preview_point_drag_{-1};
  QPointF preview_point_drag_offset_;
  bool processing_overlay_visible_{false};
  bool processing_render_wait_active_{false};
  // A mouse release that arrived while processing_render_wait_active_ (wasm
  // delivers DOM input synchronously into the nested wait loop). Parked here
  // instead of dropped so the gesture that owns the press still ends;
  // wait_for_processing_operation replays it once the outermost wait unwinds.
  struct DeferredWaitRelease {
    QPointF position;
    QPointF global_position;
    Qt::MouseButton button{Qt::NoButton};
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
  };
  std::optional<DeferredWaitRelease> deferred_wait_release_;
  int preview_renders_in_flight_{0};
  int preview_render_progress_{-1};
  bool preview_render_refining_{false};
  QElapsedTimer preview_render_started_{};
  // Started when a background refresh or deferred Move commit begins with
  // nothing else of the kind in flight; invalid once both are idle.
  QElapsedTimer background_refresh_started_{};
  void note_background_refresh_state();
  int processing_operation_depth_{0};
  int processing_operation_delay_ms_{-1};  // <0 = processing_overlay_delay_ms()
  std::chrono::steady_clock::time_point processing_operation_started_{};
  bool processing_operation_owns_overlay_{false};
  QString processing_overlay_message_{};
  int processing_animation_frame_{0};
  StrokePixelMap<std::uint8_t> brush_stroke_pixels_;
  StrokePixelMap<float> brush_stroke_alpha_caps_;  // mask brush + clone max-cap
  StrokePixelMap<float> brush_stroke_accumulated_alpha_;
  StrokePixelMap<float> brush_stroke_union_coverage_;
  std::unordered_map<std::uint64_t, EditColor> brush_stroke_wet_edge_primary_;
  QRect brush_stroke_wet_edge_pending_rect_{};
  std::optional<QPointF> brush_stroke_last_stamp_position_;
  double brush_stroke_distance_since_last_stamp_{0.0};
  patchy::SmudgeState smudge_state_;
  QImage clone_source_cache_{};
  // The Clone/Healing source of a 16/32-bit document at its depth (canvas-sized RGBA,
  // native domain), beside the 8-bit cache above (docs/high-bit-depth.md).
  PixelBuffer clone_source_deep_{};
  bool clone_source_set_{false};
  bool clone_aligned_{true};
  bool clone_aligned_offset_set_{false};
  std::optional<PatternResource> pattern_stamp_pattern_;
  bool pattern_stamp_aligned_{true};
  std::optional<QPoint> pattern_stamp_origin_;
  int healing_diffusion_{5};
  // Shared by Clone, Healing, Spot Healing, and Patch: checked samples the
  // merged document (the historical behavior), unchecked samples the active
  // layer alone.
  bool retouch_sample_all_layers_{true};
  int local_adjustment_strength_{50};
  LocalToneRange local_tone_range_{LocalToneRange::Midtones};
  bool local_protect_tones_{true};
  SpongeMode sponge_mode_{SpongeMode::Desaturate};
  bool sponge_vibrance_{true};
  int eyedropper_sample_size_{1};
  SampleLayers eyedropper_sample_layers_{SampleLayers::AllLayers};
  bool eyedropper_picks_background_{false};
  QPoint eyedropper_hover_widget_position_{};
  bool eyedropper_hover_position_valid_{false};
  bool eyedropper_show_sample_ring_{true};
  bool sample_loupe_visible_{false};
  bool sample_loupe_hides_cursor_{false};
  QPoint sample_loupe_pointer_{};
  QColor sample_loupe_previous_color_{};
  // Display-encoded pixels of sample_loupe_source_rect_, read once per pointer move.
  QImage sample_loupe_pixels_{};
  QRect sample_loupe_view_rect_{};
  QRect sample_loupe_source_rect_{};
  QRect sample_loupe_sample_rect_{};
  PenInputSettings pen_input_settings_{};
  std::optional<PenInputSample> active_pen_input_sample_{};
  std::optional<PenInputSample> last_pen_input_sample_{};
  QElapsedTimer pen_proximity_clock_{};
  qint64 last_tablet_event_ms_{-1};
  Qt::MouseButton mouse_pen_action_button_{Qt::NoButton};
  bool handling_tablet_event_{false};
  bool pen_button_suppressing_paint_{false};
  bool pen_zoom_dragging_{false};
  // Shared by the pen ZoomCanvas drag and Scrubby Zoom (canvas_widget_events.cpp).
  QPointF zoom_drag_anchor_widget_{};
  QPointF zoom_drag_last_pos_{};
  std::vector<MovingLayer> moving_layers_;
  QPoint text_rect_start_{};
  QPoint text_rect_current_{};
  std::vector<LayerId> selected_layer_ids_;
  std::optional<QRect> move_hover_outline_rect_;
  // The artboard the hover outline frames, when it frames one: its label takes the
  // selected color while hovered (GitHub issue 259).
  std::optional<LayerId> move_hover_artboard_id_;
  QPoint move_press_widget_position_{};
  QPoint move_preview_delta_{};
  std::vector<RenderedDocumentPatch> move_preview_patches_;
  std::optional<QPoint> move_preview_patches_delta_{};
  bool moving_layers_use_outline_preview_{false};
  bool move_drag_uses_proxy_preview_{false};
  // A live preview frame rendered slower than the latch threshold; the next
  // move switches to the proxy (the area gate cannot price the stack the drag
  // crosses). Persists across drags of the same moving set at the same
  // composite level (mirrors transform_live_frame_slow_): reset by
  // reset_move_live_latch() on document/tool/lock changes and by the
  // press-time key mismatch, NOT by clear_move_proxy().
  bool move_live_frame_slow_{false};
  std::uint64_t move_live_latch_key_{0};
  // Snapshot of the moving subtree at delta zero (ARGB32_Premultiplied,
  // possibly downscaled) and the canvas-clipped document rect it covers.
  QImage move_proxy_image_{};
  QRect move_proxy_document_rect_{};
  // Whether the snapshot's unclipped effect-rect union hung off the canvas at
  // build: a clipped snapshot is missing content, so it must not be retained
  // across a commit (a per-drag rebuild re-clips it fresh each time).
  bool move_proxy_rect_canvas_clipped_{false};
  // Sorted selection the retained base/proxy belong to (empty = nothing
  // retained) and the composite level they were built at.
  std::vector<LayerId> retained_move_ids_;
  int retained_move_composite_level_{-1};
  // An external document change arrived while a drag was in flight; the
  // release must not retain (the base would go stale silently).
  bool move_external_change_during_drag_{false};
  // The press matched the retained selection; counted into
  // move_preview_cache_reuses only when the press becomes a real drag.
  bool move_press_reused_retained_caches_{false};
  // Deferred Move commit (mouseReleaseEvent, September 2026): when the
  // accurate release render would block behind the processing overlay, the
  // layer bounds change at once, the last preview frame stays on screen as a
  // hold (base plus proxy or patches, mip-sized when the base was), and a
  // worker renders the accurate full-res patches from a document snapshot; a
  // queued completion lands them in the render cache. Until then the cache is
  // stale inside the job's region but never dirty: readers that need exact
  // pixels (selection engines, transform/move bases, the curves clipping
  // preview) call wait_for_move_commit_job() first while paint never waits,
  // any other document change cancels the job and folds its region
  // into that change's invalidation, and a further commit restarts one job
  // over the union of both regions.
  struct MoveCommitJob {
    std::uint64_t generation{0};
    QRegion region{};
    std::shared_future<std::vector<RenderedDocumentPatch>> result{};
  };
  std::optional<MoveCommitJob> move_commit_job_{};
  struct MoveCommitWorker;
  std::shared_ptr<MoveCommitWorker> move_commit_worker_{};
  std::uint64_t move_commit_job_generation_{0};
  QImage move_commit_hold_base_{};
  int move_commit_hold_scale_level_{0};
  QImage move_commit_hold_proxy_{};
  QRect move_commit_hold_proxy_rect_{};
  std::optional<QRect> move_commit_hold_proxy_clip_{};
  std::vector<RenderedDocumentPatch> move_commit_hold_patches_{};
  // Delta the live preview patches were last rendered at, at either composite
  // level (move_preview_patches_delta_ is reset for scaled patches so the
  // release never reuses them; the hold may still show them).
  std::optional<QPoint> move_preview_patches_rendered_delta_{};
  QImage move_base_cache_{};
  // Level the base was composited at (preview-scaled document); 0 = full-res.
  int move_base_cache_scale_level_{0};
  // Level the live preview patches were composited at; their document_rects
  // stay full-res, the images are 2^level smaller. Scaled patches are never
  // reused for the release cache patch.
  int move_preview_patches_scale_level_{0};
  std::vector<QImage> move_base_display_mip_cache_{};
  qint64 move_base_display_mip_source_key_{0};
  // PREVIEW-ONLY scaled document cache (display-resolution compositing).
  std::optional<Document> preview_scaled_document_{};
  int preview_scaled_document_level_{0};
  std::shared_ptr<PreviewScaleCache> preview_scale_cache_{};
  bool move_preview_in_flight_{false};
  bool move_preview_requested_{false};
  std::uint64_t move_preview_generation_{0};
  std::shared_ptr<std::atomic_bool> move_preview_cancel_{};
  std::optional<LayerId> transform_layer_id_;
  QRectF transform_original_rect_{};
  QRectF transform_current_rect_{};
  QRectF transform_drag_start_rect_{};
  QPointF transform_drag_start_point_{};
  QPointF transform_drag_grab_offset_{};
  // Skew/Distort/Perspective state: per-corner offsets in the ORIGINAL box's frame
  // and units (layer top left, top right, bottom right, bottom left), mapped through
  // the box's affine (rect, angle, flips). All zero is exactly the plain affine
  // session, which keeps every pinned commit byte unchanged.
  std::array<QPointF, 4> transform_corner_offsets_{};
  std::array<QPointF, 4> transform_drag_start_corner_offsets_{};
  TransformDistortMode transform_distort_mode_{TransformDistortMode::None};
  TransformDragKind transform_drag_kind_{TransformDragKind::Scale};
  // The modifiers the session cursor last showed (hover or a modifier key), so a
  // cursor refresh without an event keeps the gesture hint.
  Qt::KeyboardModifiers transform_cursor_modifiers_{};
  TransformHandle transform_drag_handle_{TransformHandle::None};
  double transform_angle_{0.0};
  double transform_start_angle_{0.0};
  double transform_scale_x_sign_{1.0};
  double transform_scale_y_sign_{1.0};
  double transform_drag_start_scale_x_sign_{1.0};
  double transform_drag_start_scale_y_sign_{1.0};
  TransformInterpolation transform_interpolation_{TransformInterpolation::Bicubic};
  CanvasAnchor transform_reference_point_{CanvasAnchor::Center};
  // Application preference, pushed in from MainWindow; not per-session state, so
  // the session resets must leave it alone.
  bool shift_keeps_transform_aspect_{false};
  bool show_transform_drag_values_{true};
  bool snap_transforms_to_pixel_grid_{true};
  // Drag readout bookkeeping: the last painted panel rect (for the bounded
  // repaint union), the moving set's zero-delta extent captured when a Move
  // drag starts, and the last status-bar mirror text.
  QRect drag_readout_dirty_rect_{};
  std::optional<QRectF> move_readout_base_rect_{};
  QString drag_readout_status_text_{};
  // Alignment guides of the live Move drag (docs/alignment.md) and the widget
  // rect they last painted, for the bounded repaint union.
  std::optional<SnapMatch> move_snap_x_{};
  std::optional<SnapMatch> move_snap_y_{};
  QRect move_snap_guides_dirty_rect_{};
  QImage transform_base_cache_{};
  std::vector<QImage> transform_base_display_mip_cache_{};
  qint64 transform_base_display_mip_source_key_{0};
  QImage transform_source_image_{};
  // transform_source_image_ as display pixels for the preview blits and the proxy: a
  // 32-bit layer's linear floats sRGB-encoded with the preview exposure. The commit keeps
  // resampling the float source. Rebuilt when the source image or the exposure changes.
  [[nodiscard]] const QImage& transform_preview_source_image() const;
  mutable QImage transform_source_display_image_{};
  mutable qint64 transform_source_display_key_{0};
  mutable float transform_source_display_exposure_{0.0F};
  // Composited preview for layers whose blend/opacity/mask/styles the plain
  // rotated blit cannot represent: patches over the transformed bounds (plus
  // effects) drawn above transform_base_cache_, replacing the historical
  // full-canvas per-mouse-move recomposite.
  std::vector<RenderedDocumentPatch> transform_preview_patches_;
  QRect transform_preview_patches_rect_{};
  QRect transform_source_local_rect_{};
  bool transform_requires_composited_preview_{false};
  // Heavy composited-preview drags latch onto a bounded low-res proxy blit
  // (blend/mask/styles approximated until release) instead of resampling and
  // patch-compositing the full transformed area per mouse-move. Sticky for
  // the rest of the drag; the proxy image persists across drags in a session.
  bool transform_drag_uses_proxy_preview_{false};
  bool transform_preview_patches_banded_{false};  // drag frames; release re-renders exactly
  // A live composited-preview refresh ran over the latch threshold; the next
  // drag move latches the proxy. Persists across drags within the session
  // (the layer stays expensive) and resets with the session state.
  bool transform_live_frame_slow_{false};
  // Session cache of the linked raster masks' non-default crops, keyed by the
  // layer content revision (mask edits and Layer Style live edits bump it, a
  // mouse-move never does). Looked up on the main thread before a preview
  // compute starts; cleared with the rest of the session state.
  std::unordered_map<LayerId, TransformLinkedMaskSource> transform_mask_sources_;
  // Level the transform base was composited at (preview-scaled document);
  // 0 = full-res.
  int transform_base_cache_scale_level_{0};
  QImage transform_proxy_image_{};
  double transform_proxy_layer_opacity_{1.0};
  // Multi-target Free Transform session (folder / multi-selection). Invariant
  // while transforming_layer_: transform_layer_id_ has a value XOR
  // transform_targets_ is non-empty; the single-layer path never reads these.
  std::vector<TransformTarget> transform_targets_;
  std::vector<LayerId> transform_mask_only_ids_;
  std::vector<LayerId> transform_session_root_ids_;
  // One composited snapshot of only the target subtree (non-target
  // non-adjustment leaves hidden), blitted with the painter transform during
  // drags; approximate like the move proxy, accurate patches at release and
  // numeric edits. Rect is the identity doc-space rect the snapshot covers.
  QImage transform_multi_snapshot_{};
  QRect transform_multi_snapshot_rect_{};
  int transform_multi_snapshot_scale_level_{0};
  // The artboard the targets sit on (all of them), the snapshot blit's clip: the snapshot
  // renders that artboard as a plain group, without paper or clip (docs/artboards.md).
  std::optional<QRect> transform_multi_snapshot_clip_{};
  // Commit-time preview composition (see arm_transform_commit_hold): shown
  // instead of the stale render cache while the commit's refresh is pending.
  // Armed just before the commit's document_changed call (the fresh flag lets
  // that one notification through); any other document change clears it, and
  // paint drops it once the cache settles. May be mip-sized when the session
  // base came from the preview-scaled document.
  QImage transform_commit_hold_image_{};
  int transform_commit_hold_scale_level_{0};
  bool transform_commit_hold_fresh_{false};
  bool warping_layer_{false};
  bool dragging_warp_handle_{false};
  int warp_drag_index_{-1};
  std::optional<LayerId> warp_layer_id_;
  WarpMeshGrid warp_mesh_{};           // 4x4 working cage, content space
  WarpMeshGrid warp_original_mesh_{};  // for the no-change check on commit
  std::array<double, 9> warp_content_to_document_{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  std::array<double, 9> warp_document_to_content_{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  double warp_content_width_{0.0};
  double warp_content_height_{0.0};
  bool warp_target_smart_object_{false};
  QString warp_style_{QStringLiteral("warpCustom")};
  double warp_style_value_{0.0};
  QImage warp_source_image_{};
  QImage warp_base_cache_{};
  // Level the warp base was composited at (preview-scaled document); 0 = full-res.
  int warp_base_cache_scale_level_{0};
  std::vector<QImage> warp_base_display_mip_cache_{};
  qint64 warp_base_display_mip_source_key_{0};
  // Bounded patches of the warped layer over its effect rect, drawn above the
  // (layer-hidden) base cache.
  std::vector<RenderedDocumentPatch> warp_preview_patches_{};
  // Composite level the patches rendered at (their images are that mip of their
  // full-res document_rect; 0 = full-res), and the full-res document area they are
  // valid for (outside it the warped layer was not rendered).
  int warp_preview_patches_scale_level_{0};
  QRect warp_preview_covered_rect_{};
  // warp_source_image_ halved once per entry (index 0 = level 1), built on demand.
  std::vector<QImage> warp_source_mips_{};
  bool warp_preview_view_refresh_pending_{false};
  // A handle drag moved the mesh since the last preview render; the next paint
  // renders it once (GitHub #298: rendering per mouse move fell behind fast input).
  bool warp_drag_preview_pending_{false};
  // True when warp_content_to_document_ carries a composed free-transform stage
  // (the single-session toggle), so commit must bake even with an untouched mesh.
  bool warp_entry_changed_{false};
  // Pending warp stashed across a warp -> free-transform mode switch: the affine
  // stage previews over the baked warp, and commit composes its delta into this
  // map for one final bake. Set only by switch_warp_to_free_transform; cleared by
  // the free-transform commit/cancel or by resuming the cage.
  bool transform_has_pending_warp_{false};
  bool pending_warp_changed_{false};
  bool pending_warp_smart_object_{false};
  WarpMeshGrid pending_warp_mesh_{};
  std::vector<TransformSessionStep> transform_session_undo_steps_;
  std::vector<TransformSessionStep> transform_session_redo_steps_;
  TransformSessionStep transform_session_step_at_press_{};
  bool transform_session_last_step_was_nudge_{false};
  WarpMeshGrid pending_warp_original_mesh_{};
  std::array<double, 9> pending_warp_content_to_document_{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  double pending_warp_content_width_{0.0};
  double pending_warp_content_height_{0.0};
  QString pending_warp_style_{QStringLiteral("warpCustom")};
  double pending_warp_style_value_{0.0};
  QImage pending_warp_source_image_{};
  std::optional<LayerId> move_transform_controls_layer_id_{};
  std::function<void(QString)> before_edit_callback_;
  std::function<void(const PaintStrokeHistoryEvent&)> paint_stroke_history_callback_;
  // Set between arm_paint_stroke_history and finish_paint_stroke_history;
  // active_edit_target_changed_impl unions every reported region into the rect.
  bool paint_stroke_history_armed_{false};
  PaintStrokeHistoryEvent paint_stroke_history_event_{};
  std::optional<patchy::LayerGrowthReserve> stroke_growth_reserve_{};
  std::function<void(QString, SelectionSnapshot, bool)> selection_history_callback_;
  std::function<void()> quick_mask_changed_callback_;
  std::function<bool(LayerId, QString, PixelBuffer, QRegion)> smart_filter_mask_committed_callback_;
  std::function<void(SelectionMode)> selection_mode_changed_callback_;
  std::function<void(QColor)> color_picked_callback_;
  std::function<void(QColor)> background_color_picked_callback_;
  // The Eyedropper tool's own Alt+click picks the background color; the paint
  // tools' temporary Alt eyedropper always picks the foreground (issue 150).
  bool color_pick_to_background_{false};
  // One "no layer to sample" warning per pick gesture, not one per drag step.
  bool color_pick_layer_warned_{false};
  std::function<void(const CanvasReadGesture&)> transient_read_callback_;
  QCursor transient_read_cursor_{Qt::CrossCursor};
  bool transient_read_dragging_{false};
  std::optional<CurvesClippingMode> curves_clipping_mode_{};
  std::optional<CurvesChannel> curves_clipping_channel_{};
  QImage curves_clipping_preview_image_{};
  std::vector<QImage> curves_clipping_display_mip_cache_{};
  qint64 curves_clipping_display_mip_source_key_{0};
  // Proofed copies of the images the paint path draws, by QImage::cacheKey, least recently
  // used first. Bounded in count and bytes; a miss costs one pass over that image.
  struct ProofedImage {
    qint64 source_key{0};
    QImage image;
  };
  std::shared_ptr<const CmykProofTransform> proof_display_transform_;
  std::vector<ProofedImage> proofed_images_;
  std::function<void()> brush_settings_changed_callback_;
  std::function<void(PenButtonAction)> pen_button_action_callback_;
  std::function<void(QPoint, QRect)> text_requested_callback_;
  std::function<bool(QPointF, bool)> text_entry_selection_drag_callback_;
  std::function<void(LayerId)> active_layer_changed_callback_;
  std::function<void(std::vector<LayerId>, LayerId)> layer_selection_requested_callback_;
  std::function<bool(std::vector<LayerId>)> move_duplicate_requested_callback_;
  std::function<void(QString)> status_callback_;
  std::function<QList<QAction*>()> selection_context_actions_callback_;
  std::function<QList<QAction*>()> selection_tool_context_actions_callback_;
  std::function<QList<QAction*>()> zoom_tool_context_actions_callback_;
  std::function<QList<QAction*>()> shape_context_actions_callback_;
  std::function<QList<QAction*>()> layer_context_actions_callback_;
  bool vector_preview_enabled_{false};
  std::optional<std::pair<LayerId, QTransform>> vector_preview_transform_key_;
  // The transform scene's document, kept for the pixel grid's tones: the render cache still
  // holds the layer where it was, so the grid over the frame samples this document instead
  // (a composite of the view's document rect only, made on the preview worker).
  std::shared_ptr<const Document> vector_preview_transform_document_;
  QImage vector_preview_grid_image_;
  QRect vector_preview_grid_rect_;
  // Set by paintEvent when the sharp frame covered a Free Transform session's content, so
  // draw_free_transform draws only the box, not the resampled pixels over the frame.
  bool transform_vector_preview_drawn_{false};
  std::uint64_t vector_preview_generation_{1};
  std::uint64_t vector_preview_completed_generation_{0};
  std::shared_ptr<const VectorPreviewScene> vector_preview_scene_;
  std::shared_ptr<std::atomic_bool> vector_preview_cancel_;
  bool vector_preview_in_flight_{false};
  std::optional<VectorPreviewView> vector_preview_requested_view_;
  std::optional<VectorPreviewView> vector_preview_completed_view_;
  VectorPreviewFallback vector_preview_fallback_{VectorPreviewFallback::None};
  QImage vector_preview_image_;
  QString vector_preview_status_;
  std::function<void(QString, bool)> vector_preview_status_callback_;
  std::function<void(QString)> error_status_callback_;
  std::function<void(CanvasInfoState)> info_callback_;
  std::function<void()> document_changed_callback_;
  std::function<void(DocumentChangeReason)> document_changed_reason_callback_;
  std::function<void()> view_changed_callback_;
  std::function<void()> transform_controls_changed_callback_;
  std::function<bool(LayerId)> text_layer_transform_render_callback_;
  std::function<bool(const std::vector<LayerId>&)> missing_font_transform_confirm_callback_;
  std::function<bool(LayerId)> smart_object_transform_render_callback_;
  std::function<std::optional<QImage>(LayerId, QString*)> smart_object_source_image_callback_;
  std::function<void(LayerId)> smart_object_paint_prompt_callback_;
};

// Resamples `source` through `source_to_document` into document space (straight-alpha
// RGBA8888; samples are premultiplied-weighted so transparent texels never bleed color).
// Shared by the free-transform commit path and the smart-object preview renderer.
struct TransformedImage {
  QImage image;
  Rect bounds{};
  // resample_warped_rgba8 only: (pixel, cell) inverse tests the walk ran, the warp
  // renderer's work measure (GitHub issue 299: a stretched cell must not test its
  // whole bounding box).
  std::uint64_t cell_pixel_tests{0};
};
TransformedImage resample_transformed_rgba8(const QImage& source, const QTransform& source_to_document,
                                            CanvasWidget::TransformInterpolation interpolation);
// Warp-mesh variant: inverts each cell of the forward-evaluated surface grid
// (core/warp_mesh) per output pixel; folds resolve first-writer-wins in row-major
// cell order (deterministic simplification). Output rows split across workers,
// byte-identical to the sequential walk. `clip` (grid document space) limits the
// output to that rect; the kept pixels equal the unclipped render's.
TransformedImage resample_warped_rgba8(const QImage& source, const WarpSurfaceGrid& grid,
                                       CanvasWidget::TransformInterpolation interpolation,
                                       const std::optional<Rect>& clip = std::nullopt);
// Gray8 variant for linked layer masks (multi-target Free Transform commit):
// samples outside the source read as `default_color`, never 0, so a transformed
// mask blends toward its implicit surround instead of growing a fringe at the
// new bounds edge.
struct TransformedMask {
  PixelBuffer pixels;
  Rect bounds{};
};
TransformedMask resample_transformed_gray8(const PixelBuffer& source, std::uint8_t default_color,
                                           const QTransform& source_to_document,
                                           CanvasWidget::TransformInterpolation interpolation);

}  // namespace patchy::ui
