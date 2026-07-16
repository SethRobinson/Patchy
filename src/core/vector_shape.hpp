#pragma once

#include "core/layer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Vector shape/path data model: shape layers, vector masks, and document paths.
// A shape layer stays LayerKind::Pixel (the text/smart-object pattern): the
// editable source of truth is a VectorShapeContent held on the layer, and
// pixels() carries the rasterized fill+stroke cache. Photoshop interop encodes
// this model into the vmsk/vogk/vstk/SoCo/GdFl/PtFl tagged blocks; every
// encoding fact referenced here was pinned by observation of Photoshop 27.8
// (docs/vector-tools.md, July 2026).
namespace patchy {

// One bezier knot. Control points are absolute document-pixel positions:
// control_in leads toward the PREVIOUS anchor, control_out toward the NEXT.
// (PSD knot records store the pairs in (in, anchor, out) order, y before x,
// as 8.24 fixed-point fractions of the canvas extent - converted at I/O.)
// smooth mirrors Photoshop's linked-knot selectors (1/4) vs corner (2/5).
struct PathAnchor {
  double anchor_x{0.0};
  double anchor_y{0.0};
  double in_x{0.0};
  double in_y{0.0};
  double out_x{0.0};
  double out_y{0.0};
  bool smooth{false};

  friend bool operator==(const PathAnchor&, const PathAnchor&) = default;
};

// Values match the PSD subpath-length-record operation field.
enum class PathCombineOp : std::uint8_t {
  Xor = 0,
  Add = 1,
  Subtract = 2,
  Intersect = 3
};

struct PathSubpath {
  std::vector<PathAnchor> anchors;
  bool closed{true};
  PathCombineOp op{PathCombineOp::Add};
  // PSD length records carry a per-subpath index that groups the subpaths of
  // one shape unit (a custom-shape stamp is several subpaths sharing a group;
  // vogk origination entries join via the same value). Subpaths of one group
  // rasterize together under the even-odd rule; combine ops apply BETWEEN
  // groups. Simple shapes use one subpath per group.
  std::int32_t shape_group{0};

  friend bool operator==(const PathSubpath&, const PathSubpath&) = default;
};

struct VectorPathBounds {
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
};

struct VectorPath {
  std::vector<PathSubpath> subpaths;
  // Raw values of the selector-6 (path fill rule) and selector-8 (initial
  // fill) records; Photoshop 27.8 writes zeros and renders even-odd within a
  // group regardless, so these are preserved rather than interpreted.
  std::uint16_t fill_rule_value{0};
  std::uint16_t initial_fill_value{0};

  [[nodiscard]] bool empty() const noexcept;
  // Hull of anchors and control points (a bezier never escapes it). nullopt
  // for an empty path.
  [[nodiscard]] std::optional<VectorPathBounds> bounds() const noexcept;
  // Next unused shape_group value (for appending independent subpaths).
  [[nodiscard]] std::int32_t next_shape_group() const noexcept;

  friend bool operator==(const VectorPath&, const VectorPath&) = default;
};

enum class VectorFillKind : std::uint8_t {
  None,
  Solid,
  Gradient,
  Pattern
};

// Fill appearance for shape layers, fill layers, and stroke paint. The
// gradient reuses the layer-style gradient model (GdFl's Grad object parses
// with the same code as gradient overlays); pattern tiles live in the
// document PatternStore, referenced by id like pattern overlays.
struct VectorFill {
  VectorFillKind kind{VectorFillKind::Solid};
  RgbColor color{0, 0, 0};
  LayerStyleGradient gradient{};
  // GdFl noisePreSeed, preserved on read; authored fills write 0.
  std::int32_t gradient_noise_pre_seed{0};
  std::string pattern_id;
  std::string pattern_name;
  double pattern_scale{1.0};
  double pattern_angle_degrees{0.0};
  bool pattern_linked{true};
  double pattern_phase_x{0.0};
  double pattern_phase_y{0.0};

