// Photoshop's parameterless Smart Filters (Blur, Blur More, Sharpen, Sharpen
// More, Invert): descriptor import/export and pixels pinned against the
// self-authored Photoshop 27.x captures photoshop-smart-filter-classic.psd and
// photoshop-smart-filter-invert.psd (docs/smart-filters-native.md).

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/rect_utils.hpp"
#include "core/smart_object.hpp"
#include "filters/filter_engine.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "test_groups.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using patchy::test::require_layer_named;
using patchy::test::require_smart_filter_stack;

const patchy::UnknownPsdBlock &require_sold_block(const patchy::Layer &layer) {
  const auto &blocks = layer.unknown_psd_blocks();
  const auto found = std::find_if(
      blocks.begin(), blocks.end(), [](const patchy::UnknownPsdBlock &block) {
        return block.key == "SoLd" || block.key == "SoLE";
      });
  CHECK(found != blocks.end());
  return *found;
}

patchy::psd::DescriptorObject
descriptor_from_sold(std::span<const std::uint8_t> sold) {
  patchy::psd::BigEndianReader reader(sold);
  CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) ==
        "soLD");
  CHECK(reader.read_u32() == 4U);
  CHECK(reader.read_u32() == 16U);
  return patchy::psd::read_descriptor(reader);
}

const std::vector<patchy::psd::DescriptorValue> &
filter_fx_list(const patchy::psd::DescriptorObject &sold) {
  const auto *root = patchy::psd::descriptor_object(sold, "filterFX");
  CHECK(root != nullptr);
  const auto *list = patchy::psd::descriptor_value(*root, "filterFXList");
  CHECK(list != nullptr &&
        list->type == patchy::psd::DescriptorValue::Type::List);
  return list->list_value;
}

std::vector<std::uint8_t>
descriptor_bytes(const patchy::psd::DescriptorObject &object) {
  patchy::psd::BigEndianWriter writer;
  patchy::psd::write_descriptor(writer, object);
  return writer.bytes();
}

struct ClassicCase {
  std::string_view fixture;
  std::string_view layer_name;
  std::vector<patchy::SmartFilterKind> kinds;
  std::int32_t offset_x;
};

constexpr std::string_view kClassicFixture = "photoshop-smart-filter-classic.psd";
constexpr std::string_view kInvertFixture = "photoshop-smart-filter-invert.psd";

const std::array<ClassicCase, 10> &classic_cases() {
  using Kind = patchy::SmartFilterKind;
  static const std::array<ClassicCase, 10> cases{{
      {kClassicFixture, "Blur", {Kind::Blur}, 0},
      {kClassicFixture, "Blur More", {Kind::BlurMore}, 0},
      {kClassicFixture, "Sharpen", {Kind::Sharpen}, 0},
      {kClassicFixture, "Sharpen More", {Kind::SharpenMore}, 0},
      {kClassicFixture, "Blur then Sharpen More", {Kind::Blur, Kind::SharpenMore}, 0},
      {kClassicFixture, "Sharpen past left edge", {Kind::Sharpen}, -14},
      {kClassicFixture, "Blur More past left edge", {Kind::BlurMore}, -14},
      {kInvertFixture, "Invert", {Kind::Invert}, 0},
      {kInvertFixture, "Invert then Blur", {Kind::Invert, Kind::Blur}, 0},
      {kInvertFixture, "Invert past left edge", {Kind::Invert}, -14},
  }};
  return cases;
}

patchy::Document read_fixture(std::string_view name) {
  return patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(name));
}

patchy::Document read_classic_fixture() { return read_fixture(kClassicFixture); }

