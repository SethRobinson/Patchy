#include "formats/vector_export_plan.hpp"

#include "core/blend_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace patchy::vector_export {
namespace {

PathCombineOp group_op(const VectorPath& group) {
  return group.subpaths.empty() ? PathCombineOp::Add : group.subpaths.front().op;
}

bool bounds_disjoint(const VectorPath& a, const VectorPath& b) {
  const auto bounds_a = a.bounds();
  const auto bounds_b = b.bounds();
  if (!bounds_a.has_value() || !bounds_b.has_value()) {
    return true;
  }
  return bounds_a->right <= bounds_b->left || bounds_b->right <= bounds_a->left ||
         bounds_a->bottom <= bounds_b->top || bounds_b->bottom <= bounds_a->top;
}

bool point_inside_group(const VectorPath& group, double x, double y) {
  const auto bounds = group.bounds();
  return bounds.has_value() && x >= bounds->left && x <= bounds->right && y >= bounds->top && y <= bounds->bottom;
}

}  // namespace

std::vector<VectorPath> split_shape_groups(const VectorPath& path) {
  std::vector<VectorPath> groups;
  std::map<std::int32_t, std::size_t> index_of;
  for (const auto& subpath : path.subpaths) {
    const auto found = index_of.find(subpath.shape_group);
    if (found == index_of.end()) {
      index_of.emplace(subpath.shape_group, groups.size());
      groups.emplace_back();
      groups.back().subpaths.push_back(subpath);
    } else {
      groups[found->second].subpaths.push_back(subpath);
    }
  }
  return groups;
}

CombineExport classify_combine(const VectorPath& path) {
  const auto groups = split_shape_groups(path);
  if (groups.size() <= 1) {
    return CombineExport::SinglePath;  // even-odd within one group is our exact rule
  }
  // Adds followed by Subtracts, holes inside their outlines, disjoint
  // outlines: exactly representable as one even-odd path (the letter-O and
  // decomposed-nonzero shapes).
  std::size_t first_subtract = groups.size();
  bool ordered = true;
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto op = group_op(groups[i]);
    if (op == PathCombineOp::Intersect || op == PathCombineOp::Xor) {
      return CombineExport::Unsupported;
    }
    if (op == PathCombineOp::Subtract) {
      first_subtract = std::min(first_subtract, i);
    } else if (i > first_subtract) {
      ordered = false;  // Add after a Subtract: sequential semantics we cannot fold
    }
  }
  const auto adds_disjoint = [&groups, first_subtract] {
    for (std::size_t i = 0; i < first_subtract; ++i) {
      for (std::size_t j = i + 1; j < first_subtract; ++j) {
        if (!bounds_disjoint(groups[i], groups[j])) {
          return false;
        }
      }
    }
    return true;
  };
  if (ordered && first_subtract < groups.size()) {
    if (!adds_disjoint()) {
      return CombineExport::Unsupported;
    }
    for (std::size_t i = first_subtract; i < groups.size(); ++i) {
      if (groups[i].subpaths.empty() || groups[i].subpaths.front().anchors.empty()) {
        continue;
      }
      const auto& probe = groups[i].subpaths.front().anchors.front();
      bool inside_an_add = false;
      for (std::size_t j = 0; j < first_subtract && !inside_an_add; ++j) {
        inside_an_add = point_inside_group(groups[j], probe.anchor_x, probe.anchor_y);
      }
      if (!inside_an_add) {
        return CombineExport::Unsupported;
      }
    }
    return CombineExport::SinglePath;
  }
  // All Add: disjoint outlines fold into one even-odd path; overlapping
  // unions need separate sibling paths (exact only for opaque paint - the
  // caller checks).
  if (first_subtract == groups.size()) {
    return adds_disjoint() ? CombineExport::SinglePath : CombineExport::SeparatePaths;
  }
  return CombineExport::Unsupported;
}

