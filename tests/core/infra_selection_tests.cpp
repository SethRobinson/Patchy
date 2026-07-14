#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/blend_math.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "core/gradient_presets.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_registry.hpp"
#include "formats/gif_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/ilbm_document_io.hpp"
#include "formats/image_density_probe.hpp"
#include "formats/palette_io.hpp"
#include "formats/pcx_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "formats/raw_tone.hpp"
#include "formats/raw_white_balance.hpp"
#include "formats/tga_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/plugin_host.hpp"
#include "psd/abr_reader.hpp"
#include "psd/grd_io.hpp"
#include "psd/asl_io.hpp"
#include "psd/pat_reader.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "psd/psd_document_io.hpp"
#include "core/contour_presets.hpp"
#include "core/magnetic_lasso.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_contour.hpp"
#include "core/style_presets.hpp"
#include "core/pixel_tools.hpp"
#include "core/quick_select.hpp"
#include "render/compositor.hpp"
#include "render/layer_compositor.hpp"
#include "render/tile_cache.hpp"
#include "support/string_utils.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "synthetic_dng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <exception>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_test_support.hpp"
#include "test_groups.hpp"

namespace {

using patchy::test::solid_rgb;

void plugin_host_and_legacy_probe_work() {
  patchy::PluginHost host;
  host.register_plugin({PATCHY_PLUGIN_FILTER, "com.patchy.test", "Test", 1, 0, 0, {}});
  CHECK(host.find("com.patchy.test") != nullptr);
  CHECK(host.plugins_by_kind(PATCHY_PLUGIN_FILTER).size() == 1);

  const auto fixture = std::filesystem::path("test_sample_filter.8bf");
  {
    std::vector<std::uint8_t> bytes(512, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = 0x80;
    bytes[0x80] = 'P';
    bytes[0x81] = 'E';
#if defined(_M_IX86) || defined(__i386__)
    bytes[0x84] = 0x4c;
    bytes[0x85] = 0x01;
#elif defined(_M_ARM64) || defined(__aarch64__)
    bytes[0x84] = 0x64;
    bytes[0x85] = 0xaa;
#else
    bytes[0x84] = 0x64;
    bytes[0x85] = 0x86;
#endif
    std::ofstream output(fixture, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  patchy::LegacyPhotoshopAdapter adapter;
  const auto probe = adapter.probe(fixture);
#ifdef _WIN32
  CHECK(probe.supported);
#else
  // Legacy plug-ins are Windows PE binaries; off-Windows the probe rejects them with a
  // platform reason no matter how well the architecture matches the host.
  CHECK(!probe.supported);
#endif
  CHECK(probe.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(!probe.architecture.empty());
  std::filesystem::remove(fixture);

#ifdef PATCHY_SOURCE_DIR
  const auto real_plugin =
      std::filesystem::path(PATCHY_SOURCE_DIR) / "test-fixtures" / "photoshop-plugins" / "Greyscale64.8bf";
  CHECK(std::filesystem::exists(real_plugin));
  const auto real_probe = adapter.probe(real_plugin);
  CHECK(real_probe.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(real_probe.architecture == "x64");
#endif
}

void tile_cache_stores_and_invalidates() {
  patchy::TileCache cache(128);
  patchy::TileKey key{0, 0, 0};
  cache.put(key, solid_rgb(2, 2, 9, 8, 7));
  CHECK(cache.find(key).has_value());
  cache.invalidate(key);
  CHECK(!cache.find(key).has_value());
}

void color_manager_assigns_profiles() {
  patchy::Document document(1, 1, patchy::PixelFormat::rgb8());
  patchy::ColorManager manager;
  manager.assign_icc_profile(document, {1, 2, 3});
  CHECK(document.color_state().embedded_icc_profile.size() == 3);
}

// ---------------------------------------------------------------------------
// Quick Select
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> quick_select_image(std::int32_t width, std::int32_t height,
                                             std::array<std::uint8_t, 4> rgba) {
  std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < image.size(); i += 4) {
    image[i] = rgba[0];
    image[i + 1] = rgba[1];
    image[i + 2] = rgba[2];
    image[i + 3] = rgba[3];
  }
  return image;
}

void quick_select_fill_rect(std::vector<std::uint8_t>& image, std::int32_t width, patchy::Rect rect,
                            std::array<std::uint8_t, 4> rgba) {
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      auto* px = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      px[0] = rgba[0];
      px[1] = rgba[1];
      px[2] = rgba[2];
      px[3] = rgba[3];
    }
  }
}

void quick_select_fill_mask(std::vector<std::uint8_t>& mask, std::int32_t width, patchy::Rect rect,
                            std::uint8_t value) {
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    std::fill_n(mask.data() + static_cast<std::size_t>(y) * width + rect.x,
                static_cast<std::size_t>(rect.width), value);
  }
}

void quick_select_apply_delta(std::vector<std::uint8_t>& selection, std::int32_t width,
                              const patchy::QuickSelectResult& result, bool subtract) {
  for (const auto& run : result.delta_runs) {
    for (std::int32_t x = run.x0; x <= run.x1; ++x) {
      selection[static_cast<std::size_t>(run.y) * width + x] = subtract ? 0 : 255;
    }
  }
}

int quick_select_count_in_rect(const std::vector<std::uint8_t>& mask, std::int32_t width, patchy::Rect rect) {
  int count = 0;
  for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
    for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) {
      count += mask[static_cast<std::size_t>(y) * width + x] != 0 ? 1 : 0;
    }
  }
  return count;
}

