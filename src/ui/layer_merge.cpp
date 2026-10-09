#include "core/vector_compound.hpp"
#include "ui/layer_merge.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_raster.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/vector_preview_renderer.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPointer>
#include <QScopeGuard>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace patchy::ui {
namespace {

class LayerMergeStrings {
  Q_DECLARE_TR_FUNCTIONS(LayerMerge)
};

bool ordinary_appearance(const Layer& layer) {
  return !layer.clipped() && !layer.mask().has_value() && layer.vector_mask() == nullptr &&
         layer.layer_style().empty() && layer.smart_filter_stack() == nullptr &&
         !blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_blending_ranges()) &&
         layer.channel_restriction_supported() && layer.restricted_channels() == 0;
}

bool simple_group(const Layer& layer) {
  // Normal groups isolate their children's blending. Keep that boundary, even
  // when the user allows merging across folders.
  return ordinary_appearance(layer) && layer.blend_mode() == BlendMode::PassThrough &&
         layer.opacity() == 1.0F && layer.fill_opacity() == 1.0F &&
         !blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_group_boundary_blending_ranges());
}

bool contains_locked_descendant(const Layer& group) {
  return std::any_of(group.children().begin(), group.children().end(), [](const Layer& child) {
    return layer_lock_flags(child) != kLayerLockNone || contains_locked_descendant(child);
  });
}

std::optional<VectorPathBounds> geometry_bounds(const Layer& layer) {
  const auto& shape = *layer.vector_shape();
  if (!shape.parts.empty()) {
    std::optional<VectorPathBounds> result;
    for (const auto& part : shape.parts) {
      Layer temporary(0, {}, LayerKind::Pixel);
      temporary.set_vector_shape(vector_shape_part_content(shape, part));
      const auto bounds = geometry_bounds(temporary);
      if (!bounds) { return std::nullopt; }
      if (!result) { result = bounds; }
      else {
        result->left = std::min(result->left, bounds->left);
        result->top = std::min(result->top, bounds->top);
        result->right = std::max(result->right, bounds->right);
        result->bottom = std::max(result->bottom, bounds->bottom);
      }
    }
    return result;
  }
  if (shape.path_disabled || shape.path_inverted ||
      (!shape.path.empty() && shape.path.subpaths.front().op == PathCombineOp::Subtract)) { return std::nullopt; }
  auto bounds = shape.path.bounds();
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  // Includes square caps, miter joins, outside strokes and antialias coverage.
  const double join = shape.stroke.join == VectorStrokeJoin::Miter
      ? std::max(2.0, std::abs(shape.stroke.miter_limit)) : 2.0;
  const double padding = 2.0 + (shape.stroke.enabled ? std::abs(shape.stroke.width) * join : 0.0);
  bounds->left -= padding;
  bounds->top -= padding;
  bounds->right += padding;
  bounds->bottom += padding;
  if (!std::isfinite(bounds->left) || !std::isfinite(bounds->top) ||
      !std::isfinite(bounds->right) || !std::isfinite(bounds->bottom)) {
    return std::nullopt;
  }
  return bounds;
}

bool disjoint(const std::optional<VectorPathBounds>& x, const std::optional<VectorPathBounds>& y) {
  return x.has_value() && y.has_value() &&
         (x->right < y->left || y->right < x->left || x->bottom < y->top || y->bottom < x->top);
}

unsigned vector_paint_type(const VectorShapeContent& shape) {
  const auto classify = [](const VectorFill& fill, const VectorStroke& stroke) {
    unsigned type = 0;
    if (stroke.fill_enabled && fill.kind != VectorFillKind::None) { type |= 1U << static_cast<unsigned>(fill.kind); }
    if (stroke.enabled && stroke.content.kind != VectorFillKind::None) { type |= 1U << static_cast<unsigned>(stroke.content.kind); }
    return type;
  };
  if (shape.parts.empty()) { return classify(shape.fill, shape.stroke); }
  unsigned type = 0;
  for (const auto& part : shape.parts) { type |= classify(part.fill, part.stroke); }
  return type;
}

class Planner {
public:
  Planner(const Document& document, const std::vector<LayerId>& ids, LayerMergeOptions options, bool copy)
      : document_(document), selected_(ids.begin(), ids.end()), options_(options), copy_(copy) {
    const auto index = [&](const auto& self, const std::vector<Layer>& layers) -> void {
      for (const auto& layer : layers) {
        sources_.emplace(layer.id(), &layer);
        if (layer.vector_shape() != nullptr) {
          bounds_.emplace(layer.id(), geometry_bounds(layer));
        }
        self(self, layer.children());
      }
    };
    index(index, document.layers());
  }

  LayerMergePlan run() {
    plan_.roots = visit(document_.layers(), false, false);
    summarize(plan_.roots);
    return std::move(plan_);
  }

private:
  const Layer& source(LayerId id) const { return *sources_.at(id); }

  bool can_join(const LayerMergeNode& base, const LayerMergeNode& front) const {
    if (!base.mergeable || !front.mergeable || base.vector != front.vector) {
      return false;
    }
    if (!base.vector) {
      return true;
    }
    const auto type = vector_paint_type(*source(base.sources.front()).vector_shape());
    return !options_.separate_vector_types ||
        std::all_of(front.sources.begin(), front.sources.end(), [&](LayerId id) {
          return vector_paint_type(*source(id).vector_shape()) == type;
        });
  }

