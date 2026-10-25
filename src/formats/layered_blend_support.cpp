#include "formats/layered_blend_support.hpp"

#include "core/blend_math.hpp"
#include "core/document_depth.hpp"
#include "core/layer_render_utils.hpp"
#include "core/pixel_depth.hpp"
#include "core/rect_utils.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

// The bake, in one sentence: for a layer whose mode the format lacks, render the composite
// just below it (B) and just with it (R) in the context it composites in, and write a
// Normal layer N with N-over-B = R. Where nothing below it is transparent, N is R itself,
// opaque wherever the layer changed anything; under partial transparency N's alpha and
// colour come from solving the source-over equation. Pixels the layer did not change stay
// transparent in N, so the layers below keep showing (and stay editable) there.
//
// The context: a layer composites onto the running composite of its sibling list. A Pass
// Through folder continues its parent's running composite, so its children render in the
// parent's context. Every other folder isolates: its children start from transparency, so
// their renders use a document made of that folder's children alone, and the baked layer
// rebuilds the folder's own content exactly (whatever sits above it in the folder then sees
// the same pixels). A Pass Through folder with opacity, a mask or a style mixes its result
// with the backdrop, which no child-by-child bake reproduces: it bakes whole, like a folder
// with a mode the format lacks.

namespace patchy {

namespace {

using Path = std::vector<std::size_t>;

[[nodiscard]] bool is_group(const Layer& layer) noexcept {
  return layer.kind() == LayerKind::Group;
}

[[nodiscard]] bool has_visible_non_normal(const std::vector<Layer>& layers) {
  return std::any_of(layers.begin(), layers.end(), [](const Layer& layer) {
    if (!layer.visible()) {
      return false;
    }
    const auto mode = layer.blend_mode();
    if (mode != BlendMode::Normal && mode != BlendMode::PassThrough) {
      return true;
    }
    return is_group(layer) && has_visible_non_normal(layer.children());
  });
}

// A folder whose mode is stored but which the format would still render differently.
[[nodiscard]] bool pass_through_needs_isolation(LayeredSaveFormat format, const Layer& layer, BitDepth depth) {
  return is_group(layer) && layer.blend_mode() == BlendMode::PassThrough &&
         !layered_format_stores_blend_mode(format, BlendMode::PassThrough, true, depth) &&
         has_visible_non_normal(layer.children());
}

[[nodiscard]] bool mode_lost(LayeredSaveFormat format, const Layer& layer, BitDepth depth) {
  if (is_group(layer) && layer.blend_mode() == BlendMode::PassThrough) {
    return false;  // judged by pass_through_needs_isolation
  }
  return !layered_format_stores_blend_mode(format, layer.blend_mode(), is_group(layer), depth);
}

[[nodiscard]] bool affected(LayeredSaveFormat format, const Layer& layer, BitDepth depth) {
  return mode_lost(format, layer, depth) || pass_through_needs_isolation(format, layer, depth);
}

void collect(const std::vector<Layer>& layers, LayeredSaveFormat format, BitDepth depth,
             UnsupportedBlendModes& found) {
  for (const auto& layer : layers) {
    if (affected(format, layer, depth)) {
      found.layer_names.push_back(layer.name());
      if (std::find(found.modes.begin(), found.modes.end(), layer.blend_mode()) == found.modes.end()) {
        found.modes.push_back(layer.blend_mode());
      }
    }
    if (is_group(layer)) {
      collect(layer.children(), format, depth, found);
    }
  }
}

[[nodiscard]] bool subtree_needs_bake(const std::vector<Layer>& layers, LayeredSaveFormat format, BitDepth depth) {
  return std::any_of(layers.begin(), layers.end(), [&](const Layer& layer) {
    if (!layer.visible()) {
      return false;
    }
    return affected(format, layer, depth) || (is_group(layer) && subtree_needs_bake(layer.children(), format, depth));
  });
}

// A Pass Through folder whose children composite straight onto the parent's running
// composite (nothing at folder level mixes the result with the backdrop again).
[[nodiscard]] bool continues_parent_composite(const Layer& group) {
  if (group.blend_mode() != BlendMode::PassThrough) {
    return false;
  }
  return group.opacity() >= 1.0F && group.fill_opacity() >= 1.0F && !group.mask().has_value() && group.vector_mask() == nullptr &&
         group.layer_style().empty() && group.raw_psd_blending_ranges().empty() && group.restricted_channels() == 0;
}

[[nodiscard]] std::vector<Layer>& list_at(Document& document, const Path& path) {
  auto* list = &document.layers();
  for (const auto index : path) {
    list = &(*list)[index].children();
  }
  return *list;
}

[[nodiscard]] const std::vector<Layer>& list_at(const Document& document, const Path& path) {
  const auto* list = &document.layers();
  for (const auto index : path) {
    list = &(*list)[index].children();
  }
  return *list;
}

void clear_lost_modes(Layer& layer, LayeredSaveFormat format, BitDepth depth) {
  if (mode_lost(format, layer, depth)) {
    layer.set_blend_mode(BlendMode::Normal);
  }
  for (auto& child : layer.children()) {
    clear_lost_modes(child, format, depth);
  }
}

[[nodiscard]] Rect unit_bounds(const Document& document, const std::vector<Layer>& list, std::size_t first,
                               std::size_t last) {
  const Rect canvas{0, 0, document.width(), document.height()};
  Rect bounds{};
  for (std::size_t i = first; i <= last; ++i) {
    const auto& layer = list[i];
    if (layer.kind() != LayerKind::Pixel) {
      return canvas;  // folders and adjustments reach past their own bounds
    }
    bounds = unite_rect(bounds, layer_render_bounds(layer));
  }
  return intersect_rect(bounds, canvas);
}

// The composite of `context`'s render root up to the unit [first, last] of the list at
// `list_path`, with the unit included or not, within `rect`.
[[nodiscard]] PixelBuffer render_running(const Document& document, const Path& context, const Path& list_path,
                                         std::size_t first, std::size_t last, bool include_unit, Rect rect) {
  Document temp = document;
  if (!context.empty()) {
    const Path parent_path(context.begin(), context.end() - 1);
    const auto& parent = list_at(document, parent_path);
    temp.layers() = parent[context.back()].children();
  }
  auto* list = &temp.layers();
  for (std::size_t level = context.size(); level < list_path.size(); ++level) {
    const auto index = list_path[level];
    for (std::size_t j = index + 1; j < list->size(); ++j) {
      (*list)[j].set_visible(false);
    }
    list = &(*list)[index].children();
  }
  for (std::size_t j = last + 1; j < list->size(); ++j) {
    (*list)[j].set_visible(false);
  }
  if (!include_unit) {
    for (std::size_t j = first; j <= last; ++j) {
      (*list)[j].set_visible(false);
    }
  }
  return flatten_document_rgba8_rect(temp, rect);
}

// One pixel of N over a partly covered B: Ra = Na + Ba (1 - Na) and Rc Ra = Nc Na +
// Bc Ba (1 - Na). With B and R rounded to bytes, Na from the alpha equation alone is poorly
// known when Ba is near 1 (Ra - Ba is a level or two), and the colour it implies can leave
// 0..255. So every byte Na whose composite alpha lands within a level of Ra is tried, each
// with Nc solved for the alpha it really reaches, and the one whose composite is nearest R
// (alpha first, then colour weighted by cover) wins.
void solve_partial_cover(const std::uint8_t* bp, const std::uint8_t* rp, std::uint8_t* np) {
  const double ba = bp[3] / 255.0;
  const double ra = rp[3] / 255.0;
  const auto to_na8 = [&](double alpha) { return std::clamp(std::lround((alpha - ba) / (1.0 - ba) * 255.0), 1L, 255L); };
  const auto low = to_na8(ra - 1.0 / 255.0);
  const auto high = std::max(low, to_na8(ra + 1.0 / 255.0));
  double best = 1e9;
  for (long candidate = low; candidate <= high; ++candidate) {
    const double na = static_cast<double>(candidate) / 255.0;
    const double reached = na + ba * (1.0 - na);
    double cost = std::abs(reached - ra) * 255.0 * 4.0;
    std::array<std::uint8_t, 3> colour{};
    for (std::size_t c = 0; c < 3; ++c) {
      const double value = (rp[c] / 255.0 * reached - bp[c] / 255.0 * ba * (1.0 - na)) / na;
      colour[c] = static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
      const double shown = (colour[c] / 255.0 * na + bp[c] / 255.0 * ba * (1.0 - na)) / reached;
      cost = std::max(cost, std::abs(shown - rp[c] / 255.0) * 255.0 * reached);
    }
    if (cost < best) {
      best = cost;
      np[0] = colour[0];
      np[1] = colour[1];
      np[2] = colour[2];
      np[3] = static_cast<std::uint8_t>(candidate);
    }
  }
}

// N with N-over-B = R, per pixel, in straight RGBA8. Transparent where B and R agree.
[[nodiscard]] PixelBuffer solve_over(const PixelBuffer& below, const PixelBuffer& result, bool& any) {
  PixelBuffer out(result.width(), result.height(), PixelFormat::rgba8());
  out.clear(0);
  any = false;
  const auto b = below.data();
  const auto r = result.data();
  auto storage = out.data();
  const auto count = static_cast<std::size_t>(result.width()) * static_cast<std::size_t>(result.height());
  for (std::size_t i = 0; i < count; ++i) {
    const auto* bp = b.data() + i * 4U;
    const auto* rp = r.data() + i * 4U;
    auto* np = storage.data() + i * 4U;
    if (bp[0] == rp[0] && bp[1] == rp[1] && bp[2] == rp[2] && bp[3] == rp[3]) {
      continue;
    }
    if (rp[3] == 0) {
      continue;  // nothing to add (a mode cannot remove coverage)
    }
    any = true;
    if (bp[3] == 255 || bp[3] == 0) {
      np[0] = rp[0];
      np[1] = rp[1];
      np[2] = rp[2];
      np[3] = bp[3] == 255 ? 255 : rp[3];
      continue;
    }
    solve_partial_cover(bp, rp, np);
  }
  return out;
}

class Baker {
public:
  Baker(Document& document, LayeredSaveFormat format)
      : document_(document), format_(format), depth_(document_bit_depth(document)) {}