void quick_select_maxflow_solves_tiny_grid() {
  // Three nodes in a row: source-(5)->n0 -(2)- n1 -(4)- n2 -(5)->sink. The only path
  // bottlenecks on the 2-capacity arc, so flow = 2 and the cut separates n0 from n1/n2.
  patchy::detail::GridMaxflow graph(3, 1);
  graph.set_terminal_caps(0, 5.0f, 0.0f);
  graph.set_terminal_caps(2, 0.0f, 5.0f);
  graph.set_neighbor_cap(0, 1, 2.0f);
  graph.set_neighbor_cap(1, 2, 4.0f);
  const double flow = graph.solve();
  CHECK(std::abs(flow - 2.0) < 1e-6);
  CHECK(graph.is_source_side(0));
  CHECK(!graph.is_source_side(1));
  CHECK(!graph.is_source_side(2));
}

// Reference max-flow (Edmonds-Karp over an explicit capacity matrix) used to validate the
// Boykov-Kolmogorov implementation on small random grids.
double quick_select_reference_maxflow(std::vector<std::vector<double>> capacity, int source, int sink) {
  const int node_count = static_cast<int>(capacity.size());
  double flow = 0.0;
  while (true) {
    std::vector<int> parent(node_count, -1);
    parent[source] = source;
    std::vector<int> queue{source};
    for (std::size_t head = 0; head < queue.size() && parent[sink] < 0; ++head) {
      const int node = queue[head];
      for (int next = 0; next < node_count; ++next) {
        if (parent[next] < 0 && capacity[node][next] > 1e-12) {
          parent[next] = node;
          queue.push_back(next);
        }
      }
    }
    if (parent[sink] < 0) {
      return flow;
    }
    double bottleneck = std::numeric_limits<double>::max();
    for (int node = sink; node != source; node = parent[node]) {
      bottleneck = std::min(bottleneck, capacity[parent[node]][node]);
    }
    for (int node = sink; node != source; node = parent[node]) {
      capacity[parent[node]][node] -= bottleneck;
      capacity[node][parent[node]] += bottleneck;
    }
    flow += bottleneck;
  }
}