  std::vector<LayerMergeNode> visit(const std::vector<Layer>& layers, bool all, bool inherited_lock) {
    std::vector<LayerMergeNode> result;
    const auto append = [&](LayerMergeNode node) {
      for (auto it = result.rbegin(); it != result.rend(); ++it) {
        if (can_join(*it, node)) {
          auto& base = *it;
          base.sources.insert(base.sources.end(), node.sources.begin(), node.sources.end());
          base.rasterize = !base.vector;
          base.changed = true;
          ++plan_.removed_layers;
          plan_.changed = true;
          return;
        }
        // Collect matching types through other selected vector runs only
        // when the moved artwork cannot touch anything it crosses.
        if (!it->selected || !it->mergeable || !it->vector || !node.vector ||
            std::any_of(it->sources.begin(), it->sources.end(), [&](LayerId below) {
              return std::any_of(node.sources.begin(), node.sources.end(), [&](LayerId above) {
                return !disjoint(bounds_.at(below), bounds_.at(above));
              });
            })) {
          break;
        }
      }
      result.push_back(std::move(node));
    };
    for (std::size_t i = 0; i < layers.size(); ++i) {
      const auto& layer = layers[i];
      const bool selected = all || selected_.contains(layer.id());
      const auto locks = layer_lock_flags(layer);
      const bool locked = inherited_lock ||
          ((!options_.keep_vectors && layer.kind() != LayerKind::Group)
               ? (locks & (kLayerLockImagePixels | kLayerLockTransparentPixels)) != 0
               : locks != kLayerLockNone) ||
          (!options_.keep_vectors && !options_.within_groups && contains_locked_descendant(layer));
      // A base and every member of its clipping stack stay together. Moving a
      // boundary would change which alpha the compositor uses for clipping.
      const bool clipping = layer.clipped() || (i + 1 < layers.size() && layers[i + 1].clipped());
      LayerMergeNode node;
      node.sources = {layer.id()};
      node.selected = selected;
      if (layer.kind() == LayerKind::Group &&
          (!selected || options_.keep_vectors || options_.within_groups || locked || clipping || !layer.visible())) {
        const auto removed_before = plan_.removed_layers;
        node.children = visit(layer.children(), selected, locked || clipping || !layer.visible());
        node.rebuild_group = true;
        node.changed = removed_before != plan_.removed_layers ||
            std::any_of(node.children.begin(), node.children.end(), [](const auto& child) { return child.changed; });
        if (selected && !options_.within_groups && !locked && !clipping && layer.visible() &&
            !node.children.empty() && simple_group(layer)) {
          for (auto& child : node.children) {
            append(std::move(child));
          }
          ++plan_.removed_layers;
          plan_.changed = true;
        } else {
          append(std::move(node));
        }
        continue;
      }
      node.vector = options_.keep_vectors && layer_is_vector_shape(layer);
      const bool common = selected && !locked && layer.visible() && !clipping &&
                          ordinary_appearance(layer) && layer.blend_mode() == BlendMode::Normal &&
                          vector_lock_reason(layer).empty();
      if (node.vector) {
        node.mergeable = common && (!layer_is_compound_vector(layer) ||
            (layer.opacity() == 1.0F && layer.fill_opacity() == 1.0F));
      } else if (options_.keep_vectors) {
        node.mergeable = common && layer.kind() == LayerKind::Pixel && !layer_pixels_are_procedural(layer) &&
                         !layer_has_vector_shape_marker(layer) && layer.vector_shape() == nullptr;
      } else {
        node.mergeable = selected && !locked && layer.visible() && !clipping;
        node.rasterize = node.mergeable && (layer.kind() == LayerKind::Group ||
            (copy_ && ordinary_appearance(layer) && layer.blend_mode() == BlendMode::Normal));
        node.changed = node.rasterize;
        if (node.rasterize) {
          plan_.removed_layers += layer_descendant_count(layer);
        }
        plan_.changed = plan_.changed || node.rasterize;
      }
      append(std::move(node));
    }
    return result;
  }

  void summarize(const std::vector<LayerMergeNode>& nodes) {
    for (const auto& node : nodes) {
      if (node.rebuild_group) {
        summarize(node.children);
      } else if (node.selected) {
        plan_.result_ids.push_back(node.sources.front());
        const auto& layer = source(node.sources.front());
        if (!node.rasterize && (layer_has_vector_shape_marker(layer) || layer.vector_shape() != nullptr ||
                               !vector_lock_reason(layer).empty())) {
          ++plan_.vector_layers;
        } else if (node.rasterize || (layer.kind() == LayerKind::Pixel && !layer_pixels_are_procedural(layer))) {
          ++plan_.bitmap_layers;
        } else {
          ++plan_.kept_layers;
        }
      }
    }
  }

  const Document& document_;
  std::set<LayerId> selected_;
  LayerMergeOptions options_;
  bool copy_{false};
  std::map<LayerId, const Layer*> sources_;
  std::map<LayerId, std::optional<VectorPathBounds>> bounds_;
  LayerMergePlan plan_;
};

