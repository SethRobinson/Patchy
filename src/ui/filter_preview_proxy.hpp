#pragma once

#include "filters/filter_registry.hpp"

#include <QImage>
#include <QRegion>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace patchy::ui {

// Shared by the gallery's center preview and the direct filter dialogs
// (moved verbatim out of the gallery TU, July 2026). A proxy is a bounded
// premultiplied-bilinear downsample of an immutable layer plus its scaled
// selection; renders scale pixel-distance parameters through
// FilterRegistry::scale.
inline constexpr int kFilterProxyMaximumDimension = 640;

struct FilterPreviewProxy {
  PixelBuffer original;
  QRegion selection;
  bool selection_restricted{false};
  double spatial_scale{1.0};
};

struct FilterProxyRender {
  QImage image;
  Rect bounds{};
  std::vector<Rect> entry_input_bounds;
  std::shared_ptr<const FilterRenderResult> exact_result;
};

// Latest-generation-wins detached render worker. At most one request runs;
// the newest pending request replaces older pending work; closing
// invalidates every unfinished result.
struct FilterProxyPreviewState {
  struct Work {
    std::uint64_t generation{0};
    FilterRecipe recipe;
    std::vector<std::uint64_t> entry_ids;
    std::uint64_t active_entry_id{0};
    std::string active_filter_id;
  };

  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::function<void(Work)> start;
  std::function<void(FilterProxyRender, FilterRecipe,
                     std::vector<std::uint64_t>, std::uint64_t, std::string)>
      apply;
  std::function<void(QString)> fail;
};

// An exact renderer may opt into a recipe by returning its full-resolution,
// document-space result; nullopt keeps the proxy renderer as the fallback.
using FilterProxyExactRecipeRenderer =
    std::function<std::optional<FilterRenderResult>(const FilterRecipe&,
                                                    const FilterProgress*)>;

void enqueue_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state, FilterRecipe recipe,
    std::vector<std::uint64_t> entry_ids, std::uint64_t active_entry_id,
    std::string active_filter_id);
void cancel_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state);
void close_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state);

// Bounded premultiplied bilinear resampling. It reads the immutable source in
// place and allocates only the bounded proxy, even for very large layers.
[[nodiscard]] PixelBuffer make_proxy_pixels(const PixelBuffer& source,
                                            int width, int height);
[[nodiscard]] FilterPreviewProxy make_filter_preview_proxy(
    const PixelBuffer& source, Rect bounds, const QRegion& selection,
    int maximum_dimension);
[[nodiscard]] QImage image_from_pixels(const PixelBuffer& pixels);
[[nodiscard]] QImage align_original_to_bounds(const QImage& original,
                                              Rect result_bounds);
// Exact renderers return full-resolution document-space bounds. Convert both
// the image and those bounds to the proxy's local coordinate system so the
// result and the immutable Before image share one origin and scale.
[[nodiscard]] FilterProxyRender exact_render_to_proxy(
    std::shared_ptr<const FilterRenderResult> exact, Rect source_bounds,
    const FilterPreviewProxy& proxy);
[[nodiscard]] FilterProxyRender render_filter_proxy(
    const FilterPreviewProxy& proxy, const FilterRegistry& registry,
    const FilterInvocation& invocation,
    const FilterProgress* progress = nullptr);
[[nodiscard]] FilterProxyRender render_filter_proxy(
    const FilterPreviewProxy& proxy, const FilterRegistry& registry,
    const FilterRecipe& recipe, const FilterProgress* progress = nullptr);

// The generation-guarded detached-worker start callback (the gallery's
// historical body): renders the work's recipe (or the host's exact renderer
// result) against the proxy, then reports through state->apply/fail on the
// UI thread and starts any pending work. Hosts assign the result to
// state->start after setting apply/fail.
[[nodiscard]] std::function<void(FilterProxyPreviewState::Work)>
make_filter_proxy_render_start(
    std::shared_ptr<FilterProxyPreviewState> state,
    std::shared_ptr<const FilterRegistry> registry, FilterPreviewProxy proxy,
    Rect exact_source_bounds, FilterProxyExactRecipeRenderer exact_renderer);

}  // namespace patchy::ui
