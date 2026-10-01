#include "core/blend_math.hpp"
#include "core/environment.hpp"
#include "core/document.hpp"
#include "core/layer.hpp"
#include "render/compositor.hpp"
#include "render/gpu_document_capabilities.hpp"
#include "render/pixel_comparison.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/memory_info.hpp"
#include "render/gpu_wait_budget.hpp"
#include "ui/webgpu_render_backend.hpp"

#include <QApplication>
#include <QByteArray>
#include <QImage>
#include <QRegion>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::int32_t kTileSize = 256;

// CTest maps this exit status to "Not Run (Skipped)" through SKIP_RETURN_CODE.
// A skip is therefore visible as a skip, never counted as a pass.
constexpr int kSkipExitCode = 77;

std::size_t tile_count(std::int32_t width, std::int32_t height) {
  const auto horizontal = (width + kTileSize - 1) / kTileSize;
  const auto vertical = (height + kTileSize - 1) / kTileSize;
  return static_cast<std::size_t>(horizontal) * static_cast<std::size_t>(vertical);
}

std::size_t padded_row_bytes(std::int32_t width) {
  const auto source = static_cast<std::size_t>(width) * 4U;
  return ((source + 255U) / 256U) * 256U;
}

std::size_t regional_readback_bytes(std::int32_t width, std::int32_t height) {
  std::size_t total = 0;
  for (std::int32_t y = 0; y < height; y += kTileSize) {
    const auto tile_height = std::min(kTileSize, height - y);
    for (std::int32_t x = 0; x < width; x += kTileSize) {
      const auto tile_width = std::min(kTileSize, width - x);
      total += padded_row_bytes(tile_width) * static_cast<std::size_t>(tile_height);
    }
  }
  return total;
}

patchy::PixelBuffer pixel_buffer_from_image(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  patchy::PixelBuffer result(image.width(), image.height(), patchy::PixelFormat::rgb8());
  result.clear(0);
  for (int y = 0; y < image.height(); ++y) {
    const auto* source_row = image.constScanLine(y);
    auto destination_row = result.row(y);
    for (int x = 0; x < image.width(); ++x) {
      const auto source_offset = static_cast<std::size_t>(x) * 4U;
      const auto destination_offset = static_cast<std::size_t>(x) * 3U;
      destination_row[destination_offset + 0U] = source_row[source_offset + 0U];
      destination_row[destination_offset + 1U] = source_row[source_offset + 1U];
      destination_row[destination_offset + 2U] = source_row[source_offset + 2U];
    }
  }
  return result;
}

void require_opaque(const QImage& source, const std::string& label) {
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      if (row[static_cast<std::size_t>(x) * 4U + 3U] != 255U) {
        throw std::runtime_error(label + " published a non-opaque frame");
      }
    }
  }
}

// The snapshot handed to the backend comes from CanvasWidget itself, so the
// canvas-to-backend conversion (capability gate, Fill folding, Grayscale8
// masks, Blend If ranges, document-space rectangles) is what gets validated.
// The canvas is driven with the offscreen platform at zoom 1 with the view
// anchored at the document origin; the Dawn tier only consumes document-space
// rectangles, so the widget placement does not influence the result.
patchy::ui::CanvasGpuDocument gpu_document_from(const patchy::Document& document) {
  patchy::ui::CanvasWidget canvas;
  canvas.resize(std::max(64, document.width()), std::max(64, document.height()));
  canvas.set_document(const_cast<patchy::Document*>(&document));
  canvas.set_zoom(1.0);
  patchy::ui::CanvasGpuDocument result;
  QString reason;
  if (!canvas.gpu_document_snapshot(result, &reason)) {
    throw std::runtime_error("canvas rejected the document for GPU composition: " + reason.toStdString());
  }
  if (result.layers.empty()) {
    throw std::runtime_error("canvas produced an empty GPU snapshot");
  }
  return result;
}

void require_cpu_fallback(const patchy::Document& document, const std::string& label) {
  patchy::ui::CanvasWidget canvas;
  canvas.resize(std::max(64, document.width()), std::max(64, document.height()));
  canvas.set_document(const_cast<patchy::Document*>(&document));
  patchy::ui::CanvasGpuDocument result;
  QString reason;
  if (canvas.gpu_document_snapshot(result, &reason)) {
    throw std::runtime_error(label + ": canvas accepted a document that must stay on the CPU compositor");
  }
  if (reason.isEmpty()) {
    throw std::runtime_error(label + ": canvas rejected the document without a reason");
  }
}

patchy::PixelBuffer cpu_frame(const patchy::Document& document) {
  return patchy::Compositor{}.flatten_rgb8(document);
}

void require_equivalent(const patchy::Document& document, const QImage& gpu_frame, const std::string& label) {
  require_opaque(gpu_frame, label);
  const auto report = patchy::compare_pixel_buffers(cpu_frame(document), pixel_buffer_from_image(gpu_frame));
  patchy::PixelComparisonPolicy policy;
  policy.max_channel_delta = 1;
  policy.max_differing_pixels = std::max<std::uint64_t>(8U,
                                                        static_cast<std::uint64_t>(document.width()) *
                                                            static_cast<std::uint64_t>(document.height()) / 1000U);
  policy.max_mean_abs_channel_delta = 0.1;
  policy.max_differing_fraction = 0.001;
  if (!report.within(policy)) {
    std::ostringstream message;
    message << label << " differs from CPU: max delta=" << report.max_channel_delta
            << ", differing pixels=" << report.differing_pixels
            << ", mean delta=" << report.mean_abs_channel_delta;
    throw std::runtime_error(message.str());
  }
}

