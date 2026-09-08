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

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QScopeGuard>
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

LayerMergePlan plan_layer_merge(const Document& document, const std::vector<LayerId>& ids, LayerMergeOptions options, bool copy) {
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
                                                        const std::vector<LayerId>& ids, bool copy) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("mergeLayersDialog"));
  dialog.setWindowTitle(copy ? LayerMergeStrings::tr("Merge Visible to New Layer (Copy)") : LayerMergeStrings::tr("Merge Layers"));
  dialog.setMinimumWidth(440);
  auto* layout = new QVBoxLayout(&dialog);
  auto* intro = new QLabel(copy ? LayerMergeStrings::tr("Choose how to merge a copy of the visible layers.")
                               : LayerMergeStrings::tr("Choose how to merge the selected layers and their groups."), &dialog);
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
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("mergeLayersSummaryLabel"));
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(copy ? LayerMergeStrings::tr("Create Copy") : LayerMergeStrings::tr("Merge"));
  layout->addWidget(buttons);
  const auto options = [&] {
    return LayerMergeOptions{vectors->isChecked(), groups->isChecked(), types->isChecked(),
                             hide_originals == nullptr || hide_originals->isChecked()};
  };
  const auto update = [&] {
    const auto choice = options();
    types->setEnabled(choice.keep_vectors);
    note->setText(choice.keep_vectors
        ? LayerMergeStrings::tr("Merged vectors keep their colors, strokes, and paint order. Masks, effects, and blending that need separate layers stay intact.")
        : LayerMergeStrings::tr("Merged artwork becomes pixels. Undo restores the original layers."));
    const auto plan = plan_layer_merge(document, ids, choice, copy);
    summary->setText(LayerMergeStrings::tr("Result: %1 vector layers, %2 bitmap layers, %3 other layers kept.")
        .arg(static_cast<qulonglong>(plan.vector_layers)).arg(static_cast<qulonglong>(plan.bitmap_layers))
        .arg(static_cast<qulonglong>(plan.kept_layers)) + QStringLiteral("\n") +
        (copy ? LayerMergeStrings::tr("The original layers are kept. Multiple outputs are placed in a new group.") :
         plan.changed ? LayerMergeStrings::tr("%1 layers removed by merging.").arg(static_cast<qulonglong>(plan.removed_layers))
                      : LayerMergeStrings::tr("These layers need to stay separate with the selected options.")));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(copy ? !plan.roots.empty() : plan.changed);
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
