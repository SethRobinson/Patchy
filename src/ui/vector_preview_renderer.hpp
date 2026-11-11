#pragma once

#include "core/document.hpp"
#include "core/vector_shape.hpp"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patchy::ui {

inline constexpr std::uint64_t kVectorPreviewRasterBudget = 128ULL * 1024 * 1024;
inline constexpr int kVectorPreviewTileSize = 256;
// A shape layer's Feather is blurred at view resolution while its reach (the three box
// radii of the scaled blur, about three times the feather) stays within this many view
// pixels: every tile blurs that far past its own edge. A wider feather is so smooth that
// the shape's document pixels, read bilinearly, show the same picture at a fraction of the
// cost (docs/vector-preview.md, "Shape Feather and Density").
inline constexpr int kVectorPreviewNativeFeatherReach = 192;

enum class VectorPreviewFallback { None, Content, Paint, Masks, Blending, Effects, Coordinates, Memory, Failed };
[[nodiscard]] QString vector_preview_fallback_text(VectorPreviewFallback reason);

// A pattern paint of a magnified view (a shape's pattern fill or stroke, a Pattern
// Overlay), kept as the document holds it. Each tile draws it as the pixel view shows it:
// sampled at document pixels the way the document render samples it, then enlarged by the
// view's display rule into a tile-sized pattern the paint reads one texel per view pixel
// (docs/vector-preview.md, "Pattern sampling").
struct VectorPreviewPattern {
  std::string id;         // the view tile, in the pattern store of the tile being drawn
  std::string source_id;  // the document's pattern
  float scale{1.0F};
  float angle_degrees{0.0F};
  bool linked{false};
  float phase_x{0.0F};
  float phase_y{0.0F};
  // A shape's own paint is read as far past the tile's area as its Feather blurs.
  bool shape_paint{false};
};

// Immutable render properties and shared, copy-on-write source pixels. No
// mutable document references or duplicated source rasters. Temporary copies
// replace their pixels/masks with viewport-sized surfaces on the worker.
struct VectorPreviewNode {
  Layer layer;
  Rect source_bounds;
  std::optional<VectorShapeContent> shape;
  bool compound{false};
  // The shape's Feather is too wide to blur at view resolution: its document pixels (and
  // effect planes) are read bilinearly instead (kVectorPreviewNativeFeatherReach).
  bool sampled{false};
  std::optional<QRectF> bounds;  // null = potentially covers the entire canvas
  Rect fill_bounds;
  Rect stroke_bounds;
  // Filled per render for a magnified view: the layer's pattern paints and the document
  // position of its effects reference point, which anchors the linked ones.
  std::vector<VectorPreviewPattern> view_patterns;
  std::array<double, 2> pattern_reference{};
  std::vector<VectorPreviewNode> children;
};

struct VectorPreviewScene {
  std::vector<VectorPreviewNode> layers;
  PatternStore patterns;
  QSize canvas;
  bool has_vectors{false};
  VectorPreviewFallback fallback{VectorPreviewFallback::None};
};

struct VectorPreviewView {
  QSize pixels;                  // physical viewport pixels
  double scale{1.0};             // physical pixels per document pixel
  QPointF offset;                // document origin in the physical viewport
  // The canvas's display smoothing rule for this view (uses_smooth_display_scaling,
  // docs/view-navigation.md "Display interpolation"): pixel layers, raster masks and
  // pattern paints are read bilinearly, as the composite is drawn, instead of as exact
  // blocks. Vector geometry is sharp either way.
  bool smooth_pixels{false};
  friend bool operator==(const VectorPreviewView&, const VectorPreviewView&) = default;
};

struct VectorPreviewResult {
  QImage image;
  VectorPreviewFallback fallback{VectorPreviewFallback::None};
  // Conservative peak reservation for output, retained frame, temporary raster
  // planes and compositor group buffers. Path geometry is accounted separately
  // by the scene and is never proportional to the enlarged document's area.
  std::uint64_t peak_raster_bytes{0};
  double elapsed_ms{0.0};
};

[[nodiscard]] VectorPreviewScene build_vector_preview_scene(const Document& document);
[[nodiscard]] VectorPreviewResult render_vector_preview(
    const VectorPreviewScene& scene, const VectorPreviewView& view,
    std::uint64_t retained_frame_bytes = 0, const std::atomic_bool* cancelled = nullptr);

}  // namespace patchy::ui