  void run() { bake_list({}, {}); }

  BlendBakeResult result;

private:
  void bake_list(const Path& list_path, const Path& context) {
    for (std::size_t i = 0; i < list_at(document_, list_path).size(); ++i) {
      const auto& layer = std::as_const(list_at(document_, list_path))[i];
      if (!layer.visible()) {
        if (subtree_has_lost_mode(layer)) {
          clear_lost_modes(list_at(document_, list_path)[i], format_, depth_);
          ++result.cleared;
        }
        continue;
      }
      Path child_path = list_path;
      child_path.push_back(i);
      bool whole = affected(format_, layer, depth_);
      if (!whole && is_group(layer)) {
        if (continues_parent_composite(layer)) {
          bake_list(child_path, context);
        } else if (layer.blend_mode() != BlendMode::PassThrough) {
          bake_list(child_path, child_path);
        } else {
          whole = subtree_needs_bake(layer.children(), format_, depth_);
        }
      }
      if (!whole) {
        continue;
      }
      // A clipping run composites as one (its layers clip to the base's pixels): bake it whole.
      const auto& list = std::as_const(list_at(document_, list_path));
      std::size_t first = i;
      while (first > 0 && list[first].clipped()) {
        --first;
      }
      std::size_t last = i;
      while (last + 1 < list.size() && list[last + 1].clipped()) {
        ++last;
      }
      i = bake_unit(list_path, context, first, last);
    }
  }

