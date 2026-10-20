// Table-described native Smart Filters (core/smart_filter_schema.hpp): schema
// consistency, descriptor authoring and patching for every schema, and the
// Photoshop fixtures of docs/smart-filters-distort-stylize.md.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_schema.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

patchy::SmartFilterStack single_entry_stack(const patchy::SmartFilterSchema& schema,
                                            patchy::SchemaSmartFilter settings) {
  patchy::SmartFilterStack stack;
  stack.support = patchy::SmartFilterStackSupport::Supported;
  stack.mask.linked = false;
  patchy::SmartFilterEntry entry;
  entry.kind = schema.kind;
  entry.native_name = schema.entry_name;
  entry.native_class_id = schema.class_id;
  entry.native_filter_id = schema.filter_id;
  entry.has_options = schema.has_filter_object;
  entry.parameters = std::move(settings);
  stack.entries.push_back(std::move(entry));
  return stack;
}

// Every field at its other extreme: maximum for integers, the last option,
// the flipped boolean. Distinct from the defaults wherever a field has room.
patchy::SchemaSmartFilter alternate_settings(const patchy::SmartFilterSchema& schema) {
  patchy::SchemaSmartFilter settings;
  for (const auto& field : schema.fields) {
    switch (field.type) {
      case patchy::SmartFilterFieldType::Integer:
        settings.values.push_back(field.default_value == field.maximum ? field.minimum
                                                                       : field.maximum);
        break;
      case patchy::SmartFilterFieldType::Enum:
        settings.values.push_back(static_cast<std::int32_t>(field.options.size()) - 1 ==
                                          field.default_value
                                      ? 0
                                      : static_cast<std::int32_t>(field.options.size()) - 1);
        break;
      case patchy::SmartFilterFieldType::Boolean:
        settings.values.push_back(field.default_value != 0 ? 0 : 1);
        break;
      case patchy::SmartFilterFieldType::PointList:
        settings.values.push_back(3);
        settings.points.insert(settings.points.end(), {10.0, 0.0, -12.5, 64.0, 7.0, 128.0});
        break;
    }
  }
  return settings;
}

void smart_filter_schema_tables_are_consistent() {
  std::set<int> kinds;
  std::set<std::string> classes;
  CHECK(!patchy::smart_filter_schemas().empty());
  for (const auto& schema : patchy::smart_filter_schemas()) {
    CHECK(kinds.insert(static_cast<int>(schema.kind)).second);
    CHECK(classes.insert(schema.class_id).second);
    CHECK(schema.kind != patchy::SmartFilterKind::Unsupported);
    CHECK(patchy::find_smart_filter_schema(schema.kind) == &schema);
    if (schema.has_filter_object) {
      CHECK(patchy::find_smart_filter_schema_for_class(schema.class_id, schema.filter_id) ==
            &schema);
      CHECK(patchy::find_smart_filter_schema_without_object(schema.filter_id) == nullptr);
    } else {
      CHECK(patchy::find_smart_filter_schema_without_object(schema.filter_id) == &schema);
      CHECK(schema.fields.empty());
    }
    CHECK(patchy::find_smart_filter_schema_for_class(schema.class_id, schema.filter_id + 1U) ==
          nullptr);
    CHECK(std::string_view(schema.invocation_id).starts_with("patchy.smart_filters."));
    const auto defaults = patchy::default_schema_settings(schema);
    CHECK(patchy::schema_settings_valid(schema, defaults));
    CHECK(patchy::schema_settings_valid(schema, alternate_settings(schema)));
    std::set<std::string> keys;
    for (const auto& field : schema.fields) {
      CHECK(keys.insert(field.key).second);
      if (field.type == patchy::SmartFilterFieldType::Integer) {
        CHECK(field.minimum <= field.default_value && field.default_value <= field.maximum);
        CHECK(field.minimum <= field.slider_minimum &&
              field.slider_minimum < field.slider_maximum &&
              field.slider_maximum <= field.maximum);
      } else if (field.type == patchy::SmartFilterFieldType::Enum) {
        CHECK(field.enum_type != nullptr && !field.options.empty());
      }
    }
    auto too_short = defaults;
    too_short.values.push_back(0);
    CHECK(!patchy::schema_settings_valid(schema, too_short));
  }
}