  friend bool operator==(const VectorFill&, const VectorFill&) = default;
};

enum class VectorStrokeAlignment : std::uint8_t { Inside, Center, Outside };
enum class VectorStrokeCap : std::uint8_t { Butt, Round, Square };
enum class VectorStrokeJoin : std::uint8_t { Miter, Round, Bevel };

// Shape stroke (vstk strokeStyle descriptor; 16-item canonical order recorded
// in docs/vector-tools.md). Dash entries are stored in stroke-width multiples,
// matching the descriptor's unitless values.
struct VectorStroke {
  bool enabled{false};
  bool fill_enabled{true};
  double width{1.0};
  double dash_offset{0.0};
  double miter_limit{100.0};
  VectorStrokeCap cap{VectorStrokeCap::Butt};
  VectorStrokeJoin join{VectorStrokeJoin::Miter};
  VectorStrokeAlignment alignment{VectorStrokeAlignment::Center};
  bool scale_lock{false};
  bool stroke_adjust{false};
  std::vector<double> dashes;
  BlendMode blend_mode{BlendMode::Normal};
  double opacity{1.0};
  VectorFill content{};
  double resolution{72.0};

  friend bool operator==(const VectorStroke&, const VectorStroke&) = default;
};

// Live-shape parameter kinds. Values are Patchy's own; the PSD keyOriginType
// integers (rect 1, rounded rect 2, line 4, ellipse 5) map at I/O. Kinds
// Photoshop was not observed writing import as Custom with the raw origination
// descriptor preserved (the path still renders and stays editable; only
// live-parameter editing is unavailable).
enum class LiveShapeKind : std::uint8_t {
  None,
  Rectangle,
  RoundedRectangle,
  Line,
  Ellipse,
  Custom
};

// One vogk keyDescriptorList entry: the regenerating annotation for a live
// subpath group. The PATH stays the render source of truth; editing a live
// parameter regenerates the group's subpaths, and direct anchor edits drop
// the origination for that group (Photoshop's live-shape invalidation).
struct LiveShapeParams {
  LiveShapeKind kind{LiveShapeKind::None};
  double resolution{72.0};
  // keyOriginShapeBBox, document pixels.
  double left{0.0};
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
  // Rounded-rect corner radii in model order TL, TR, BR, BL. (The descriptor
  // stores them as topRight, topLeft, bottomLeft, bottomRight.)
  std::array<double, 4> corner_radii{0.0, 0.0, 0.0, 0.0};
  // keyOriginBoxCorners rectangleCornerA..D as (x, y) pairs.
  std::array<double, 8> box_corners{};
  // vogk Trnf: xx, xy, yx, yy, tx, ty.
  std::array<double, 6> transform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  // Line kind (keyOriginLine*).
  double line_start_x{0.0};
  double line_start_y{0.0};
  double line_end_x{0.0};
  double line_end_y{0.0};
  double line_weight{1.0};
  bool arrow_start{false};
  bool arrow_end{false};
  double arrow_width{0.0};
  double arrow_length{0.0};
  std::int32_t arrow_concavity{0};
  bool arrow_width_unit_pixels{true};
  bool arrow_length_unit_pixels{true};
  // Joins this entry to PathSubpath::shape_group.
  std::int32_t index{0};
  // Custom/unmodeled origination: the entry's raw descriptor bytes, re-emitted
  // verbatim while the group is untouched.
  std::vector<std::uint8_t> raw_descriptor;

