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
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
  // Largest RGB difference allowed on visible pixels; 0 is byte-exact.
  int rgb_tolerance{0};
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

  // Patchy's render of the stack against Photoshop's baked layer.
  const auto canvas = patchy::Rect{0, 0, original.width(), original.height()};
  const auto rendered =
      patchy::render_smart_filter_stack(source.pixels(), source.bounds(), canvas, *stack);
  const auto expected_bounds = filtered.bounds();
  CHECK(rendered.bounds.x == expected_bounds.x && rendered.bounds.y == expected_bounds.y);
  CHECK(rendered.bounds.width == expected_bounds.width &&
        rendered.bounds.height == expected_bounds.height);
  int worst_rgb = 0;
  int worst_alpha = 0;
  for (std::int32_t y = 0; y < expected_bounds.height; ++y) {
    for (std::int32_t x = 0; x < expected_bounds.width; ++x) {
      const auto* expected = filtered.pixels().pixel(x, y);
      const auto* actual = rendered.pixels.pixel(x, y);
      worst_alpha = std::max(worst_alpha, std::abs(expected[3] - actual[3]));
      if (expected[3] == 0) {
        continue;
      }
      for (int channel = 0; channel < 3; ++channel) {
        worst_rgb = std::max(worst_rgb, std::abs(expected[channel] - actual[channel]));
      }
    }
  }
  CHECK(worst_alpha == 0);
  CHECK(worst_rgb <= fixture.rgb_tolerance);
}

void psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render() {
  const std::vector<SchemaFixture> fixtures{
      {"photoshop-smart-filter-solarize.psd", patchy::SmartFilterKind::Solarize, {}},
      {"photoshop-smart-filter-find-edges.psd", patchy::SmartFilterKind::FindEdges, {}},
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
      {"psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render",
       psd_photoshop_distort_stylize_smart_filter_fixtures_round_trip_and_render},
  };
}