LayerMergePlan plan_single_vector_merge(const Document& document, const std::vector<LayerId>& ids,
                                       const LayerMergeOptions& options) {
  LayerMergePlan plan;
  const std::set<LayerId> selected(ids.begin(), ids.end());
  struct Candidate { LayerId id; std::vector<LayerId> ancestors; std::size_t order; };
  std::vector<Candidate> candidates;
  std::map<LayerId, const Layer*> sources;
  std::set<LayerId> clipping_boundaries;
  std::set<std::pair<LayerId, LayerMergeBlocker>> reported;
  std::size_t order = 0;
  const auto issue = [&](LayerId id, LayerMergeBlocker reason) {
    if (reported.emplace(id, reason).second) { plan.blockers.push_back({id, reason}); }
  };
  const auto gather = [&](const auto& self, const std::vector<Layer>& layers,
                          std::vector<LayerId> ancestors, bool all, bool locked, bool hidden) -> void {
    for (std::size_t i = 0; i < layers.size(); ++i) {
      const auto& layer = layers[i];
      sources.emplace(layer.id(), &layer);
      const bool chosen = all || selected.contains(layer.id());
      const bool layer_locked = locked || layer_lock_flags(layer) != kLayerLockNone;
      const bool layer_hidden = hidden || !layer.visible();
      const bool clipping = layer.clipped() || (i + 1 < layers.size() && layers[i + 1].clipped());
      if (clipping) { clipping_boundaries.insert(layer.id()); }
      if (layer.kind() == LayerKind::Group) {
        auto next = ancestors;
        next.push_back(layer.id());
        self(self, layer.children(), std::move(next), chosen, layer_locked, layer_hidden);
        if (chosen && layer_locked) { issue(layer.id(), LayerMergeBlocker::Locked); }
        continue;
      }
      const auto position = order++;
      if (!chosen) { continue; }
      const auto* shape = layer.vector_shape();
      if (layer_locked) { issue(layer.id(), LayerMergeBlocker::Locked); }
      else if (layer_hidden) { issue(layer.id(), LayerMergeBlocker::Hidden); }
      else if (!vector_lock_reason(layer).empty()) { issue(layer.id(), LayerMergeBlocker::UnsupportedVector); }
      else if (!layer_is_vector_shape(layer)) { issue(layer.id(), LayerMergeBlocker::NotVector); }
      else if (clipping) { issue(layer.id(), LayerMergeBlocker::Clipping); }
      else if (layer.mask() || layer.vector_mask()) { issue(layer.id(), LayerMergeBlocker::Mask); }
      else if (layer.smart_filter_stack()) { issue(layer.id(), LayerMergeBlocker::Filters); }
      else if (layer.blend_mode() != BlendMode::Normal ||
               blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_blending_ranges()) ||
               !layer.channel_restriction_supported() || layer.restricted_channels() != 0) {
        issue(layer.id(), LayerMergeBlocker::Blending);
      } else if (layer_is_compound_vector(layer) && (layer.opacity() != 1.0F || layer.fill_opacity() != 1.0F)) {
        issue(layer.id(), LayerMergeBlocker::OpacityBoundary);
      } else if (shape->feather != 0.0 || shape->density != 255) {
        issue(layer.id(), LayerMergeBlocker::VectorEdges);
      }
      if (layer_is_vector_shape(layer)) { candidates.push_back({layer.id(), ancestors, position}); }
    }
  };
  gather(gather, document.layers(), {}, false, false, false);
  if (candidates.size() < 2) { issue(0, LayerMergeBlocker::TooFewVectors); }
  if (options.effects_source && std::none_of(candidates.begin(), candidates.end(), [&](const auto& item) {
        return item.id == *options.effects_source;
      })) { issue(*options.effects_source, LayerMergeBlocker::InvalidEffectsSource); }
  if (!candidates.empty()) {
    // An enclosing group's appearance remains valid when every merged shape
    // stays inside it. Only boundaries actually crossed by the move must be plain.
    auto common = candidates.front().ancestors;
    for (const auto& candidate : candidates) {
      std::size_t count = 0;
      while (count < common.size() && count < candidate.ancestors.size() &&
             common[count] == candidate.ancestors[count]) { ++count; }
      common.resize(count);
    }
    for (const auto& candidate : candidates) {
      for (std::size_t i = common.size(); i < candidate.ancestors.size(); ++i) {
        const auto id = candidate.ancestors[i];
        if (!simple_group(*sources.at(id)) || clipping_boundaries.contains(id)) {
          issue(id, LayerMergeBlocker::GroupBoundary);
        }
      }
    }
  }
  if (!plan.blockers.empty()) { return plan; }
  std::vector<LayerId> merged_ids;
  for (const auto& candidate : candidates) { merged_ids.push_back(candidate.id); }
  const std::set<LayerId> merged(merged_ids.begin(), merged_ids.end());
  const auto bottom = merged_ids.front();
  plan.removed_layers = merged_ids.size() - 1;
  plan.changes_stacking_order = candidates.back().order - candidates.front().order + 1 != candidates.size() ||
      std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.ancestors != candidates.front().ancestors;
      });
  const auto build = [&](const auto& self, const std::vector<Layer>& layers, bool all) -> std::vector<LayerMergeNode> {
    std::vector<LayerMergeNode> nodes;
    for (const auto& layer : layers) {
      const bool chosen = all || selected.contains(layer.id());
      if (merged.contains(layer.id()) && layer.id() != bottom) { continue; }
      LayerMergeNode node;
      node.sources = {layer.id()};
      if (layer.kind() == LayerKind::Group) {
        node.children = self(self, layer.children(), chosen);
        if (chosen && !layer.children().empty() && simple_group(layer) &&
            !clipping_boundaries.contains(layer.id())) {
          for (auto& child : node.children) { nodes.push_back(std::move(child)); }
          ++plan.removed_layers;
          continue;
        }
        node.rebuild_group = true;
        node.changed = node.children.size() != layer.children().size() ||
            std::any_of(node.children.begin(), node.children.end(), [](const auto& child) { return child.changed; });
      } else if (layer.id() == bottom) {
        node.sources = merged_ids;
        node.vector = node.selected = node.changed = node.replace_effects = true;
        node.effects_source = options.effects_source;
      }
      nodes.push_back(std::move(node));
    }
    return nodes;
  };
  plan.roots = build(build, document.layers(), false);
  plan.result_ids = {bottom};
  plan.vector_layers = 1;
  plan.changed = true;
  return plan;
}

}  // namespace