void smart_filter_schema_descriptors_author_parse_and_patch() {
  patchy::SmartObjectPlacement placement;
  placement.uuid = "41414141-5151-6161-8777-858585858585";
  placement.transform = {3.0, 5.0, 35.0, 5.0, 35.0, 29.0, 3.0, 29.0};
  placement.width = 32.0;
  placement.height = 24.0;
  placement.resolution = 72.0;
  const std::string placed_uuid = "dfdfdfdf-fbfb-bdbd-8444-616161616161";

  for (const auto& schema : patchy::smart_filter_schemas()) {
    const auto stack = single_entry_stack(schema, patchy::default_schema_settings(schema));
    const auto authored =
        patchy::psd::author_placed_layer_sold_payload(placement, placed_uuid, &stack);
    const auto info = patchy::psd::parse_placed_layer_block("SoLd", authored);
    CHECK(info.has_value() && info->smart_filters.has_value());
    CHECK(info->smart_filters->support == patchy::SmartFilterStackSupport::Supported);
    CHECK(info->smart_filters->entries.size() == 1U);
    const auto& parsed = info->smart_filters->entries.front();
    CHECK(parsed.kind == schema.kind);
    CHECK(parsed.native_name == schema.entry_name);
    CHECK(parsed.native_class_id == (schema.has_filter_object ? schema.class_id : ""));
    CHECK(parsed.has_options == schema.has_filter_object);
    CHECK(parsed.native_filter_id == schema.filter_id);
    CHECK(parsed.parameters == stack.entries.front().parameters);

    // Keys in schema order with the schema's types.
    patchy::psd::BigEndianReader reader(authored);
    CHECK(patchy::psd::key_string(patchy::psd::read_signature(reader)) == "soLD");
    CHECK(reader.read_u32() == 4U);
    CHECK(reader.read_u32() == 16U);
    const auto descriptor = patchy::psd::read_descriptor(reader);
    const auto* root = patchy::psd::descriptor_object(descriptor, "filterFX");
    CHECK(root != nullptr);
    const auto* list = patchy::psd::descriptor_value(*root, "filterFXList");
    CHECK(list != nullptr && list->list_value.size() == 1U &&
          list->list_value.front().object_value != nullptr);
    const auto* filter =
        patchy::psd::descriptor_object(*list->list_value.front().object_value, "Fltr");
    if (!schema.has_filter_object) {
      CHECK(filter == nullptr);
      continue;
    }
    CHECK(filter != nullptr && filter->class_id == schema.class_id &&
          filter->class_id_long_form == schema.class_id_long_form);
    CHECK(filter->name == schema.object_name);
    CHECK(filter->key_order.size() == schema.fields.size());
    for (std::size_t index = 0; index < schema.fields.size() && index < filter->key_order.size();
         ++index) {
      CHECK(filter->key_order[index].key == schema.fields[index].key);
    }

    auto edited = stack;
    edited.entries.front().parameters = alternate_settings(schema);
    const patchy::psd::SmartFilterDescriptorEdit edit{
        patchy::psd::SmartFilterDescriptorAction::Replace, &edited};
    const auto regenerated = patchy::psd::regenerate_placed_layer_payload(
        "SoLd", authored, placement, nullptr, placed_uuid, edit);
    CHECK(regenerated.has_value());
    const auto reread = patchy::psd::parse_placed_layer_block("SoLd", *regenerated);
    CHECK(reread.has_value() && reread->smart_filters.has_value());
    CHECK(reread->smart_filters->entries.front().kind == schema.kind);
    CHECK(reread->smart_filters->entries.front().parameters ==
          edited.entries.front().parameters);
  }
}

