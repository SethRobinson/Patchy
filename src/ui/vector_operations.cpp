#include "ui/vector_operations.hpp"
#include "core/path_fit.hpp"
#include "core/vector_raster.hpp"
#include "ui/selection_outline.hpp"
#include <QLineF>
#include <algorithm>
#include <cmath>
namespace patchy::ui {
namespace {
// Materializes the vector-mask coverage cache onto the full canvas (zero
// outside cache_bounds).
PixelBuffer vector_mask_full_coverage(const LayerVectorMask& mask, int width, int height) {
  PixelBuffer coverage(width, height, PixelFormat::gray8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto local_x = x - mask.cache_bounds.x;
      const auto local_y = y - mask.cache_bounds.y;
      std::uint8_t value = 0;
      if (!mask.cache.empty() && local_x >= 0 && local_y >= 0 && local_x < mask.cache.width() &&
          local_y < mask.cache.height()) {
        value = *mask.cache.pixel(local_x, local_y);
      }
      *coverage.pixel(x, y) = value;
    }
  }
  return coverage;
}

}

// Dense document-space polylines for stroking with the brush engine: one per
// subpath, sampled every ~2 px (the engine applies its own stamp spacing
// between input events). Closed subpaths traverse their closing segment; open
// subpaths do NOT gain the fill-only implied chord.
std::vector<std::vector<QPointF>> stroke_polylines_for_path(const VectorPath& path) {
  std::vector<std::vector<QPointF>> polylines;
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.size() < 2) {
      continue;
    }
    std::vector<QPointF> points;
    const auto anchor_count = subpath.anchors.size();
    const auto segment_count = subpath.closed ? anchor_count : anchor_count - 1;
    points.emplace_back(subpath.anchors[0].anchor_x, subpath.anchors[0].anchor_y);
    for (std::size_t i = 0; i < segment_count; ++i) {
      const auto& a = subpath.anchors[i];
      const auto& b = subpath.anchors[(i + 1) % anchor_count];
      const QPointF p0(a.anchor_x, a.anchor_y);
      const QPointF p1(a.out_x, a.out_y);
      const QPointF p2(b.in_x, b.in_y);
      const QPointF p3(b.anchor_x, b.anchor_y);
      const auto hull =
          QLineF(p0, p1).length() + QLineF(p1, p2).length() + QLineF(p2, p3).length();
      const int steps = std::clamp(static_cast<int>(std::lround(hull / 2.0)), 4, 400);
      for (int step = 1; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        const double u = 1.0 - t;
        points.emplace_back(u * u * u * p0.x() + 3.0 * t * u * u * p1.x() +
                                3.0 * t * t * u * p2.x() + t * t * t * p3.x(),
                            u * u * u * p0.y() + 3.0 * t * u * u * p1.y() +
                                3.0 * t * t * u * p2.y() + t * t * t * p3.y());
      }
    }
    polylines.push_back(std::move(points));
  }
  return polylines;
}

// Full-canvas grayscale coverage of a path (no feather).
PixelBuffer path_selection_coverage(const VectorPath& path, int width, int height) {
  VectorRasterOptions options;
  options.clip = Rect::from_size(width, height);
  auto coverage = rasterize_vector_path(path, options);
  PixelBuffer full(width, height, PixelFormat::gray8());
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto local_x = x - coverage.bounds.x;
      const auto local_y = y - coverage.bounds.y;
      std::uint8_t value = 0;
      if (!coverage.pixels.empty() && local_x >= 0 && local_y >= 0 &&
          local_x < coverage.pixels.width() && local_y < coverage.pixels.height()) {
        value = *coverage.pixels.pixel(local_x, local_y);
      }
      *full.pixel(x, y) = value;
    }
  }
  return full;
}