  friend bool operator==(const LiveShapeParams&, const LiveShapeParams&) = default;
};

// The whole editable content of a shape/fill layer.
struct VectorShapeContent {
  VectorPath path;         // empty path == full-canvas fill layer
  // The shape's vmsk flags: a disabled path fills the whole canvas, an
  // inverted one fills the complement.
  bool path_disabled{false};
  bool path_inverted{false};
  VectorFill fill{};
  VectorStroke stroke{};
  std::vector<LiveShapeParams> origination;
};

// A vector mask on any layer. Coexists with (multiplies against) the raster
// mask. The cache is the rasterized grayscale coverage, regenerated on edit -
// never at render time. When a PSD carried Photoshop's baked derived plane
// (density/feather set), the cache seeds from that plane until the first edit.
struct LayerVectorMask {
  VectorPath path;
  bool disabled{false};   // vmsk flags bit 2
  bool inverted{false};   // vmsk flags bit 0
  bool unlinked{false};   // vmsk flags bit 1 (does not follow layer moves)
  bool hides_effects{false};
  // Mask parameters (Properties panel): density raw 0..255, feather pixels.
  std::uint8_t density{255};
  double feather{0.0};
  Rect cache_bounds{};
  PixelBuffer cache{};    // gray8 coverage within cache_bounds
};

// --- layer metadata flags (tiny status strings; the structured content lives
// in Layer's vector fields, added with the compositor integration) ---

inline constexpr const char* kLayerMetadataVectorShape = "patchy.vector.shape";  // "1" = shape/fill layer
// "psd_raster_preview" (Photoshop's baked pixels kept at import) or
// "patchy_raster" (rasterized by Patchy).
inline constexpr const char* kLayerMetadataVectorRasterStatus = "patchy.vector.raster_status";
// Present when vector content diverged from the preserved PSD blocks; the
// writer then regenerates vmsk/vogk/vstk/SoCo/GdFl/PtFl instead of re-emitting
// the originals.
inline constexpr const char* kLayerMetadataVectorBlockDirty = "patchy.vector.block_dirty";
// Why the vector content cannot be edited: "" (editable) or "unparsed" (a
// vector block failed to parse; everything stays byte-preserved and locked).
inline constexpr const char* kLayerMetadataVectorLock = "patchy.vector.lock";

inline constexpr const char* kVectorRasterStatusPhotoshop = "psd_raster_preview";
inline constexpr const char* kVectorRasterStatusPatchy = "patchy_raster";

[[nodiscard]] bool layer_has_vector_shape_marker(const Layer& layer);
// The real predicate: the metadata marker AND the structured content.
[[nodiscard]] bool layer_is_vector_shape(const Layer& layer);
// Text, smart-object, or vector-shape: pixels are a derived cache of richer
// content, so hand edits must rasterize first (the shared guard condition).
[[nodiscard]] bool layer_pixels_are_procedural(const Layer& layer);
[[nodiscard]] std::string vector_lock_reason(const Layer& layer);
[[nodiscard]] bool layer_vector_block_dirty(const Layer& layer);
void mark_layer_vector_block_dirty(Layer& layer);
// The rasterize / merge-target semantic: drops the vector fields, every
// patchy.vector.* key, and the preserved PSD vector blocks so the layer
// becomes a plain pixel layer everywhere, resave included (the
// strip_layer_smart_object_data pattern).
void strip_layer_vector_data(Layer& layer);

// --- path text codec (tests, custom-shape library storage; PSD uses the
// binary record forms instead) ---

[[nodiscard]] std::string serialize_vector_path(const VectorPath& path);
[[nodiscard]] std::optional<VectorPath> parse_vector_path(std::string_view text);

// --- PSD path-record fixed point (i32 8.24 as a fraction of the canvas
// extent; the same convention translate_vector_mask_payload patches) ---

[[nodiscard]] std::int32_t path_coordinate_to_fixed(double pixels, std::int32_t extent) noexcept;
[[nodiscard]] double path_coordinate_from_fixed(std::int32_t fixed, std::int32_t extent) noexcept;

// Uniform affine transform of every anchor/control point (a*x + c*y + tx,
// b*x + d*y + ty), used by move/transform/resize integration.
void transform_vector_path(VectorPath& path, const std::array<double, 6>& matrix);
void translate_vector_path(VectorPath& path, double dx, double dy);
// Translates the path AND every absolute-coordinate origination field
// (bbox, box corners, line endpoints, transform translation).
void translate_vector_shape_content(VectorShapeContent& content, double dx, double dy);

}  // namespace patchy