// Wave keeps Photoshop's units (COM probes, docs/smart-filters-distort-stylize.md):
// one generator of wavelength 50 and amplitude 10 moves pixels by up to 7.8125 px
// with a 100 px period, the horizontal shift depends only on the row, and a 0 %
// vertical scale leaves rows in place.
void smart_filter_wave_keeps_photoshop_units() {
  const auto* schema = patchy::find_smart_filter_schema(patchy::SmartFilterKind::Wave);
  CHECK(schema != nullptr);
  constexpr std::int32_t width = 256;
  constexpr std::int32_t height = 240;
  patchy::PixelBuffer ramp(width, height, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      auto* px = ramp.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x);
      px[1] = 0;
      px[2] = 0;
      px[3] = 255;
    }
  }
  auto stack = single_entry_stack(*schema, patchy::SchemaSmartFilter{{0, 1, 50, 50, 10, 10, 100, 0, 1, 7}});
  const patchy::Rect bounds{0, 0, width, height};
  const auto result = patchy::render_smart_filter_stack(ramp, bounds, bounds, stack);
  CHECK(result.bounds.width == width && result.bounds.height == height);
  std::vector<int> shift(static_cast<std::size_t>(height));
  int largest = 0;
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row_shift = static_cast<int>(result.pixels.pixel(128, y)[0]) - 128;
    shift[static_cast<std::size_t>(y)] = row_shift;
    largest = std::max(largest, std::abs(row_shift));
    for (std::int32_t x = 32; x < width - 32; x += 16) {
      CHECK(std::abs(static_cast<int>(result.pixels.pixel(x, y)[0]) - x - row_shift) <= 1);
    }
  }
  CHECK(largest >= 7 && largest <= 8);
  for (std::int32_t y = 0; y + 100 < height; ++y) {
    CHECK(std::abs(shift[static_cast<std::size_t>(y)] - shift[static_cast<std::size_t>(y + 100)]) <= 1);
  }
}

// Ripple's shift is linear in Amount, so Amount 0 leaves every pixel in place.
void smart_filter_ripple_amount_zero_is_identity() {
  const auto* schema = patchy::find_smart_filter_schema(patchy::SmartFilterKind::Ripple);
  CHECK(schema != nullptr);
  patchy::PixelBuffer pixels(40, 30, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 30; ++y) {
    for (std::int32_t x = 0; x < 40; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(x * 6);
      px[1] = static_cast<std::uint8_t>(y * 8);
      px[2] = static_cast<std::uint8_t>((x * y) & 255);
      px[3] = 255;
    }
  }
  const patchy::Rect bounds{0, 0, 40, 30};
  for (std::int32_t size = 0; size < 3; ++size) {
    const auto still = patchy::render_smart_filter_stack(
        pixels, bounds, bounds, single_entry_stack(*schema, patchy::SchemaSmartFilter{{0, size}}));
    CHECK(still.bounds.width == 40 && still.bounds.height == 30);
    CHECK(std::equal(still.pixels.data().begin(), still.pixels.data().end(),
                     pixels.data().begin()));
    const auto moved = patchy::render_smart_filter_stack(
        pixels, bounds, bounds, single_entry_stack(*schema, patchy::SchemaSmartFilter{{300, size}}));
    CHECK(!std::equal(moved.pixels.data().begin(), moved.pixels.data().end(),
                      pixels.data().begin()));
  }
}

const patchy::UnknownPsdBlock& require_sold_block(const patchy::Layer& layer) {
  const auto& blocks = layer.unknown_psd_blocks();
  const auto found = std::find_if(blocks.begin(), blocks.end(), [](const auto& block) {
    return block.key == "SoLd" || block.key == "SoLE";
  });
  CHECK(found != blocks.end());
  return *found;
}