void quick_select_maxflow_matches_reference_on_random_grids() {
  std::uint32_t rng_state = 0xC0FFEE01u;
  const auto next_random = [&rng_state]() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
  };
  for (int round = 0; round < 60; ++round) {
    const std::int32_t width = 2 + static_cast<std::int32_t>(next_random() % 4);   // 2..5
    const std::int32_t height = 2 + static_cast<std::int32_t>(next_random() % 4);  // 2..5
    const int nodes = width * height;
    const int source = nodes;
    const int sink = nodes + 1;
    std::vector<std::vector<double>> capacity(static_cast<std::size_t>(nodes) + 2,
                                              std::vector<double>(static_cast<std::size_t>(nodes) + 2, 0.0));
    patchy::detail::GridMaxflow graph(width, height);

    std::vector<double> source_caps(static_cast<std::size_t>(nodes));
    std::vector<double> sink_caps(static_cast<std::size_t>(nodes));
    for (int node = 0; node < nodes; ++node) {
      // Integer-valued capacities keep every intermediate float sum exact.
      const auto source_cap = static_cast<double>(next_random() % 11);
      const auto sink_cap = static_cast<double>(next_random() % 11);
      source_caps[static_cast<std::size_t>(node)] = source_cap;
      sink_caps[static_cast<std::size_t>(node)] = sink_cap;
      graph.set_terminal_caps(node, static_cast<float>(source_cap), static_cast<float>(sink_cap));
      capacity[static_cast<std::size_t>(source)][static_cast<std::size_t>(node)] = source_cap;
      capacity[static_cast<std::size_t>(node)][static_cast<std::size_t>(sink)] = sink_cap;
    }
    const auto connect = [&](int a, int b) {
      const auto cap = static_cast<double>(next_random() % 11);
      graph.set_neighbor_cap(a, b, static_cast<float>(cap));
      capacity[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = cap;
      capacity[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = cap;
    };
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const int node = y * width + x;
        if (x + 1 < width) {
          connect(node, node + 1);
        }
        if (y + 1 < height) {
          connect(node, node + width);
          if (x + 1 < width) {
            connect(node, node + width + 1);
          }
          if (x > 0) {
            connect(node, node + width - 1);
          }
        }
      }
    }

    const double expected = quick_select_reference_maxflow(capacity, source, sink);
    const double flow = graph.solve();
    CHECK(std::abs(flow - expected) < 1e-3);

    // The labeling must describe a cut whose value equals the max flow (min-cut duality).
    double cut = 0.0;
    for (int node = 0; node < nodes; ++node) {
      if (graph.is_source_side(node)) {
        cut += sink_caps[static_cast<std::size_t>(node)];
      } else {
        cut += source_caps[static_cast<std::size_t>(node)];
      }
    }
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const int node = y * width + x;
        const auto arc_across = [&](int other) {
          if (graph.is_source_side(node) && !graph.is_source_side(other)) {
            cut += capacity[static_cast<std::size_t>(node)][static_cast<std::size_t>(other)];
          } else if (!graph.is_source_side(node) && graph.is_source_side(other)) {
            cut += capacity[static_cast<std::size_t>(other)][static_cast<std::size_t>(node)];
          }
        };
        if (x + 1 < width) {
          arc_across(node + 1);
        }
        if (y + 1 < height) {
          arc_across(node + width);
          if (x + 1 < width) {
            arc_across(node + width + 1);
          }
          if (x > 0) {
            arc_across(node + width - 1);
          }
        }
      }
    }
    CHECK(std::abs(cut - expected) < 1e-3);
  }
}

void quick_select_stroke_grabs_flat_region_and_respects_edges() {
  const std::int32_t width = 200;
  const std::int32_t height = 150;
  const patchy::Rect object{60, 40, 80, 70};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, object, {200, 30, 20, 255});
  // Scatter background noise the segmentation must not leak through.
  std::uint32_t noise_state = 0xBADD5EEDu;
  for (int i = 0; i < 30; ++i) {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    const std::int32_t x = static_cast<std::int32_t>(noise_state % width);
    const std::int32_t y = static_cast<std::int32_t>((noise_state >> 8) % height);
    if (!object.contains(x, y)) {
      quick_select_fill_rect(image, width, patchy::Rect{x, y, 1, 1}, {90, 90, 90, 255});
    }
  }

  // The stroke covers the object's middle; the remaining margin to the object edges stays
  // within the spread budget (growth is bounded like Photoshop's, so a tiny stroke in a huge
  // shape deliberately does NOT grab far corners).
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect seed_rect{70, 50, 60, 50};
  quick_select_fill_mask(seeds, width, seed_rect, 255);

  patchy::QuickSelectParams params;
  params.brush_radius = 10;
  const auto result = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, nullptr,
                                                   seeds.data(), seed_rect, params);
  CHECK(!result.empty());

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_apply_delta(selection, width, result, false);
  const int object_hits = quick_select_count_in_rect(selection, width, object);
  const int total_hits = quick_select_count_in_rect(selection, width, patchy::Rect::from_size(width, height));
  const int object_area = object.width * object.height;
  const int background_area = width * height - object_area;
  CHECK(object_hits >= object_area * 95 / 100);
  CHECK(total_hits - object_hits <= background_area / 100);
}

