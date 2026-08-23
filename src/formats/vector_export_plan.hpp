#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/vector_shape.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The target-independent half of a structure-preserving vector export. SVG and
// editable PDF both walk the layer stack bottom-up, keep shape layers as real
// vectors where the target can express them, and flatten runs that need the
// compositor (adjustment layers, blend modes the target lacks) into one raster
// chunk. The policy lives here once so the two writers cannot drift; each writer
// adds the checks only its own format cares about (docs/svg.md "Export").
namespace patchy::vector_export {

// How a shape layer's combine structure maps onto a flat "one path, one fill
// rule" target.
enum class CombineExport {
  SinglePath,     // one even-odd path is exact
  SeparatePaths,  // one path per Add group (union of opaque paint)
  Unsupported     // rasterize
};

// Subpaths grouped by shape_group in first-appearance order (matches
// sequential-combine rasterization).
[[nodiscard]] std::vector<VectorPath> split_shape_groups(const VectorPath& path);
[[nodiscard]] CombineExport classify_combine(const VectorPath& path);

// True when painting the fill twice over an overlap looks the same as painting
// it once (the SeparatePaths form needs this).
[[nodiscard]] bool paint_is_opaque(const VectorFill& fill, const PatternStore& patterns);
// Linear, Radial, and Reflected are the gradient types both targets express.
[[nodiscard]] bool gradient_type_supported(const VectorFill& fill);

// The checks a shape layer must pass for EITHER target before it can stay a
// vector: a real unlocked shape, no layer style, full fill opacity, an enabled
// non-inverted path, supported gradient types, a Normal stroke blend, a
// foldable combine structure (with opaque paint where overlaps double-cover),
// an opaque fill under an Outside stroke, and a plain vector mask (enabled,
// full density, no feather). Raster masks are the caller's decision.
[[nodiscard]] bool shape_layer_exportable_as_vector(const Layer& layer, const PatternStore& patterns);

// A group stays a container when it carries no style, full fill opacity, a plain
// vector mask, and no disabled raster mask.
[[nodiscard]] bool group_exportable(const Layer& group);

// One unit of a sibling list: a layer, or a clipping base plus its clipped run.
struct Unit {
  std::size_t begin{0};
  std::size_t end{0};  // [begin, end)
};
[[nodiscard]] std::vector<Unit> build_units(const std::vector<Layer>& siblings);

// Does compositing this unit need the pixels below it in a way the target cannot
// express per element? Adjustment layers and blend modes the target lacks force
// merging everything below into one chunk; a pass-through group propagates a
// barrier inside it to this sibling level.
[[nodiscard]] bool unit_is_barrier(const std::vector<Layer>& siblings, const Unit& unit,
                                   const std::function<bool(BlendMode)>& blend_expressible);

// Gradient geometry inverted from the import mapping: Patchy's calibrated span is
// the center chord of the fill's aligned bounds (docs/vector-tools.md "GdFl
// gradient fill geometry"); an empty path means a full-canvas fill layer.
struct GradientExportGeometry {
  double center_x{0.0};
  double center_y{0.0};
  double radius{0.0};  // radial
  double x1{0.0};      // linear / reflected: the ramp's two ends
  double y1{0.0};
  double x2{0.0};
  double y2{0.0};
  bool reflected{false};  // mirror the ramp about its ends (span already halved)
};
[[nodiscard]] GradientExportGeometry gradient_export_geometry(const LayerStyleGradient& gradient,
                                                              const VectorPath& path, std::int32_t document_width,
                                                              std::int32_t document_height);

// The ramp as linearly interpolated stops (offsets ascending, 0..1). Plain ramps
// emit their real stops (merged union of the color and alpha locations, reverse
// via 1-x); non-identity midpoints, the Classic ease, and noise gradients resample
// into 65 dense stops.
struct GradientExportStop {
  double offset{0.0};
  RgbColor color{};
  float opacity{1.0F};
};
[[nodiscard]] std::vector<GradientExportStop> gradient_export_stops(const LayerStyleGradient& gradient);

// Tight alpha bounding box; nullopt for fully transparent pixels.
[[nodiscard]] std::optional<Rect> opaque_bounds(const PixelBuffer& pixels);
[[nodiscard]] PixelBuffer crop_pixels(const PixelBuffer& pixels, Rect rect);

}  // namespace patchy::vector_export
