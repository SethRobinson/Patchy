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
// quantization (Heckbert 1982) refined by fixed-iteration integer Lloyd
// k-means (Lloyd 1957/1982), exact optimal scalar quantization for grayscale
// (Bruce 1965 / Lloyd-Max), box-blur pre-smoothing and 3x3 label majority
// filtering (classic image processing), connected-component speckle removal,
// the selection-outline boundary walk, Douglas-Peucker (1973) and Schneider
// (1990) fitting in core/path_fit. Every stage is integer or deterministic
// double math with fixed tie-breaks (the cross-toolchain rule), including the
// parallel stages: workers only fill disjoint position-indexed slots, so the
// output is bit-identical to a sequential run.
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
  // those holes and the enclosed regions stack on top of it, and every shape
  // additionally grows a few pixels UNDER its later-painted neighbors
  // (never into earlier-painted or untraced pixels, so visible geometry and
  // the silhouette are unchanged), which hides the hairline gaps between
  // abutting anti-aliased edges.
  enum class Method : std::uint8_t { Abutting, Overlapping };

  Mode mode{Mode::Color};
  int colors{16};           // Color / Grayscale palette size, kMinColors..kMaxColors
  int threshold{128};       // BlackAndWhite: luminance < threshold is black, 1..255
  int paths{50};            // 0..100, curve fit fidelity (see image_trace_fit_tolerance)
  int corners{75};          // 0..100, corner sharpness (see image_trace_corner_angle)
  int noise{25};            // regions smaller than this many pixels are merged away, 1..100
  int smoothing{0};         // denoise blur before quantization, 0..kMaxSmoothing px (0 = off)
  int max_anchors{0};       // anchor budget: refit at coarser tolerance until met (0 = unlimited)
  Method method{Method::Abutting};
  bool snap_curves_to_lines{false};
  bool ignore_white{false};  // white regions become untraced (transparent)

  static constexpr int kMinColors = 2;
  static constexpr int kMaxColors = 256;
  static constexpr int kMaxSmoothing = 10;

  friend bool operator==(const ImageTraceOptions&, const ImageTraceOptions&) = default;
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

// Traces an RGB, RGBA, or gray buffer. Pixels with alpha below 128 are
// untraced. 16-bit and float buffers convert to 8-bit first (value/257 with
// rounding; floats clamp to 0..1, linear). `cancelled` (optional) is polled
// between stages and regions, including from worker threads, so the
// predicate must be thread-safe (an atomic read, like the dialog's); a
// cancelled trace returns an empty result. Buffers with other channel counts
// also return an empty result. `max_workers` caps the parallel fan-out
// (0 = automatic, 1 = forced sequential; the output is identical either way).
[[nodiscard]] ImageTraceResult trace_image(const PixelBuffer& pixels, const ImageTraceOptions& options,
                                           const std::function<bool()>& cancelled = {}, int max_workers = 0);

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