void quick_select_second_stroke_adds_monotonically() {
  const std::int32_t width = 200;
  const std::int32_t height = 150;
  const patchy::Rect object{60, 40, 80, 70};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, object, {40, 90, 200, 255});

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  patchy::QuickSelectParams params;
  params.brush_radius = 8;

  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect first_seed{65, 45, 25, 60};
  quick_select_fill_mask(seeds, width, first_seed, 255);
  const auto first = patchy::quick_select_segment(image.data(), width, height,
                                                  static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                  seeds.data(), first_seed, params);
  CHECK(!first.empty());
  quick_select_apply_delta(selection, width, first, false);
  const int after_first = quick_select_count_in_rect(selection, width, object);

  std::fill(seeds.begin(), seeds.end(), std::uint8_t{0});
  const patchy::Rect second_seed{110, 45, 25, 60};
  quick_select_fill_mask(seeds, width, second_seed, 255);
  const auto second = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                   seeds.data(), second_seed, params);
  // The second stroke may only add pixels that were not already selected.
  for (const auto& run : second.delta_runs) {
    for (std::int32_t x = run.x0; x <= run.x1; ++x) {
      CHECK(selection[static_cast<std::size_t>(run.y) * width + x] == 0);
    }
  }
  quick_select_apply_delta(selection, width, second, false);
  const int after_second = quick_select_count_in_rect(selection, width, object);
  CHECK(after_second >= after_first);
  CHECK(after_second >= object.width * object.height * 95 / 100);
  const int total = quick_select_count_in_rect(selection, width, patchy::Rect::from_size(width, height));
  CHECK(total - after_second <= (width * height - object.width * object.height) / 100);
}

void quick_select_subtract_removes_only_connected_area() {
  const std::int32_t width = 220;
  const std::int32_t height = 150;
  const patchy::Rect blob_a{20, 20, 80, 80};   // larger than a tap's spread budget
  const patchy::Rect blob_b{150, 80, 40, 40};
  auto image = quick_select_image(width, height, {255, 255, 255, 255});
  quick_select_fill_rect(image, width, blob_a, {30, 60, 180, 255});
  quick_select_fill_rect(image, width, blob_b, {30, 60, 180, 255});

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_fill_mask(selection, width, blob_a, 255);
  quick_select_fill_mask(selection, width, blob_b, 255);

  // A subtract tap in a blob much larger than the spread budget shaves a budget-sized area
  // around the brush, always within the previously-selected blob (Photoshop-like locality).
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect tap_rect{55, 55, 10, 10};
  quick_select_fill_mask(seeds, width, tap_rect, 255);
  patchy::QuickSelectParams params;
  params.brush_radius = 8;
  params.subtract = true;
  const auto tap = patchy::quick_select_segment(image.data(), width, height,
                                                static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                seeds.data(), tap_rect, params);
  CHECK(!tap.empty());
  for (const auto& run : tap.delta_runs) {
    CHECK(run.y >= blob_a.y && run.y < blob_a.y + blob_a.height);
    CHECK(run.x0 >= blob_a.x && run.x1 < blob_a.x + blob_a.width);
  }
  quick_select_apply_delta(selection, width, tap, true);
  const int after_tap = quick_select_count_in_rect(selection, width, blob_a);
  CHECK(after_tap < blob_a.width * blob_a.height);          // something was shaved...
  CHECK(after_tap >= blob_a.width * blob_a.height * 30 / 100);  // ...but a tap must not nuke the blob
  CHECK(quick_select_count_in_rect(selection, width, blob_b) == blob_b.width * blob_b.height);

  // A stroke covering most of the blob removes it entirely (the unbrushed remainder is within
  // the budget and cheaper to drop than to fence off), still without touching blob B.
  quick_select_fill_mask(selection, width, blob_a, 255);  // restore blob A
  std::fill(seeds.begin(), seeds.end(), std::uint8_t{0});
  const patchy::Rect stroke_rect{30, 30, 60, 60};
  quick_select_fill_mask(seeds, width, stroke_rect, 255);
  const auto stroke = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, selection.data(),
                                                   seeds.data(), stroke_rect, params);
  CHECK(!stroke.empty());
  quick_select_apply_delta(selection, width, stroke, true);
  CHECK(quick_select_count_in_rect(selection, width, blob_a) <= blob_a.width * blob_a.height * 5 / 100);
  CHECK(quick_select_count_in_rect(selection, width, blob_b) == blob_b.width * blob_b.height);
}