QStringList layer_merge_blocker_messages(const Document& document, const LayerMergePlan& plan) {
  QStringList result;
  for (const auto& blocker : plan.blockers) {
    QString reason;
    switch (blocker.reason) {
      case LayerMergeBlocker::TooFewVectors: reason = LayerMergeStrings::tr("Select at least two editable vector layers."); break;
      case LayerMergeBlocker::NotVector: reason = LayerMergeStrings::tr("This layer is not an editable vector layer."); break;
      case LayerMergeBlocker::Locked: reason = LayerMergeStrings::tr("Unlock this layer and its parent groups first."); break;
      case LayerMergeBlocker::Hidden: reason = LayerMergeStrings::tr("Show this layer and its parent groups first."); break;
      case LayerMergeBlocker::Clipping: reason = LayerMergeStrings::tr("A clipping relationship requires this layer to stay separate."); break;
      case LayerMergeBlocker::Mask: reason = LayerMergeStrings::tr("A separate mask prevents this vector merge."); break;
      case LayerMergeBlocker::Filters: reason = LayerMergeStrings::tr("Smart Filters require this layer to stay separate."); break;
      case LayerMergeBlocker::Blending: reason = LayerMergeStrings::tr("The blend mode, Blend If, or channel settings require a separate layer."); break;
      case LayerMergeBlocker::UnsupportedVector: reason = LayerMergeStrings::tr("Preserved vector data cannot be edited."); break;
      case LayerMergeBlocker::OpacityBoundary: reason = LayerMergeStrings::tr("This compound vector's opacity requires it to stay separate."); break;
      case LayerMergeBlocker::GroupBoundary: reason = LayerMergeStrings::tr("This group's appearance prevents moving shapes across its boundary."); break;
      case LayerMergeBlocker::InvalidEffectsSource: reason = LayerMergeStrings::tr("Choose effects from a vector layer included in the merge."); break;
      case LayerMergeBlocker::VectorEdges: reason = LayerMergeStrings::tr("Vector feather or density requires this shape to stay separate."); break;
    }
    const auto* layer = document.find_layer(blocker.layer_id);
    result.push_back(layer ? LayerMergeStrings::tr("%1: %2").arg(QString::fromStdString(layer->name()), reason) : reason);
  }
  return result;
}

bool merge_selection_contains_vectors(const Document& document, const std::vector<LayerId>& ids) {
  const auto contains = [](const auto& self, const Layer& layer) -> bool {
    return layer_has_vector_shape_marker(layer) || layer.vector_shape() != nullptr || !vector_lock_reason(layer).empty() ||
           std::any_of(layer.children().begin(), layer.children().end(), [&](const auto& child) { return self(self, child); });
  };
  return std::any_of(ids.begin(), ids.end(), [&](LayerId id) {
    const auto* layer = document.find_layer(id);
    return layer != nullptr && contains(contains, *layer);
  });
}

LayerMergePlan plan_layer_merge(const Document& document, const std::vector<LayerId>& ids, LayerMergeOptions options, bool copy) {
  if (options.single_vector) { return plan_single_vector_merge(document, ids, options); }
  if (copy && !options.keep_vectors && !options.within_groups && !document.layers().empty()) {
    // Copy mode supplies the complete visible tree. Flattening that complete
    // stack preserves backdrop-dependent blending and clipping relationships.
    LayerMergePlan plan;
    LayerMergeNode node;
    for (const auto& layer : document.layers()) {
      node.sources.push_back(layer.id());
      plan.removed_layers += 1 + layer_descendant_count(layer);
    }
    --plan.removed_layers;
    node.selected = node.rasterize = node.changed = true;
    plan.result_ids = {node.sources.front()};
    plan.roots.push_back(std::move(node));
    plan.bitmap_layers = 1;
    plan.changed = true;
    return plan;
  }
  return Planner(document, ids, options, copy).run();
}