void psd_photoshop_classic_smart_filters_import_and_round_trip() {
  for (const auto &test_case : classic_cases()) {
    const auto document = read_fixture(test_case.fixture);
    const auto &layer = require_layer_named(document, test_case.layer_name);
    CHECK(patchy::layer_is_smart_object(layer));
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto &stack =
        require_smart_filter_stack(document, test_case.layer_name);
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == test_case.kinds.size());
    for (std::size_t index = 0; index < stack.entries.size(); ++index) {
      const auto &entry = stack.entries[index];
      CHECK(entry.kind == test_case.kinds[index]);
      const auto *native = patchy::parameterless_smart_filter_native(entry.kind);
      CHECK(native != nullptr);
      CHECK(entry.native_name == native->native_name);
      CHECK(entry.native_filter_id == native->filter_id);
      CHECK(entry.native_class_id.empty());
      CHECK(!entry.has_options);
      CHECK(std::holds_alternative<std::monostate>(entry.parameters));
    }

    // Patchy authors the same entry bytes Photoshop wrote.
    const auto photoshop_sold =
        descriptor_from_sold(require_sold_block(layer).payload);
    const auto &photoshop_entries = filter_fx_list(photoshop_sold);
    CHECK(photoshop_entries.size() == stack.entries.size());
    const auto placement = patchy::smart_object_placement_from_layer(layer);
    CHECK(placement.has_value());
    const auto authored_sold = descriptor_from_sold(
        patchy::psd::author_placed_layer_sold_payload(
            *placement, patchy::smart_object_placed_uuid(layer), &stack));
    const auto &authored_entries = filter_fx_list(authored_sold);
    CHECK(authored_entries.size() == photoshop_entries.size());
    for (std::size_t index = 0; index < authored_entries.size(); ++index) {
      CHECK(photoshop_entries[index].object_value != nullptr &&
            authored_entries[index].object_value != nullptr);
      CHECK(patchy::psd::descriptor_value(
                *authored_entries[index].object_value, "Fltr") == nullptr);
      // Photoshop wrote the Invert capture's foreground as HSBC (its picker
      // model); Patchy authors RGB colours, so compare everything else.
      auto photoshop_entry = *photoshop_entries[index].object_value;
      for (const auto *key : {"FrgC", "BckC"}) {
        const auto *color = patchy::psd::descriptor_object(photoshop_entry, key);
        CHECK(color != nullptr);
        if (color->class_id != "RGBC") {
          photoshop_entry.values[key] =
              authored_entries[index].object_value->values.at(key);
        }
      }
      CHECK(descriptor_bytes(*authored_entries[index].object_value) ==
            descriptor_bytes(photoshop_entry));
    }
  }

  // A clean save keeps every SoLd byte for byte.
  for (const auto &test_case : classic_cases()) {
    const auto document = read_fixture(test_case.fixture);
    const auto clean = patchy::psd::DocumentIo::read(
        patchy::psd::DocumentIo::write_layered_rgb8(document));
    CHECK(require_sold_block(require_layer_named(clean, test_case.layer_name))
              .payload ==
          require_sold_block(require_layer_named(document, test_case.layer_name))
              .payload);
  }

  // Blending edits patch the parameterless entry in place.
  const auto document = read_classic_fixture();
  auto edited = document;
  const auto &original_layer = require_layer_named(document, "Sharpen More");
  auto *edited_layer = edited.find_layer(original_layer.id());
  CHECK(edited_layer != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  edited_stack.entries.front().opacity = 0.42;
  edited_stack.entries.front().blend_mode = patchy::BlendMode::Screen;
  edited_stack.entries.front().enabled = false;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto &reread_entry =
      require_smart_filter_stack(reread, "Sharpen More").entries.front();
  CHECK(reread_entry.kind == patchy::SmartFilterKind::SharpenMore);
  CHECK(std::abs(reread_entry.opacity - 0.42) < 1e-9);
  CHECK(reread_entry.blend_mode == patchy::BlendMode::Screen);
  CHECK(!reread_entry.enabled);
  CHECK(!reread_entry.has_options);
  CHECK(patchy::psd::descriptor_value(
            *filter_fx_list(descriptor_from_sold(
                                require_sold_block(require_layer_named(
                                                       reread, "Sharpen More"))
                                    .payload))
                 .front()
                 .object_value,
            "Fltr") == nullptr);

  // An HSB foreground colour stays verbatim through a blending edit.
  auto inverted = read_fixture(kInvertFixture);
  auto *invert_layer =
      inverted.find_layer(require_layer_named(inverted, "Invert").id());
  CHECK(invert_layer != nullptr);
  auto invert_stack = *invert_layer->smart_filter_stack();
  invert_stack.entries.front().opacity = 0.5;
  invert_layer->set_smart_filter_stack(std::move(invert_stack));
  patchy::mark_layer_smart_object_block_dirty(*invert_layer);
  const auto invert_reread = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(inverted));
  CHECK(std::abs(require_smart_filter_stack(invert_reread, "Invert")
                     .entries.front()
                     .opacity -
                 0.5) < 1e-9);
  const auto invert_sold = descriptor_from_sold(
      require_sold_block(require_layer_named(invert_reread, "Invert")).payload);
  const auto *foreground = patchy::psd::descriptor_object(
      *filter_fx_list(invert_sold).front().object_value, "FrgC");
  CHECK(foreground != nullptr && foreground->class_id == "HSBC");
}

