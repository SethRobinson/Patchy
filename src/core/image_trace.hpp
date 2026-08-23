#pragma once

#include "core/layer.hpp"
#include "core/vector_shape.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Raster-to-vector tracing (Trace Image to Shapes): quantize a pixel layer to
// a few colors, walk the boundary of every color region, and fit the contours
// into bezier subpaths. Output is one compound path per color (or per color
// and nesting depth), ready to become shape layers.
//
// The pipeline is built from published, long-expired techniques: median-cut
// quantization (Heckbert 1982), connected-component speckle removal, the
// selection-outline boundary walk, Douglas-Peucker (1973) and Schneider
// (1990) fitting in core/path_fit. Every stage is integer or deterministic
// double math with fixed tie-breaks (the cross-toolchain rule).
//
// Legal boundary (docs/legal-constraints.md, "Vector tracing"): tracing runs
// ONCE on explicit request into static shape layers. Never attach a live
// re-tracing link between the source layer and its traced result, never vary
// parameters per region, and never derive a path from two user-picked edge
// points. Centerline (stroke) tracing is not implemented.
namespace patchy {

struct ImageTraceOptions {
  enum class Mode : std::uint8_t { Color, Grayscale, BlackAndWhite };
  // Abutting: every color region is an exact cutout, holes stay holes.
  // Overlapping: a region that encloses other regions is painted without
  // those holes and the enclosed regions stack on top of it, which hides the
  // hairline gaps between abutting anti-aliased edges.
  enum class Method : std::uint8_t { Abutting, Overlapping };

  Mode mode{Mode::Color};
  int colors{16};           // Color / Grayscale palette size, kMinColors..kMaxColors
  int threshold{128};       // BlackAndWhite: luminance < threshold is black, 1..255
  int paths{50};            // 0..100, curve fit fidelity (see image_trace_fit_tolerance)
  int corners{75};          // 0..100, corner sharpness (see image_trace_corner_angle)
  int noise{25};            // regions smaller than this many pixels are merged away, 1..100
  Method method{Method::Abutting};
  bool snap_curves_to_lines{false};
  bool ignore_white{false};  // white regions become untraced (transparent)

  static constexpr int kMinColors = 2;
  static constexpr int kMaxColors = 64;
};

// One traced color: a compound path whose outer contours are Add groups and
// whose holes are Subtract groups (per-subpath groups in row-major order,
// the Make Work Path convention). `depth` is the nesting depth in Overlapping
// mode (0 for every Abutting layer); `area` the region's pixel count.
struct ImageTraceLayer {
  RgbColor color{};
  VectorPath path;
  std::int64_t area{0};
  int depth{0};
};

struct ImageTraceResult {
  std::vector<ImageTraceLayer> layers;  // back to front
  std::size_t anchor_count{0};
  std::size_t palette_size{0};
};

// paths 0..100 -> fit tolerance in pixels, log scale from 4 px (loose) at 0
// through 1 px at 50 to 0.25 px (tight) at 100.
[[nodiscard]] double image_trace_fit_tolerance(int paths) noexcept;
// corners 0..100 -> corner angle threshold in degrees, 120 at 0 to 30 at 100.
[[nodiscard]] double image_trace_corner_angle(int corners) noexcept;

// Traces an 8-bit RGB, RGBA, or gray buffer. Pixels with alpha below 128 are
// untraced. `cancelled` (optional) is polled between stages and regions; a
// cancelled trace returns an empty result. Other pixel formats also return an
// empty result.
[[nodiscard]] ImageTraceResult trace_image(const PixelBuffer& pixels, const ImageTraceOptions& options,
                                           const std::function<bool()>& cancelled = {});

// Paints the traced layers back to front as solid fills into a straight-alpha
// RGBA8 buffer of the given size (the dialog preview and the coverage tests).
[[nodiscard]] PixelBuffer render_image_trace(const ImageTraceResult& result, std::int32_t width,
                                             std::int32_t height);

class Document;

// Builds the group layer that holds one solid shape layer per traced color
// (named "#RRGGBB", back to front), with ids allocated from `document`, the
// paths offset by the source layer's origin, and pixels baked against the
// document canvas. The caller inserts the group and records undo.
[[nodiscard]] Layer build_image_trace_group(Document& document, const ImageTraceResult& result,
                                            std::int32_t origin_x, std::int32_t origin_y,
                                            std::string group_name);

}  // namespace patchy
