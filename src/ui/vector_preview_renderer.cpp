#include "ui/vector_preview_renderer.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "ui/image_document_io.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace patchy::ui {
namespace {

// Leave ample headroom for fixed-point differences, doubled control points,
// stroke miters and origin subtraction in the existing 24.8 rasterizer.
constexpr double kCoordinateLimit = 1'000'000.0;

bool solid_paint(const VectorFill& paint) {
  return paint.kind == VectorFillKind::None || paint.kind == VectorFillKind::Solid;
}

double stroke_padding(const VectorStroke& stroke) {
  return stroke.enabled ? stroke.width * (stroke.join == VectorStrokeJoin::Miter
      ? std::max(2.0, stroke.miter_limit) : 2.0) : 0.0;
}

void copy_nodes(const std::vector<Layer>& layers, std::vector<VectorPreviewNode>& out,
                VectorPreviewFallback& reason) {
  for (const auto& layer : layers) {
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }
    if (layer.mask().has_value() || layer.vector_mask() != nullptr) {
      reason = VectorPreviewFallback::Masks;
    } else if (layer.clipped() || !blend_if_is_identity(layer.blend_if()) ||
               layer.blend_if_payload_status() == BlendIfPayloadStatus::Unsupported ||
               layer.restricted_channels() != 0 || !layer.channel_restriction_supported() ||
               (layer.blend_mode() != BlendMode::Normal &&
                !(layer.kind() == LayerKind::Group && layer.blend_mode() == BlendMode::PassThrough))) {
      reason = VectorPreviewFallback::Blending;
    } else if (!layer.layer_style().empty() || layer.smart_filter_stack() != nullptr) {
      reason = VectorPreviewFallback::Effects;
    }
    if (reason != VectorPreviewFallback::None) {
      return;
    }
    VectorPreviewNode node;
    node.id = layer.id();
    node.opacity = layer.opacity();
    node.fill_opacity = layer.fill_opacity();
    node.blend = layer.blend_mode();
    node.group = layer.kind() == LayerKind::Group;
    if (node.group) {
      copy_nodes(layer.children(), node.children, reason);
    } else if (layer.kind() != LayerKind::Pixel || layer.vector_shape() == nullptr ||
               layer_is_text(layer) || layer_is_smart_object(layer) || !vector_lock_reason(layer).empty()) {
      reason = VectorPreviewFallback::Content;
    } else {
      const auto& source = *layer.vector_shape();
      for (const auto& subpath : source.path.subpaths) {
        for (const auto& a : subpath.anchors) {
          for (const double value : {a.anchor_x, a.anchor_y, a.in_x, a.in_y, a.out_x, a.out_y}) {
            if (!std::isfinite(value)) { reason = VectorPreviewFallback::Coordinates; return; }
          }
        }
      }
      if (source.stroke.enabled && (!std::isfinite(source.stroke.width) || source.stroke.width < 0.0 ||
          !std::isfinite(source.stroke.miter_limit) || !std::isfinite(source.stroke.dash_offset) ||
          !std::isfinite(source.stroke.opacity) || std::any_of(source.stroke.dashes.begin(), source.stroke.dashes.end(),
                                                            [](double dash) { return !std::isfinite(dash); }))) {
        reason = VectorPreviewFallback::Coordinates;
        return;
      }
      if ((source.stroke.fill_enabled && !solid_paint(source.fill)) ||
          (source.stroke.enabled && !solid_paint(source.stroke.content))) {
        reason = VectorPreviewFallback::Paint;
      } else if (source.stroke.enabled && source.stroke.blend_mode != BlendMode::Normal) {
        reason = VectorPreviewFallback::Blending;
      } else {
        node.shape.emplace();
        auto& shape = *node.shape;
        shape.path = source.path;
        shape.path_disabled = source.path_disabled;
        shape.path_inverted = source.path_inverted;
        shape.fill = source.fill;
        shape.stroke = source.stroke;
        const auto bounds = shape.path.bounds();
        const bool complement = shape.path_disabled || shape.path_inverted ||
            (!shape.path.subpaths.empty() && shape.path.subpaths.front().op == PathCombineOp::Subtract);
        if (bounds && !complement) {
          // Bounds include control points. Stroke alignment can consume the
          // full width, with square caps and miters extending farther.
          const double padding = stroke_padding(shape.stroke);
          node.bounds = QRectF(QPointF(bounds->left - padding, bounds->top - padding),
                               QPointF(bounds->right + padding, bounds->bottom + padding));
        }
      }
    }
    if (reason != VectorPreviewFallback::None) {
      return;
    }
    out.push_back(std::move(node));
  }
}