void smart_filter_classic_entry_with_a_filter_object_stays_unsupported() {
  // A parameterless filterID beside an unexpected Fltr object is not the
  // shape Photoshop writes, so it fails closed.
  const auto document = read_classic_fixture();
  const auto &layer = require_layer_named(document, "Blur");
  auto sold = descriptor_from_sold(require_sold_block(layer).payload);
  auto *root = const_cast<patchy::psd::DescriptorObject *>(
      patchy::psd::descriptor_object(sold, "filterFX"));
  auto *list = const_cast<patchy::psd::DescriptorValue *>(
      patchy::psd::descriptor_value(*root, "filterFXList"));
  auto &item = *list->list_value.front().object_value;
  patchy::psd::DescriptorValue filter;
  filter.type = patchy::psd::DescriptorValue::Type::Object;
  filter.object_value = std::make_shared<patchy::psd::DescriptorObject>();
  filter.object_value->class_id = "Blr ";
  item.key_order.push_back({"Fltr", false});
  item.values.emplace("Fltr", std::move(filter));
  patchy::psd::BigEndianWriter writer;
  writer.write_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>("soLD"), 4));
  writer.write_u32(4U);
  writer.write_u32(16U);
  patchy::psd::write_descriptor(writer, sold);
  const auto info =
      patchy::psd::parse_placed_layer_block("SoLd", writer.bytes());
  CHECK(info.has_value() && info->smart_filters.has_value());
  CHECK(info->smart_filters->entries.front().kind ==
        patchy::SmartFilterKind::Unsupported);
  CHECK(info->smart_filters->support ==
        patchy::SmartFilterStackSupport::Unsupported);
}

void smart_filter_classic_kernels_match_photoshop_fixture() {
  for (const auto &test_case : classic_cases()) {
    const auto document = read_fixture(test_case.fixture);
    const auto &source = require_layer_named(document, "_Source");
    CHECK(source.smart_filter_stack() == nullptr);
    const auto document_rect =
        patchy::Rect{0, 0, document.width(), document.height()};
    const auto &expected = require_layer_named(document, test_case.layer_name);
    const auto &stack =
        require_smart_filter_stack(document, test_case.layer_name);
    auto placed_bounds = source.bounds();
    placed_bounds.x += test_case.offset_x;
    // Photoshop's FEid cache, and so its filter canvas, is the document
    // united with the placed content (the moved layers' records start at
    // x = -5).
    const auto canvas = patchy::unite_rect(document_rect, placed_bounds);
    const auto actual = patchy::render_smart_filter_stack(
        source.pixels(), placed_bounds, canvas, stack);
    const auto expected_bounds = expected.bounds();
    const auto bounds_equal = actual.bounds.x == expected_bounds.x &&
                              actual.bounds.y == expected_bounds.y &&
                              actual.bounds.width == expected_bounds.width &&
                              actual.bounds.height == expected_bounds.height;
    if (!bounds_equal) {
      std::cerr << test_case.layer_name << ": bounds " << actual.bounds.x
                << "," << actual.bounds.y << " " << actual.bounds.width << "x"
                << actual.bounds.height << " vs Photoshop " << expected_bounds.x
                << "," << expected_bounds.y << " " << expected_bounds.width
                << "x" << expected_bounds.height << "\n";
    }
    CHECK(bounds_equal);
    const auto area = patchy::unite_rect(actual.bounds, expected_bounds);
    const auto sample = [](const patchy::PixelBuffer &pixels, patchy::Rect at,
                           std::int32_t x, std::int32_t y) {
      static constexpr std::array<std::uint8_t, 4> kClear{0, 0, 0, 0};
      return at.contains(x, y) ? pixels.pixel(x - at.x, y - at.y)
                               : kClear.data();
    };
    std::size_t mismatched = 0;
    std::size_t visible = 0;
    for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
      for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
        const auto *want = sample(expected.pixels(), expected_bounds, x, y);
        const auto *got = sample(actual.pixels, actual.bounds, x, y);
        if (want[3] == 0U && got[3] == 0U) {
          continue;
        }
        ++visible;
        if (!std::equal(want, want + 4, got)) {
          ++mismatched;
        }
      }
    }
    CHECK(visible > 0U);
    if (mismatched != 0U) {
      std::cerr << test_case.layer_name << ": " << mismatched << " of "
                << visible << " pixels differ from Photoshop\n";
    }
    // The blurs are byte-exact. The sharpens borrow colours for transparent
    // neighbours, where Photoshop's tie between equally near visible pixels
    // is not fully pinned (docs/smart-filters-native.md): allow 1 in 200.
    const auto sharpens = std::any_of(
        test_case.kinds.begin(), test_case.kinds.end(), [](auto kind) {
          return kind == patchy::SmartFilterKind::Sharpen ||
                 kind == patchy::SmartFilterKind::SharpenMore;
        });
    CHECK(sharpens ? mismatched * 200U <= visible : mismatched == 0U);
  }
}