  [[nodiscard]] bool subtree_has_lost_mode(const Layer& layer) const {
    if (mode_lost(format_, layer, depth_)) {
      return true;
    }
    return std::any_of(layer.children().begin(), layer.children().end(),
                       [&](const Layer& child) { return subtree_has_lost_mode(child); });
  }

  // Replaces the unit with its baked layer; returns the index the unit now ends at.
  std::size_t bake_unit(const Path& list_path, const Path& context, std::size_t first, std::size_t last) {
    const auto rect = unit_bounds(document_, std::as_const(list_at(document_, list_path)), first, last);
    bool any = false;
    PixelBuffer baked;
    if (!rect.empty()) {
      const auto below = render_running(document_, context, list_path, first, last, false, rect);
      const auto with_unit = render_running(document_, context, list_path, first, last, true, rect);
      if (!below.empty() && !with_unit.empty()) {
        baked = solve_over(below, with_unit, any);
      }
    }
    auto& list = list_at(document_, list_path);
    if (!any) {
      // The unit changes nothing visible: it keeps its layers, with Normal where needed.
      for (std::size_t j = first; j <= last; ++j) {
        clear_lost_modes(list[j], format_, depth_);
        if (pass_through_needs_isolation(format_, std::as_const(list[j]), depth_)) {
          list[j].set_blend_mode(BlendMode::Normal);
        }
      }
      return last;
    }
    const auto& base = std::as_const(list[first]);
    if (depth_ != BitDepth::UInt8) {
      baked = convert_pixel_buffer_depth(baked, depth_, SampleKind::Color);
    }
    Layer replacement(document_.allocate_layer_id(), base.name(), std::move(baked));
    replacement.set_bounds(rect);
    ++result.baked;
    if (last > first || is_group(base)) {
      ++result.merged;
    }
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(first), list.begin() + static_cast<std::ptrdiff_t>(last) + 1);
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(first), std::move(replacement));
    return first;
  }

  Document& document_;
  LayeredSaveFormat format_;
  BitDepth depth_;
};

}  // namespace

bool layered_format_stores_blend_mode(LayeredSaveFormat format, BlendMode mode, bool is_group,
                                      BitDepth depth) noexcept {
  switch (format) {
    case LayeredSaveFormat::Photoshop:
      if (blend_mode_is_non_photoshop(mode)) {
        return false;
      }
      if (mode == BlendMode::PassThrough) {
        return is_group;
      }
      return blend_mode_supported_at_depth(mode, depth);
    case LayeredSaveFormat::Aseprite:
      return aseprite::stores_blend_mode(mode);
  }
  return false;
}

UnsupportedBlendModes find_unsupported_blend_modes(const Document& document, LayeredSaveFormat format) {
  UnsupportedBlendModes found;
  collect(document.layers(), format, document_bit_depth(document), found);
  return found;
}

BlendBakeResult bake_unsupported_blend_modes(Document& document, LayeredSaveFormat format) {
  Baker baker(document, format);
  baker.run();
  return baker.result;
}

}  // namespace patchy
