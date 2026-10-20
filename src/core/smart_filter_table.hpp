#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace patchy {

enum class SmartFilterKind;

// Table-described native Smart Filters (docs/smart-filters-pixelate-render.md).
// Each filter's Photoshop descriptor is a flat list of fields; one table entry
// per filter drives the PSD parser, the authored and patched descriptors, the
// settings dialog, and the layer-row tooltip, so a filter is a table row plus
// its render kernel. Field order is Photoshop's key order inside `Fltr`.
enum class SmartFilterTableFieldType : std::uint8_t {
  Integer,        // 'long'
  UnitDouble,     // 'UntF' with `unit`
  Enumerated,     // 'enum' of `enum_type`, value one of `options`
  Boolean,        // 'bool'
  IntegerList,    // 'VlLs' of exactly `list_length` 'long' items
};

struct SmartFilterTableOption {
  const char *native_value;   // the descriptor's enum value id
  bool native_value_long_form;
  const char *token;          // the dialog/invocation token
  const char *label;          // English, PATCHY_TRANSLATE_NOOP("QObject")
};

struct SmartFilterTableField {
  const char *key;
  bool key_long_form;
  SmartFilterTableFieldType type;
  // Invocation parameter key; empty for a field with no dialog control (a
  // stored random seed), which keeps its imported or authored value.
  const char *parameter_key;
  const char *label;  // English, PATCHY_TRANSLATE_NOOP("QObject"); may be null
  double minimum{0.0};
  double maximum{0.0};
  double default_value{0.0};
  // Slider range; the spin box keeps minimum..maximum.
  double practical_minimum{0.0};
  double practical_maximum{0.0};
  // UnitDouble only: '#Pxl', '#Prc' or '#Ang', and whether Photoshop only
  // produces whole numbers there (fractional imports stay unsupported).
  const char *unit{nullptr};
  bool whole_numbers{true};
  const char *suffix{nullptr};  // English dialog suffix (" px", " %")
  // Enumerated only.
  const char *enum_type{nullptr};
  bool enum_type_long_form{false};
  std::vector<SmartFilterTableOption> options{};
  const char *default_token{nullptr};
  // IntegerList only.
  std::uint32_t list_length{0};
  // Photoshop may omit the key; an absent key reads as the default and an
  // imported entry without it gets it written only when the value changes.
  bool optional{false};
};

struct SmartFilterTableSpec {
  SmartFilterKind kind;
  const char *entry_name;       // filterFX 'Nm  ', e.g. "Offset..."
  const char *filter_name;      // the Fltr object's name, e.g. "Offset"
  // The Fltr class id; null for a filter without settings, whose entry has no
  // Fltr object at all (Photoshop identifies it by filterID alone).
  const char *class_id;
  bool class_id_long_form;
  std::uint32_t filter_id;      // filterFX 'filterID'
  const char *display_name;     // English, PATCHY_TRANSLATE_NOOP("QObject")
  const char *invocation_id;    // "patchy.smart_filters.<name>"
  bool has_options;             // Photoshop's 'hasoptions' for a new entry
  std::vector<SmartFilterTableField> fields;
};

using SmartFilterTableValue =
    std::variant<std::int32_t, double, std::string, bool,
                 std::vector<std::int32_t>>;

// Settings of a table-described filter: one value per field of its spec, in
// table order (Integer: int32, UnitDouble: double, Enumerated: the option
// token, Boolean: bool, IntegerList: the list).
struct TableSmartFilter {
  std::vector<SmartFilterTableValue> values;
  bool operator==(const TableSmartFilter &) const = default;
};

// Null for every kind outside the table.
[[nodiscard]] const SmartFilterTableSpec *
smart_filter_table_spec(SmartFilterKind kind) noexcept;
[[nodiscard]] const std::vector<SmartFilterTableSpec> &smart_filter_table();

// The spec's defaults.
[[nodiscard]] TableSmartFilter default_table_smart_filter(
    const SmartFilterTableSpec &spec);

// Whether `filter` has one in-range value of the right type per field.
[[nodiscard]] bool table_smart_filter_valid(const SmartFilterTableSpec &spec,
                                            const TableSmartFilter &filter);

[[nodiscard]] std::optional<std::size_t> table_field_index(
    const SmartFilterTableSpec &spec, const char *key);

}  // namespace patchy
