#include "ui/filter_preview_proxy.hpp"

#include "ui/background_workers.hpp"
#include "ui/filter_workflows.hpp"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace patchy::ui {
namespace {

[[nodiscard]] std::uint8_t clamp_byte(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

}  // namespace

void enqueue_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state, FilterRecipe recipe,
    std::vector<std::uint64_t> entry_ids, std::uint64_t active_entry_id,
    std::string active_filter_id) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation =
      state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  FilterProxyPreviewState::Work work{
      generation, std::move(recipe), std::move(entry_ids), active_entry_id,
      std::move(active_filter_id)};
  if (state->in_flight) {
    state->pending = std::move(work);
    return;
  }
  state->start(std::move(work));
}

void cancel_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state) {
  if (state == nullptr || state->closed) {
    return;
  }
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
}

void close_filter_proxy_preview(
    const std::shared_ptr<FilterProxyPreviewState>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
  state->start = {};
  state->apply = {};
  state->fail = {};
}

PixelBuffer make_proxy_pixels(const PixelBuffer& source, int width,
                              int height) {
  if (source.width() == width && source.height() == height) {
    return source;
  }
  PixelBuffer result(width, height, source.format());
  const auto channels = source.format().channels;
  const auto scale_x = static_cast<double>(source.width()) / width;
  const auto scale_y = static_cast<double>(source.height()) / height;
  for (int y = 0; y < height; ++y) {
    const auto sy = (y + 0.5) * scale_y - 0.5;
    const auto y0 = std::clamp(static_cast<int>(std::floor(sy)), 0,
                               source.height() - 1);
    const auto y1 = std::min(y0 + 1, source.height() - 1);
    const auto fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
    for (int x = 0; x < width; ++x) {
      const auto sx = (x + 0.5) * scale_x - 0.5;
      const auto x0 = std::clamp(static_cast<int>(std::floor(sx)), 0,
                                 source.width() - 1);
      const auto x1 = std::min(x0 + 1, source.width() - 1);
      const auto fx = std::clamp(sx - std::floor(sx), 0.0, 1.0);
      const std::array<const std::uint8_t*, 4> samples = {
          source.pixel(x0, y0), source.pixel(x1, y0),
          source.pixel(x0, y1), source.pixel(x1, y1)};
      const std::array<double, 4> weights = {
          (1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
          (1.0 - fx) * fy, fx * fy};
      auto* destination = result.pixel(x, y);
      double alpha = 0.0;
      std::array<double, 3> premultiplied{};
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample_alpha =
            channels >= 4 ? samples[index][3] / 255.0 : 1.0;
        alpha += weights[index] * sample_alpha;
        for (int channel = 0; channel < 3; ++channel) {
          premultiplied[static_cast<std::size_t>(channel)] +=
              weights[index] * samples[index][channel] * sample_alpha;
        }
      }
      for (int channel = 0; channel < 3; ++channel) {
        destination[channel] =
            alpha > 0.0
                ? clamp_byte(premultiplied[static_cast<std::size_t>(channel)] /
                             alpha)
                : 0;
      }
      if (channels >= 4) {
        destination[3] = clamp_byte(alpha * 255.0);
      }
    }
  }
  return result;
}

FilterPreviewProxy make_filter_preview_proxy(const PixelBuffer& source,
                                             Rect bounds,
                                             const QRegion& selection,
                                             int maximum_dimension) {
  FilterPreviewProxy proxy;
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 ||
      source.format().channels < 3) {
    return proxy;
  }
  const auto largest = std::max(source.width(), source.height());
  proxy.spatial_scale =
      largest > maximum_dimension
          ? static_cast<double>(maximum_dimension) / largest
          : 1.0;
  const auto width = std::max(
      1, static_cast<int>(std::lround(source.width() * proxy.spatial_scale)));
  const auto height = std::max(
      1, static_cast<int>(std::lround(source.height() * proxy.spatial_scale)));
  proxy.original = make_proxy_pixels(source, width, height);
  proxy.selection_restricted = !selection.isEmpty();
  if (!proxy.selection_restricted) {
    return proxy;
  }

  const QRect source_bounds(bounds.x, bounds.y, source.width(), source.height());
  const auto selected = selection.intersected(QRegion(source_bounds));
  const auto scale_x = static_cast<double>(width) / source.width();
  const auto scale_y = static_cast<double>(height) / source.height();
  for (const auto& rect : selected) {
    const auto local_left = rect.left() - bounds.x;
    const auto local_top = rect.top() - bounds.y;
    const auto local_right = rect.right() + 1 - bounds.x;
    const auto local_bottom = rect.bottom() + 1 - bounds.y;
    const auto left = static_cast<int>(std::floor(local_left * scale_x));
    const auto top = static_cast<int>(std::floor(local_top * scale_y));
    const auto right = static_cast<int>(std::ceil(local_right * scale_x));
    const auto bottom = static_cast<int>(std::ceil(local_bottom * scale_y));
    proxy.selection += QRect(left, top, std::max(1, right - left),
                             std::max(1, bottom - top));
  }
  proxy.selection &= QRegion(QRect(0, 0, width, height));
  return proxy;
}