bool paint_is_opaque(const VectorFill& fill, const PatternStore& patterns) {
  switch (fill.kind) {
    case VectorFillKind::None:
      return true;  // nothing painted, nothing to double-cover
    case VectorFillKind::Solid:
      return true;  // solid alpha rides the layer opacity, uniform over the union
    case VectorFillKind::Gradient:
      return std::all_of(fill.gradient.alpha_stops.begin(), fill.gradient.alpha_stops.end(),
                         [](const GradientAlphaStop& stop) { return stop.opacity >= 0.9999F; });
    case VectorFillKind::Pattern: {
      const auto* resource = patterns.find(fill.pattern_id);
      if (resource == nullptr || resource->tile.empty()) {
        return false;
      }
      const auto data = resource->tile.data();
      for (std::size_t i = 3; i < data.size(); i += 4) {
        if (data[i] != 255) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

bool gradient_type_supported(const VectorFill& fill) {
  if (fill.kind != VectorFillKind::Gradient) {
    return true;
  }
  const auto type = fill.gradient.type;
  return type == LayerStyleGradientType::Linear || type == LayerStyleGradientType::Radial ||
         type == LayerStyleGradientType::Reflected;
}

bool shape_layer_exportable_as_vector(const Layer& layer, const PatternStore& patterns) {
  if (!layer_is_vector_shape(layer) || !vector_lock_reason(layer).empty()) {
    return false;
  }
  if (!layer.layer_style().empty() || std::abs(layer.fill_opacity() - 1.0F) > 0.0001F) {
    return false;
  }
  const auto& shape = *layer.vector_shape();
  if (shape.path_disabled || shape.path_inverted) {
    return false;
  }
  if (!gradient_type_supported(shape.fill) || !gradient_type_supported(shape.stroke.content)) {
    return false;
  }
  if (shape.fill.kind == VectorFillKind::Gradient && shape.fill.gradient.form == GradientDefinitionForm::Noise &&
      shape.fill.gradient.type != LayerStyleGradientType::Linear &&
      shape.fill.gradient.type != LayerStyleGradientType::Radial &&
      shape.fill.gradient.type != LayerStyleGradientType::Reflected) {
    return false;
  }
  if (shape.stroke.enabled && shape.stroke.blend_mode != BlendMode::Normal) {
    return false;
  }
  const auto combine = classify_combine(shape.path);
  if (combine == CombineExport::Unsupported) {
    return false;
  }
  if (combine == CombineExport::SeparatePaths &&
      (!paint_is_opaque(shape.fill, patterns) ||
       (shape.stroke.enabled &&
        (shape.stroke.opacity < 0.9999 || shape.stroke.alignment != VectorStrokeAlignment::Center)))) {
    return false;  // double-painted overlaps would differ from the union
  }
  if (shape.stroke.enabled && shape.stroke.alignment == VectorStrokeAlignment::Outside &&
      (shape.fill.kind == VectorFillKind::None || !paint_is_opaque(shape.fill, patterns) ||
       !shape.stroke.fill_enabled)) {
    return false;  // the under-fill trick needs an opaque fill covering the inner half
  }
  if (const auto* mask = layer.vector_mask();
      mask != nullptr && (mask->disabled || mask->density != 255 || mask->feather > 0.0001)) {
    return false;
  }
  return true;
}

bool group_exportable(const Layer& group) {
  if (!group.layer_style().empty() || std::abs(group.fill_opacity() - 1.0F) > 0.0001F) {
    return false;
  }
  if (const auto* mask = group.vector_mask();
      mask != nullptr && (mask->disabled || mask->density != 255 || mask->feather > 0.0001)) {
    return false;
  }
  if (group.mask().has_value() && group.mask()->disabled) {
    return false;
  }
  return true;
}

std::vector<Unit> build_units(const std::vector<Layer>& siblings) {
  std::vector<Unit> units;
  std::size_t index = 0;
  while (index < siblings.size()) {
    Unit unit{index, index + 1};
    if (!siblings[index].clipped()) {
      while (unit.end < siblings.size() && siblings[unit.end].clipped()) {
        ++unit.end;
      }
    }
    // An orphaned clipped layer at the bottom stays its own unit (the
    // compositor renders it unclipped defensively).
    units.push_back(unit);
    index = unit.end;
  }
  return units;
}

bool unit_is_barrier(const std::vector<Layer>& siblings, const Unit& unit,
                     const std::function<bool(BlendMode)>& blend_expressible) {
  const Layer& base = siblings[unit.begin];
  if (base.kind() == LayerKind::Adjustment) {
    return true;
  }
  if (base.blend_mode() != BlendMode::PassThrough && !blend_expressible(base.blend_mode())) {
    return true;
  }
  if (base.kind() == LayerKind::Group && base.blend_mode() == BlendMode::PassThrough) {
    // A pass-through group does not isolate: a barrier inside it reaches
    // this sibling level, and rasterizing a non-representable pass-through
    // group standalone would bake its children's blending against nothing.
    if (!group_exportable(base)) {
      return true;
    }
    const auto child_units = build_units(base.children());
    for (const auto& child_unit : child_units) {
      if (unit_is_barrier(base.children(), child_unit, blend_expressible)) {
        return true;
      }
    }
  }
  return false;
}

GradientExportGeometry gradient_export_geometry(const LayerStyleGradient& gradient, const VectorPath& path,
                                                std::int32_t document_width, std::int32_t document_height) {
  GradientExportGeometry geometry;
  const auto path_bounds = path.bounds();
  const double left = path_bounds.has_value() ? path_bounds->left : 0.0;
  const double top = path_bounds.has_value() ? path_bounds->top : 0.0;
  const double width =
      std::max(1.0, path_bounds.has_value() ? path_bounds->right - path_bounds->left : document_width);
  const double height =
      std::max(1.0, path_bounds.has_value() ? path_bounds->bottom - path_bounds->top : document_height);
  geometry.center_x = left + width * (0.5 + gradient.offset_x_percent / 100.0);
  geometry.center_y = top + height * (0.5 + gradient.offset_y_percent / 100.0);

  if (gradient.type == LayerStyleGradientType::Radial) {
    geometry.radius = std::max(width, height) / 2.0 * std::max(0.01F, gradient.scale);
    return geometry;
  }
  const double radians = static_cast<double>(gradient.angle_degrees) * std::numbers::pi / 180.0;
  const double direction_x = std::cos(radians);
  const double direction_y = -std::sin(radians);  // screen y grows downward
  const double abs_cos = std::abs(direction_x);
  const double abs_sin = std::abs(direction_y);
  double span = std::min(abs_cos > 1e-9 ? width / abs_cos : std::numeric_limits<double>::infinity(),
                         abs_sin > 1e-9 ? height / abs_sin : std::numeric_limits<double>::infinity());
  span = std::max(1.0, span) * std::max(0.01F, gradient.scale);
  if (gradient.type == LayerStyleGradientType::Reflected) {
    span *= 0.5;  // the import doubles it back: Reflected mirrors the ramp
    geometry.reflected = true;
  }
  geometry.x1 = geometry.center_x - direction_x * span * 0.5;
  geometry.y1 = geometry.center_y - direction_y * span * 0.5;
  geometry.x2 = geometry.center_x + direction_x * span * 0.5;
  geometry.y2 = geometry.center_y + direction_y * span * 0.5;
  return geometry;
}

std::vector<GradientExportStop> gradient_export_stops(const LayerStyleGradient& gradient) {
  std::vector<GradientExportStop> stops;
  // Both targets interpolate stops linearly. Non-identity midpoints, the
  // Classic catmull-rom ease, and noise gradients all need resampling into
  // dense linear stops; plain ramps emit their real stops.
  const bool linear_exact =
      gradient.form == GradientDefinitionForm::Solid &&
      (gradient.interpolation == GradientInterpolationMethod::Linear ||
       (gradient.interpolation == GradientInterpolationMethod::Classic && gradient.smoothness == 0)) &&
      std::all_of(gradient.color_stops.begin(), gradient.color_stops.end(),
                  [](const GradientColorStop& stop) { return std::abs(stop.midpoint - 0.5F) < 0.0001F; }) &&
      std::all_of(gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
                  [](const GradientAlphaStop& stop) { return std::abs(stop.midpoint - 0.5F) < 0.0001F; });
  if (linear_exact) {
    // Merged ascending union of the color and alpha stop locations, each
    // evaluated exactly; reverse maps locations through 1-x.
    std::vector<float> locations;
    for (const auto& stop : gradient.color_stops) {
      locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
    }
    for (const auto& stop : gradient.alpha_stops) {
      locations.push_back(std::clamp(stop.location, 0.0F, 1.0F));
    }
    std::sort(locations.begin(), locations.end());
    locations.erase(std::unique(locations.begin(), locations.end(),
                                [](float a, float b) { return std::abs(a - b) < 0.0001F; }),
                    locations.end());
    if (locations.empty()) {
      locations = {0.0F, 1.0F};
    }
    if (gradient.reverse) {
      std::reverse(locations.begin(), locations.end());  // 1-x below keeps offsets ascending
    }
    for (const auto location : locations) {
      const double offset = gradient.reverse ? 1.0 - location : location;
      stops.push_back({offset, gradient_color(gradient, location, true), gradient_stop_opacity(gradient, location, true)});
    }
    return stops;
  }
  constexpr int kSamples = 64;
  for (int i = 0; i <= kSamples; ++i) {
    const float offset = static_cast<float>(i) / kSamples;
    const float sample_at = gradient.reverse ? 1.0F - offset : offset;
    stops.push_back({offset, gradient_color(gradient, sample_at, true), gradient_stop_opacity(gradient, sample_at, true)});
  }
  return stops;
}

std::optional<Rect> opaque_bounds(const PixelBuffer& pixels) {
  std::int32_t left = pixels.width();
  std::int32_t top = pixels.height();
  std::int32_t right = -1;
  std::int32_t bottom = -1;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    const auto row = pixels.row(y);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left) {
    return std::nullopt;
  }
  return Rect{left, top, right - left + 1, bottom - top + 1};
}

PixelBuffer crop_pixels(const PixelBuffer& pixels, Rect rect) {
  PixelBuffer result(rect.width, rect.height, PixelFormat::rgba8());
  for (std::int32_t y = 0; y < rect.height; ++y) {
    const auto source = pixels.row(rect.y + y);
    std::copy_n(source.data() + static_cast<std::size_t>(rect.x) * 4U, static_cast<std::size_t>(rect.width) * 4U,
                result.row(y).data());
  }
  return result;
}

}  // namespace patchy::vector_export