patchy::Document make_document(std::int32_t width, std::int32_t height) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer background(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* pixel = background.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((17 + x * 3 + y) % 251);
      pixel[1] = static_cast<std::uint8_t>((31 + y * 5 + x / 3) % 251);
      pixel[2] = static_cast<std::uint8_t>((47 + x + y * 2) % 251);
      pixel[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(background));

  const auto overlay_width = std::min<std::int32_t>(320, width);
  const auto overlay_height = std::min<std::int32_t>(180, height);
  patchy::PixelBuffer overlay(overlay_width, overlay_height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < overlay_height; ++y) {
    for (std::int32_t x = 0; x < overlay_width; ++x) {
      auto* pixel = overlay.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((190 + x / 2 + y) % 251);
      pixel[1] = static_cast<std::uint8_t>((65 + x + y / 2) % 251);
      pixel[2] = static_cast<std::uint8_t>((21 + x / 3 + y * 2) % 251);
      pixel[3] = 255;
    }
  }
  patchy::Layer overlay_layer(document.allocate_layer_id(), "Overlay", std::move(overlay));
  overlay_layer.set_bounds(patchy::Rect{96, 40, overlay_width, overlay_height});
  document.add_layer(std::move(overlay_layer));
  return document;
}

// A translucent overlay with an alpha gradient and fully transparent holes,
// so partial source alpha, alpha accumulation and holes over the backdrop
// are all exercised by every blend mode below.
patchy::Document make_partial_alpha_document(std::int32_t width, std::int32_t height) {
  auto document = make_document(width, height);
  auto& overlay = document.layers().back();
  auto& pixels = overlay.pixels();
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      const auto hole = ((x / 23) + (y / 19)) % 5 == 0;
      pixel[3] = hole ? 0 : static_cast<std::uint8_t>(std::min(255, 8 + (x * 255) / std::max(1, pixels.width() - 1)));
    }
  }
  return document;
}

struct BlendCase {
  patchy::BlendMode mode;
  const char* name;
};

constexpr std::array<BlendCase, 20> kAcceptedBlendModes{{
    {patchy::BlendMode::Normal, "Normal"},           {patchy::BlendMode::Multiply, "Multiply"},
    {patchy::BlendMode::Screen, "Screen"},           {patchy::BlendMode::Overlay, "Overlay"},
    {patchy::BlendMode::Darken, "Darken"},           {patchy::BlendMode::Lighten, "Lighten"},
    {patchy::BlendMode::ColorDodge, "ColorDodge"},   {patchy::BlendMode::ColorBurn, "ColorBurn"},
    {patchy::BlendMode::HardLight, "HardLight"},     {patchy::BlendMode::SoftLight, "SoftLight"},
    {patchy::BlendMode::Difference, "Difference"},   {patchy::BlendMode::LinearBurn, "LinearBurn"},
    {patchy::BlendMode::PinLight, "PinLight"},       {patchy::BlendMode::Exclusion, "Exclusion"},
    {patchy::BlendMode::LinearDodge, "LinearDodge"}, {patchy::BlendMode::Subtract, "Subtract"},
    {patchy::BlendMode::Divide, "Divide"},           {patchy::BlendMode::VividLight, "VividLight"},
    {patchy::BlendMode::LinearLight, "LinearLight"}, {patchy::BlendMode::HardMix, "HardMix"},
}};

patchy::Document make_masked_document() {
  auto document = make_document(257, 257);
  auto& layer = document.layers().back();
  const auto bounds = layer.bounds();
  patchy::LayerMask mask;
  mask.bounds = bounds;
  mask.pixels = patchy::PixelBuffer(bounds.width, bounds.height, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>(((x / 17 + y / 13) % 2) == 0 ? 255 : 0);
    }
  }
  mask.default_color = 0;
  mask.density = 255;
  layer.set_mask(std::move(mask));
  return document;
}

patchy::Document make_blend_if_document() {
  auto document = make_document(257, 257);
  auto& layer = document.layers().back();
  layer.set_blend_mode(patchy::BlendMode::Multiply);
  patchy::LayerBlendIf settings;
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)].this_layer =
      patchy::BlendIfThresholds{32, 96, 160, 224};
  if (!layer.set_blend_if(settings)) {
    throw std::runtime_error("could not install supported Blend If fixture");
  }
  return document;
}

void compose_full_and_check(patchy::ui::WebGpuRenderBackend& backend, const patchy::Document& document,
                            const std::string& label) {
  QImage frame;
  QString reason;
  if (!backend.compose(gpu_document_from(document), frame, &reason)) {
    throw std::runtime_error(label + " failed: " + reason.toStdString());
  }
  const auto expected_tiles = tile_count(document.width(), document.height());
  CHECK(backend.last_rendered_tile_count() == expected_tiles);
  CHECK(backend.last_submitted_pass_count() == expected_tiles * 3U);
  CHECK(backend.last_readback_bytes() == regional_readback_bytes(document.width(), document.height()));
  CHECK(backend.last_composition_metrics().queue_submissions == 1U);
  CHECK(backend.last_composition_metrics().queue_waits == 1U);
  require_equivalent(document, frame, label);
}

