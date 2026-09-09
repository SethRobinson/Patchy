#include "core/vector_compound.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/vector_raster.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace patchy {
namespace {
constexpr const char* kCompoundBlock = "pvcl";
const std::vector<std::uint8_t> kCompoundPayload{'P', 'V', 'C', 'L', 0, 0, 0, 1};
constexpr const char* kOpenPathStrokesMetadata = "patchy.psd.openPathStrokes";

void copy_properties(const Layer& from, Layer& to) {
  to.set_visible(from.visible());
  to.set_clipped(from.clipped());
  to.set_opacity(from.opacity());
  to.set_fill_opacity(from.fill_opacity());
  to.set_blend_mode(from.blend_mode());
  to.set_lock_flags(from.lock_flags());
  to.set_bounds(from.bounds());
  to.set_blend_if_payload(from.raw_psd_blending_ranges(), from.blend_if_rgb_compatible());
  if (from.channel_restriction_supported()) { to.set_restricted_channels(from.restricted_channels()); }
  else { to.set_channel_restriction_unsupported(); }
  to.metadata() = from.metadata();
  to.unknown_psd_blocks() = from.unknown_psd_blocks();
  to.layer_style() = from.layer_style();
  if (from.mask()) { to.set_mask(*from.mask()); }
  if (from.vector_mask()) { to.set_vector_mask(*from.vector_mask()); }
}

bool needs_open_path_strokes(const Layer& layer) {
  const auto* shape = layer.vector_shape();
  return layer_is_vector_shape(layer) && vector_lock_reason(layer).empty() && shape->parts.empty() &&
      !shape->path_disabled && shape->stroke.enabled && shape->stroke.width > 0.0 &&
      shape->stroke.content.kind == VectorFillKind::Solid &&
      shape->stroke.alignment == VectorStrokeAlignment::Center && shape->path.subpaths.size() > 1 &&
      std::any_of(shape->path.subpaths.begin(), shape->path.subpaths.end(),
                  [](const auto& path) { return !path.closed; });
}

bool plain_vector_group_child(const Layer& layer) {
  return layer.visible() && layer.opacity() == 1.0F && layer.fill_opacity() == 1.0F &&
      !layer.clipped() && layer.lock_flags() == kLayerLockNone && !layer.mask() && !layer.vector_mask() &&
      layer.layer_style().empty() && !layer.smart_filter_stack() && layer.blend_mode() == BlendMode::Normal &&
      layer.restricted_channels() == 0 && layer.channel_restriction_supported() &&
      !blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_blending_ranges());
}

