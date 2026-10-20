#pragma once

#include "core/smart_filter.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace patchy {

// Table-described native Smart Filters (the Distort and Stylize families,
// docs/smart-filters-distort-stylize.md). Each schema names Photoshop's `Fltr`
// class, its integer filterID, and the settings in Photoshop's key order;
// SchemaSmartFilter holds one integer per field in that order. Import, PSD
// authoring and patching, render validation, the settings dialog and the
// Layers panel all read these tables, so a filter is described once.
enum class SmartFilterFieldType : std::uint8_t {
  Integer,   // plain descriptor integer
  Enum,      // enumerated value; the integer is the option index
  Boolean,   // descriptor boolean; the integer is 0 or 1
  // A list of `Pnt ` objects with double `Hrzn`/`Vrtc` (Shear's curve); the
  // integer is the point count and SchemaSmartFilter::points holds the pairs.
  PointList,
};

struct SmartFilterFieldOption {
  const char* native_value;     // enum value id (charID or stringID)
  const char* parameter_value;  // FilterInvocation option key
  const char* label;            // untranslated "QObject" source text
};

struct SmartFilterField {
  const char* key;              // descriptor key (charID or stringID)
  const char* parameter_key;    // FilterInvocation parameter key
  const char* label;            // untranslated "QObject" source text
  SmartFilterFieldType type{SmartFilterFieldType::Integer};
  const char* enum_type{nullptr};
  std::span<const SmartFilterFieldOption> options{};
  std::int32_t minimum{0};
  std::int32_t maximum{0};
  std::int32_t default_value{0};
  // Dialog slider range; the spin box takes minimum..maximum.
  std::int32_t slider_minimum{0};
  std::int32_t slider_maximum{0};
  const char* suffix{nullptr};  // untranslated "QObject" source text
  bool angle{false};
  // Settings Photoshop stores but has no dialog control for (random seeds).
  bool hidden{false};
};

struct SmartFilterSchema {
  SmartFilterKind kind{SmartFilterKind::Unsupported};
  const char* class_id;
  bool class_id_long_form{false};
  std::uint32_t filter_id{0};
  const char* object_name;      // descriptor object name ("Twirl")
  const char* entry_name;       // filterFX `Nm  ` ("Twirl...")
  const char* display_name;     // untranslated "QObject" source text
  const char* invocation_id;    // "patchy.smart_filters.twirl"
  std::span<const SmartFilterField> fields{};
  // Filters without settings (Solarize, Find Edges) store no `Fltr` object and
  // `hasoptions` false; their entry is identified by `filterID` alone.
  bool has_filter_object{true};
};

[[nodiscard]] const SmartFilterSchema* find_smart_filter_schema(SmartFilterKind kind) noexcept;
[[nodiscard]] const SmartFilterSchema* find_smart_filter_schema_for_class(
    std::string_view class_id, std::uint32_t filter_id) noexcept;
// The schema of a filter that stores no `Fltr` object, by `filterID`.
[[nodiscard]] const SmartFilterSchema* find_smart_filter_schema_without_object(
    std::uint32_t filter_id) noexcept;
[[nodiscard]] std::span<const SmartFilterSchema> smart_filter_schemas() noexcept;

// The schema's default settings (every field's default_value).
[[nodiscard]] SchemaSmartFilter default_schema_settings(const SmartFilterSchema& schema);

// True when `settings` has one in-range value per schema field.
[[nodiscard]] bool schema_settings_valid(const SmartFilterSchema& schema,
                                         const SchemaSmartFilter& settings) noexcept;

}  // namespace patchy