void full_frames_match_cpu_for_boundaries(patchy::ui::WebGpuRenderBackend& backend) {
  for (const auto& [width, height] : std::vector<std::pair<std::int32_t, std::int32_t>>{{255, 255}, {256, 256},
                                                                                             {257, 257}}) {
    compose_full_and_check(backend, make_document(width, height),
                           "boundary " + std::to_string(width) + "x" + std::to_string(height));
  }
}

void supported_shader_features_match_cpu(patchy::ui::WebGpuRenderBackend& backend) {
  compose_full_and_check(backend, make_masked_document(), "gray8 mask");
  compose_full_and_check(backend, make_blend_if_document(), "Blend If");
}

// Every blend mode the capability matrix accepts, each composed with an
// opaque overlay, a partially transparent overlay, and reduced layer opacity.
void all_accepted_blend_modes_match_cpu(patchy::ui::WebGpuRenderBackend& backend) {
  for (const auto& blend : kAcceptedBlendModes) {
    {
      auto document = make_document(257, 129);
      document.layers().back().set_blend_mode(blend.mode);
      compose_full_and_check(backend, document, std::string("opaque ") + blend.name);
    }
    {
      auto document = make_partial_alpha_document(257, 129);
      document.layers().back().set_blend_mode(blend.mode);
      compose_full_and_check(backend, document, std::string("partial alpha ") + blend.name);
    }
    {
      auto document = make_partial_alpha_document(257, 129);
      auto& overlay = document.layers().back();
      overlay.set_blend_mode(blend.mode);
      overlay.set_opacity(0.6F);
      compose_full_and_check(backend, document, std::string("opacity 60% ") + blend.name);
    }
  }
}

// Fill is folded into opacity on the GPU. That matches the CPU for every mode
// without special Fill handling; the eight special modes must be rejected by
// the canvas while Fill is below 100% and accepted again at 100%.
void fill_opacity_matches_cpu_or_stays_on_cpu(patchy::ui::WebGpuRenderBackend& backend) {
  for (const auto& blend : kAcceptedBlendModes) {
    auto document = make_partial_alpha_document(257, 129);
    auto& overlay = document.layers().back();
    overlay.set_blend_mode(blend.mode);
    overlay.set_fill_opacity(0.45F);
    if (patchy::blend_mode_has_special_fill(blend.mode)) {
      require_cpu_fallback(document, std::string("special Fill ") + blend.name);
      overlay.set_fill_opacity(1.0F);
      compose_full_and_check(backend, document, std::string("Fill 100% ") + blend.name);
      continue;
    }
    compose_full_and_check(backend, document, std::string("Fill 45% ") + blend.name);
    overlay.set_opacity(0.7F);
    compose_full_and_check(backend, document, std::string("Fill 45% opacity 70% ") + blend.name);
  }
}

// Masks and Blend If combined with a non-Normal mode and partial alpha, so the
// mask multiply, Blend If factor and blend math are exercised together.
void masked_blend_if_partial_alpha_matches_cpu(patchy::ui::WebGpuRenderBackend& backend) {
  auto document = make_partial_alpha_document(257, 257);
  auto& overlay = document.layers().back();
  overlay.set_blend_mode(patchy::BlendMode::Screen);
  overlay.set_opacity(0.8F);
  const auto bounds = overlay.bounds();
  patchy::LayerMask mask;
  mask.bounds = bounds;
  mask.pixels = patchy::PixelBuffer(bounds.width, bounds.height, patchy::PixelFormat::gray8());
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
    }
  }
  mask.default_color = 255;
  mask.density = 200;
  overlay.set_mask(std::move(mask));
  patchy::LayerBlendIf settings;
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Gray)].underlying_layer =
      patchy::BlendIfThresholds{20, 70, 180, 240};
  settings.channels[static_cast<std::size_t>(patchy::BlendIfChannel::Red)].this_layer =
      patchy::BlendIfThresholds{0, 40, 200, 255};
  if (!overlay.set_blend_if(settings)) {
    throw std::runtime_error("could not install combined Blend If fixture");
  }
  compose_full_and_check(backend, document, "gray mask + Blend If + partial alpha + Screen 80%");
}