std::optional<VectorShapeContent> restored_open_path_strokes(const std::vector<Layer>& children) {
  if (children.empty()) { return std::nullopt; }
  const auto* fill = children[0].vector_shape();
  const bool carrier = fill && !fill->stroke.enabled;
  const std::size_t first = carrier ? 1 : 0;
  if (carrier && (!plain_vector_group_child(children[0]) || !layer_is_vector_shape(children[0]) ||
      layer_is_compound_vector(children[0]) || fill->path.subpaths.size() < 2)) { return std::nullopt; }
  if (children.size() <= first) { return std::nullopt; }
  const auto& strokes = children[first];
  const bool grouped = children.size() == first + 1 && strokes.kind() == LayerKind::Group;
  if (grouped && (!strokes.visible() || strokes.clipped() ||
      strokes.fill_opacity() != 1.0F || strokes.lock_flags() != kLayerLockNone || strokes.mask() ||
      strokes.vector_mask() || !strokes.layer_style().empty() || strokes.smart_filter_stack() ||
      strokes.restricted_channels() != 0 || !strokes.channel_restriction_supported() ||
      blend_if_payload_has_non_identity_or_unsupported(strokes.raw_psd_blending_ranges()) ||
      blend_if_payload_has_non_identity_or_unsupported(strokes.raw_psd_group_boundary_blending_ranges()))) {
    return std::nullopt;
  }
  const auto count = grouped ? strokes.children().size() : children.size() - first;
  if (count < 2 || (carrier && count != fill->path.subpaths.size())) { return std::nullopt; }
  VectorShapeContent result;
  if (carrier) { result = *fill; }
  std::optional<VectorStroke> common;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& child = grouped ? strokes.children()[i] : children[i + first];
    if (!plain_vector_group_child(child) || !layer_is_vector_shape(child) || layer_is_compound_vector(child) ||
        !vector_lock_reason(child).empty()) { return std::nullopt; }
    const auto& shape = *child.vector_shape();
    if (shape.path.subpaths.size() != 1 || shape.path_disabled || shape.path_inverted ||
        shape.stroke.fill_enabled || !shape.stroke.enabled || shape.stroke.opacity != 1.0 ||
        shape.stroke.blend_mode != BlendMode::Normal || shape.stroke.content.kind != VectorFillKind::Solid ||
        shape.stroke.alignment != VectorStrokeAlignment::Center) { return std::nullopt; }
    // Photoshop renumbers group indices within each native child on resave.
    // Geometry, closure and order must still match the full fill path exactly.
    const auto& a = shape.path.subpaths[0];
    if (carrier) {
      const auto& b = fill->path.subpaths[i];
      if (a.anchors != b.anchors || a.closed != b.closed) { return std::nullopt; }
    } else {
      if (!shape.origination.empty() || (i != 0 && shape.fill != result.fill)) { return std::nullopt; }
      if (i == 0) { result.fill = shape.fill; }
      result.path.subpaths.push_back(a);
    }
    if (common && *common != shape.stroke) { return std::nullopt; }
    common = shape.stroke;
  }
  result.stroke = *common;
  result.stroke.opacity = grouped ? strokes.opacity() : 1.0;
  result.stroke.blend_mode = grouped ? strokes.blend_mode() : BlendMode::Normal;
  result.stroke.fill_enabled = carrier && fill->stroke.fill_enabled;
  return result;
}
}  // namespace

CompoundVectorGroupKind compound_vector_group_kind(const Layer& layer) {
  if (layer.kind() != LayerKind::Group) { return CompoundVectorGroupKind::None; }
  if (layer.metadata().contains(kOpenPathStrokesMetadata)) { return CompoundVectorGroupKind::OpenPathStrokes; }
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.payload != kCompoundPayload) { continue; }
    if (block.key == kCompoundBlock) { return CompoundVectorGroupKind::Content; }
    if (block.key == "pvfi") { return CompoundVectorGroupKind::FillOpacity; }
  }
  return CompoundVectorGroupKind::None;
}

void set_compound_vector_group_kind(Layer& layer, CompoundVectorGroupKind kind) {
  layer.metadata().erase(kOpenPathStrokesMetadata);
  std::erase_if(layer.unknown_psd_blocks(), [](const auto& block) {
    return block.key == kCompoundBlock || block.key == "pvfi";
  });
  if (kind == CompoundVectorGroupKind::OpenPathStrokes) {
    layer.metadata()[kOpenPathStrokesMetadata] = "1";
  } else if (kind != CompoundVectorGroupKind::None) {
    layer.unknown_psd_blocks().push_back({kind == CompoundVectorGroupKind::Content ? kCompoundBlock : "pvfi", kCompoundPayload});
  }
}

bool layer_is_compound_vector(const Layer& layer) {
  return layer_is_vector_shape(layer) && !layer.vector_shape()->parts.empty();
}

bool document_has_compound_vectors(const Document& document) {
  const auto visit = [](const auto& self, const std::vector<Layer>& layers) -> bool {
    return std::any_of(layers.begin(), layers.end(), [&](const auto& layer) {
      return layer_is_compound_vector(layer) || self(self, layer.children());
    });
  };
  return visit(visit, document.layers());
}