Document visible_document_for_merge_copy(const Document& document) {
  Document result = document;
  const auto visible = [&](const auto& self, const std::vector<Layer>& siblings) -> std::vector<Layer> {
    std::vector<Layer> copies;
    bool base_visible = true;
    for (const auto& layer : siblings) {
      if (!layer.clipped()) { base_visible = layer.visible(); }
      if (!layer.visible() || (layer.clipped() && !base_visible)) { continue; }
      auto copy = layer;
      // A copy can merge locked sources without modifying those originals.
      copy.set_lock_flags(kLayerLockNone);
      if (layer.kind() == LayerKind::Group) { copy.children() = self(self, layer.children()); }
      copies.push_back(std::move(copy));
    }
    return copies;
  };
  result.layers() = visible(visible, document.layers());
  result.clear_active_layer();
  return result;
}

Document render_layer_merge(const Document& document, const LayerMergePlan& plan,
                           const std::function<std::optional<Layer>(const Layer&)>& raster_source) {
  if (!plan.blockers.empty()) { throw std::runtime_error("Cannot merge protected vector layers"); }
  const auto render = [&](const auto& self, const std::vector<LayerMergeNode>& nodes) -> std::vector<Layer> {
    std::vector<Layer> layers;
    layers.reserve(nodes.size());
    for (const auto& node : nodes) {
      const auto& base = *document.find_layer(node.sources.front());
      Layer output = base;
      if (node.rebuild_group && node.changed) {
        output.children() = self(self, node.children);
      } else if (node.vector && node.sources.size() > 1) {
        std::vector<const Layer*> parts;
        parts.reserve(node.sources.size());
        for (const auto id : node.sources) { parts.push_back(document.find_layer(id)); }
        output.set_vector_shape(combine_vector_appearances(parts));
        output.set_opacity(1.0F);
        output.set_fill_opacity(1.0F);
        if (node.replace_effects) {
          output.layer_style() = {};
          clear_layer_psd_style_source(output);
          if (node.effects_source) {
            const auto& from = *document.find_layer(*node.effects_source);
            output.layer_style() = from.layer_style();
            // Copy complete native effect descriptors as well as modeled effects.
            // They now apply once to the combined silhouette, retaining unknown settings.
            for (const auto& block : from.unknown_psd_blocks()) {
              if (block.key == "lfx2" || block.key == "lrFX" || block.key == "plFX" || block.key == "lmfx") {
                output.unknown_psd_blocks().push_back(block);
              }
            }
            const auto anchor = layer_effects_reference_point(from);
            set_layer_effects_reference_point(output, anchor[0], anchor[1]);
          }
        }
        mark_layer_vector_block_dirty(output);
        update_vector_shape_raster(output, Rect::from_size(document.width(), document.height()),
                                   &document.metadata().patterns);
      } else if (node.rasterize) {
        Document scratch(document.width(), document.height(), document.format());
        scratch.metadata().patterns = document.metadata().patterns;
        // 16/32-bit documents merge at their depth (docs/high-bit-depth.md).
        scratch.color_state().bit_depth = document.color_state().bit_depth;
        Rect bounds;
        for (const auto id : node.sources) {
          const auto& source = *document.find_layer(id);
          auto copy = raster_source ? raster_source(source) : std::optional<Layer>(source);
          if (!copy.has_value()) {
            throw std::runtime_error("Layer has no renderable pixels");
          }
          bounds = unite_rect(bounds, layer_render_bounds(*copy));
          scratch.add_layer(std::move(*copy));
        }
        bounds = intersect_rect(bounds, Rect::from_size(document.width(), document.height()));
        PixelBuffer pixels;
        if (!bounds.empty()) {
          const ScopedDocumentDepthRender depth_render;
          const auto image = qimage_from_document_rect(scratch, QRect(bounds.x, bounds.y, bounds.width, bounds.height), true);
          if (image.isNull()) {
            throw std::runtime_error("Could not render merged pixels");
          }
          pixels = pixels_from_image_at_depth(image, document.color_state().bit_depth);
        }
        output = Layer(base.id(), base.name(), std::move(pixels));
        output.set_bounds(bounds);
      }
      layers.push_back(std::move(output));
    }
    return layers;
  };
  auto layers = render(render, plan.roots);
  Document result = document;
  result.layers() = std::move(layers);
  if (!plan.result_ids.empty()) {
    result.set_active_layer(plan.result_ids.front());
  } else if (document.active_layer_id().has_value() &&
             std::as_const(result).find_layer(*document.active_layer_id()) == nullptr) {
    result.clear_active_layer();
  }
  return result;
}

