#include "core/smart_filter_table.hpp"

#include "core/smart_filter.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace patchy {

namespace {

using Type = SmartFilterTableFieldType;

SmartFilterTableField integer_field(const char *key, const char *parameter_key,
                                    const char *label, double minimum,
                                    double maximum, double default_value,
                                    double practical_minimum,
                                    double practical_maximum,
                                    const char *suffix = nullptr) {
  SmartFilterTableField field{key, false, Type::Integer, parameter_key, label};
  field.minimum = minimum;
  field.maximum = maximum;
  field.default_value = default_value;
  field.practical_minimum = practical_minimum;
  field.practical_maximum = practical_maximum;
  field.suffix = suffix;
  return field;
}

SmartFilterTableField unit_field(const char *key, const char *unit,
                                 bool whole_numbers, const char *parameter_key,
                                 const char *label, double minimum,
                                 double maximum, double default_value,
                                 double practical_minimum,
                                 double practical_maximum,
                                 const char *suffix) {
  SmartFilterTableField field{key, false, Type::UnitDouble, parameter_key,
                              label};
  field.unit = unit;
  field.whole_numbers = whole_numbers;
  field.minimum = minimum;
  field.maximum = maximum;
  field.default_value = default_value;
  field.practical_minimum = practical_minimum;
  field.practical_maximum = practical_maximum;
  field.suffix = suffix;
  return field;
}

SmartFilterTableField enum_field(const char *key, bool key_long_form,
                                 const char *enum_type,
                                 bool enum_type_long_form,
                                 const char *parameter_key, const char *label,
                                 std::vector<SmartFilterTableOption> options,
                                 const char *default_token) {
  SmartFilterTableField field{key, key_long_form, Type::Enumerated,
                              parameter_key, label};
  field.enum_type = enum_type;
  field.enum_type_long_form = enum_type_long_form;
  field.options = std::move(options);
  field.default_token = default_token;
  return field;
}

SmartFilterTableField list_field(const char *key, const char *label,
                                 double minimum, double maximum,
                                 std::uint32_t length) {
  SmartFilterTableField field{key, false, Type::IntegerList, nullptr, label};
  field.minimum = minimum;
  field.maximum = maximum;
  field.list_length = length;
  return field;
}

// Maximum and Minimum share Rds plus the optional preserveShape key; Photoshop
// writes Squareness as a stringID and Roundness as the charID 'Rndn'. Only
// Squareness is calibrated, so a Roundness entry stays unmodeled.
std::vector<SmartFilterTableField> morphology_fields() {
  auto radius = unit_field("Rds ", "#Pxl", false, "radius",
                           PATCHY_TRANSLATE_NOOP("QObject", "Radius"), 0.1,
                           1000.0, 1.0, 1.0, 100.0,
                           PATCHY_TRANSLATE_NOOP("QObject", " px"));
  auto shape = enum_field(
      "preserveShape", true, "preserveShape", true, nullptr, nullptr,
      {{"squareness", true, "squareness",
        PATCHY_TRANSLATE_NOOP("QObject", "Squareness")}},
      "squareness");
  shape.optional = true;
  return {radius, shape};
}

std::vector<SmartFilterTableSpec> make_table() {
  std::vector<SmartFilterTableSpec> table;
  // Offset (Filter > Other): plain integers Hrzn and Vrtc, then the
  // undefined-area enum (October 2026 captures).
  table.push_back(
      {SmartFilterKind::Offset, "Offset...", "Offset", "Ofst", false,
       0x4f667374U, PATCHY_TRANSLATE_NOOP("QObject", "Offset"),
       "patchy.smart_filters.offset", true,
       {integer_field("Hrzn", "horizontal",
                      PATCHY_TRANSLATE_NOOP("QObject", "Horizontal"),
                      -300000.0, 300000.0, 0.0, -1000.0, 1000.0,
                      PATCHY_TRANSLATE_NOOP("QObject", " px")),
        integer_field("Vrtc", "vertical",
                      PATCHY_TRANSLATE_NOOP("QObject", "Vertical"), -300000.0,
                      300000.0, 0.0, -1000.0, 1000.0,
                      PATCHY_TRANSLATE_NOOP("QObject", " px")),
        enum_field("Fl  ", false, "FlMd", false, "undefined_areas",
                   PATCHY_TRANSLATE_NOOP("QObject", "Undefined Areas"),
                   {{"Bckg", false, "transparent",
                     PATCHY_TRANSLATE_NOOP("QObject", "Set to Transparent")},
                    {"Rpt ", false, "repeat",
                     PATCHY_TRANSLATE_NOOP("QObject", "Repeat Edge Pixels")},
                    {"Wrp ", false, "wrap",
                     PATCHY_TRANSLATE_NOOP("QObject", "Wrap Around")}},
                   "wrap")}});
  table.push_back({SmartFilterKind::Maximum, "Maximum...", "Maximum", "Mxm ",
                   false, 0x4d786d20U,
                   PATCHY_TRANSLATE_NOOP("QObject", "Maximum"),
                   "patchy.smart_filters.maximum", true, morphology_fields()});
  table.push_back({SmartFilterKind::Minimum, "Minimum...", "Minimum", "Mnm ",
                   false, 0x4d6e6d20U,
                   PATCHY_TRANSLATE_NOOP("QObject", "Minimum"),
                   "patchy.smart_filters.minimum", true, morphology_fields()});
  // Custom: the 5x5 Mtrx list (row-major), then Scl and Ofst.
  table.push_back(
      {SmartFilterKind::Custom, "Custom...", "Custom", "Cstm", false,
       0x4373746dU, PATCHY_TRANSLATE_NOOP("QObject", "Custom"),
       "patchy.smart_filters.custom", true,
       {list_field("Mtrx", PATCHY_TRANSLATE_NOOP("QObject", "Matrix"), -999.0,
                   999.0, 25U),
        integer_field("Scl ", "scale",
                      PATCHY_TRANSLATE_NOOP("QObject", "Scale"), 1.0, 9999.0,
                      1.0, 1.0, 100.0),
        integer_field("Ofst", "offset",
                      PATCHY_TRANSLATE_NOOP("QObject", "Offset"), -9999.0,
                      9999.0, 0.0, -255.0, 255.0)}});
  // Fragment has no settings: Photoshop writes no Fltr object.
  table.push_back({SmartFilterKind::Fragment, "Fragment", "Fragment", nullptr,
                   false, 0x4672676dU,
                   PATCHY_TRANSLATE_NOOP("QObject", "Fragment"),
                   "patchy.smart_filters.fragment", false, {}});
  return table;
}

}  // namespace