VectorShapeContent vector_shape_part_content(const VectorShapeContent& shape, const VectorShapePart& part) {
  VectorShapeContent result;
  result.fill = part.fill;
  result.stroke = part.stroke;
  result.path_disabled = part.path_disabled;
  result.path_inverted = part.path_inverted;
  const std::set<std::int32_t> groups(part.groups.begin(), part.groups.end());
  for (const auto& path : shape.path.subpaths) {
    if (groups.contains(path.shape_group)) { result.path.subpaths.push_back(path); }
  }
  for (const auto& origin : shape.origination) {
    if (groups.contains(origin.index)) { result.origination.push_back(origin); }
  }
  // Deleting a part's last path must remove its paint, not create a fill layer.
  if (result.path.empty() && !part.whole_canvas && !part.path_disabled) {
    result.fill.kind = VectorFillKind::None;
    result.stroke.enabled = false;
  }
  return result;
}

VectorShapeContent combine_vector_appearances(std::span<const Layer* const> layers) {
  VectorShapeContent result;
  std::int64_t next_group = 0;
  for (const auto* layer : layers) {
    if (layer == nullptr || !layer_is_vector_shape(*layer)) {
      throw std::runtime_error("Expected an editable vector layer");
    }
    const auto& source = *layer->vector_shape();
    std::map<std::int32_t, std::int32_t> groups;
    for (auto path : source.path.subpaths) {
      if (!groups.contains(path.shape_group)) {
        if (next_group >= std::numeric_limits<std::int32_t>::max()) {
          throw std::runtime_error("Too many vector shape groups");
        }
        groups.emplace(path.shape_group, static_cast<std::int32_t>(next_group++));
      }
      path.shape_group = groups.at(path.shape_group);
      result.path.subpaths.push_back(std::move(path));
    }
    for (auto origin : source.origination) {
      const auto found = groups.find(origin.index);
      if (found == groups.end()) { continue; }
      // Unmodeled descriptors contain their old group index. Retain editable
      // curves but drop that live-parameter annotation after regrouping.
      if (origin.kind == LiveShapeKind::Custom && !origin.raw_descriptor.empty()) { continue; }
      origin.index = found->second;
      result.origination.push_back(std::move(origin));
    }
    auto parts = source.parts;
    if (parts.empty()) {
      VectorShapePart part;
      for (const auto& [old_id, new_id] : groups) {
        (void)new_id;
        part.groups.push_back(old_id);
      }
      part.whole_canvas = source.path.empty();
      part.path_disabled = source.path_disabled;
      part.path_inverted = source.path_inverted;
      part.fill = source.fill;
      part.stroke = source.stroke;
      part.pattern_anchor = layer_effects_reference_point(*layer);
      parts.push_back(std::move(part));
    } else if (layer->opacity() != 1.0F || layer->fill_opacity() != 1.0F) {
      // A translucent merged object is an isolation boundary. The planner
      // retains it as a separate layer rather than multiplying its parts.
      throw std::runtime_error("Cannot remove a vector opacity boundary");
    }
    for (auto& part : parts) {
      std::vector<std::int32_t> remapped;
      for (const auto group : part.groups) {
        if (const auto found = groups.find(group); found != groups.end()) { remapped.push_back(found->second); }
      }
      part.groups = std::move(remapped);
      part.opacity *= layer->opacity();
      part.fill_opacity *= layer->fill_opacity();
      result.parts.push_back(std::move(part));
    }
  }
  if (!result.parts.empty()) {
    result.fill = result.parts.front().fill;
    result.stroke = result.parts.front().stroke;
  }
  return result;
}