bool safe_number(double value) {
  return std::isfinite(value) && std::abs(value) <= kCoordinateLimit;
}

void transform_nodes(std::vector<VectorPreviewNode>& nodes, const VectorPreviewView& view) {
  for (auto& node : nodes) {
    if (node.group) {
      transform_nodes(node.children, view);
      continue;
    }
    if (node.bounds) {
      const auto& b = *node.bounds;
      node.bounds = QRectF(b.topLeft() * view.scale + view.offset, b.size() * view.scale);
      if (!node.bounds->adjusted(-2, -2, 2, 2).intersects(QRectF(QPointF(), QSizeF(view.pixels)))) {
        node.shape.reset();
        continue;
      }
    }
    auto& shape = *node.shape;
    transform_vector_path(shape.path, {view.scale, 0.0, 0.0, view.scale, view.offset.x(), view.offset.y()});
    shape.stroke.width *= view.scale;  // dash lengths/offset are width multiples
    const double stroke_extent = stroke_padding(shape.stroke);
    if (!safe_number(stroke_extent) || stroke_extent < 0.0) {
      throw VectorPreviewFallback::Coordinates;
    }
    for (const auto& subpath : shape.path.subpaths) {
      for (const auto& a : subpath.anchors) {
        for (const double value : {a.anchor_x, a.anchor_y, a.in_x, a.in_y, a.out_x, a.out_y}) {
          if (!safe_number(value) || std::abs(value) + stroke_extent > kCoordinateLimit) {
            throw VectorPreviewFallback::Coordinates;
          }
        }
      }
    }
  }
}

struct RasterBudget {
  std::uint64_t retained{};
  std::uint64_t peak{};
  std::uint64_t workspace{};
  void check(std::uint64_t temporary) {
    if (retained > kVectorPreviewRasterBudget || temporary > kVectorPreviewRasterBudget - retained) {
      throw VectorPreviewFallback::Memory;
    }
    peak = std::max(peak, retained + temporary);
  }
  void check_workspace(std::uint64_t temporary) {
    // Composition needs the deepest group's workspace with ALL tile layers
    // retained, including layers that follow that group in stacking order.
    workspace = std::max(workspace, temporary);
    check(workspace);
  }
};

void raster_nodes(const std::vector<VectorPreviewNode>& nodes, std::vector<Layer>& layers, Rect tile,
                  RasterBudget& budget, int depth, const std::atomic_bool* cancelled) {
  const auto tile_area = static_cast<std::uint64_t>(tile.width) * tile.height;
  for (const auto& node : nodes) {
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
      return;
    }
    if (node.group) {
      // Covers group isolation, pass-through snapshots, and the compositor's
      // temporary alpha planes along the deepest active group stack.
      budget.check_workspace(tile_area * 64U * static_cast<std::uint64_t>(depth + 1));
      Layer group(node.id, {}, LayerKind::Group);
      group.set_blend_mode(node.blend);
      group.set_opacity(node.opacity);
      group.set_fill_opacity(node.fill_opacity);
      raster_nodes(node.children, group.children(), tile, budget, depth + 1, cancelled);
      layers.push_back(std::move(group));
    } else if (node.shape && (!node.bounds || node.bounds->adjusted(-2, -2, 2, 2).intersects(
                                        QRectF(tile.x, tile.y, tile.width, tile.height)))) {
      // The rasterizer's coverage/combined/split/trim planes are tile-bounded.
      // Reserve conservatively before it allocates, then retain only RGBA.
      budget.check_workspace(tile_area * 64U * static_cast<std::uint64_t>(depth + 1));
      auto raster = rasterize_vector_shape(*node.shape, tile, nullptr, nullptr);
      if (raster.bounds.empty()) {
        continue;
      }
      budget.retained += static_cast<std::uint64_t>(raster.pixels.data().size());
      budget.check_workspace(tile_area * 64U * static_cast<std::uint64_t>(depth + 1));
      Layer layer(node.id, {}, std::move(raster.pixels));
      layer.set_bounds(raster.bounds);
      layer.set_opacity(node.opacity);
      layer.set_fill_opacity(node.fill_opacity);
      layers.push_back(std::move(layer));
    }
  }
}

}  // namespace