Document render_layer_merge_with_processing(
    CanvasWidget* canvas, const Document& document, const LayerMergePlan& plan,
    const std::function<std::optional<Layer>(const Layer&)>& raster_source) {
  const QPointer<CanvasWidget> target(canvas);
  const Document snapshot = document;
  if (target) { target->begin_processing_operation(LayerMergeStrings::tr("Merging layers...")); }
  const auto finish = qScopeGuard([target] { if (target) { target->end_processing_operation(); } });
  // Prepare text/font-dependent sources on the UI thread before launching the
  // independent raster work. The worker only sees immutable snapshots.
  std::map<LayerId, Layer> prepared_sources;
  const auto prepare = [&](const auto& self, const std::vector<LayerMergeNode>& nodes) -> void {
    for (const auto& node : nodes) {
      if (node.rasterize && raster_source) {
        for (const auto id : node.sources) {
          auto source = raster_source(*snapshot.find_layer(id));
          if (!source) { throw std::runtime_error("Layer has no renderable pixels"); }
          prepared_sources.emplace(id, std::move(*source));
          if (target) { target->tick_processing_operation(); }
        }
      }
      self(self, node.children);
    }
  };
  prepare(prepare, plan.roots);
  if (target) { target->tick_processing_operation(); }
  auto future = launch_async([source = snapshot, plan, prepared_sources = std::move(prepared_sources)] {
    return render_layer_merge(source, plan, [&](const Layer& layer) -> std::optional<Layer> {
      const auto found = prepared_sources.find(layer.id());
      return found == prepared_sources.end() ? layer : found->second;
    });
  });
  if (target) {
    target->wait_for_processing_operation([&future] {
      return future.wait_for(std::chrono::milliseconds(16)) == std::future_status::ready;
    });
  }
  return future.get();
}