Layer expand_compound_vector_layer(const Layer& layer) {
  Layer group(layer.id(), layer.name(), LayerKind::Group);
  copy_properties(layer, group);
  strip_layer_vector_data(group);
  if (layer.vector_mask()) { group.set_vector_mask(*layer.vector_mask()); }
  set_compound_vector_group_kind(group, CompoundVectorGroupKind::Content);
  group.set_fill_opacity(1.0F); // Native folders ignore Fill; use an inner opacity boundary.
  const auto& shape = *layer.vector_shape();
  for (const auto& part : shape.parts) {
    Layer child(0, layer.name(), LayerKind::Pixel);
    child.set_bounds(layer.bounds());
    child.set_opacity(part.opacity);
    child.set_fill_opacity(part.fill_opacity);
    set_layer_effects_reference_point(child, part.pattern_anchor[0], part.pattern_anchor[1]);
    child.metadata()[kLayerMetadataVectorShape] = "1";
    child.set_vector_shape(vector_shape_part_content(shape, part));
    mark_layer_vector_block_dirty(child);
    group.add_child(std::move(child));
  }
  if (layer.fill_opacity() != 1.0F) {
    Layer fill(0, layer.name(), LayerKind::Group);
    fill.set_opacity(layer.fill_opacity());
    set_compound_vector_group_kind(fill, CompoundVectorGroupKind::FillOpacity);
    fill.children() = std::move(group.children());
    group.children().clear();
    group.add_child(std::move(fill));
  }
  return group;
}

Document expand_compound_vectors(const Document& document, bool bake) {
  Document result = document;
  const auto visit = [&](const auto& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (layer_is_compound_vector(layer)) {
        layer = expand_compound_vector_layer(layer);
        const auto initialize = [&](const auto& recurse, std::vector<Layer>& children) -> void {
          for (auto& child : children) {
            const auto id = result.allocate_layer_id();
            child = child.clone_with_id(id);
            if (child.kind() == LayerKind::Group) { recurse(recurse, child.children()); }
            else if (bake) {
              update_vector_shape_raster(child, Rect::from_size(result.width(), result.height()),
                                         &std::as_const(result).metadata().patterns);
            }
          }
        };
        initialize(initialize, layer.children());
      } else if (layer.kind() == LayerKind::Group) {
        self(self, layer.children());
      }
    }
  };
  visit(visit, result.layers());
  return result;
}

bool document_has_open_path_strokes(const Document& document) {
  const auto visit = [](const auto& self, const std::vector<Layer>& layers) -> bool {
    return std::any_of(layers.begin(), layers.end(), [&](const auto& layer) {
      return needs_open_path_strokes(layer) || self(self, layer.children());
    });
  };
  return visit(visit, document.layers());
}

