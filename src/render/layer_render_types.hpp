#pragma once

#include "core/layer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace patchy::render_detail {

struct LayerBoundsOverride {
  LayerId layer_id{};
  Rect bounds{};
  const PixelBuffer* pixels{nullptr};
  std::optional<Rect> mask_bounds{};
  std::optional<bool> visible{};
};

// ---------------------------------------------------------------------------
// Optional cache hook for the expensive per-effect float masks (distance
// transforms, spread expansions, interior blurs). A provider that returns a
// hit lets a renderer skip the whole mask prep; on a miss the renderer
// computes the mask over its FULL domain (not the legacy draw-clipped window)
// and offers it back. Masks depend only on layer-local content, so entries
// keyed by content_revision survive layer moves. full_domain_allowed gates
// byte-stability: full renders must produce identical bytes with and without
// a provider, so full-domain masks are only used where the legacy windowed
// domain would have been the full domain anyway (see the UI-side provider).

enum class StyleMaskKind : std::uint8_t {
  DropShadow,
  OuterGlow,
  InnerShadow,
  InnerGlow,
  BevelHeight,
  Stroke,
  Satin,
  GroupSilhouette,
};

struct StyleMaskEntry {
  std::vector<float> primary;
  // BevelHeight keeps the alpha mask alongside the height mask.
  std::vector<float> secondary;
  std::shared_ptr<const PixelBuffer> group_pixels{};
  // Stroke only, and only when the content has semi-transparent pixels inside its
  // contour (StrokeMaskPlanes): the part of the band that sits UNDER the content and
  // the band membership that knocks the content out. Empty otherwise.
  std::vector<float> underlay;
  std::vector<float> inside;
  std::vector<float> knockout;
  bool partial_content{false};
};

class StyleMaskProvider {
public:
  virtual ~StyleMaskProvider() = default;
  // May the renderer swap its legacy draw-clipped mask window for `domain`
  // (document space)? Must be false whenever that could change full-render
  // output bytes.
  [[nodiscard]] virtual bool full_domain_allowed(const Layer& layer, Rect domain) const = 0;
  [[nodiscard]] virtual std::shared_ptr<const StyleMaskEntry> fetch(const Layer& layer, StyleMaskKind kind,
                                                                    std::uint32_t effect_index, Rect domain,
                                                                    Rect bounds,
                                                                    std::optional<Rect> mask_bounds) = 0;
  virtual void store(const Layer& layer, StyleMaskKind kind, std::uint32_t effect_index, Rect domain, Rect bounds,
                     std::optional<Rect> mask_bounds, std::shared_ptr<const StyleMaskEntry> entry) = 0;

  // A paint stroke's patch render (docs/performance.md, "Smart invalidation"): this group's
  // effect masks are computed over the draw-clipped windows a pixel layer's are, and none
  // is cached. The next dab changes the group again, so a whole-group mask would serve one
  // patch. Everywhere else a group's masks cover its whole content.
  [[nodiscard]] virtual bool group_masks_windowed(const Layer& /*group*/) const { return false; }

  // The flattened children of `group` as they were before the edits that touched only
  // `changed` (document space): the silhouette over `bounds` is then `pixels` with those
  // rects flattened again, instead of every child over the whole group. nullopt when no
  // such base is known, or it does not cover exactly `bounds`.
  struct GroupSilhouetteBase {
    PixelBuffer pixels;
    std::vector<Rect> changed;
  };
  [[nodiscard]] virtual std::optional<GroupSilhouetteBase> group_silhouette_base(const Layer& /*group*/,
                                                                               Rect /*bounds*/) {
    return std::nullopt;
  }
};

}  // namespace patchy::render_detail