// Every document-global block Patchy preserves, serialized, in file order.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> global_blocks(
    const patchy::Document& document) {
  std::vector<std::pair<std::size_t, std::pair<std::string, std::vector<std::uint8_t>>>> indexed;
  const auto& metadata = document.metadata();
  for (const auto& block : metadata.unknown_psd_resources) {
    indexed.push_back({block.original_global_index, {block.key, block.payload}});
  }
  for (const auto& block : metadata.smart_objects.blocks) {
    indexed.push_back({block.original_global_index,
                       {block.key, patchy::psd::serialize_linked_layer_block(block)}});
  }
  for (const auto& block : metadata.smart_filter_effects.blocks) {
    indexed.push_back({block.original_global_index,
                       {block.key, patchy::psd::serialize_filter_effects_block(block)}});
  }
  std::sort(indexed.begin(), indexed.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> result;
  for (auto& item : indexed) {
    result.push_back(std::move(item.second));
  }
  return result;
}

// One Photoshop capture per filter (docs/smart-filters-distort-stylize.md,
// "Fixtures"): "Layer 1" is the filtered Smart Object, "_source" a hidden copy
// of its unfiltered pixels. Checks the native fields, a clean resave that keeps
// SoLd and every global block byte-identical, an edit that re-reads, and
// Patchy's render of the stack against Photoshop's baked layer.
struct SchemaFixture {
  const char* file;
  patchy::SmartFilterKind kind;
  std::vector<std::int32_t> values;
  // Zero means byte-exact. Otherwise the documented tolerance of a filter whose
  // Photoshop samples cannot be reproduced exactly: mean absolute differences over
  // the union of both rasters (RGB where both are visible).
  double mean_rgb_tolerance{0.0};
  double mean_alpha_tolerance{0.0};
  // Photoshop draws this filter's pattern from a random generator probes cannot
  // reproduce: Patchy renders its own deterministic pattern, so only the render
  // itself (repeatable, inside the canvas, changed) is checked.
  bool own_pattern{false};
};

void check_schema_fixture(const SchemaFixture& fixture) {
  const auto* schema = patchy::find_smart_filter_schema(fixture.kind);
  CHECK(schema != nullptr);
  const auto original = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path(fixture.file));
  const auto& filtered = patchy::test::require_layer_named(original, "Layer 1");
  const auto& source = patchy::test::require_layer_named(original, "_source");
  CHECK(patchy::layer_is_smart_object(filtered));
  CHECK(patchy::smart_object_lock_reason(filtered).empty());
  const auto* stack = filtered.smart_filter_stack();
  CHECK(stack != nullptr && stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 1U);
  const auto& entry = stack->entries.front();
  CHECK(entry.kind == fixture.kind);
  CHECK(entry.native_name == schema->entry_name);
  CHECK(entry.native_filter_id == schema->filter_id);
  CHECK(entry.has_options == schema->has_filter_object);
  const auto* settings = std::get_if<patchy::SchemaSmartFilter>(&entry.parameters);
  CHECK(settings != nullptr && settings->values == fixture.values);

  // A clean resave keeps the native bytes.
  const auto original_globals = global_blocks(original);
  const auto original_sold = require_sold_block(filtered).payload;
  const auto clean =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(original));
  CHECK(global_blocks(clean) == original_globals);
  CHECK(require_sold_block(patchy::test::require_layer_named(clean, "Layer 1")).payload ==
        original_sold);

  // An edit patches SoLd only and re-reads.
  auto edited = original;
  auto* edited_layer = edited.find_layer(filtered.id());
  CHECK(edited_layer != nullptr);
  auto edited_stack = *edited_layer->smart_filter_stack();
  auto& edited_entry = edited_stack.entries.front();
  edited_entry.parameters = alternate_settings(*schema);
  edited_entry.opacity = 0.42;
  edited_entry.blend_mode = patchy::BlendMode::Screen;
  const auto edited_parameters = edited_entry.parameters;
  edited_layer->set_smart_filter_stack(std::move(edited_stack));
  patchy::mark_layer_smart_object_block_dirty(*edited_layer);
  const auto reread =
      patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(edited));
  const auto& reread_layer = patchy::test::require_layer_named(reread, "Layer 1");
  const auto* reread_stack = reread_layer.smart_filter_stack();
  CHECK(reread_stack != nullptr &&
        reread_stack->support == patchy::SmartFilterStackSupport::Supported);
  const auto& reread_entry = reread_stack->entries.front();
  CHECK(reread_entry.kind == fixture.kind);
  CHECK(reread_entry.parameters == edited_parameters);
  CHECK(std::abs(reread_entry.opacity - 0.42) < 1e-9);
  CHECK(reread_entry.blend_mode == patchy::BlendMode::Screen);
  CHECK(require_sold_block(reread_layer).payload != original_sold);
  CHECK(global_blocks(reread) == original_globals);

  // Patchy's render of the stack against Photoshop's baked layer, compared in
  // document space over the union of both rasters.
  const auto canvas = patchy::Rect{0, 0, original.width(), original.height()};
  const auto rendered =
      patchy::render_smart_filter_stack(source.pixels(), source.bounds(), canvas, *stack);
  const auto expected_bounds = filtered.bounds();
  if (fixture.own_pattern) {
    const auto again =
        patchy::render_smart_filter_stack(source.pixels(), source.bounds(), canvas, *stack);
    CHECK(again.bounds.x == rendered.bounds.x && again.bounds.y == rendered.bounds.y);
    CHECK(again.bounds.width == rendered.bounds.width &&
          again.bounds.height == rendered.bounds.height);
    CHECK(again.pixels.data().size() == rendered.pixels.data().size() &&
          std::equal(again.pixels.data().begin(), again.pixels.data().end(),
                     rendered.pixels.data().begin()));
    CHECK(rendered.bounds.x >= 0 && rendered.bounds.y >= 0 &&
          rendered.bounds.x + rendered.bounds.width <= canvas.width &&
          rendered.bounds.y + rendered.bounds.height <= canvas.height);
    // Patchy's own pattern for review (docs/smart-filters-distort-stylize.md):
    // a PAM image (RGB_ALPHA) of the render beside the PS fixture.
    std::filesystem::create_directories("test-artifacts");
    {
      std::ofstream pam(std::filesystem::path("test-artifacts") /
                            (std::string(fixture.file) + ".patchy.pam"),
                        std::ios::binary);
      pam << "P7\nWIDTH " << rendered.bounds.width << "\nHEIGHT " << rendered.bounds.height
          << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
      for (std::int32_t y = 0; y < rendered.bounds.height; ++y) {
        pam.write(reinterpret_cast<const char*>(rendered.pixels.pixel(0, y)),
                  static_cast<std::streamsize>(rendered.bounds.width) * 4);
      }
    }
    CHECK(!(rendered.bounds.x == source.bounds().x && rendered.bounds.y == source.bounds().y &&
            rendered.pixels.data().size() == source.pixels().data().size() &&
            std::equal(rendered.pixels.data().begin(), rendered.pixels.data().end(),
                       source.pixels().data().begin())));
    return;
  }
  const auto sample = [](const patchy::PixelBuffer& pixels, patchy::Rect bounds, std::int32_t x,
                         std::int32_t y) -> std::array<int, 4> {
    if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width ||
        y >= bounds.y + bounds.height) {
      return {0, 0, 0, 0};
    }
    const auto* px = pixels.pixel(x - bounds.x, y - bounds.y);
    return {px[0], px[1], px[2], px[3]};
  };
  int worst_rgb = 0;
  int worst_alpha = 0;
  double rgb_sum = 0.0;
  double alpha_sum = 0.0;
  std::size_t rgb_count = 0;
  std::size_t alpha_count = 0;
  for (std::int32_t y = 0; y < canvas.height; ++y) {
    for (std::int32_t x = 0; x < canvas.width; ++x) {
      const auto expected = sample(filtered.pixels(), expected_bounds, x, y);
      const auto actual = sample(rendered.pixels, rendered.bounds, x, y);
      if (expected[3] == 0 && actual[3] == 0) {
        continue;
      }
      const auto alpha_delta = std::abs(expected[3] - actual[3]);
      worst_alpha = std::max(worst_alpha, alpha_delta);
      alpha_sum += alpha_delta;
      ++alpha_count;
      if (expected[3] == 0 || actual[3] == 0) {
        continue;
      }
      for (int channel = 0; channel < 3; ++channel) {
        const auto delta = std::abs(expected[static_cast<std::size_t>(channel)] -
                                    actual[static_cast<std::size_t>(channel)]);
        worst_rgb = std::max(worst_rgb, delta);
        rgb_sum += delta;
        ++rgb_count;
      }
    }
  }
  CHECK(rgb_count > 0U && alpha_count > 0U);
  if (fixture.mean_rgb_tolerance == 0.0) {
    CHECK(rendered.bounds.x == expected_bounds.x && rendered.bounds.y == expected_bounds.y);
    CHECK(rendered.bounds.width == expected_bounds.width &&
          rendered.bounds.height == expected_bounds.height);
    CHECK(worst_alpha == 0);
    CHECK(worst_rgb == 0);
  } else {
    CHECK(rgb_sum / static_cast<double>(rgb_count) <= fixture.mean_rgb_tolerance);
    CHECK(alpha_sum / static_cast<double>(alpha_count) <= fixture.mean_alpha_tolerance);
  }
}

void psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render() {
  const std::vector<SchemaFixture> fixtures{
      {"photoshop-smart-filter-solarize.psd", patchy::SmartFilterKind::Solarize, {}},
      {"photoshop-smart-filter-find-edges.psd", patchy::SmartFilterKind::FindEdges, {}},
      // Amount 10, Ridges 5, Pond Ripples; Amount -25, Ridges 3, Around Center.
      {"photoshop-smart-filter-zigzag.psd", patchy::SmartFilterKind::ZigZag, {10, 5, 2}, 3.0, 2.0},
      {"photoshop-smart-filter-zigzag-around.psd", patchy::SmartFilterKind::ZigZag, {-25, 3, 0},
       3.0, 2.0},
      // Sine, 3 generators, wavelength 10..60, amplitude 2..8, 100 %, Repeat Edge Pixels.
      {"photoshop-smart-filter-wave.psd", patchy::SmartFilterKind::Wave,
       {0, 3, 10, 60, 2, 8, 100, 100, 1, 4242}, 0.0, 0.0, true},
      // Amount 150, Medium. Photoshop stored this entry's foreground color as HSB.
      {"photoshop-smart-filter-ripple.psd", patchy::SmartFilterKind::Ripple, {150, 1}, 0.0, 0.0,
       true},
      // Curve (0,0) (24,40) (-16,128), Wrap Around.
      // 8-bit rows sit up to 0.7 px off the 16-bit-calibrated curve; the fixture's noise
      // block magnifies that (docs/smart-filters-distort-stylize.md, Shear).
      {"photoshop-smart-filter-shear.psd", patchy::SmartFilterKind::Shear, {3, 0, 0, 2}, 8.0, 4.5},
      // Captured through ArtLayer.applyTwirl(120), applyPinch(60), applySpherize(80, Normal)
      // and applyPolarCoordinates(Rectangular to Polar).
      {"photoshop-smart-filter-twirl.psd", patchy::SmartFilterKind::Twirl, {120}, 3.0, 3.0},
      {"photoshop-smart-filter-pinch.psd", patchy::SmartFilterKind::Pinch, {60}, 3.0, 3.0},
      {"photoshop-smart-filter-spherize.psd", patchy::SmartFilterKind::Spherize, {80, 0}, 3.0, 3.0},
      {"photoshop-smart-filter-polar-coordinates.psd", patchy::SmartFilterKind::PolarCoordinates, {0},
       3.0, 3.0},
      // Level 140, Upper. Byte-exact on opaque probes; two pixels beside fully transparent
      // ones differ because PS's raster keeps different hidden RGB there.
      {"photoshop-smart-filter-trace-contour.psd", patchy::SmartFilterKind::TraceContour, {140, 1},
       0.5, 0.01},
  };
  for (const auto& fixture : fixtures) {
    check_schema_fixture(fixture);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_distort_stylize_tests() {
  return {
      {"smart_filter_schema_tables_are_consistent", smart_filter_schema_tables_are_consistent},
      {"smart_filter_schema_descriptors_author_parse_and_patch",
       smart_filter_schema_descriptors_author_parse_and_patch},
      {"smart_filter_wave_keeps_photoshop_units", smart_filter_wave_keeps_photoshop_units},
      {"smart_filter_ripple_amount_zero_is_identity", smart_filter_ripple_amount_zero_is_identity},
      {"psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render",
       psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render},
  };
}