void smart_filter_radial_zoom_tracks_photoshop_fixture() {
  // Zoom is Patchy's own ray average, calibrated to Photoshop's streak extent
  // (1 +/- amount / 400) about the content center; it is not byte-exact
  // (docs/smart-filters-native.md). Pin the measured agreement.
  const auto document = read_fixture("photoshop-smart-filter-zoom.psd");
  const auto &source = require_layer_named(document, "_Source");
  const auto canvas = patchy::unite_rect(
      patchy::Rect{0, 0, document.width(), document.height()},
      source.bounds());
  for (const auto *name : {"Zoom 25 Best", "Zoom 60 Good", "Zoom 10 Draft"}) {
    const auto &expected = require_layer_named(document, name);
    const auto &stack = require_smart_filter_stack(document, name);
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(std::get<patchy::RadialBlurSmartFilter>(
              stack.entries.front().parameters)
              .zoom);
    const auto actual = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, stack);
    const auto area = patchy::unite_rect(actual.bounds, expected.bounds());
    const auto sample = [](const patchy::PixelBuffer &pixels, patchy::Rect at,
                           std::int32_t x, std::int32_t y) {
      static constexpr std::array<std::uint8_t, 4> kClear{0, 0, 0, 0};
      return at.contains(x, y) ? pixels.pixel(x - at.x, y - at.y)
                               : kClear.data();
    };
    double rgb_total = 0.0;
    std::size_t rgb_count = 0;
    int alpha_max = 0;
    for (std::int32_t y = area.y; y < area.y + area.height; ++y) {
      for (std::int32_t x = area.x; x < area.x + area.width; ++x) {
        const auto *want = sample(expected.pixels(), expected.bounds(), x, y);
        const auto *got = sample(actual.pixels, actual.bounds, x, y);
        alpha_max = std::max(alpha_max, std::abs(int{want[3]} - int{got[3]}));
        if (want[3] == 255U && got[3] == 255U) {
          for (std::size_t channel = 0; channel < 3U; ++channel) {
            rgb_total += std::abs(int{want[channel]} - int{got[channel]});
            ++rgb_count;
          }
        }
      }
    }
    const auto rgb_mean = rgb_count == 0U ? 255.0 : rgb_total / rgb_count;
    std::cerr << name << ": mean opaque RGB difference " << rgb_mean
              << ", max alpha difference " << alpha_max << "\n";
    CHECK(rgb_count > 0U);
    CHECK(rgb_mean <= 3.0);
    CHECK(alpha_max <= 64);
  }
}

patchy::SmartFilterStack parameterless_stack(patchy::SmartFilterKind kind) {
  const auto *native = patchy::parameterless_smart_filter_native(kind);
  CHECK(native != nullptr);
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = kind;
  entry.native_name = native->native_name;
  entry.native_filter_id = native->filter_id;
  entry.has_options = false;
  entry.parameters = std::monostate{};
  stack.entries.push_back(std::move(entry));
  stack.support = patchy::smart_filter_stack_support(stack);
  return stack;
}

