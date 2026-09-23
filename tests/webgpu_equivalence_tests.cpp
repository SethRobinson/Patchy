#include "core/document.hpp"
#include "core/layer.hpp"
#include "render/compositor.hpp"
#include "render/gpu_document_capabilities.hpp"
#include "render/pixel_comparison.hpp"
#include "test_harness.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/webgpu_render_backend.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QRegion>
#include <QString>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::int32_t kTileSize = 256;

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

patchy::ui::CanvasGpuDocument gpu_document_from(const patchy::Document& document) {
  patchy::ui::CanvasGpuDocument result;
  result.document_size = QSize(document.width(), document.height());
  result.canvas_rect = QRectF(0.0, 0.0, document.width(), document.height());
  const auto capability = patchy::gpu_document_capability(document);
  result.shader_composition = capability.mode == patchy::GpuDocumentRenderMode::PixelStackShader;
  result.layers.reserve(document.layers().size());

  for (const auto& layer : document.layers()) {
    if (!layer.visible() || layer.opacity() <= 0.0F) {
      continue;
    }

    patchy::ui::CanvasGpuLayer gpu_layer;
    gpu_layer.id = layer.id();
    gpu_layer.revision = layer.render_revision();
    gpu_layer.pixel_revision = layer.pixel_revision();
    gpu_layer.content_revision = layer.content_revision();
    gpu_layer.mask_revision = layer.mask_revision();
    gpu_layer.image = patchy::ui::qimage_from_pixel_buffer(layer.pixels());
    const auto bounds = layer.bounds();
    gpu_layer.document_rect = QRectF(bounds.x, bounds.y, bounds.width, bounds.height);
    gpu_layer.rect = gpu_layer.document_rect;
    gpu_layer.opacity = static_cast<qreal>(std::clamp(layer.opacity() * layer.fill_opacity(), 0.0F, 1.0F));
    gpu_layer.blend_mode = static_cast<int>(layer.blend_mode());

    const auto blend_if_status = layer.blend_if_payload_status();
    if (blend_if_status == patchy::BlendIfPayloadStatus::Supported &&
        !patchy::blend_if_is_identity(layer.blend_if())) {
      const auto settings = layer.blend_if();
      gpu_layer.has_blend_if = true;
      for (std::size_t index = 0; index < settings.channels.size(); ++index) {
        const auto copy_thresholds = [](const patchy::BlendIfThresholds& thresholds) {
          return patchy::ui::CanvasGpuBlendIfThresholds{thresholds.black_low, thresholds.black_high,
                                                        thresholds.white_low, thresholds.white_high};
        };
        gpu_layer.blend_if[index] =
            patchy::ui::CanvasGpuBlendIfRanges{copy_thresholds(settings.channels[index].this_layer),
                                               copy_thresholds(settings.channels[index].underlying_layer)};
      }
    }

    if (layer.mask().has_value() && !layer.mask()->disabled) {
      const auto& mask = *layer.mask();
      gpu_layer.has_mask = true;
      if (!mask.pixels.empty()) {
        gpu_layer.mask_image = QImage(mask.pixels.width(), mask.pixels.height(), QImage::Format_Grayscale8);
        for (int y = 0; y < mask.pixels.height(); ++y) {
          std::memcpy(gpu_layer.mask_image.scanLine(y), mask.pixels.row(y).data(),
                      static_cast<std::size_t>(mask.pixels.width()));
        }
        gpu_layer.mask_document_rect = QRectF(mask.bounds.x, mask.bounds.y, mask.bounds.width, mask.bounds.height);
        gpu_layer.mask_rect = gpu_layer.mask_document_rect;
      }
      gpu_layer.mask_default = static_cast<qreal>(mask.default_color) / 255.0;
      gpu_layer.mask_density = static_cast<qreal>(mask.density) / 255.0;
    }
    result.layers.push_back(std::move(gpu_layer));
  }
  return result;
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

void dirty_regions_recompute_only_intersecting_tiles(patchy::ui::WebGpuRenderBackend& backend) {
  auto document = make_document(513, 257);
  QImage full_frame;
  QString reason;
  if (!backend.compose(gpu_document_from(document), full_frame, &reason)) {
    throw std::runtime_error("dirty-region initial composition failed: " + reason.toStdString());
  }
  CHECK(backend.last_rendered_tile_count() == 6U);
  CHECK(backend.last_submitted_pass_count() == 18U);

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
  QCoreApplication application(argc, argv);
  patchy::ui::WebGpuRenderBackend backend;
  if (!backend.initialize()) {
    std::cout << "[SKIP] Dawn/WebGPU equivalence: " << backend.last_error() << '\n';
    std::cout << "Configure with PATCHY_ENABLE_WEBGPU=ON and an installed Dawn prefix, then run on a hardware adapter.\n";
    return 0;
  }

  int failures = 0;
  failures += run_test("webgpu_full_frames_match_cpu_for_boundaries",
                       [&backend] { full_frames_match_cpu_for_boundaries(backend); });
  failures += run_test("webgpu_supported_shader_features_match_cpu",
                       [&backend] { supported_shader_features_match_cpu(backend); });
  failures += run_test("webgpu_dirty_regions_recompute_only_intersecting_tiles",
                       [&backend] { dirty_regions_recompute_only_intersecting_tiles(backend); });
  failures += run_test("webgpu_resource_reuse_benchmark",
                       [] {
                         patchy::ui::WebGpuRenderBackend benchmark_backend;
                         if (!benchmark_backend.initialize()) {
                           throw std::runtime_error("benchmark backend initialization failed: " +
                                                    std::string(benchmark_backend.last_error()));
                         }
                         benchmark_full_dirty_and_idle(benchmark_backend);
                       });
  return failures == 0 ? 0 : 1;
}