// Regression for the July 2026 "brushing an eye selects half the face" report: on textured
// photo-like content whose stroke colors also appear in the background samples, the selection
// must hug the brushed object instead of flooding to the solve-window border. (The original
// cause was over-smoothed histograms plus a background-decontamination hack that turned every
// stroke color into strong foreground.)
void quick_select_photo_texture_stroke_stays_local() {
  const std::int32_t width = 260;
  const std::int32_t height = 200;
  std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
  std::uint32_t noise_state = 0x1234ABCDu;
  const auto next_noise = [&noise_state] {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return noise_state;
  };
  const double center_x = 130.0;
  const double center_y = 100.0;
  const double radius_x = 48.0;
  const double radius_y = 32.0;
  const auto ellipse_distance = [&](double x, double y) {
    const double dx = (x - center_x) / radius_x;
    const double dy = (y - center_y) / radius_y;
    return std::sqrt(dx * dx + dy * dy);
  };
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      // Skin-tone background with a horizontal gradient and per-pixel luminance noise.
      const int gradient = x * 30 / width;
      const int jitter = static_cast<int>(next_noise() % 25) - 12;
      const int skin_r = 215 + gradient / 2 + jitter;
      const int skin_g = 170 + gradient / 3 + jitter;
      const int skin_b = 140 + gradient / 3 + jitter;
      // Dark "eye" ellipse with a soft blended rim (a few pixels wide).
      const int dark_r = 70 + jitter / 2;
      const int dark_g = 45 + jitter / 2;
      const int dark_b = 35 + jitter / 2;
      const double edge = std::clamp((ellipse_distance(x, y) - 1.0) * (radius_x / 4.0), 0.0, 1.0);
      auto* px = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
      px[0] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_r + (skin_r - dark_r) * edge)), 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_g + (skin_g - dark_g) * edge)), 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_b + (skin_b - dark_b) * edge)), 0, 255));
      px[3] = 255;
    }
  }

  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(width) * height, 0);
  const patchy::Rect seed_rect{115, 92, 30, 14};  // well inside the ellipse
  quick_select_fill_mask(seeds, width, seed_rect, 255);

  patchy::QuickSelectParams params;
  params.brush_radius = 10;
  const auto result = patchy::quick_select_segment(image.data(), width, height,
                                                   static_cast<std::ptrdiff_t>(width) * 4, nullptr,
                                                   seeds.data(), seed_rect, params);
  CHECK(!result.empty());

  std::vector<std::uint8_t> selection(static_cast<std::size_t>(width) * height, 0);
  quick_select_apply_delta(selection, width, result, false);
  int inside_area = 0;
  int inside_hits = 0;
  int outside_hits = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const double distance = ellipse_distance(x, y);
      const bool selected = selection[static_cast<std::size_t>(y) * width + x] != 0;
      if (distance <= 1.0) {
        ++inside_area;
        inside_hits += selected ? 1 : 0;
      } else if (distance > 1.15 && selected) {
        ++outside_hits;  // beyond the soft rim: this is leakage
      }
    }
  }
  CHECK(inside_hits >= inside_area * 70 / 100);
  CHECK(outside_hits <= width * height * 3 / 100);
}