std::optional<LayerMergeOptions> show_layer_merge_dialog(QWidget* parent, const Document& document,
                                                        const std::vector<LayerId>& ids, bool copy,
                                                        std::optional<Document>* prepared_result) {
  if (prepared_result) { prepared_result->reset(); }
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("mergeLayersDialog"));
  dialog.setWindowTitle(copy ? LayerMergeStrings::tr("Merge Visible to New Layer (Copy)") : LayerMergeStrings::tr("Merge Layers"));
  auto* outer = new QVBoxLayout(&dialog);
  auto* body = new QHBoxLayout();
  outer->addLayout(body);
  auto* controls = new QWidget(&dialog);
  // Leave the dialog minimum to its layout so showing the preview expands
  // the window instead of squeezing two columns below their minimum widths.
  controls->setMinimumWidth(420);
  auto* layout = new QVBoxLayout(controls);
  layout->setContentsMargins(0, 0, 0, 0);
  body->addWidget(controls);
  auto* intro = new QLabel(copy ? LayerMergeStrings::tr("Choose how to merge a copy of the visible layers.")
                               : LayerMergeStrings::tr("Choose how to merge the selected layers and their groups."), &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto* vectors = new QCheckBox(LayerMergeStrings::tr("Keep vector layers editable"), &dialog);
  vectors->setObjectName(QStringLiteral("mergeKeepVectorsCheck"));
  vectors->setChecked(true);
  vectors->setToolTip(LayerMergeStrings::tr("Keep editable shapes. Turn off to merge the artwork into bitmap layers."));
  layout->addWidget(vectors);
  auto* single = new QCheckBox(LayerMergeStrings::tr("Merge into one vector layer"), &dialog);
  single->setObjectName(QStringLiteral("mergeSingleVectorCheck"));
  layout->addWidget(single);
  auto* effects_page = new QWidget(&dialog);
  auto* effects_form = new QFormLayout(effects_page);
  effects_form->setContentsMargins(0, 0, 0, 0);
  auto* effects = new QComboBox(effects_page);
  effects->setObjectName(QStringLiteral("mergeVectorEffectsCombo"));
  effects->addItem(LayerMergeStrings::tr("Remove layer effects"));
  effects->addItem(LayerMergeStrings::tr("Use effects from a layer"));
  effects_form->addRow(LayerMergeStrings::tr("Layer effects:"), effects);
  auto* effects_source = new QComboBox(effects_page);
  effects_source->setObjectName(QStringLiteral("mergeEffectsSourceCombo"));
  effects_source->setMinimumContentsLength(18);
  effects_source->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  effects_form->addRow(LayerMergeStrings::tr("Use effects from:"), effects_source);
  layout->addWidget(effects_page);
  auto* groups = new QCheckBox(LayerMergeStrings::tr("Merge within each group separately"), &dialog);
  groups->setObjectName(QStringLiteral("mergeWithinGroupsCheck"));
  groups->setChecked(false);
  groups->setToolTip(LayerMergeStrings::tr("Keep folders and merge their contents separately. Turn off to merge across ordinary Pass Through groups."));
  layout->addWidget(groups);
  auto* types = new QCheckBox(LayerMergeStrings::tr("Separate merges for different vector types"), &dialog);
  types->setObjectName(QStringLiteral("mergeSeparateVectorTypesCheck"));
  types->setChecked(true);
  types->setToolTip(LayerMergeStrings::tr("Merge solid artwork, gradients, and patterns separately. Colors and stroke settings stay intact within each merged vector layer."));
  layout->addWidget(types);
  QCheckBox* hide_originals = nullptr;
  if (copy) {
    hide_originals = new QCheckBox(LayerMergeStrings::tr("Hide original layers"), &dialog);
    hide_originals->setObjectName(QStringLiteral("mergeHideOriginalsCheck"));
    hide_originals->setChecked(true);
    hide_originals->setToolTip(LayerMergeStrings::tr("Keep the originals, but hide them so transparent artwork is not displayed twice."));
    layout->addWidget(hide_originals);
  }
  auto* note = new QLabel(&dialog);
  note->setTextFormat(Qt::PlainText);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("mergeLayersSummaryLabel"));
  summary->setTextFormat(Qt::PlainText);
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* effects_details = new QPlainTextEdit(&dialog);
  effects_details->setObjectName(QStringLiteral("mergeLayersEffectsDetails"));
  effects_details->setReadOnly(true);
  effects_details->setMaximumHeight(8 * effects_details->fontMetrics().height() + 12);
  const auto effects_explanation = LayerMergeStrings::tr("These layers keep their own effects and stay separate in a vector merge. Turning off \"Keep vector layers editable\" rasterizes merged artwork.");
  effects_details->setAccessibleName(effects_explanation);
  layout->addWidget(effects_details);
  std::map<LayerId, QString> styled_layer_names;
  const std::set<LayerId> selected(ids.begin(), ids.end());
  const auto index_styled_layers = [&](const auto& self, const std::vector<Layer>& layers, bool all) -> void {
    for (const auto& layer : layers) {
      const bool chosen = all || selected.contains(layer.id());
      if (!layer.layer_style().empty()) {
        styled_layer_names.emplace(layer.id(), QString::fromStdString(layer.name()));
      }
      if (chosen && layer_is_vector_shape(layer)) {
        effects_source->addItem(QString::fromStdString(layer.name()), QVariant::fromValue<qulonglong>(layer.id()));
      }
      self(self, layer.children(), chosen);
    }
  };
  index_styled_layers(index_styled_layers, document.layers(), false);
  const auto reference = effects_source->findData(QVariant::fromValue<qulonglong>(document.active_layer_id().value_or(0)));
  if (reference >= 0) { effects_source->setCurrentIndex(reference); }
  auto* preview_page = new QWidget(&dialog);
  auto* preview_layout = new QVBoxLayout(preview_page);
  preview_layout->setContentsMargins(0, 0, 0, 0);
  auto* preview_label = new QLabel(preview_page);
  preview_label->setObjectName(QStringLiteral("mergePreviewStatusLabel"));
  preview_label->setWordWrap(true);
  preview_layout->addWidget(preview_label);
  auto* preview = new ZoomableImagePreview(preview_page);
  preview->setObjectName(QStringLiteral("mergeVectorPreview"));
  preview->setMinimumSize(300, 220);
  preview_layout->addWidget(preview, 1);
  auto* preview_check = new QCheckBox(LayerMergeStrings::tr("Preview"), preview_page);
  preview_check->setObjectName(QStringLiteral("mergePreviewCheck"));
  preview_check->setChecked(true);
  preview_layout->addWidget(preview_check);
  body->addWidget(preview_page, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(copy ? LayerMergeStrings::tr("Create Copy") : LayerMergeStrings::tr("Merge"));
  outer->addWidget(buttons);
  const auto options = [&] {
    LayerMergeOptions result{vectors->isChecked(), groups->isChecked(), types->isChecked(),
                             hide_originals == nullptr || hide_originals->isChecked()};
    result.single_vector = single->isChecked();
    if (result.single_vector && effects->currentIndex() == 1) {
      result.effects_source = effects_source->currentData().toULongLong();
    }
    return result;
  };
  struct PreviewResult { std::optional<Document> document; QImage before; QImage after; };
  auto ready = std::make_shared<PreviewResult>();
  auto state = std::make_shared<AsyncPixelPreviewState<LayerMergePlan>>();
  auto source = std::make_shared<const Document>(document);
  const auto close_preview = qScopeGuard([state] { close_async_pixel_preview(state); });
  const auto display_preview = [ready, preview, preview_check, preview_label, source] {
    const auto& image = preview_check->isChecked() ? ready->after : ready->before;
    preview->set_image(image, QSize(source->width(), source->height()));
    preview_label->setText(image.isNull() ? LayerMergeStrings::tr("Preview unavailable.") :
        preview_check->isChecked() ? LayerMergeStrings::tr("Merged artwork") : LayerMergeStrings::tr("Original artwork"));
  };
  state->start = [state, source, ready, display_preview, buttons](const LayerMergePlan& plan) {
    state->in_flight = true;
    const auto generation = ++state->generation;
    auto* app = QCoreApplication::instance();
    const auto original_image = ready->before;
    run_tracked_background_worker([state, source, plan, ready, display_preview, buttons, generation, app, original_image] {
      auto result = std::make_shared<PreviewResult>();
      const auto render_preview = [](const Document& doc) {
        const auto size = QSize(doc.width(), doc.height()).scaled(QSize(400, 320), Qt::KeepAspectRatio)
            .expandedTo(QSize(1, 1));
        const auto scale = std::min(static_cast<double>(size.width()) / doc.width(),
                                    static_cast<double>(size.height()) / doc.height());
        return render_vector_preview(build_vector_preview_scene(doc), {size, scale, {}}).image;
      };
      try {
        result->document = render_layer_merge(*source, plan);
        result->before = original_image.isNull() ? render_preview(*source) : original_image;
        result->after = render_preview(*result->document);
      } catch (...) { result->document.reset(); }
      if (!app) { return; }
      QMetaObject::invokeMethod(app, [state, ready, display_preview, buttons, generation, result] {
        state->in_flight = false;
        if (state->closed) { return; }
        if (!state->pending && generation == state->generation) {
          *ready = std::move(*result);
          display_preview();
          buttons->button(QDialogButtonBox::Ok)->setEnabled(ready->document.has_value());
        }
        if (state->pending && state->start) {
          auto next = std::move(*state->pending);
          state->pending.reset();
          state->start(next);
        }
      }, Qt::QueuedConnection);
    });
  };
  const auto update = [&] {
    const auto choice = options();
    ++state->generation;
    state->pending.reset();
    ready->document.reset();
    ready->after = {};
    vectors->setEnabled(!choice.single_vector);
    single->setEnabled(choice.keep_vectors);
    groups->setEnabled(!choice.single_vector);
    types->setEnabled(choice.keep_vectors && !choice.single_vector);
    effects_page->setVisible(choice.single_vector);
    effects_source->setEnabled(effects->currentIndex() == 1);
    preview_page->setVisible(choice.single_vector);
    const auto plan = plan_layer_merge(document, ids, choice, copy);
    if (choice.single_vector) {
      const auto issues = layer_merge_blocker_messages(document, plan);
      effects_details->setPlainText(issues.join(QChar('\n')));
      effects_details->setAccessibleName(LayerMergeStrings::tr("These layers need to stay separate with the selected options."));
      effects_details->setVisible(!issues.isEmpty());
      note->setText(choice.effects_source
          ? LayerMergeStrings::tr("Effects from %1 apply once to the combined silhouette. Each shape keeps its own fill and vector stroke.").arg(effects_source->currentText())
          : LayerMergeStrings::tr("Individual layer effects will be removed. Each shape keeps its own fill and vector stroke."));
      summary->setText(plan.blockers.empty()
          ? LayerMergeStrings::tr("Result: 1 editable vector layer, replacing %1 in the layer stack.").arg(QString::fromStdString(document.find_layer(plan.result_ids.front())->name())) +
            (plan.changes_stacking_order ? QStringLiteral("\n") + LayerMergeStrings::tr("Stacking relative to unselected layers will change. Review the preview before merging.") : QString())
          : LayerMergeStrings::tr("These layers need to stay separate with the selected options."));
      buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
      preview->set_image({});
      preview_label->setText(plan.blockers.empty() ? LayerMergeStrings::tr("Updating preview...") : QString());
      if (plan.blockers.empty()) { enqueue_async_pixel_preview(state, plan); }
      return;
    }
    note->setText(choice.keep_vectors
        ? LayerMergeStrings::tr("Merged vectors keep their colors, strokes, and paint order. Masks, effects, and blending that need separate layers stay intact.")
        : LayerMergeStrings::tr("Merged artwork becomes pixels. Undo restores the original layers."));
    QStringList styled_layers;
    const auto collect_styled_layers = [&](const auto& self, const std::vector<LayerMergeNode>& nodes) -> void {
      for (const auto& node : nodes) {
        if (node.selected && (node.vector || node.rebuild_group)) {
          if (const auto found = styled_layer_names.find(node.sources.front()); found != styled_layer_names.end()) {
            styled_layers.push_back(found->second);
          }
        }
        self(self, node.children);
      }
    };
    if (choice.keep_vectors) { collect_styled_layers(collect_styled_layers, plan.roots); }
    effects_details->setPlainText(effects_explanation + QStringLiteral("\n\n") + styled_layers.join(QChar('\n')));
    effects_details->setAccessibleName(effects_explanation);
    effects_details->setVisible(!styled_layers.isEmpty());
    summary->setText(LayerMergeStrings::tr("Result: %1 vector layers, %2 bitmap layers, %3 other layers kept.")
        .arg(static_cast<qulonglong>(plan.vector_layers)).arg(static_cast<qulonglong>(plan.bitmap_layers))
        .arg(static_cast<qulonglong>(plan.kept_layers)) + QStringLiteral("\n") +
        (copy ? LayerMergeStrings::tr("The original layers are kept. Multiple outputs are placed in a new group.") :
         plan.changed ? LayerMergeStrings::tr("%1 layers removed by merging.").arg(static_cast<qulonglong>(plan.removed_layers))
                      : LayerMergeStrings::tr("These layers need to stay separate with the selected options.")));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(copy ? !plan.roots.empty() : plan.changed);
  };
  QObject::connect(vectors, &QCheckBox::toggled, &dialog, update);
  QObject::connect(single, &QCheckBox::toggled, &dialog, update);
  QObject::connect(effects, &QComboBox::currentIndexChanged, &dialog, update);
  QObject::connect(effects_source, &QComboBox::currentIndexChanged, &dialog, update);
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, display_preview);
  QObject::connect(groups, &QCheckBox::toggled, &dialog, update);
  QObject::connect(types, &QCheckBox::toggled, &dialog, update);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  update();
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const auto result = options();
  if (result.single_vector) {
    if (!ready->document || state->pending || state->in_flight) { return std::nullopt; }
    if (prepared_result) { *prepared_result = std::move(ready->document); }
  }
  return result;
}

}  // namespace patchy::ui