QImage image_from_pixels(const PixelBuffer& pixels) {
  if (pixels.empty()) {
    return {};
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto channels = pixels.format().channels;
  for (int y = 0; y < pixels.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* source = pixels.pixel(x, y);
      row[x * 4 + 0] = source[0];
      row[x * 4 + 1] = source[1];
      row[x * 4 + 2] = source[2];
      row[x * 4 + 3] = channels >= 4 ? source[3] : 255;
    }
  }
  return image;
}

QImage align_original_to_bounds(const QImage& original, Rect result_bounds) {
  if (original.isNull() || result_bounds.width <= 0 ||
      result_bounds.height <= 0) {
    return original;
  }
  QImage aligned(result_bounds.width, result_bounds.height,
                 QImage::Format_RGBA8888);
  aligned.fill(Qt::transparent);
  QPainter painter(&aligned);
  painter.drawImage(-result_bounds.x, -result_bounds.y, original);
  return aligned;
}

FilterProxyRender exact_render_to_proxy(
    std::shared_ptr<const FilterRenderResult> exact, Rect source_bounds,
    const FilterPreviewProxy& proxy) {
  if (exact == nullptr || exact->pixels.empty() || exact->bounds.empty() ||
      proxy.original.empty() || source_bounds.width <= 0 ||
      source_bounds.height <= 0 ||
      exact->pixels.width() != exact->bounds.width ||
      exact->pixels.height() != exact->bounds.height) {
    throw std::invalid_argument("Invalid exact visual-filter render result");
  }

  const auto scale_x = static_cast<double>(proxy.original.width()) /
                       static_cast<double>(source_bounds.width);
  const auto scale_y = static_cast<double>(proxy.original.height()) /
                       static_cast<double>(source_bounds.height);
  const auto left = static_cast<int>(std::floor(
      (static_cast<double>(exact->bounds.x) - source_bounds.x) * scale_x));
  const auto top = static_cast<int>(std::floor(
      (static_cast<double>(exact->bounds.y) - source_bounds.y) * scale_y));
  const auto right = static_cast<int>(std::ceil(
      (static_cast<double>(exact->bounds.x) + exact->bounds.width -
       source_bounds.x) *
      scale_x));
  const auto bottom = static_cast<int>(std::ceil(
      (static_cast<double>(exact->bounds.y) + exact->bounds.height -
       source_bounds.y) *
      scale_y));
  const auto width = std::max(1, right - left);
  const auto height = std::max(1, bottom - top);
  auto pixels = make_proxy_pixels(exact->pixels, width, height);
  return {image_from_pixels(pixels), Rect{left, top, width, height}, {},
          std::move(exact)};
}

FilterProxyRender render_filter_proxy(const FilterPreviewProxy& proxy,
                                      const FilterRegistry& registry,
                                      const FilterInvocation& invocation,
                                      const FilterProgress* progress) {
  const auto local_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  if (proxy.original.empty() ||
      (proxy.selection_restricted && proxy.selection.isEmpty())) {
    return {image_from_pixels(proxy.original), local_bounds, {}, nullptr};
  }
  const auto scaled = registry.scale(invocation, proxy.spatial_scale);
  if (!scaled.has_value()) {
    return {image_from_pixels(proxy.original), local_bounds, {}, nullptr};
  }
  const FilterPreviewSettings settings{true, *scaled};
  auto result_bounds = local_bounds;
  auto pixels = build_filter_preview_pixels(proxy.original, proxy.selection,
                                            local_bounds, registry, settings,
                                            progress, &result_bounds);
  return {image_from_pixels(pixels), result_bounds, {}, nullptr};
}

