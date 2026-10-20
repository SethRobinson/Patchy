#include "core/smart_filter_schema.hpp"

#include "support/translate_noop.hpp"

#include <array>

namespace patchy {
namespace {

// Keys, key order, types and ranges come from Photoshop 2026 COM captures and
// its filter dialogs (docs/smart-filters-distort-stylize.md). Never reorder
// fields: SchemaSmartFilter::values follows this order.

constexpr std::array kSchemas{
    // Neither stores a Fltr object; the entry name has no ellipsis.
    SmartFilterSchema{SmartFilterKind::Solarize, "Slrz", false, 0x536c727aU, "Solarize",
                      "Solarize", PATCHY_TRANSLATE_NOOP("QObject", "Solarize"),
                      "patchy.smart_filters.solarize", {}, false},
    SmartFilterSchema{SmartFilterKind::FindEdges, "FndE", false, 0x466e6445U, "Find Edges",
                      "Find Edges", PATCHY_TRANSLATE_NOOP("QObject", "Find Edges"),
                      "patchy.smart_filters.find_edges", {}, false},
};

}  // namespace

std::span<const SmartFilterSchema> smart_filter_schemas() noexcept { return kSchemas; }

const SmartFilterSchema* find_smart_filter_schema(SmartFilterKind kind) noexcept {
  for (const auto& schema : kSchemas) {
    if (schema.kind == kind) {
      return &schema;
    }
  }
  return nullptr;
}

const SmartFilterSchema* find_smart_filter_schema_for_class(std::string_view class_id,
                                                            std::uint32_t filter_id) noexcept {
  for (const auto& schema : kSchemas) {
    if (schema.has_filter_object && class_id == schema.class_id &&
        filter_id == schema.filter_id) {
      return &schema;
    }
  }
  return nullptr;
}

const SmartFilterSchema* find_smart_filter_schema_without_object(
    std::uint32_t filter_id) noexcept {
  for (const auto& schema : kSchemas) {
    if (!schema.has_filter_object && filter_id == schema.filter_id) {
      return &schema;
    }
  }
  return nullptr;
}

SchemaSmartFilter default_schema_settings(const SmartFilterSchema& schema) {
  SchemaSmartFilter settings;
  settings.values.reserve(schema.fields.size());
  for (const auto& field : schema.fields) {
    settings.values.push_back(field.default_value);
  }
  return settings;
}

bool schema_settings_valid(const SmartFilterSchema& schema,
                           const SchemaSmartFilter& settings) noexcept {
  if (settings.values.size() != schema.fields.size()) {
    return false;
  }
  for (std::size_t index = 0; index < schema.fields.size(); ++index) {
    const auto& field = schema.fields[index];
    const auto value = settings.values[index];
    switch (field.type) {
      case SmartFilterFieldType::Integer:
        if (value < field.minimum || value > field.maximum) {
          return false;
        }
        break;
      case SmartFilterFieldType::Enum:
        if (value < 0 || static_cast<std::size_t>(value) >= field.options.size()) {
          return false;
        }
        break;
      case SmartFilterFieldType::Boolean:
        if (value != 0 && value != 1) {
          return false;
        }
        break;
    }
  }
  return true;
}

}  // namespace patchy
