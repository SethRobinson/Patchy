#include "ui/layer_merge.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/rect_utils.hpp"
#include "core/vector_raster.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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

bool appendable_path(const VectorShapeContent& shape) {
  // Opaque Custom descriptors are emitted verbatim by the PSD writer. Their
  // embedded keyOriginIndex cannot follow a remapped shape group, so retain
  // the source layer instead of saving inconsistent live-shape annotations.
  if (std::any_of(shape.origination.begin(), shape.origination.end(), [](const auto& origin) {
        return origin.kind == LiveShapeKind::Custom && !origin.raw_descriptor.empty();
      })) {
    return false;
  }
  if (shape.path.empty() || shape.path_disabled || shape.path_inverted ||
      shape.path.subpaths.front().op == PathCombineOp::Subtract) {
    return false;
  }
  const auto first_group = shape.path.subpaths.front().shape_group;
  // Intersect operates on the entire accumulated path, including earlier
  // layers. Keeping this layer separate preserves its independent operation.
  return std::none_of(shape.path.subpaths.begin(), shape.path.subpaths.end(), [first_group](const auto& path) {
    return path.shape_group != first_group && path.op == PathCombineOp::Intersect;
  });
}

bool opaque_union(const Layer& layer) {
  const auto& shape = *layer.vector_shape();
  return layer.opacity() == 1.0F && layer.fill_opacity() == 1.0F &&
         shape.fill.kind == VectorFillKind::Solid && shape.stroke.fill_enabled && !shape.stroke.enabled &&
         std::all_of(shape.path.subpaths.begin(), shape.path.subpaths.end(), [](const auto& path) {
           return path.op == PathCombineOp::Add;
         });
}

bool contains_locked_descendant(const Layer& group) {
  return std::any_of(group.children().begin(), group.children().end(), [](const Layer& child) {
    return layer_lock_flags(child) != kLayerLockNone || contains_locked_descendant(child);
  });
}

bool matching_paint(const VectorFill& a, const VectorFill& b) {
  if (a.kind != b.kind) { return false; }
  switch (a.kind) {
    case VectorFillKind::None: return true;
    case VectorFillKind::Solid: return a.color == b.color;
    case VectorFillKind::Gradient: return a.gradient == b.gradient && a.gradient_noise_pre_seed == b.gradient_noise_pre_seed;
    case VectorFillKind::Pattern:
      return a.pattern_id == b.pattern_id && a.pattern_scale == b.pattern_scale &&
             a.pattern_angle_degrees == b.pattern_angle_degrees && a.pattern_linked == b.pattern_linked &&
             a.pattern_phase_x == b.pattern_phase_x && a.pattern_phase_y == b.pattern_phase_y;
  }
  return false;
}

bool matching_stroke(const VectorStroke& a, const VectorStroke& b) {
  if (a.enabled != b.enabled) { return false; }
  if (!a.enabled) { return true; }
  auto left = a;
  auto right = b;
  left.content = {}; right.content = {};
  // Fill enablement belongs to the separate shape paint, not the stroke.
  left.fill_enabled = true; right.fill_enabled = true;
  return left == right && matching_paint(a.content, b.content);
}

