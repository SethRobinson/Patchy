#pragma once

// Shared helpers moved from tests/test_main.cpp (used by more than one test group).

#include "core/layer.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace patchy {
class Document;
struct EditOptions;
struct SmartFilterStack;
struct BrushTip;
}

namespace patchy::test {

patchy::PixelBuffer solid_rgb(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                 std::uint8_t b);

patchy::PixelBuffer solid_rgba(std::int32_t width, std::int32_t height, std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b, std::uint8_t a);

patchy::Document make_tool_document();

// A `width` x `height` RGB document whose only layer is `layer_name` filled with
// solid_rgb(width, height, r, g, b): the two-line setup most core tests start with.
patchy::Document solid_rgb_document(std::int32_t width, std::int32_t height, std::string layer_name,
                                    std::uint8_t r, std::uint8_t g, std::uint8_t b);

patchy::EditOptions tool_options(std::uint8_t r = 220, std::uint8_t g = 20, std::uint8_t b = 40);

void write_u16_le(std::ofstream& file, std::uint16_t value);

void write_u32_le(std::ofstream& file, std::uint32_t value);

void write_rgb8_bmp_artifact(const std::string& name, const patchy::PixelBuffer& pixels);

void write_bmp_artifact(const std::string& name, const patchy::Document& document);

const patchy::Layer* find_layer_named(const std::vector<patchy::Layer>& layers, std::string_view name);

bool close_float(float actual, float expected, float tolerance = 0.01F);

struct RgbDiffMetrics {
  std::uint64_t pixels{0};
  std::uint64_t differing_pixels{0};
  double mean_abs_channel_delta{0.0};
  int max_channel_delta{0};
};

RgbDiffMetrics rgb_diff_metrics(const patchy::PixelBuffer& left, const patchy::PixelBuffer& right);

patchy::LayerId active_tool_layer(const patchy::Document& document);

const patchy::Layer& require_layer_named(const patchy::Document& document, std::string_view name);

const patchy::SmartFilterStack& require_smart_filter_stack(const patchy::Document& document,
                                                           std::string_view name);

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