void quick_select_enhance_edge_smooths_staircase() {
  const std::int32_t width = 60;
  const std::int32_t height = 60;
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
  quick_select_fill_mask(mask, width, patchy::Rect{0, 0, 30, height}, 255);  // straight edge at x=30
  mask[static_cast<std::size_t>(10) * width + 31] = 255;  // 1px spur on the edge
  mask[static_cast<std::size_t>(15) * width + 15] = 0;    // 1px hole inside
  mask[static_cast<std::size_t>(45) * width + 45] = 255;  // isolated island outside

  auto banded = mask;  // band-restricted copy: only the top half may change
  patchy::enhance_selection_edge(mask, width, height, patchy::Rect::from_size(width, height));
  CHECK(mask[static_cast<std::size_t>(10) * width + 31] == 0);   // spur removed
  CHECK(mask[static_cast<std::size_t>(15) * width + 15] == 255); // hole filled
  CHECK(mask[static_cast<std::size_t>(45) * width + 45] == 0);   // island removed
  for (std::int32_t y = 0; y < height; ++y) {                    // straight edge intact
    CHECK(mask[static_cast<std::size_t>(y) * width + 29] == 255);
    CHECK(mask[static_cast<std::size_t>(y) * width + 30] == 0);
  }

  patchy::enhance_selection_edge(banded, width, height, patchy::Rect{0, 0, width, 30});
  CHECK(banded[static_cast<std::size_t>(10) * width + 31] == 0);   // inside band: smoothed
  CHECK(banded[static_cast<std::size_t>(45) * width + 45] == 255); // outside band: untouched
}

std::vector<std::uint8_t> magnetic_two_tone_image(std::int32_t width, std::int32_t height,
                                                  const std::function<bool(std::int32_t, std::int32_t)>& is_light,
                                                  std::uint8_t dark, std::uint8_t light) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const auto value = is_light(x, y) ? light : dark;
      auto* px = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
      px[0] = value;
      px[1] = value;
      px[2] = value;
      px[3] = 255;
    }
  }
  return rgba;
}

std::vector<patchy::PointI32> magnetic_reference_line(patchy::PointI32 from, patchy::PointI32 to) {
  std::vector<patchy::PointI32> line;
  const auto dx = std::abs(to.x - from.x);
  const auto dy = std::abs(to.y - from.y);
  const std::int32_t sx = from.x < to.x ? 1 : -1;
  const std::int32_t sy = from.y < to.y ? 1 : -1;
  std::int32_t error = dx - dy;
  auto point = from;
  while (true) {
    line.push_back(point);
    if (point == to) {
      break;
    }
    const auto doubled = 2 * error;
    if (doubled > -dy) {
      error -= dy;
      point.x += sx;
    }
    if (doubled < dx) {
      error += dx;
      point.y += sy;
    }
  }
  return line;
}

void check_magnetic_path_is_8_connected(const std::vector<patchy::PointI32>& path) {
  CHECK(!path.empty());
  for (std::size_t i = 1; i < path.size(); ++i) {
    CHECK(std::abs(path[i].x - path[i - 1].x) <= 1);
    CHECK(std::abs(path[i].y - path[i - 1].y) <= 1);
    CHECK(!(path[i] == path[i - 1]));
  }
}

