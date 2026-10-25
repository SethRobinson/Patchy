#include "render/compositor.hpp"

#include "core/blend_math.hpp"
#include "core/environment.hpp"
#include "core/worker_budget.hpp"
#include "render/composite_targets.hpp"
#include "render/native_cmyk.hpp"
#include "render/native_duotone.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace patchy {

PixelBuffer Compositor::flatten_rgba_deep(const Document& document) const {
  const auto canvas = Rect::from_size(document.width(), document.height());
  const auto domain = deep_domain_for(document.color_state().bit_depth);
  const auto flatten_strip = [&document, domain](Rect clip) {
    render_detail::DeepCompositeTarget target(clip, false, domain);
    render_detail::composite_layers(target, document.layers(), clip, nullptr, true, nullptr,
                                    &document.metadata().patterns);
    return target.to_pixel_buffer();
  };
  // Strips under flatten_rgb8's rules (below): clip compositing equals a full walk.
  const auto area = static_cast<std::int64_t>(document.width()) * static_cast<std::int64_t>(document.height());
  const auto strips = max_blocking_fanout_workers(
      std::clamp(std::min(document.height() / 128, patchy::hardware_worker_threads()), 1, 16));
  if (strips < 2 || area < 4'000'000 || environment_variable_is_set("PATCHY_RENDER_SINGLE_THREADED")) {
    return flatten_strip(canvas);
  }
  struct StripJob {
    Rect clip{};
    std::future<PixelBuffer> pixels;
  };
  std::vector<StripJob> jobs;
  const auto rows_per_strip = (document.height() + strips - 1) / strips;
  for (std::int32_t start = 0; start < document.height(); start += rows_per_strip) {
    const Rect clip{0, start, document.width(), std::min(rows_per_strip, document.height() - start)};
    jobs.push_back(StripJob{clip, std::async(std::launch::async, flatten_strip, clip)});
  }
  std::optional<PixelBuffer> output;
  for (auto& job : jobs) {
    const auto strip = job.pixels.get();
    if (!output.has_value()) {
      output.emplace(document.width(), document.height(), strip.format());
    }
    for (std::int32_t row = 0; row < strip.height(); ++row) {
      const auto source = strip.row(row);
      std::memcpy(output->row(job.clip.y + row).data(), source.data(), source.size());
    }
  }
  return std::move(*output);
}