QString vector_preview_fallback_text(VectorPreviewFallback reason) {
  const auto tr = [](const char* text) { return QCoreApplication::translate("VectorPreview", text); };
  switch (reason) {
    case VectorPreviewFallback::None: return {};
    case VectorPreviewFallback::Content: return tr("Pixel view: visible content includes non-vector or unsupported layers.");
    case VectorPreviewFallback::Paint: return tr("Pixel view: gradient and pattern paints are not supported by Vector Preview.");
    case VectorPreviewFallback::Masks: return tr("Pixel view: separate layer masks are not supported by Vector Preview.");
    case VectorPreviewFallback::Blending: return tr("Pixel view: clipping or advanced blending is not supported by Vector Preview.");
    case VectorPreviewFallback::Effects: return tr("Pixel view: layer effects and filters are not supported by Vector Preview.");
    case VectorPreviewFallback::Coordinates: return tr("Pixel view: vector coordinates exceed the preview range at this zoom.");
    case VectorPreviewFallback::Memory: return tr("Pixel view: Vector Preview reached its memory limit.");
    case VectorPreviewFallback::Failed: return tr("Pixel view: Vector Preview could not render this view.");
  }
  return {};
}

VectorPreviewScene build_vector_preview_scene(const Document& document) {
  VectorPreviewScene scene;
  if (document.palette_editing() || document.format().color_mode != ColorMode::RGB ||
      document.format().bit_depth != BitDepth::UInt8) {
    scene.fallback = VectorPreviewFallback::Content;
  } else {
    copy_nodes(document.layers(), scene.layers, scene.fallback);
  }
  if (scene.fallback != VectorPreviewFallback::None) {
    scene.layers.clear();
  }
  return scene;
}

VectorPreviewResult render_vector_preview(const VectorPreviewScene& scene, const VectorPreviewView& view,
                                         std::uint64_t retained_frame_bytes, const std::atomic_bool* cancelled) {
  VectorPreviewResult result;
  const auto start = std::chrono::steady_clock::now();
  RasterBudget budget{retained_frame_bytes, retained_frame_bytes};
  try {
    if (scene.fallback != VectorPreviewFallback::None) {
      throw scene.fallback;
    }
    if (view.pixels.isEmpty() || !std::isfinite(view.scale) || view.scale <= 0.0 ||
        !std::isfinite(view.offset.x()) || !std::isfinite(view.offset.y())) {
      throw VectorPreviewFallback::Coordinates;
    }
    const auto output_bytes = static_cast<std::uint64_t>(view.pixels.width()) * view.pixels.height() * 4U;
    budget.check(output_bytes);
    budget.retained += output_bytes;
    auto nodes = scene.layers;
    transform_nodes(nodes, view);
    QImage image(view.pixels, QImage::Format_RGBA8888);
    if (image.isNull()) {
      throw VectorPreviewFallback::Memory;
    }
    image.fill(Qt::transparent);
    for (int y = 0; y < view.pixels.height(); y += kVectorPreviewTileSize) {
      for (int x = 0; x < view.pixels.width(); x += kVectorPreviewTileSize) {
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
          return result;
        }
        // All tiles use one coordinate system and the same quantized paths;
        // clipping does not change curve flattening or the antialias phase.
        const Rect tile{x, y, std::min(kVectorPreviewTileSize, view.pixels.width() - x),
                        std::min(kVectorPreviewTileSize, view.pixels.height() - y)};
        const auto base_bytes = budget.retained;
        budget.workspace = 0;
        Document scratch(view.pixels.width(), view.pixels.height(), PixelFormat::rgba8());
        raster_nodes(nodes, scratch.layers(), tile, budget, 0, cancelled);
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
          return result;
        }
        budget.check_workspace(static_cast<std::uint64_t>(tile.width) * tile.height * 64U);
        const auto rendered = qimage_from_document_rect(scratch, QRect(x, y, tile.width, tile.height), true);
        if (rendered.isNull()) {
          throw VectorPreviewFallback::Memory;
        }
        for (int row = 0; row < tile.height; ++row) {
          std::memcpy(image.scanLine(y + row) + x * 4, rendered.constScanLine(row),
                      static_cast<std::size_t>(tile.width) * 4U);
        }
        budget.retained = base_bytes;
      }
    }
    result.image = std::move(image);
  } catch (VectorPreviewFallback reason) {
    result.fallback = reason;
  } catch (const std::bad_alloc&) {
    result.fallback = VectorPreviewFallback::Memory;
  } catch (...) {
    result.fallback = VectorPreviewFallback::Failed;
  }
  result.peak_raster_bytes = budget.peak;
  result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return result;
}

}  // namespace patchy::ui