void dirty_regions_recompute_only_intersecting_tiles(patchy::ui::WebGpuRenderBackend& backend) {
  auto document = make_document(513, 257);
  QImage full_frame;
  QString reason;
  if (!backend.compose(gpu_document_from(document), full_frame, &reason)) {
    throw std::runtime_error("dirty-region initial composition failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 6U);
  CHECK(backend.last_submitted_pass_count() == 18U);
  CHECK(backend.last_composition_metrics().queue_submissions == 1U);
  CHECK(backend.last_composition_metrics().queue_waits == 1U);

  auto& overlay = document.layers().back();
  overlay.pixels().pixel(204, 80)[0] = 12;
  const auto changed_snapshot = gpu_document_from(document);
  const QRegion dirty_region(QRect(300, 120, 1, 1));
  QImage incremental_frame;
  if (!backend.compose_incremental(changed_snapshot, dirty_region, full_frame, incremental_frame, &reason)) {
    throw std::runtime_error("dirty-region composition failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 1U);
  CHECK(backend.last_submitted_pass_count() == 3U);
  CHECK(backend.last_readback_bytes() == regional_readback_bytes(256, 256));
  CHECK(backend.last_composition_metrics().queue_submissions == 1U);
  CHECK(backend.last_composition_metrics().queue_waits == 1U);
  require_equivalent(document, incremental_frame, "dirty-region frame");

  QImage idle_frame;
  if (!backend.compose_incremental(changed_snapshot, QRegion{}, incremental_frame, idle_frame, &reason)) {
    throw std::runtime_error("empty dirty-region composition failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 0U);
  CHECK(backend.last_submitted_pass_count() == 0U);
  CHECK(backend.last_readback_bytes() == 0U);
  CHECK(idle_frame == incremental_frame);
}

void device_loss_recovery_rebuilds_dawn_resources() {
  struct InjectionEnvironmentReset final {
    ~InjectionEnvironmentReset() { qunsetenv("PATCHY_WEBGPU_INJECT_DEVICE_LOSS"); }
  } injection_environment_reset;

  auto document = make_document(513, 257);
  patchy::ui::WebGpuRenderBackend backend;
  if (!backend.initialize()) {
    throw std::runtime_error("device-loss backend initialization failed: " + std::string(backend.last_error()));
  }

  const auto initial_snapshot = gpu_document_from(document);
  QImage initial_frame;
  QString reason;
  if (!backend.compose(initial_snapshot, initial_frame, &reason)) {
    throw std::runtime_error("device-loss baseline composition failed: " + reason.toStdString());
  }
  require_equivalent(document, initial_frame, "device-loss baseline frame");

  qputenv("PATCHY_WEBGPU_INJECT_DEVICE_LOSS", QByteArrayLiteral("before-submit"));
  QImage failed_frame = initial_frame;
  CHECK(!backend.compose(initial_snapshot, failed_frame, &reason));
  qunsetenv("PATCHY_WEBGPU_INJECT_DEVICE_LOSS");
  CHECK(backend.state() == patchy::GpuBackendState::Lost);
  CHECK(failed_frame == initial_frame);
  CHECK(reason.contains(QStringLiteral("controlled WebGPU device loss")));
  CHECK(backend.recover());
  CHECK(backend.state() == patchy::GpuBackendState::Ready);
  CHECK(backend.last_composition_metrics().queue_submissions == 0U);

  QImage recovered_frame;
  if (!backend.compose(initial_snapshot, recovered_frame, &reason)) {
    throw std::runtime_error("device-loss submit recovery failed: " + reason.toStdString());
  }
  require_equivalent(document, recovered_frame, "device-loss submit recovery frame");

  auto& overlay = document.layers().back();
  overlay.pixels().pixel(204, 80)[0] = 12;
  const auto changed_snapshot = gpu_document_from(document);
  const QRegion dirty_region(QRect(300, 120, 1, 1));
  qputenv("PATCHY_WEBGPU_INJECT_DEVICE_LOSS", QByteArrayLiteral("before-readback"));
  QImage failed_incremental = recovered_frame;
  CHECK(!backend.compose_incremental(changed_snapshot, dirty_region, recovered_frame, failed_incremental, &reason));
  qunsetenv("PATCHY_WEBGPU_INJECT_DEVICE_LOSS");
  CHECK(backend.state() == patchy::GpuBackendState::Lost);
  CHECK(failed_incremental == recovered_frame);
  CHECK(reason.contains(QStringLiteral("controlled WebGPU device loss")));
  CHECK(backend.recover());

  QImage recovered_incremental;
  if (!backend.compose_incremental(changed_snapshot, dirty_region, recovered_frame, recovered_incremental, &reason)) {
    throw std::runtime_error("device-loss readback recovery failed: " + reason.toStdString());
  }
  require_equivalent(document, recovered_incremental, "device-loss readback recovery frame");
  CHECK(backend.last_rendered_tile_count() == 1U);
  CHECK(backend.last_composition_metrics().source_upload_bytes > 0U);
}

// A zero frame budget makes the first queue wait expire immediately. That
// exercises the real timeout path (no injected error): the composition fails
// without exposing a frame, the backend reports Lost, recovery works once,
// and after kMaxConsecutiveWaitTimeouts the backend refuses to recover so the
// canvas stops paying the budget on every repaint. Restoring the budget must
// let a fresh backend compose again.
void wait_budget_timeout_fails_fast_and_disables_after_repeats() {
  struct BudgetEnvironmentReset final {
    ~BudgetEnvironmentReset() { qunsetenv("PATCHY_WEBGPU_FRAME_BUDGET_MS"); }
  } budget_environment_reset;
  auto document = make_document(513, 257);
  const auto snapshot = gpu_document_from(document);
  patchy::ui::WebGpuRenderBackend backend;
  if (!backend.initialize()) {
    throw std::runtime_error("wait-budget backend initialization failed: " + std::string(backend.last_error()));
  }
  QImage baseline;
  QString reason;
  if (!backend.compose(snapshot, baseline, &reason)) {
    throw std::runtime_error("wait-budget baseline composition failed: " + reason.toStdString());
  }
  CHECK(backend.consecutive_wait_timeouts() == 0U);
  CHECK(backend.last_composition_metrics().wait_timeouts == 0U);

  qputenv("PATCHY_WEBGPU_FRAME_BUDGET_MS", QByteArrayLiteral("0"));
  const auto first_start = std::chrono::steady_clock::now();
  QImage untouched = baseline;
  CHECK(!backend.compose(snapshot, untouched, &reason));
  const auto first_elapsed = std::chrono::steady_clock::now() - first_start;
  CHECK(first_elapsed < std::chrono::seconds(5));
  CHECK(untouched == baseline);
  CHECK(backend.state() == patchy::GpuBackendState::Lost);
  CHECK(reason.contains(QStringLiteral("wait budget")));
  CHECK(backend.last_composition_metrics().wait_timeouts >= 1U);
  CHECK(backend.consecutive_wait_timeouts() == 1U);

  CHECK(backend.recover());
  CHECK(backend.state() == patchy::GpuBackendState::Ready);
  CHECK(!backend.compose(snapshot, untouched, &reason));
  CHECK(untouched == baseline);
  CHECK(backend.consecutive_wait_timeouts() == patchy::ui::WebGpuRenderBackend::kMaxConsecutiveWaitTimeouts);

  CHECK(!backend.recover());
  CHECK(backend.state() == patchy::GpuBackendState::Failed);
  CHECK(QString::fromStdString(std::string(backend.last_error())).contains(QStringLiteral("consecutive")));

  qunsetenv("PATCHY_WEBGPU_FRAME_BUDGET_MS");
  patchy::ui::WebGpuRenderBackend fresh;
  if (!fresh.initialize()) {
    throw std::runtime_error("wait-budget fresh backend initialization failed: " + std::string(fresh.last_error()));
  }
  QImage recovered;
  if (!fresh.compose(snapshot, recovered, &reason)) {
    throw std::runtime_error("wait-budget composition after restoring the budget failed: " + reason.toStdString());
  }
  CHECK(recovered == baseline);
  CHECK(fresh.consecutive_wait_timeouts() == 0U);
}

std::uint64_t cpu_reference_time_ns(const patchy::Document& document) {
  const auto start = std::chrono::steady_clock::now();
  const auto frame = cpu_frame(document);
  if (frame.empty()) {
    throw std::runtime_error("CPU reference compositor returned an empty frame");
  }
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

void print_benchmark_metrics(const char* scenario, const patchy::ui::WebGpuRenderBackend& backend,
                             std::uint64_t cpu_ns) {
  const auto metrics = backend.last_composition_metrics();
  std::cout << "[METRIC] scenario=" << scenario << " tiles=" << backend.last_rendered_tile_count()
            << " readback_bytes=" << backend.last_readback_bytes()
            << " source_upload_bytes=" << metrics.source_upload_bytes
            << " mask_upload_bytes=" << metrics.mask_upload_bytes
            << " clear_upload_bytes=" << metrics.clear_upload_bytes
            << " source_reuses=" << metrics.source_texture_reuses
            << " mask_reuses=" << metrics.mask_texture_reuses
            << " scratch_reuses=" << metrics.scratch_texture_reuses
            << " uniform_reuses=" << metrics.uniform_buffer_reuses
            << " readback_reuses=" << metrics.readback_buffer_reuses
            << " bind_group_reuses=" << metrics.bind_group_reuses
            << " queue_submissions=" << metrics.queue_submissions
            << " queue_waits=" << metrics.queue_waits
            << " dawn_ns=" << metrics.composition_time_ns << " cpu_ns=" << cpu_ns << '\n';
}

void benchmark_full_dirty_and_idle(patchy::ui::WebGpuRenderBackend& backend) {
  auto document = make_document(513, 257);
  const auto snapshot = gpu_document_from(document);
  QString reason;
  QImage full_frame;
  if (!backend.compose(snapshot, full_frame, &reason)) {
    throw std::runtime_error("benchmark full frame failed: " + reason.toStdString());
  }
  require_equivalent(document, full_frame, "benchmark full frame");
  print_benchmark_metrics("full_cold", backend, cpu_reference_time_ns(document));

  QImage warm_full_frame;
  if (!backend.compose(snapshot, warm_full_frame, &reason)) {
    throw std::runtime_error("benchmark warm full frame failed: " + reason.toStdString());
  }
  require_equivalent(document, warm_full_frame, "benchmark warm full frame");
  const auto warm_metrics = backend.last_composition_metrics();
  CHECK(warm_metrics.source_upload_bytes == 0U);
  CHECK(warm_metrics.mask_upload_bytes == 0U);
  CHECK(warm_metrics.source_texture_reuses >= 2U);
  CHECK(warm_metrics.mask_texture_reuses >= 2U);
  CHECK(warm_metrics.scratch_texture_reuses > 0U);
  CHECK(warm_metrics.uniform_buffer_reuses >= 2U);
  CHECK(warm_metrics.readback_buffer_reuses > 0U);
  CHECK(warm_metrics.bind_group_reuses > 0U);
  CHECK(warm_metrics.queue_submissions == 1U);
  CHECK(warm_metrics.queue_waits == 1U);
  print_benchmark_metrics("full_warm", backend, cpu_reference_time_ns(document));

  auto& overlay = document.layers().back();
  overlay.pixels().pixel(204, 80)[0] = 12;
  const auto single_dirty_snapshot = gpu_document_from(document);
  QImage single_dirty_frame;
  if (!backend.compose_incremental(single_dirty_snapshot, QRegion(QRect(300, 120, 1, 1)), warm_full_frame,
                                   single_dirty_frame, &reason)) {
    throw std::runtime_error("benchmark single dirty tile failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 1U);
  const auto single_dirty_metrics = backend.last_composition_metrics();
  CHECK(single_dirty_metrics.source_upload_bytes > 0U);
  CHECK(single_dirty_metrics.mask_upload_bytes == 0U);
  CHECK(single_dirty_metrics.queue_submissions == 1U);
  CHECK(single_dirty_metrics.queue_waits == 1U);
  require_equivalent(document, single_dirty_frame, "benchmark single dirty tile");
  print_benchmark_metrics("dirty_single", backend, cpu_reference_time_ns(document));

  overlay.pixels().pixel(20, 20)[1] = 19;
  overlay.pixels().pixel(204, 80)[2] = 23;
  const auto multi_dirty_snapshot = gpu_document_from(document);
  QRegion multi_dirty;
  multi_dirty += QRect(10, 10, 1, 1);
  multi_dirty += QRect(300, 120, 1, 1);
  QImage multi_dirty_frame;
  if (!backend.compose_incremental(multi_dirty_snapshot, multi_dirty, single_dirty_frame, multi_dirty_frame,
                                   &reason)) {
    throw std::runtime_error("benchmark multiple dirty tiles failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 2U);
  CHECK(backend.last_composition_metrics().source_upload_bytes > 0U);
  CHECK(backend.last_composition_metrics().mask_upload_bytes == 0U);
  CHECK(backend.last_composition_metrics().queue_submissions == 1U);
  CHECK(backend.last_composition_metrics().queue_waits == 1U);
  require_equivalent(document, multi_dirty_frame, "benchmark multiple dirty tiles");
  print_benchmark_metrics("dirty_multiple", backend, cpu_reference_time_ns(document));

  QImage idle_frame;
  if (!backend.compose_incremental(multi_dirty_snapshot, QRegion{}, multi_dirty_frame, idle_frame, &reason)) {
    throw std::runtime_error("benchmark idle frame failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 0U);
  CHECK(backend.last_readback_bytes() == 0U);
  CHECK(idle_frame == multi_dirty_frame);
  print_benchmark_metrics("idle", backend, cpu_reference_time_ns(document));
}

// Representative end-to-end benchmark (discussion #37). Unlike the 513x257
// two-layer scenarios above it measures what the canvas actually pays per
// repaint on realistic documents: snapshot construction through
// CanvasWidget::gpu_document_snapshot() (layer/mask conversion and the image
// cache), Dawn composition including readback, and the CPU reference. Each
// scenario reports median latency over several iterations plus the process
// RSS delta, so a per-document result can be compared honestly. The numbers
// are printed, not asserted: this is evidence, not a speed-up claim.
struct RepresentativeScenario {
  const char* name;
  std::int32_t width;
  std::int32_t height;
  int layer_count;
  bool masks;
  bool mixed_blend_modes;
};

constexpr std::array<RepresentativeScenario, 4> kRepresentativeScenarios{{
    {"large_2_layers_4096x2304", 4096, 2304, 2, false, false},
    {"medium_24_layers_2048x1536", 2048, 1536, 24, false, false},
    {"medium_24_layers_masks_blends_2048x1536", 2048, 1536, 24, true, true},
    {"many_64_layers_1024x1024", 1024, 1024, 64, true, true},
}};

patchy::Document make_representative_document(const RepresentativeScenario& scenario) {
  constexpr std::array<patchy::BlendMode, 6> kModes{
      patchy::BlendMode::Normal,   patchy::BlendMode::Multiply, patchy::BlendMode::Screen,
      patchy::BlendMode::Overlay,  patchy::BlendMode::Darken,   patchy::BlendMode::Lighten,
  };
  auto document = make_document(scenario.width, scenario.height);
  // make_document() adds Background and one Overlay; add the rest as
  // mid-sized layers spread over the canvas so every tile sees several.
  for (int index = 2; index < scenario.layer_count; ++index) {
    const auto layer_width = std::max<std::int32_t>(64, scenario.width / 3);
    const auto layer_height = std::max<std::int32_t>(64, scenario.height / 3);
    patchy::PixelBuffer pixels(layer_width, layer_height, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < layer_height; ++y) {
      for (std::int32_t x = 0; x < layer_width; ++x) {
        auto* pixel = pixels.pixel(x, y);
        pixel[0] = static_cast<std::uint8_t>((index * 37 + x) % 251);
        pixel[1] = static_cast<std::uint8_t>((index * 59 + y) % 251);
        pixel[2] = static_cast<std::uint8_t>((index * 83 + x / 2 + y / 2) % 251);
        pixel[3] = static_cast<std::uint8_t>(((x + y) % 9 == 0) ? 0 : 128 + (index * 13) % 128);
      }
    }
    patchy::Layer layer(document.allocate_layer_id(), "Layer " + std::to_string(index), std::move(pixels));
    const auto x0 = static_cast<std::int32_t>((index * 173) % std::max<std::int32_t>(1, scenario.width - layer_width));
    const auto y0 = static_cast<std::int32_t>((index * 97) % std::max<std::int32_t>(1, scenario.height - layer_height));
    layer.set_bounds(patchy::Rect{x0, y0, layer_width, layer_height});
    if (scenario.mixed_blend_modes) {
      layer.set_blend_mode(kModes[static_cast<std::size_t>(index) % kModes.size()]);
      layer.set_opacity(0.55F + 0.45F * static_cast<float>(index % 3) / 2.0F);
    }
    if (scenario.masks && index % 2 == 0) {
      patchy::LayerMask mask;
      mask.bounds = layer.bounds();
      mask.pixels = patchy::PixelBuffer(layer_width, layer_height, patchy::PixelFormat::gray8());
      for (std::int32_t y = 0; y < layer_height; ++y) {
        for (std::int32_t x = 0; x < layer_width; ++x) {
          mask.pixels.pixel(x, y)[0] = static_cast<std::uint8_t>((x * 5 + y * 3 + index) % 256);
        }
      }
      layer.set_mask(std::move(mask));
    }
    document.add_layer(std::move(layer));
  }
  return document;
}

// Resident/peak memory in MB for the benchmark record. Reads /proc directly on
// Linux (VmRSS, VmHWM) and falls back to the shared ui probe elsewhere; -1
// when unavailable.
struct ProcessMemoryMb {
  long long resident{-1};
  long long peak{-1};
};

ProcessMemoryMb process_memory_mb() {
  ProcessMemoryMb result;
#if defined(__linux__)
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    const auto parse_kb = [&line](const char* key) -> long long {
      if (line.rfind(key, 0) != 0) {
        return -1;
      }
      long long kb = 0;
      std::istringstream fields(line.substr(std::strlen(key)));
      fields >> kb;
      return fields ? kb / 1024 : -1;
    };
    if (const auto rss = parse_kb("VmRSS:"); rss >= 0) {
      result.resident = rss;
    }
    if (const auto hwm = parse_kb("VmHWM:"); hwm >= 0) {
      result.peak = hwm;
    }
  }
#endif
  if (result.resident < 0) {
    result.resident = patchy::ui::current_process_memory_mb();
  }
  if (result.peak < 0) {
    result.peak = patchy::ui::peak_process_memory_mb();
  }
  return result;
}

std::uint64_t median_ns(std::vector<std::uint64_t> samples) {
  if (samples.empty()) {
    return 0;
  }
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

template <typename Fn>
std::uint64_t time_ns(Fn&& fn) {
  const auto start = std::chrono::steady_clock::now();
  fn();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

void representative_documents_benchmark(patchy::ui::WebGpuRenderBackend& backend) {
  constexpr int kIterations = 5;
  // The interactive path stops at the 250 ms frame budget (GpuWaitBudget) and
  // falls back to the CPU. A benchmark must measure the real duration instead,
  // so it runs under a generous budget and reports, per scenario, whether the
  // warm median would have exceeded the interactive default. The variable is
  // restored afterwards; the compositor reads it on every composition.
  const auto default_frame_budget = patchy::gpu_wait_budget_from_environment().frame;
  const auto previous_budget = qgetenv("PATCHY_WEBGPU_FRAME_BUDGET_MS");
  const bool had_previous_budget = qEnvironmentVariableIsSet("PATCHY_WEBGPU_FRAME_BUDGET_MS");
  qputenv("PATCHY_WEBGPU_FRAME_BUDGET_MS", QByteArrayLiteral("20000"));
  struct RestoreBudget {
    QByteArray previous;
    bool had_previous;
    ~RestoreBudget() {
      if (had_previous) {
        qputenv("PATCHY_WEBGPU_FRAME_BUDGET_MS", previous);
      } else {
        qunsetenv("PATCHY_WEBGPU_FRAME_BUDGET_MS");
      }
    }
  } restore_budget{previous_budget, had_previous_budget};
  for (const auto& scenario : kRepresentativeScenarios) {
    const auto document = make_representative_document(scenario);
    const auto memory_before = process_memory_mb();

    // The canvas is the real snapshot producer; keep one alive across the
    // iterations exactly like the application does, so the layer image cache
    // and the Dawn texture cache are both exercised (cold first, then warm).
    patchy::ui::CanvasWidget canvas;
    canvas.resize(1280, 800);
    canvas.set_document(const_cast<patchy::Document*>(&document));
    canvas.set_zoom(1.0);

    std::vector<std::uint64_t> snapshot_ns;
    std::vector<std::uint64_t> compose_ns;
    std::vector<std::uint64_t> cpu_ns;
    std::uint64_t cold_total_ns = 0;
    QImage frame;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
      patchy::ui::CanvasGpuDocument snapshot;
      QString reason;
      bool accepted = false;
      const auto s_ns = time_ns([&] { accepted = canvas.gpu_document_snapshot(snapshot, &reason); });
      if (!accepted) {
        throw std::runtime_error(std::string("representative scenario rejected by the canvas: ") + scenario.name +
                                 ": " + reason.toStdString());
      }
      const auto c_ns = time_ns([&] {
        if (!backend.compose(snapshot, frame, &reason)) {
          throw std::runtime_error(std::string("representative scenario composition failed: ") + scenario.name +
                                   ": " + reason.toStdString());
        }
      });
      cpu_ns.push_back(cpu_reference_time_ns(document));
      if (iteration == 0) {
        cold_total_ns = s_ns + c_ns;
      } else {
        snapshot_ns.push_back(s_ns);
        compose_ns.push_back(c_ns);
      }
    }
    // Deep stacks drift: the Dawn path stores the running backdrop, including
    // its accumulated alpha, in RGBA8Unorm scratch textures between layer
    // passes, while the CPU compositor keeps a float alpha plane. Each masked,
    // translucent layer therefore re-quantises alpha on the GPU, and with 24
    // or 64 such layers the final bytes deviate by a few levels on a small
    // set of pixels even though every layer's math is CPU-mirrored. The strict
    // <=1 policy stays in the equivalence tests above; here the deviation is
    // reported in the record and only a visible divergence (more than four
    // levels, more than 0.1% of pixels, or a mean above 0.1) fails. Removing
    // the drift needs 16-bit float intermediates; see docs/performance.md.
    require_opaque(frame, std::string("representative ") + scenario.name);
    const auto comparison = patchy::compare_pixel_buffers(cpu_frame(document), pixel_buffer_from_image(frame));
    patchy::PixelComparisonPolicy deep_stack_policy;
    deep_stack_policy.max_channel_delta = 4;
    deep_stack_policy.max_mean_abs_channel_delta = 0.1;
    deep_stack_policy.max_differing_fraction = 0.001;
    deep_stack_policy.max_differing_pixels =
        static_cast<std::uint64_t>(scenario.width) * static_cast<std::uint64_t>(scenario.height) / 1000U;
    if (!comparison.within(deep_stack_policy)) {
      std::ostringstream message;
      message << "representative " << scenario.name << " differs from CPU: max delta=" << comparison.max_channel_delta
              << ", differing pixels=" << comparison.differing_pixels
              << ", mean delta=" << comparison.mean_abs_channel_delta;
      throw std::runtime_error(message.str());
    }
    const auto memory_after = process_memory_mb();
    const auto metrics = backend.last_composition_metrics();
    std::cout << "[METRIC] scenario=representative_" << scenario.name << " layers=" << document.layers().size()
              << " pixels=" << (static_cast<std::uint64_t>(scenario.width) * static_cast<std::uint64_t>(scenario.height))
              << " cold_end_to_end_ns=" << cold_total_ns
              << " warm_snapshot_median_ns=" << median_ns(snapshot_ns)
              << " warm_dawn_median_ns=" << median_ns(compose_ns)
              << " warm_end_to_end_median_ns=" << (median_ns(snapshot_ns) + median_ns(compose_ns))
              << " cpu_median_ns=" << median_ns(cpu_ns)
              << " readback_bytes=" << backend.last_readback_bytes()
              << " warm_source_upload_bytes=" << metrics.source_upload_bytes
              << " wait_timeouts=" << metrics.wait_timeouts
              << " exceeds_default_frame_budget="
              << (std::chrono::nanoseconds(static_cast<std::int64_t>(median_ns(compose_ns))) > default_frame_budget ? 1 : 0)
              << " cpu_gpu_max_delta=" << comparison.max_channel_delta
              << " cpu_gpu_differing_pixels=" << comparison.differing_pixels
              << " rss_before_mb=" << memory_before.resident << " rss_after_mb=" << memory_after.resident
              << " peak_rss_mb=" << memory_after.peak << '\n';
  }
}

int run_test(const char* name, const std::function<void()>& test) {
  try {
    test();
    std::cout << "[PASS] " << name << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
    return 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  // CanvasWidget needs a QApplication; the offscreen platform is enough
  // because only the document snapshot is taken from it, never a painted frame.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  }
  QApplication application(argc, argv);
  patchy::ui::WebGpuRenderBackend backend;
  if (!backend.initialize()) {
    const bool require_hardware = qEnvironmentVariableIntValue("PATCHY_WEBGPU_TESTS_REQUIRE_HARDWARE") != 0;
    std::cout << (require_hardware ? "[FAIL] " : "[SKIP] ") << "Dawn/WebGPU equivalence: " << backend.last_error()
              << '\n';
    std::cout << "Configure with PATCHY_ENABLE_WEBGPU=ON and an installed Dawn prefix, then run on a hardware adapter.\n";
    return require_hardware ? 1 : kSkipExitCode;
  }

  int failures = 0;
  failures += run_test("webgpu_full_frames_match_cpu_for_boundaries",
                       [&backend] { full_frames_match_cpu_for_boundaries(backend); });
  failures += run_test("webgpu_supported_shader_features_match_cpu",
                       [&backend] { supported_shader_features_match_cpu(backend); });
  failures += run_test("webgpu_all_accepted_blend_modes_match_cpu",
                       [&backend] { all_accepted_blend_modes_match_cpu(backend); });
  failures += run_test("webgpu_fill_opacity_matches_cpu_or_stays_on_cpu",
                       [&backend] { fill_opacity_matches_cpu_or_stays_on_cpu(backend); });
  failures += run_test("webgpu_masked_blend_if_partial_alpha_matches_cpu",
                       [&backend] { masked_blend_if_partial_alpha_matches_cpu(backend); });
  failures += run_test("webgpu_dirty_regions_recompute_only_intersecting_tiles",
                       [&backend] { dirty_regions_recompute_only_intersecting_tiles(backend); });
  failures += run_test("webgpu_device_loss_recovery_rebuilds_resources",
                       device_loss_recovery_rebuilds_dawn_resources);
  failures += run_test("webgpu_wait_budget_timeout_fails_fast_and_disables_after_repeats",
                       wait_budget_timeout_fails_fast_and_disables_after_repeats);
  failures += run_test("webgpu_resource_reuse_benchmark",
                       [] {
                         patchy::ui::WebGpuRenderBackend benchmark_backend;
                         if (!benchmark_backend.initialize()) {
                           throw std::runtime_error("benchmark backend initialization failed: " +
                                                    std::string(benchmark_backend.last_error()));
                         }
                         benchmark_full_dirty_and_idle(benchmark_backend);
                       });
  // Opt-in: several seconds on integrated GPUs and hundreds of MB of layers.
  if (patchy::environment_variable_is_set("PATCHY_WEBGPU_REPRESENTATIVE_BENCHMARK")) {
    failures += run_test("webgpu_representative_documents_benchmark",
                         [&backend] { representative_documents_benchmark(backend); });
  }
  return failures == 0 ? 0 : 1;
}