std::array<std::uint8_t, 3> visible_average_color(
    const patchy::FilterRenderResult &result) {
  for (std::int32_t y = 0; y < result.bounds.height; ++y) {
    for (std::int32_t x = 0; x < result.bounds.width; ++x) {
      const auto *pixel = result.pixels.pixel(x, y);
      if (pixel[3] != 0U) {
        return {pixel[0], pixel[1], pixel[2]};
      }
    }
  }
  return {0, 0, 0};
}

void smart_filter_average_matches_photoshop_probes() {
  // Values Photoshop 27.x rendered for Average Smart Filters over these exact
  // inputs (October 2026 COM probes): transparent pixels and the canvas around
  // the placed content count as white in the plain straight-RGB mean.
  const auto stack = parameterless_stack(patchy::SmartFilterKind::Average);

  // An opaque (200, 60, 30) 12x16 square on a transparent 40x40 canvas.
  patchy::PixelBuffer square(12, 16, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 16; ++y) {
    for (std::int32_t x = 0; x < 12; ++x) {
      auto *pixel = square.pixel(x, y);
      pixel[0] = 200;
      pixel[1] = 60;
      pixel[2] = 30;
      pixel[3] = 255;
    }
  }
  const auto square_result = patchy::render_smart_filter_stack(
      square, patchy::Rect{14, 12, 12, 16}, patchy::Rect{0, 0, 40, 40}, stack);
  CHECK((visible_average_color(square_result) ==
         std::array<std::uint8_t, 3>{248, 232, 228}));
  CHECK(square_result.bounds.x == 14 && square_result.bounds.width == 12);
  CHECK(square_result.pixels.pixel(0, 0)[3] == 255U);

  // A 10x10 canvas whose top half lies outside the placed content.
  patchy::PixelBuffer lower(10, 5, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 5; ++y) {
    for (std::int32_t x = 0; x < 10; ++x) {
      auto *pixel = lower.pixel(x, y);
      pixel[0] = 100;
      pixel[1] = 150;
      pixel[2] = 200;
      pixel[3] = 255;
    }
  }
  CHECK((visible_average_color(patchy::render_smart_filter_stack(
             lower, patchy::Rect{0, 5, 10, 5}, patchy::Rect{0, 0, 10, 10},
             stack)) == std::array<std::uint8_t, 3>{178, 203, 228}));

  // Half black at alpha 64, half opaque (100, 150, 200): straight colors
  // average regardless of alpha.
  patchy::PixelBuffer mixed(10, 10, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 10; ++y) {
    for (std::int32_t x = 0; x < 10; ++x) {
      auto *pixel = mixed.pixel(x, y);
      const auto top = y < 5;
      pixel[0] = top ? 0 : 100;
      pixel[1] = top ? 0 : 150;
      pixel[2] = top ? 0 : 200;
      pixel[3] = top ? 64 : 255;
    }
  }
  const auto mixed_result = patchy::render_smart_filter_stack(
      mixed, patchy::Rect{0, 0, 10, 10}, patchy::Rect{0, 0, 10, 10}, stack);
  CHECK((visible_average_color(mixed_result) ==
         std::array<std::uint8_t, 3>{50, 75, 100}));
  CHECK(mixed_result.pixels.pixel(0, 0)[3] == 64U);
}

