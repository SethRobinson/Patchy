#pragma once

// Shared PSD byte-level helpers moved from tests/test_main.cpp (used by more than one test group).

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

constexpr std::array<std::uint8_t, 8> kTestBlendIfIdentityEntry{0, 0, 255, 255, 0, 0, 255, 255};

std::vector<std::uint8_t> test_blend_if_identity_payload();

void write_ascii4(patchy::psd::BigEndianWriter& writer, const char (&value)[5]);

void write_pascal_padded(patchy::psd::BigEndianWriter& writer, const std::string& value,
                         std::size_t padded_multiple);

std::string read_pascal_padded(patchy::psd::BigEndianReader& reader, std::size_t padded_multiple);

std::vector<std::string> psd_raw_layer_record_names(std::span<const std::uint8_t> bytes);

struct PsdLayerChannelRecord {
  std::int16_t id{0};
  std::uint32_t length{0};
  std::uint16_t compression{0};
};

std::vector<PsdLayerChannelRecord> psd_layer_channel_records(std::span<const std::uint8_t> bytes);

std::uint32_t read_u32_be_at(std::span<const std::uint8_t> bytes, std::size_t offset);

std::vector<std::uint8_t> psd_layer_extra_data(std::span<const std::uint8_t> bytes, std::int16_t target_index);

std::vector<std::uint8_t> psd_first_layer_extra_data(std::span<const std::uint8_t> bytes);

std::optional<std::vector<std::uint8_t>> psd_layer_block_payload(std::span<const std::uint8_t> extra_data,
                                                                 const char (&target_key)[5]);

void write_test_layer_block(patchy::psd::BigEndianWriter& writer, const char (&key)[5],
                            std::span<const std::uint8_t> payload);

std::optional<std::vector<std::uint8_t>> test_image_resource_payload(std::span<const std::uint8_t> resources,
                                                                     std::uint16_t id);

std::filesystem::path arrows_fixture_path();

bool layer_has_psd_block(const patchy::Layer& layer, const std::string& key);

}  // namespace patchy::test