FilterProxyRender render_filter_proxy(const FilterPreviewProxy& proxy,
                                      const FilterRegistry& registry,
                                      const FilterRecipe& recipe,
                                      const FilterProgress* progress) {
  const auto local_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  if (proxy.original.empty() || recipe.entries.empty() ||
      (proxy.selection_restricted && proxy.selection.isEmpty())) {
    return {image_from_pixels(proxy.original), local_bounds,
            std::vector<Rect>(recipe.entries.size(), local_bounds), nullptr};
  }
  const auto scaled = registry.scale(recipe, proxy.spatial_scale);
  if (!scaled.has_value()) {
    return {image_from_pixels(proxy.original), local_bounds,
            std::vector<Rect>(recipe.entries.size(), local_bounds), nullptr};
  }
  if (!proxy.selection_restricted) {
    FilterRecipeRenderTrace trace;
    auto rendered = registry.render(*scaled, proxy.original, local_bounds,
                                    true, progress, &trace);
    return {image_from_pixels(rendered.pixels), rendered.bounds,
            std::move(trace.entry_input_bounds), nullptr};
  }
  auto result_bounds = local_bounds;
  auto pixels = build_filter_preview_pixels(
      proxy.original, proxy.selection, local_bounds, registry, *scaled,
      progress, &result_bounds);
  return {image_from_pixels(pixels), result_bounds,
          std::vector<Rect>(recipe.entries.size(), local_bounds), nullptr};
}

std::function<void(FilterProxyPreviewState::Work)>
make_filter_proxy_render_start(std::shared_ptr<FilterProxyPreviewState> state,
                               std::shared_ptr<const FilterRegistry> registry,
                               FilterPreviewProxy proxy,
                               Rect exact_source_bounds,
                               FilterProxyExactRecipeRenderer exact_renderer) {
  return [state = std::move(state), registry = std::move(registry),
          proxy = std::move(proxy), exact_source_bounds,
          exact_renderer =
              std::move(exact_renderer)](FilterProxyPreviewState::Work work) {
    state->in_flight = true;
    auto rendered = std::make_shared<FilterProxyRender>();
    auto error = std::make_shared<QString>();
    auto cancelled = std::make_shared<bool>(false);
    auto* app = QCoreApplication::instance();
    const auto generation = work.generation;
    const auto active_entry_id = work.active_entry_id;
    const auto filter_id = work.active_filter_id;
    auto entry_ids = std::move(work.entry_ids);
    auto recipe = std::move(work.recipe);
    run_tracked_background_worker([app, state, registry, proxy, exact_source_bounds,
                 exact_renderer, recipe = std::move(recipe),
                 entry_ids = std::move(entry_ids), generation, active_entry_id,
                 filter_id, rendered, error, cancelled]() mutable {
      FilterProgress progress{[state, generation](int, int,
                                                  FilterProgressStage) {
        return state->generation.load(std::memory_order_acquire) == generation;
      }};
      try {
        std::optional<FilterRenderResult> exact;
        if (exact_renderer) {
          exact = exact_renderer(recipe, &progress);
        }
        if (exact.has_value()) {
          auto shared_exact =
              std::make_shared<const FilterRenderResult>(std::move(*exact));
          *rendered = exact_render_to_proxy(std::move(shared_exact),
                                            exact_source_bounds, proxy);
        } else {
          if (progress.update &&
              !progress.update(0, 1, FilterProgressStage::Filtering)) {
            throw FilterCancelled();
          }
          *rendered = render_filter_proxy(proxy, *registry, recipe, &progress);
        }
      } catch (const FilterCancelled&) {
        *cancelled = true;
      } catch (const std::exception& caught) {
        *error = QString::fromUtf8(caught.what());
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [state, generation, active_entry_id, filter_id,
           recipe = std::move(recipe), entry_ids = std::move(entry_ids),
           rendered, error, cancelled]() mutable {
            state->in_flight = false;
            const auto is_latest =
                generation ==
                state->generation.load(std::memory_order_acquire);
            if (!state->closed && is_latest && !*cancelled) {
              if (error->isEmpty() && state->apply) {
                state->apply(std::move(*rendered), std::move(recipe),
                             std::move(entry_ids), active_entry_id, filter_id);
              } else if (!error->isEmpty() && state->fail) {
                state->fail(*error);
              }
            }
            if (!state->closed && state->pending.has_value() && state->start) {
              auto next = std::move(*state->pending);
              state->pending.reset();
              state->start(std::move(next));
            }
          },
          Qt::QueuedConnection);
    });
  };
}

}  // namespace patchy::ui