Document expand_open_path_strokes(const Document& document) {
  Document result = document;
  const auto visit = [&](const auto& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (!needs_open_path_strokes(std::as_const(layer))) {
        if (layer.kind() == LayerKind::Group) { self(self, layer.children()); }
        continue;
      }
      const auto& source = std::as_const(layer);
      const auto& shape = *source.vector_shape();
      Layer group(source.id(), source.name(), LayerKind::Group);
      copy_properties(source, group);
      strip_layer_vector_data(group);
      if (source.vector_mask()) { group.set_vector_mask(*source.vector_mask()); }
      set_compound_vector_group_kind(group, CompoundVectorGroupKind::OpenPathStrokes);
      group.set_fill_opacity(1.0F);
      const auto make_child = [&](VectorShapeContent content) {
        Layer child(result.allocate_layer_id(), source.name(), PixelBuffer());
        child.set_bounds(source.bounds());
        const auto anchor = layer_effects_reference_point(source);
        set_layer_effects_reference_point(child, anchor[0], anchor[1]);
        child.metadata()[kLayerMetadataVectorShape] = "1";
        child.set_vector_shape(std::move(content));
        mark_layer_vector_block_dirty(child);
        update_vector_shape_raster(child, Rect::from_size(result.width(), result.height()),
                                   &std::as_const(result).metadata().patterns);
        return child;
      };
      const bool fill_carrier = shape.stroke.fill_enabled || shape.path_inverted || !shape.origination.empty();
      if (fill_carrier) {
        auto fill = shape;
        fill.stroke.enabled = false;
        group.add_child(make_child(std::move(fill)));
      }
      Layer strokes(result.allocate_layer_id(), source.name(), LayerKind::Group);
      strokes.set_opacity(static_cast<float>(shape.stroke.opacity));
      strokes.set_blend_mode(shape.stroke.blend_mode);
      // Every native folder costs two layer records. Avoid a redundant opacity
      // boundary so dense line art stays below Photoshop's record limit.
      const bool grouped_strokes = shape.stroke.opacity != 1.0 || shape.stroke.blend_mode != BlendMode::Normal;
      for (const auto& path : shape.path.subpaths) {
        VectorShapeContent stroke;
        stroke.path.subpaths.push_back(path);
        stroke.fill = shape.fill;
        stroke.stroke = shape.stroke;
        stroke.stroke.fill_enabled = false;
        stroke.stroke.opacity = 1.0;
        stroke.stroke.blend_mode = BlendMode::Normal;
        auto child = make_child(std::move(stroke));
        if (grouped_strokes) { strokes.add_child(std::move(child)); }
        else { group.add_child(std::move(child)); }
      }
      if (grouped_strokes) { group.add_child(std::move(strokes)); }
      if (source.fill_opacity() != 1.0F) {
        Layer fill_boundary(result.allocate_layer_id(), source.name(), LayerKind::Group);
        fill_boundary.set_opacity(source.fill_opacity());
        set_compound_vector_group_kind(fill_boundary, CompoundVectorGroupKind::FillOpacity);
        fill_boundary.children() = std::move(group.children());
        group.children().clear();
        group.add_child(std::move(fill_boundary));
      }
      layer = std::move(group);
    }
  };
  visit(visit, result.layers());
  return result;
}

void collapse_compound_vector_groups(Document& document) {
  const auto visit = [&](const auto& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (layer.kind() != LayerKind::Group) { continue; }
      self(self, layer.children());
      const auto& view = std::as_const(layer);
      const auto group_kind = compound_vector_group_kind(view);
      const bool marked = group_kind == CompoundVectorGroupKind::Content ||
                          group_kind == CompoundVectorGroupKind::OpenPathStrokes;
      if (!marked || view.children().empty()) { continue; }
      if (blend_if_payload_has_non_identity_or_unsupported(view.raw_psd_group_boundary_blending_ranges())) { continue; }
      const auto* paints = &view.children();
      float fill_opacity = 1.0F;
      if (paints->size() == 1 && paints->front().kind() == LayerKind::Group) {
        const auto& inner = paints->front();
        const bool fill_boundary = compound_vector_group_kind(inner) == CompoundVectorGroupKind::FillOpacity;
        if (!fill_boundary && group_kind != CompoundVectorGroupKind::OpenPathStrokes) { continue; }
        if (fill_boundary) {
          if (inner.lock_flags() != kLayerLockNone || inner.mask() || inner.vector_mask() || !inner.layer_style().empty() ||
              inner.blend_mode() != BlendMode::Normal || !inner.visible() || inner.clipped() ||
              inner.restricted_channels() != 0 || !inner.channel_restriction_supported() ||
              blend_if_payload_has_non_identity_or_unsupported(inner.raw_psd_blending_ranges())) { continue; }
          fill_opacity = inner.opacity();
          paints = &inner.children();
        }
      }
      auto restored = group_kind == CompoundVectorGroupKind::OpenPathStrokes
                          ? restored_open_path_strokes(*paints) : std::optional<VectorShapeContent>{};
      if (group_kind == CompoundVectorGroupKind::OpenPathStrokes && !restored) { continue; }
      std::vector<const Layer*> children;
      if (!restored) for (const auto& child : *paints) {
        // A foreign editor may have changed the group. Fail open as ordinary
        // native layers rather than discarding unrepresentable new properties.
        if (!layer_is_vector_shape(child) || layer_is_compound_vector(child) || !child.visible() || child.lock_flags() != kLayerLockNone ||
            child.clipped() || child.mask() || child.vector_mask() || !child.layer_style().empty() ||
            child.smart_filter_stack() || child.blend_mode() != BlendMode::Normal ||
            !vector_lock_reason(child).empty() || child.restricted_channels() != 0 ||
            !child.channel_restriction_supported() ||
            blend_if_payload_has_non_identity_or_unsupported(child.raw_psd_blending_ranges())) { break; }
        children.push_back(&child);
      }
      if (!restored && (children.empty() || children.size() != paints->size())) { continue; }
      const bool active_part = document.active_layer_id().has_value() &&
          layer_contains_descendant(view, *document.active_layer_id());
      const auto merged_id = view.id();
      Layer merged(view.id(), view.name(), LayerKind::Pixel);
      copy_properties(view, merged);
      merged.metadata().erase(kOpenPathStrokesMetadata);
      merged.set_fill_opacity(fill_opacity);
      std::erase_if(merged.unknown_psd_blocks(), [](const auto& block) {
        return block.key == kCompoundBlock || block.key == "lsct" || block.key == "lsdk";
      });
      merged.metadata()[kLayerMetadataVectorShape] = "1";
      merged.set_vector_shape(restored ? std::move(*restored) : combine_vector_appearances(children));
      mark_layer_vector_block_dirty(merged);
      update_vector_shape_raster(merged, Rect::from_size(document.width(), document.height()),
                                 &std::as_const(document).metadata().patterns);
      layer = std::move(merged);
      if (active_part) { document.set_active_layer(merged_id); }
    }
  };
  visit(visit, document.layers());
}