void magnetic_lasso_path_snaps_to_synthetic_edge() {
  constexpr std::int32_t size = 128;
  // Triangle-wave boundary, amplitude +-4, slope +-1 per row (integer, deterministic).
  const auto boundary = [](std::int32_t y) { return 64 + std::abs(y % 16 - 8) - 4; };
  const auto rgba =
      magnetic_two_tone_image(size, size, [&](std::int32_t x, std::int32_t y) { return x >= boundary(y); }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  const auto anchor = engine.snap({boundary(8) - 3, 8});
  CHECK(std::abs(anchor.x - boundary(anchor.y)) <= 2);
  CHECK(std::abs(anchor.y - 8) <= 5);
  engine.set_anchor(anchor);
  const auto target = engine.snap({boundary(120) + 3, 120});
  const auto path = engine.path_to(target);
  CHECK(path.size() >= 100);
  CHECK(path.front() == anchor);
  CHECK(path.back() == target);
  check_magnetic_path_is_8_connected(path);
  for (const auto& p : path) {
    CHECK(std::abs(p.x - boundary(p.y)) <= 2);
  }
}

void magnetic_lasso_flat_region_yields_straight_line() {
  constexpr std::int32_t size = 128;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t, std::int32_t) { return false; }, 128, 128);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  CHECK((engine.snap({50, 50}) == patchy::PointI32{50, 50}));
  engine.set_anchor({10, 10});
  const auto path = engine.path_to({100, 60});
  const auto expected = magnetic_reference_line({10, 10}, {100, 60});
  CHECK(path.size() == expected.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    CHECK(path[i] == expected[i]);
  }
}

void magnetic_lasso_edge_contrast_gates_weak_edges() {
  constexpr std::int32_t size = 128;
  // Faint step: 128 -> 148 at x = 64 gives gradient G8 = 20.
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 64; }, 128, 148);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);

  patchy::MagneticLassoParams low{};
  low.edge_contrast = 5;  // threshold 12 < 20: the faint edge attracts
  engine.set_params(low);
  engine.set_anchor(engine.snap({61, 10}));
  auto path = engine.path_to(engine.snap({61, 118}));
  check_magnetic_path_is_8_connected(path);
  bool rode_the_edge = false;
  for (const auto& p : path) {
    if (p.y > 20 && p.y < 100) {
      rode_the_edge = rode_the_edge || (p.x >= 62 && p.x <= 65);
    }
  }
  CHECK(rode_the_edge);

  patchy::MagneticLassoParams high{};
  high.edge_contrast = 60;  // threshold 153 > 20: gated, traces the literal line
  engine.set_params(high);
  CHECK((engine.snap({61, 10}) == patchy::PointI32{61, 10}));
  engine.set_anchor({61, 10});
  path = engine.path_to({61, 118});
  CHECK(path.size() == 109);
  for (const auto& p : path) {
    CHECK(p.x == 61);
  }
}

void magnetic_lasso_snap_respects_width() {
  constexpr std::int32_t size = 128;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 64; }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);

  patchy::MagneticLassoParams wide{};
  wide.width = 10;  // radius 5 reaches the edge columns 63/64 from x = 61
  engine.set_params(wide);
  CHECK((engine.snap({61, 40}) == patchy::PointI32{63, 40}));

  patchy::MagneticLassoParams narrow{};
  narrow.width = 4;  // radius 2 cannot reach the edge from x = 58
  engine.set_params(narrow);
  CHECK((engine.snap({58, 40}) == patchy::PointI32{58, 40}));
}

void magnetic_lasso_is_deterministic() {
  constexpr std::int32_t width = 512;
  constexpr std::int32_t height = 64;
  const auto rgba = magnetic_two_tone_image(
      width, height, [](std::int32_t, std::int32_t y) { return y >= 32; }, 30, 220);
  const auto trace = [&] {
    patchy::LiveWireEngine engine;
    engine.set_image(rgba.data(), width, height, width * 4);
    engine.set_params({});
    engine.set_anchor(engine.snap({8, 30}));
    // A near target builds the anchor-centered field, then a far target forces the window
    // regrowth path; both legs must be reproducible.
    auto path = engine.path_to(engine.snap({120, 30}));
    auto far_path = engine.path_to(engine.snap({500, 30}));
    path.insert(path.end(), far_path.begin(), far_path.end());
    return path;
  };
  const auto first = trace();
  const auto second = trace();
  CHECK(first.size() == second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    CHECK(first[i] == second[i]);
  }
  CHECK(first.size() > 500);
  for (const auto& p : first) {
    CHECK(p.y >= 29 && p.y <= 34);
  }
}