void destructive_classic_filters_share_the_native_kernels() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::PixelBuffer source(9, 7, patchy::PixelFormat::rgba8());
  std::uint32_t state = 12345U;
  for (auto &byte : source.data()) {
    state = state * 1103515245U + 12345U;
    byte = static_cast<std::uint8_t>(state >> 24U);
  }
  const auto bounds = patchy::Rect{0, 0, 9, 7};
  const std::array<std::pair<const char *, patchy::SmartFilterKind>, 4> cases{
      {{"patchy.filters.blur", patchy::SmartFilterKind::Blur},
       {"patchy.filters.blur_more", patchy::SmartFilterKind::BlurMore},
       {"patchy.filters.sharpen_more", patchy::SmartFilterKind::SharpenMore},
       {"patchy.filters.sharpen_edges", patchy::SmartFilterKind::SharpenEdges}}};
  for (const auto &[identifier, kind] : cases) {
    auto named = source;
    registry.apply(registry.default_invocation(identifier), named);
    auto legacy = source;
    registry.apply(identifier, legacy);
    const auto native =
        patchy::render_photoshop_classic_filter(source, bounds, kind);
    CHECK(named.data().size() == native.pixels.data().size());
    CHECK(std::equal(named.data().begin(), named.data().end(),
                     native.pixels.data().begin()));
    CHECK(std::equal(legacy.data().begin(), legacy.data().end(),
                     native.pixels.data().begin()));
    // Each maps onto its native Smart Filter entry, which carries no Fltr.
    patchy::FilterRecipe recipe;
    recipe.entries.push_back({registry.default_invocation(identifier)});
    const auto mapped =
        patchy::smart_filter_entries_from_recipe(recipe, registry);
    CHECK(mapped.has_value() && mapped->size() == 1U);
    CHECK(mapped->front().kind == kind);
    CHECK(!mapped->front().has_options);
    CHECK(std::holds_alternative<std::monostate>(mapped->front().parameters));
  }

  // Destructive Average: the alpha-weighted mean of the visible pixels,
  // hidden colors ignored; alpha unchanged.
  patchy::PixelBuffer layer(2, 1, patchy::PixelFormat::rgba8());
  auto *left = layer.pixel(0, 0);
  left[0] = 200;
  left[1] = 100;
  left[2] = 0;
  left[3] = 255;
  auto *right = layer.pixel(1, 0);
  right[0] = 0;
  right[1] = 0;
  right[2] = 0;
  right[3] = 85;
  registry.apply(registry.default_invocation("patchy.filters.average"), layer);
  CHECK(layer.pixel(0, 0)[0] == 150 && layer.pixel(0, 0)[1] == 75 &&
        layer.pixel(0, 0)[2] == 0 && layer.pixel(0, 0)[3] == 255);
  CHECK(layer.pixel(1, 0)[0] == 150 && layer.pixel(1, 0)[3] == 85);
  patchy::FilterRecipe average_recipe;
  average_recipe.entries.push_back(
      {registry.default_invocation("patchy.filters.average")});
  const auto average_mapped =
      patchy::smart_filter_entries_from_recipe(average_recipe, registry);
  CHECK(average_mapped.has_value() &&
        average_mapped->front().kind == patchy::SmartFilterKind::Average);
}

void smart_filter_effects_aligned_records_with_odd_lengths_stay_parsed() {
  // Photoshop pads every FEid record to four bytes. After an odd-length record
  // the unpadded read lands in the padding and still yields a small nonzero
  // length; only a full walk of both dialects tells them apart. Two records of
  // 7 and 256 bytes once made such a file opaque, preview-locking every stack.
  std::vector<std::uint8_t> payload{0, 0, 0, 3};
  const auto append_record = [&payload](std::size_t length) {
    for (int shift = 56; shift >= 0; shift -= 8) {
      payload.push_back(static_cast<std::uint8_t>(
          (static_cast<std::uint64_t>(length) >> shift) & 0xffU));
    }
    payload.insert(payload.end(), length, std::uint8_t{0x41});
    while (payload.size() % 4U != 0U) {
      payload.push_back(0U);
    }
  };
  append_record(7U);
  append_record(256U);
  const auto block = patchy::psd::parse_filter_effects_block(
      std::string_view("FEid"), std::span<const std::uint8_t>(payload));
  CHECK(!block.opaque);
  CHECK(block.records.size() == 2U);
  CHECK(patchy::psd::serialize_filter_effects_block(block) == payload);
}