PixelBuffer make_path_selection_coverage(const VectorPath& path, int width, int height,
    double feather, bool antialias, int operation_index, const PixelBuffer* existing) {
  auto coverage = path_selection_coverage(path, width, height);
  if (feather > 0.0) {
    // Triple box blur approximating a gaussian (the vector-mask feather rule:
    // box radius is about half the feather value).
    const auto radius = std::max(1, static_cast<int>(std::lround(feather * 0.5)));
    PixelBuffer scratch(width, height, PixelFormat::gray8());
    for (int pass = 0; pass < 3; ++pass) {
      // Horizontal then vertical box.
      for (int y = 0; y < height; ++y) {
        int sum = 0;
        int count = 0;
        for (int x = -radius; x <= radius; ++x) {
          if (x >= 0 && x < width) {
            sum += *coverage.pixel(x, y);
            ++count;
          }
        }
        for (int x = 0; x < width; ++x) {
          *scratch.pixel(x, y) = static_cast<std::uint8_t>(sum / std::max(1, count));
          const auto add_x = x + radius + 1;
          const auto remove_x = x - radius;
          if (add_x < width) {
            sum += *coverage.pixel(add_x, y);
            ++count;
          }
          if (remove_x >= 0) {
            sum -= *coverage.pixel(remove_x, y);
            --count;
          }
        }
      }
      for (int x = 0; x < width; ++x) {
        int sum = 0;
        int count = 0;
        for (int y = -radius; y <= radius; ++y) {
          if (y >= 0 && y < height) {
            sum += *scratch.pixel(x, y);
            ++count;
          }
        }
        for (int y = 0; y < height; ++y) {
          *coverage.pixel(x, y) = static_cast<std::uint8_t>(sum / std::max(1, count));
          const auto add_y = y + radius + 1;
          const auto remove_y = y - radius;
          if (add_y < height) {
            sum += *scratch.pixel(x, add_y);
            ++count;
          }
          if (remove_y >= 0) {
            sum -= *scratch.pixel(x, remove_y);
            --count;
          }
        }
      }
    }
  }
  if (!antialias) {
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        *value = *value >= 128 ? 255 : 0;
      }
    }
  }
  if (operation_index != 0 && existing != nullptr) {
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        const auto current = *existing->pixel(x, y);
        if (operation_index == 1) {
          *value = std::max(*value, current);
        } else if (operation_index == 2) {
          *value = static_cast<std::uint8_t>((current * (255 - *value)) / 255);
        } else {
          *value = static_cast<std::uint8_t>((current * *value) / 255);
        }
      }
    }
  }
  return coverage;
}
VectorPath fit_selection_vector_path(const QRegion& region, double tolerance) {
  const auto loops = trace_selection_outlines(region);
  VectorPath fitted;
  for (const auto& loop : loops) {
    std::vector<FitPoint> points;
    points.reserve(static_cast<std::size_t>(loop.points.size()));
    for (const auto& point : loop.points) {
      points.push_back(FitPoint{point.x(), point.y()});
    }
    auto subpath = fit_closed_loop(points, tolerance);
    if (subpath.anchors.size() < 2) {
      continue;
    }
    // Outer boundaries (clockwise in y-down coordinates) add coverage, holes
    // (counterclockwise) subtract; the tracer orders outers before their
    // holes, so the sequential combine reproduces the selection.
    subpath.op = loop_signed_area(points) >= 0.0 ? PathCombineOp::Add : PathCombineOp::Subtract;
    subpath.shape_group = static_cast<std::int32_t>(fitted.subpaths.size());
    fitted.subpaths.push_back(std::move(subpath));
  }
  return fitted;
}
void bake_vector_mask(Layer& layer, int width, int height) {
  // A disabled vector mask contributes no coverage. Removing it must also
  // preserve any disabled raster mask so toggling that mask still works.
  if (const auto* mask = std::as_const(layer).vector_mask(); !mask || mask->disabled) {
    layer.clear_vector_mask();
    auto& blocks = layer.unknown_psd_blocks();
    std::erase_if(blocks, [](const UnknownPsdBlock& block) { return block.key == "vmsk" || block.key == "vsms"; });
    mark_layer_vector_block_dirty(layer);
    return;
  }
  auto coverage = vector_mask_full_coverage(*std::as_const(layer).vector_mask(), width, height);
  if (layer.vector_mask()->density != 255) {
    // Bake the density the way the compositor applies it.
    const auto density = static_cast<int>(layer.vector_mask()->density);
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        auto* value = coverage.pixel(x, y);
        *value = static_cast<std::uint8_t>((*value * density) / 255 + (255 - density));
      }
    }
  }
  if (const auto& existing = std::as_const(layer).mask(); existing.has_value() && !existing->disabled) {
    // Both masks multiply in the compositor; the baked result does the same.
    for (int y = 0; y < coverage.height(); ++y) {
      for (int x = 0; x < coverage.width(); ++x) {
        const auto local_x = x - existing->bounds.x;
        const auto local_y = y - existing->bounds.y;
        std::uint8_t raster_value = existing->default_color;
        if (!existing->pixels.empty() && local_x >= 0 && local_y >= 0 &&
            local_x < existing->pixels.width() && local_y < existing->pixels.height()) {
          raster_value = *existing->pixels.pixel(local_x, local_y);
        }
        auto* value = coverage.pixel(x, y);
        *value = static_cast<std::uint8_t>((*value * raster_value) / 255);
      }
    }
  }
  layer.set_mask(LayerMask{Rect::from_size(width, height), std::move(coverage), 255,
                            false});
  layer.clear_vector_mask();
  auto& blocks = layer.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "vmsk" || block.key == "vsms";
  });
  mark_layer_vector_block_dirty(layer);
}
}  // namespace patchy::ui