void magnetic_lasso_prefers_opaque_side_of_alpha_edge() {
  // Black art on transparent ground: the gradient lives in the alpha channel and spans an
  // anti-aliased ramp several pixels wide (columns 62..64 here). The wire and the snap must
  // settle on the opaque side of the ramp like Photoshop, not the translucent fringe.
  constexpr std::int32_t size = 128;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4U, 0);
  const auto alpha_for_column = [](std::int32_t x) -> std::uint8_t {
    if (x < 62) {
      return 0;
    }
    switch (x) {
      case 62:
        return 64;
      case 63:
        return 160;
      case 64:
        return 240;
      default:
        return 255;
    }
  };
  for (std::int32_t y = 0; y < size; ++y) {
    for (std::int32_t x = 0; x < size; ++x) {
      rgba[(static_cast<std::size_t>(y) * size + x) * 4U + 3U] = alpha_for_column(x);
    }
  }
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  engine.set_params({});
  const auto anchor = engine.snap({60, 20});
  CHECK(anchor.x == 64);
  CHECK(anchor.y == 20);
  engine.set_anchor(anchor);
  const auto path = engine.path_to(engine.snap({60, 110}));
  check_magnetic_path_is_8_connected(path);
  CHECK(path.size() >= 80);
  for (const auto& p : path) {
    CHECK(p.x >= 64);
    CHECK(p.x <= 66);
  }
}

void magnetic_lasso_node_budget_falls_back_to_straight_line() {
  constexpr std::int32_t size = 256;
  const auto rgba =
      magnetic_two_tone_image(size, size, [](std::int32_t x, std::int32_t) { return x >= 128; }, 30, 220);
  patchy::LiveWireEngine engine;
  engine.set_image(rgba.data(), size, size, size * 4);
  patchy::MagneticLassoParams tiny_budget{};
  tiny_budget.node_budget = 1;  // clamps to the 1024 floor, far below any usable window
  engine.set_params(tiny_budget);
  engine.set_anchor({10, 128});
  const auto path = engine.path_to({245, 128});
  const auto expected = magnetic_reference_line({10, 128}, {245, 128});
  CHECK(path.size() == expected.size());
  for (std::size_t i = 0; i < path.size(); ++i) {
    CHECK(path[i] == expected[i]);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> infra_selection_tests() {
  return {
      {"plugin_host_and_legacy_probe_work", plugin_host_and_legacy_probe_work},
      {"tile_cache_stores_and_invalidates", tile_cache_stores_and_invalidates},
      {"color_manager_assigns_profiles", color_manager_assigns_profiles},
      {"quick_select_maxflow_solves_tiny_grid", quick_select_maxflow_solves_tiny_grid},
      {"quick_select_maxflow_matches_reference_on_random_grids",
       quick_select_maxflow_matches_reference_on_random_grids},
      {"quick_select_stroke_grabs_flat_region_and_respects_edges",
       quick_select_stroke_grabs_flat_region_and_respects_edges},
      {"quick_select_second_stroke_adds_monotonically", quick_select_second_stroke_adds_monotonically},
      {"quick_select_subtract_removes_only_connected_area", quick_select_subtract_removes_only_connected_area},
      {"quick_select_photo_texture_stroke_stays_local", quick_select_photo_texture_stroke_stays_local},
      {"quick_select_enhance_edge_smooths_staircase", quick_select_enhance_edge_smooths_staircase},
      {"magnetic_lasso_path_snaps_to_synthetic_edge", magnetic_lasso_path_snaps_to_synthetic_edge},
      {"magnetic_lasso_flat_region_yields_straight_line", magnetic_lasso_flat_region_yields_straight_line},
      {"magnetic_lasso_edge_contrast_gates_weak_edges", magnetic_lasso_edge_contrast_gates_weak_edges},
      {"magnetic_lasso_snap_respects_width", magnetic_lasso_snap_respects_width},
      {"magnetic_lasso_is_deterministic", magnetic_lasso_is_deterministic},
      {"magnetic_lasso_prefers_opaque_side_of_alpha_edge",
       magnetic_lasso_prefers_opaque_side_of_alpha_edge},
      {"magnetic_lasso_node_budget_falls_back_to_straight_line",
       magnetic_lasso_node_budget_falls_back_to_straight_line},
  };
}