const std::vector<SmartFilterTableSpec> &smart_filter_table() {
  static const auto table = make_table();
  return table;
}

const SmartFilterTableSpec *smart_filter_table_spec(
    SmartFilterKind kind) noexcept {
  for (const auto &spec : smart_filter_table()) {
    if (spec.kind == kind) {
      return &spec;
    }
  }
  return nullptr;
}

TableSmartFilter default_table_smart_filter(const SmartFilterTableSpec &spec) {
  TableSmartFilter filter;
  filter.values.reserve(spec.fields.size());
  for (const auto &field : spec.fields) {
    switch (field.type) {
      case Type::Integer:
        filter.values.emplace_back(
            static_cast<std::int32_t>(field.default_value));
        break;
      case Type::UnitDouble:
        filter.values.emplace_back(field.default_value);
        break;
      case Type::Enumerated:
        filter.values.emplace_back(std::string(
            field.default_token != nullptr ? field.default_token : ""));
        break;
      case Type::Boolean:
        filter.values.emplace_back(field.default_value != 0.0);
        break;
      case Type::IntegerList:
        filter.values.emplace_back(std::vector<std::int32_t>(
            field.list_length,
            static_cast<std::int32_t>(field.default_value)));
        break;
    }
  }
  return filter;
}

bool table_smart_filter_valid(const SmartFilterTableSpec &spec,
                              const TableSmartFilter &filter) {
  if (filter.values.size() != spec.fields.size()) {
    return false;
  }
  for (std::size_t index = 0; index < spec.fields.size(); ++index) {
    const auto &field = spec.fields[index];
    const auto &value = filter.values[index];
    switch (field.type) {
      case Type::Integer: {
        const auto *number = std::get_if<std::int32_t>(&value);
        if (number == nullptr || *number < field.minimum ||
            *number > field.maximum) {
          return false;
        }
        break;
      }
      case Type::UnitDouble: {
        const auto *number = std::get_if<double>(&value);
        if (number == nullptr || !std::isfinite(*number) ||
            *number < field.minimum || *number > field.maximum ||
            (field.whole_numbers && std::floor(*number) != *number)) {
          return false;
        }
        break;
      }
      case Type::Enumerated: {
        const auto *token = std::get_if<std::string>(&value);
        if (token == nullptr ||
            std::none_of(field.options.begin(), field.options.end(),
                         [token](const SmartFilterTableOption &option) {
                           return *token == option.token;
                         })) {
          return false;
        }
        break;
      }
      case Type::Boolean:
        if (!std::holds_alternative<bool>(value)) {
          return false;
        }
        break;
      case Type::IntegerList: {
        const auto *list = std::get_if<std::vector<std::int32_t>>(&value);
        if (list == nullptr || list->size() != field.list_length ||
            std::any_of(list->begin(), list->end(),
                        [&field](std::int32_t item) {
                          return item < field.minimum || item > field.maximum;
                        })) {
          return false;
        }
        break;
      }
    }
  }
  return true;
}

std::optional<std::size_t> table_field_index(const SmartFilterTableSpec &spec,
                                             const char *key) {
  for (std::size_t index = 0; index < spec.fields.size(); ++index) {
    if (std::strcmp(spec.fields[index].key, key) == 0) {
      return index;
    }
  }
  return std::nullopt;
}

}  // namespace patchy