void transform_vector_part_appearance(VectorShapeContent& shape, const std::array<double, 6>& matrix,
                                      double stroke_scale) {
  for (auto& part : shape.parts) {
    const auto anchor = part.pattern_anchor;
    part.pattern_anchor = {matrix[0] * anchor[0] + matrix[2] * anchor[1] + matrix[4],
                           matrix[1] * anchor[0] + matrix[3] * anchor[1] + matrix[5]};
    if (stroke_scale > 0.0) { part.stroke.width *= stroke_scale; }
  }
}

void update_vector_part_appearance(VectorShapeContent& shape, const VectorFill& previous_fill,
                                   const VectorStroke& previous_stroke) {
  for (auto& part : shape.parts) {
    if (shape.fill != previous_fill) { part.fill = shape.fill; }
    // A width edit must not recolor every stroke, or replace caps/dashes.
    const auto& before = previous_stroke;
    const auto& after = shape.stroke;
    if (after.content != before.content) { part.stroke.content = after.content; }
    if (after.enabled != before.enabled) { part.stroke.enabled = after.enabled; }
    if (after.fill_enabled != before.fill_enabled) { part.stroke.fill_enabled = after.fill_enabled; }
    if (after.width != before.width) { part.stroke.width = after.width; }
    if (after.dash_offset != before.dash_offset) { part.stroke.dash_offset = after.dash_offset; }
    if (after.miter_limit != before.miter_limit) { part.stroke.miter_limit = after.miter_limit; }
    if (after.cap != before.cap) { part.stroke.cap = after.cap; }
    if (after.join != before.join) { part.stroke.join = after.join; }
    if (after.alignment != before.alignment) { part.stroke.alignment = after.alignment; }
    if (after.scale_lock != before.scale_lock) { part.stroke.scale_lock = after.scale_lock; }
    if (after.stroke_adjust != before.stroke_adjust) { part.stroke.stroke_adjust = after.stroke_adjust; }
    if (after.dashes != before.dashes) { part.stroke.dashes = after.dashes; }
    if (after.blend_mode != before.blend_mode) { part.stroke.blend_mode = after.blend_mode; }
    if (after.opacity != before.opacity) { part.stroke.opacity = after.opacity; }
    if (after.resolution != before.resolution) { part.stroke.resolution = after.resolution; }
  }
}
}  // namespace patchy
