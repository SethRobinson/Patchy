#pragma once

// Shared helpers moved from tests/test_main.cpp (used by more than one test group).

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

namespace patchy::test {

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b);

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a);

patchy::Document make_tool_document();

patchy::EditOptions tool_options(std::uint8_t r = 220, std::uint8_t g = 20, std::uint8_t b = 40);

void write_u16_le(std::ofstream& file, std::uint16_t value);

void write_u32_le(std::ofstream& file, std::uint32_t value);

void write_rgb8_bmp_artifact(const std::string& name, const patchy::PixelBuffer& pixels);

void write_bmp_artifact(const std::string& name, const patchy::Document& document);

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, const std::string& name);

bool close_float(float actual, float expected, float tolerance = 0.01F);

struct RgbDiffMetrics {
  std::uint64_t pixels{0};
  std::uint64_t differing_pixels{0};
  double mean_abs_channel_delta{0.0};
  int max_channel_delta{0};
};

RgbDiffMetrics rgb_diff_metrics(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right);

patchy::LayerId active_tool_layer(const patchy::Document& document);

const patchy::Layer& require_layer_named(const patchy::Document& document, const std::string& name);

const patchy::SmartFilterStack& require_smart_filter_stack(const patchy::Document& document,
                                                           const std::string& name);

patchy::SmartFilterStack test_gaussian_smart_filter_stack(double radius);

patchy::SmartFilterStack test_high_pass_smart_filter_stack(double radius);

patchy::SmartFilterStack test_median_smart_filter_stack(double radius);

patchy::SmartFilterStack test_dust_and_scratches_smart_filter_stack(
    std::int32_t radius, std::int32_t threshold);

patchy::SmartFilterStack test_surface_blur_smart_filter_stack(
    double radius, std::int32_t threshold);

patchy::SmartFilterStack
test_unsharp_mask_smart_filter_stack(double amount, double radius,
                                     std::int32_t threshold);

patchy::SmartFilterStack
test_motion_blur_smart_filter_stack(std::int32_t angle, std::int32_t distance);

std::uint64_t fnv1a_hash_bytes(std::span<const std::uint8_t> bytes);

patchy::BrushTip make_bar_brush_tip();

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path);

}  // namespace patchy::test