std::optional<VectorPathBounds> geometry_bounds(const Layer& layer) {
  const auto& shape = *layer.vector_shape();
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

bool stable_paint_anchor(const VectorFill& paint, const Layer& a, const Layer& b) {
  // Aligned gradients use tight coverage bounds, which change after a merge.
  // Keep them independent. Canvas-aligned gradients retain their coordinates.
  if (paint.kind == VectorFillKind::Gradient && paint.gradient.align_with_layer) {
    return false;
  }
  return paint.kind != VectorFillKind::Pattern || !paint.pattern_linked ||
         layer_effects_reference_point(a) == layer_effects_reference_point(b);
}

bool matching_vectors(const Layer& base, const Layer& front, bool separate_types) {
  if (base.opacity() != front.opacity() || base.fill_opacity() != front.fill_opacity()) {
    return false;
  }
  const auto& a = *base.vector_shape();
  const auto& b = *front.vector_shape();
  if (separate_types) {
    const bool fill_a = a.stroke.fill_enabled && a.fill.kind != VectorFillKind::None;
    const bool fill_b = b.stroke.fill_enabled && b.fill.kind != VectorFillKind::None;
    if (fill_a != fill_b || (fill_a && !matching_paint(a.fill, b.fill)) || !matching_stroke(a.stroke, b.stroke)) {
      return false;
    }
  }
  return (!a.stroke.fill_enabled || stable_paint_anchor(a.fill, base, front)) &&
         (!a.stroke.enabled || stable_paint_anchor(a.stroke.content, base, front));
}

class Planner {
public:
  Planner(const Document& document, const std::vector<LayerId>& ids, LayerMergeOptions options)
      : document_(document), selected_(ids.begin(), ids.end()), options_(options) {
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
    const auto& bottom = source(base.sources.front());
    for (const auto front_id : front.sources) {
      const auto& top = source(front_id);
      if (!matching_vectors(bottom, top, options_.separate_vector_types)) {
        return false;
      }
      for (const auto base_id : base.sources) {
        const auto& previous = source(base_id);
        // With one paint, opaque additive fills can share overlapping coverage.
        // Other paths need independent extents to preserve holes, alpha and
        // the original fill/stroke ordering.
        if (!(opaque_union(previous) && opaque_union(top) && opaque_union(bottom)) &&
            !disjoint(bounds_.at(base_id), bounds_.at(front_id))) {
          return false;
        }
      }
    }
    return true;
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
        node.mergeable = common && vector_lock_reason(layer).empty() && appendable_path(*layer.vector_shape());
      } else if (options_.keep_vectors) {
        node.mergeable = common && layer.kind() == LayerKind::Pixel && !layer_pixels_are_procedural(layer) &&
                         !layer_has_vector_shape_marker(layer) && layer.vector_shape() == nullptr;
      } else {
        node.mergeable = selected && !locked && layer.visible() && !clipping;
        node.rasterize = node.mergeable && layer.kind() == LayerKind::Group;
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
  std::map<LayerId, const Layer*> sources_;
  std::map<LayerId, std::optional<VectorPathBounds>> bounds_;
  LayerMergePlan plan_;
};

void append_independent_shape(VectorShapeContent& base, const VectorShapeContent& front) {
  std::int64_t next = 0;
  for (const auto& path : base.path.subpaths) {
    next = std::max(next, static_cast<std::int64_t>(path.shape_group) + 1);
  }
  std::map<std::int32_t, std::int32_t> groups;
  const auto first = front.path.subpaths.front().shape_group;
  for (auto path : front.path.subpaths) {
    if (!groups.contains(path.shape_group)) {
      if (next >= std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("Too many vector shape groups");
      }
      groups.emplace(path.shape_group, static_cast<std::int32_t>(next++));
    }
    if (path.shape_group == first) {
      path.op = PathCombineOp::Add;
    }
    path.shape_group = groups.at(path.shape_group);
    base.path.subpaths.push_back(std::move(path));
  }
  for (auto origin : front.origination) {
    if (const auto group = groups.find(origin.index); group != groups.end()) {
      origin.index = group->second;
      base.origination.push_back(std::move(origin));
    }
  }
}

}  // namespace

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

LayerMergePlan plan_layer_merge(const Document& document, const std::vector<LayerId>& ids, LayerMergeOptions options) {
  return Planner(document, ids, options).run();
}

Document render_layer_merge(const Document& document, const LayerMergePlan& plan,
                           const std::function<std::optional<Layer>(const Layer&)>& raster_source) {
  const auto render = [&](const auto& self, const std::vector<LayerMergeNode>& nodes) -> std::vector<Layer> {
    std::vector<Layer> layers;
    layers.reserve(nodes.size());
    for (const auto& node : nodes) {
      const auto& base = *document.find_layer(node.sources.front());
      Layer output = base;
      if (node.rebuild_group && node.changed) {
        output.children() = self(self, node.children);
      } else if (node.vector && node.sources.size() > 1) {
        auto shape = *base.vector_shape();
        for (std::size_t i = 1; i < node.sources.size(); ++i) {
          append_independent_shape(shape, *document.find_layer(node.sources[i])->vector_shape());
        }
        output.set_vector_shape(std::move(shape));
        mark_layer_vector_block_dirty(output);
        update_vector_shape_raster(output, Rect::from_size(document.width(), document.height()),
                                   &document.metadata().patterns);
      } else if (node.rasterize) {
        Document scratch(document.width(), document.height(), document.format());
        scratch.metadata().patterns = document.metadata().patterns;
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
          const auto image = qimage_from_document_rect(scratch, QRect(bounds.x, bounds.y, bounds.width, bounds.height), true);
          if (image.isNull()) {
            throw std::runtime_error("Could not render merged pixels");
          }
          pixels = pixels_from_image_rgba(image);
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

std::optional<LayerMergeOptions> show_layer_merge_dialog(QWidget* parent, const Document& document,
                                                        const std::vector<LayerId>& ids) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("mergeLayersDialog"));
  dialog.setWindowTitle(LayerMergeStrings::tr("Merge Layers"));
  dialog.setMinimumWidth(440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* intro = new QLabel(LayerMergeStrings::tr("Choose how to merge the selected layers and their groups."), &dialog);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  auto* vectors = new QCheckBox(LayerMergeStrings::tr("Keep vectors and bitmaps separate"), &dialog);
  vectors->setObjectName(QStringLiteral("mergeKeepVectorsCheck"));
  vectors->setChecked(true);
  vectors->setToolTip(LayerMergeStrings::tr("Keep editable shapes. Turn off to merge the artwork into bitmap layers."));
  layout->addWidget(vectors);
  auto* groups = new QCheckBox(LayerMergeStrings::tr("Merge within each group separately"), &dialog);
  groups->setObjectName(QStringLiteral("mergeWithinGroupsCheck"));
  groups->setChecked(false);
  groups->setToolTip(LayerMergeStrings::tr("Keep folders and merge their contents separately. Turn off to merge across ordinary Pass Through groups."));
  layout->addWidget(groups);
  auto* types = new QCheckBox(LayerMergeStrings::tr("Separate merges for different vector types"), &dialog);
  types->setObjectName(QStringLiteral("mergeSeparateVectorTypesCheck"));
  types->setChecked(true);
  types->setToolTip(LayerMergeStrings::tr("Keep different fills, gradients, patterns, and strokes separate. Turn off to use the bottom shape's fill and stroke."));
  layout->addWidget(types);
  auto* note = new QLabel(&dialog);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("mergeLayersSummaryLabel"));
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(LayerMergeStrings::tr("Merge"));
  layout->addWidget(buttons);
  const auto options = [&] { return LayerMergeOptions{vectors->isChecked(), groups->isChecked(), types->isChecked()}; };
  const auto update = [&] {
    const auto choice = options();
    types->setEnabled(choice.keep_vectors);
    note->setText(choice.keep_vectors
        ? LayerMergeStrings::tr("Overlapping artwork keeps its order. Masks, effects, blending, and paint alignment that need separate layers stay intact.") +
          (choice.separate_vector_types ? QString() : QStringLiteral("\n") + LayerMergeStrings::tr("Vector merges use the bottom shape's fill and stroke."))
        : LayerMergeStrings::tr("Merged artwork becomes pixels. Undo restores the original layers."));
    const auto plan = plan_layer_merge(document, ids, choice);
    summary->setText(LayerMergeStrings::tr("Result: %1 vector layers, %2 bitmap layers, %3 other layers kept.")
        .arg(static_cast<qulonglong>(plan.vector_layers)).arg(static_cast<qulonglong>(plan.bitmap_layers))
        .arg(static_cast<qulonglong>(plan.kept_layers)) + QStringLiteral("\n") +
        (plan.changed ? LayerMergeStrings::tr("%1 layers removed by merging.").arg(static_cast<qulonglong>(plan.removed_layers))
                      : LayerMergeStrings::tr("These layers need to stay separate with the selected options.")));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(plan.changed);
  };
  QObject::connect(vectors, &QCheckBox::toggled, &dialog, update);
  QObject::connect(groups, &QCheckBox::toggled, &dialog, update);
  QObject::connect(types, &QCheckBox::toggled, &dialog, update);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  update();
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return options();
}

}  // namespace patchy::ui