void psd_photoshop_smart_sharpen_legacy_renders_and_others_stay_locked() {
  // Legacy-only Smart Sharpen (decided October 20, 2026): legacy mode,
  // Gaussian Remove, Noise Reduction 0 renders as Unsharp Mask with threshold
  // 0 (Photoshop draws both identically); every other variant stays
  // preview-locked.
  const auto document = read_fixture("photoshop-smart-filter-smart-sharpen.psd");
  const auto &source = require_layer_named(document, "_Source");
  const auto canvas = patchy::smart_filter_canvas_bounds(
      document.width(), document.height(), source.bounds());
  struct Rendered {
    const char *name;
    double amount;
    double radius;
  };
  for (const auto &item : {Rendered{"Smart Sharpen legacy 100 2", 100.0, 2.0},
                           Rendered{"Smart Sharpen legacy 250 0.7", 250.0, 0.7}}) {
    const auto &layer = require_layer_named(document, item.name);
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto &stack = require_smart_filter_stack(document, item.name);
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    const auto &entry = stack.entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::SmartSharpen);
    const auto *settings =
        std::get_if<patchy::SmartSharpenSmartFilter>(&entry.parameters);
    CHECK(settings != nullptr);
    CHECK(std::abs(settings->amount_percent - item.amount) < 1e-9);
    CHECK(std::abs(settings->radius_pixels - item.radius) < 1e-9);
    const auto actual = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, stack);
    const auto expected_bounds = layer.bounds();
    CHECK(actual.bounds.x == expected_bounds.x &&
          actual.bounds.y == expected_bounds.y &&
          actual.bounds.width == expected_bounds.width &&
          actual.bounds.height == expected_bounds.height);
    // The entry renders exactly as an Unsharp Mask entry with threshold 0.
    // On this partially transparent source Photoshop also sharpens alpha and
    // its RGB rule is unresolved, so Photoshop's baked pixels are not a
    // byte target here (docs/smart-filters-native.md, Unsharp Mask).
    auto unsharp_stack = stack;
    unsharp_stack.entries.front().kind = patchy::SmartFilterKind::UnsharpMask;
    unsharp_stack.entries.front().parameters =
        patchy::UnsharpMaskSmartFilter{item.amount, item.radius, 0};
    const auto unsharp = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, unsharp_stack);
    CHECK(unsharp.bounds.x == actual.bounds.x &&
          unsharp.bounds.width == actual.bounds.width &&
          unsharp.bounds.height == actual.bounds.height);
    CHECK(std::equal(unsharp.pixels.data().begin(), unsharp.pixels.data().end(),
                     actual.pixels.data().begin(), actual.pixels.data().end()));
  }
  for (const auto *name :
       {"Smart Sharpen default mode", "Smart Sharpen legacy more accurate",
        "Smart Sharpen legacy shadow fade"}) {
    const auto &layer = require_layer_named(document, name);
    CHECK(!patchy::smart_object_lock_reason(layer).empty());
    const auto &stack = require_smart_filter_stack(document, name);
    CHECK(stack.support == patchy::SmartFilterStackSupport::Unsupported);
    CHECK(stack.entries.front().kind == patchy::SmartFilterKind::Unsupported);
  }

  // A clean save keeps the SoLd bytes; an Amount/Radius edit patches only
  // those two values and stays in the legacy Gaussian case.
  const auto clean = patchy::psd::DocumentIo::read(
      patchy::psd::DocumentIo::write_layered_rgb8(document));
  for (const auto *name : {"Smart Sharpen legacy 100 2", "Smart Sharpen default mode"}) {
    CHECK(require_sold_block(require_layer_named(clean, name)).payload ==
          require_sold_block(require_layer_named(document, name)).payload);
  }
  auto edited = document;
  auto *edited_layer = edited.find_layer(
      require_layer_named(document, "Smart Sharpen legacy 100 2").id());
  CHECK(edited_layer != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto &edited_settings = std::get<patchy::SmartSharpenSmartFilter>(
      edited_stack.entries.front().parameters);
  edited_settings.amount_percent = 180.0;
  edited_settings.radius_pixels = 3.5;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto edited_bytes = patchy::psd::DocumentIo::write_layered_rgb8(edited);
  // For the Photoshop open check (docs/photoshop-com.md).
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      edited, std::filesystem::path("test-artifacts") / "smart-sharpen-edited.psd");
  const auto reread = patchy::psd::DocumentIo::read(edited_bytes);
  const auto &reread_entry =
      require_smart_filter_stack(reread, "Smart Sharpen legacy 100 2")
          .entries.front();
  CHECK(reread_entry.kind == patchy::SmartFilterKind::SmartSharpen);
  const auto &reread_settings =
      std::get<patchy::SmartSharpenSmartFilter>(reread_entry.parameters);
  CHECK(std::abs(reread_settings.amount_percent - 180.0) < 1e-9);
  CHECK(std::abs(reread_settings.radius_pixels - 3.5) < 1e-9);
  const auto reread_sold = descriptor_from_sold(
      require_sold_block(require_layer_named(reread, "Smart Sharpen legacy 100 2"))
          .payload);
  const auto *filter = patchy::psd::descriptor_object(
      *filter_fx_list(reread_sold).front().object_value, "Fltr");
  CHECK(filter != nullptr && filter->class_id == "smartSharpen");
  CHECK(filter->key_order.size() == 6U);
}