PixelBuffer Compositor::flatten_rgb8(const Document& document, std::vector<std::uint8_t>* merged_alpha) const {
  auto native = render_native_cmyk8(document, Rect::from_size(document.width(), document.height()));
  if (!native) native = render_native_duotone8(document, Rect::from_size(document.width(), document.height()));
  if (native) {
    PixelBuffer output(native->width(), native->height(), PixelFormat::rgb8());
    if (merged_alpha) merged_alpha->resize(static_cast<std::size_t>(native->width()) * native->height());
    for (int y = 0; y < native->height(); ++y) {
      const auto* source = native->row(y).data();
      auto* destination = output.row(y).data();
      for (int x = 0; x < native->width(); ++x) {
        std::copy_n(source + x * 4, 3, destination + x * 3);
        if (merged_alpha) (*merged_alpha)[static_cast<std::size_t>(y) * native->width() + x] = source[x * 4 + 3];
      }
    }
    return output;
  }
  if (document.color_state().bit_depth != BitDepth::UInt8) {
    // 16/32-bit documents (docs/high-bit-depth.md): the deep flatten's display values.
    // (The 8-bit walk skips deep layers.)
    const auto rgba =
        convert_pixel_buffer_depth(flatten_rgba_deep(document), BitDepth::UInt8, SampleKind::Color);
    PixelBuffer output(rgba.width(), rgba.height(), PixelFormat::rgb8());
    if (merged_alpha != nullptr) {
      merged_alpha->assign(static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height()), 0);
    }
    for (std::int32_t y = 0; y < rgba.height(); ++y) {
      const auto* source = rgba.row(y).data();
      auto* destination = output.row(y).data();
      for (std::int32_t x = 0; x < rgba.width(); ++x) {
        std::memcpy(destination + static_cast<std::size_t>(x) * 3U, source + static_cast<std::size_t>(x) * 4U, 3U);
        if (merged_alpha != nullptr) {
          (*merged_alpha)[static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba.width()) +
                          static_cast<std::size_t>(x)] = source[static_cast<std::size_t>(x) * 4U + 3U];
        }
      }
    }
    return output;
  }
  PixelBuffer output(document.width(), document.height(), PixelFormat::rgb8());
  output.clear(0);
  const auto canvas = Rect::from_size(document.width(), document.height());
  if (merged_alpha != nullptr) {
    merged_alpha->assign(static_cast<std::size_t>(std::max(0, document.width())) *
                             static_cast<std::size_t>(std::max(0, document.height())),
                         0);
  }

  // Same strip parallelism as the UI renderer (see docs/performance.md "Parallel
  // strip rendering"): strips only read the document and write private buffers, and clip
  // compositing is equivalent to a full walk. Small flattens (every compositor
  // pixel test) keep the sequential path byte for byte, as does
  // PATCHY_RENDER_SINGLE_THREADED=1.
  const auto area = static_cast<std::int64_t>(document.width()) * static_cast<std::int64_t>(document.height());
  const auto hardware_threads = patchy::hardware_worker_threads();
  // max_blocking_fanout_workers: this thread blocks on the strip joins below,
  // so on the wasm main thread the fan-out must fit the idle pthread pool or
  // it deadlocks the tab; fewer strips (or the sequential path) is the
  // correct degradation and the bytes are identical for any strip count.
  const auto strips = max_blocking_fanout_workers(
      std::clamp(std::min(document.height() / 128, hardware_threads), 1, 16));
  const bool parallel =
      strips >= 2 && area >= 4'000'000 && !environment_variable_is_set("PATCHY_RENDER_SINGLE_THREADED");
  if (parallel) {
    struct StripResult {
      PixelBuffer pixels;
      std::vector<std::uint8_t> alpha;
    };
    struct StripJob {
      Rect clip{};
      std::future<StripResult> result;
    };
    const bool want_alpha = merged_alpha != nullptr;
    std::vector<StripJob> jobs;
    jobs.reserve(static_cast<std::size_t>(strips));
    const auto rows_per_strip = (document.height() + strips - 1) / strips;
    for (std::int32_t start = 0; start < document.height(); start += rows_per_strip) {
      const auto rows = std::min(rows_per_strip, document.height() - start);
      const Rect strip_clip{0, start, document.width(), rows};
      jobs.push_back(StripJob{strip_clip, std::async(std::launch::async, [&document, strip_clip, want_alpha] {
                                PixelBuffer strip(strip_clip.width, strip_clip.height, PixelFormat::rgb8());
                                strip.clear(0);
                                Rgb8PixelBufferTarget target(strip, 0.0F, strip_clip.x, strip_clip.y);
                                render_detail::composite_layers(target, document.layers(), strip_clip, nullptr,
                                                                true, nullptr,
                                                                &document.metadata().patterns);
                                return StripResult{std::move(strip),
                                                   want_alpha ? target.alpha_bytes() : std::vector<std::uint8_t>{}};
                              })});
    }
    for (auto& job : jobs) {
      const auto strip = job.result.get();
      const auto row_bytes = static_cast<std::size_t>(strip.pixels.width()) * 3U;
      for (std::int32_t row = 0; row < strip.pixels.height(); ++row) {
        std::memcpy(output.pixel(0, job.clip.y + row), strip.pixels.pixel(0, row), row_bytes);
      }
      if (merged_alpha != nullptr) {
        std::memcpy(merged_alpha->data() +
                        static_cast<std::size_t>(job.clip.y) * static_cast<std::size_t>(document.width()),
                    strip.alpha.data(), strip.alpha.size());
      }
    }
    return output;
  }

  Rgb8PixelBufferTarget target(output, 0.0F);
  render_detail::composite_layers(target, document.layers(), canvas, nullptr, true, nullptr,
                                  &document.metadata().patterns);
  if (merged_alpha != nullptr) {
    *merged_alpha = target.alpha_bytes();
  }
  return output;
}

}  // namespace patchy
