#pragma once

#include "core/document.hpp"
#include "core/vector_shape.hpp"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace patchy::ui {

inline constexpr std::uint64_t kVectorPreviewRasterBudget = 128ULL * 1024 * 1024;
inline constexpr int kVectorPreviewTileSize = 256;

enum class VectorPreviewFallback { None, Content, Paint, Masks, Blending, Effects, Coordinates, Memory, Failed };
[[nodiscard]] QString vector_preview_fallback_text(VectorPreviewFallback reason);

// No document pixels, styles, import payloads, or mutable document references.
struct VectorPreviewNode {
  LayerId id{};
  float opacity{1.0F};
  float fill_opacity{1.0F};
  BlendMode blend{BlendMode::Normal};
  bool group{false};
  std::optional<VectorShapeContent> shape;
  std::optional<QRectF> bounds;  // null = potentially covers the entire canvas
  std::vector<VectorPreviewNode> children;
};

struct VectorPreviewScene {
  std::vector<VectorPreviewNode> layers;
  VectorPreviewFallback fallback{VectorPreviewFallback::None};
};

struct VectorPreviewView {
  QSize pixels;                  // physical viewport pixels
  double scale{1.0};             // physical pixels per document pixel
  QPointF offset;                // document origin in the physical viewport
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