void smart_filter_sharpen_edges_tracks_photoshop_fixture() {
  // Sharpen Edges is fitted, not byte-exact: within one level on 99.65% of
  // probe samples (docs/smart-filters-native.md). Pin the measured agreement
  // on Photoshop's own capture, once and twice applied (October 2026: 29 of
  // 2244 and 30 of 2084 samples differ, largest 9 and 30).
  const auto document =
      read_fixture("photoshop-smart-filter-sharpen-edges.psd");
  const auto &source = require_layer_named(document, "_Source");
  const auto canvas = patchy::smart_filter_canvas_bounds(
      document.width(), document.height(), source.bounds());
  for (const auto *name : {"Sharpen Edges", "Sharpen Edges twice"}) {
    const auto &layer = require_layer_named(document, name);
    CHECK(patchy::smart_object_lock_reason(layer).empty());
    const auto &stack = require_smart_filter_stack(document, name);
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    for (const auto &entry : stack.entries) {
      CHECK(entry.kind == patchy::SmartFilterKind::SharpenEdges);
    }
    const auto actual = patchy::render_smart_filter_stack(
        source.pixels(), source.bounds(), canvas, stack);
    const auto expected_bounds = layer.bounds();
    CHECK(actual.bounds.x == expected_bounds.x &&
          actual.bounds.y == expected_bounds.y &&
          actual.bounds.width == expected_bounds.width &&
          actual.bounds.height == expected_bounds.height);
    if (actual.bounds.width != expected_bounds.width ||
        actual.bounds.height != expected_bounds.height) {
      continue;
    }
    std::size_t samples = 0;
    std::size_t off = 0;
    int largest = 0;
    for (std::int32_t y = 0; y < expected_bounds.height; ++y) {
      for (std::int32_t x = 0; x < expected_bounds.width; ++x) {
        const auto *want = layer.pixels().pixel(x, y);
        const auto *got = actual.pixels.pixel(x, y);
        if (want[3] == 0U && got[3] == 0U) {
          continue;
        }
        for (std::size_t channel = 0; channel < 4U; ++channel) {
          const auto difference =
              std::abs(int{want[channel]} - int{got[channel]});
          ++samples;
          off += difference != 0 ? 1U : 0U;
          largest = std::max(largest, difference);
        }
      }
    }
    std::cerr << name << ": " << off << " of " << samples
              << " samples differ, largest " << largest << "\n";
    CHECK(samples > 0U);
    CHECK(off * 60U <= samples);
    CHECK(largest <= 32);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_classic_tests() {
  return {
      {"psd_photoshop_classic_smart_filters_import_and_round_trip",
       psd_photoshop_classic_smart_filters_import_and_round_trip},
      {"smart_filter_classic_entry_with_a_filter_object_stays_unsupported",
       smart_filter_classic_entry_with_a_filter_object_stays_unsupported},
      {"smart_filter_classic_kernels_match_photoshop_fixture",
       smart_filter_classic_kernels_match_photoshop_fixture},
      {"smart_filter_radial_zoom_tracks_photoshop_fixture",
       smart_filter_radial_zoom_tracks_photoshop_fixture},
      {"smart_filter_average_matches_photoshop_probes",
       smart_filter_average_matches_photoshop_probes},
      {"destructive_classic_filters_share_the_native_kernels",
       destructive_classic_filters_share_the_native_kernels},
      {"smart_filter_effects_aligned_records_with_odd_lengths_stay_parsed",
       smart_filter_effects_aligned_records_with_odd_lengths_stay_parsed},
      {"psd_photoshop_smart_sharpen_legacy_renders_and_others_stay_locked",
       psd_photoshop_smart_sharpen_legacy_renders_and_others_stay_locked},
      {"smart_filter_sharpen_edges_tracks_photoshop_fixture",
       smart_filter_sharpen_edges_tracks_photoshop_fixture},
  };
}
