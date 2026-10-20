#include "psd/psd_smart_filter_table.hpp"

#include <cmath>
#include <optional>
#include <string>

namespace patchy::psd {

namespace {

using Type = SmartFilterTableFieldType;

const SmartFilterTableOption *option_for_native(
    const SmartFilterTableField &field, const std::string &native_value) {
  for (const auto &option : field.options) {
    if (native_value == option.native_value) {
      return &option;
    }
  }
  return nullptr;
}

const SmartFilterTableOption *option_for_token(
    const SmartFilterTableField &field, const std::string &token) {
  for (const auto &option : field.options) {
    if (token == option.token) {
      return &option;
    }
  }
  return nullptr;
}

std::optional<SmartFilterTableValue> read_field(
    const DescriptorObject &filter, const SmartFilterTableField &field,
    const SmartFilterTableValue &default_value) {
  const auto *value = descriptor_value(filter, field.key);
  if (value == nullptr) {
    if (field.optional) {
      return default_value;
    }
    return std::nullopt;
  }
  switch (field.type) {
    case Type::Integer:
      if (value->type != DescriptorValue::Type::Integer ||
          value->integer_value < field.minimum ||
          value->integer_value > field.maximum) {
        return std::nullopt;
      }
      return SmartFilterTableValue{value->integer_value};
    case Type::UnitDouble:
      if (value->type != DescriptorValue::Type::UnitFloat ||
          value->unit != field.unit || !std::isfinite(value->double_value) ||
          value->double_value < field.minimum ||
          value->double_value > field.maximum ||
          (field.whole_numbers &&
           std::floor(value->double_value) != value->double_value)) {
        return std::nullopt;
      }
      return SmartFilterTableValue{value->double_value};
    case Type::Enumerated: {
      if (value->type != DescriptorValue::Type::Enum ||
          value->enum_type != field.enum_type) {
        return std::nullopt;
      }
      const auto *option = option_for_native(field, value->enum_value);
      if (option == nullptr) {
        return std::nullopt;
      }
      return SmartFilterTableValue{std::string(option->token)};
    }
    case Type::Boolean:
      if (value->type != DescriptorValue::Type::Bool) {
        return std::nullopt;
      }
      return SmartFilterTableValue{value->bool_value};
    case Type::IntegerList: {
      if (value->type != DescriptorValue::Type::List ||
          value->list_value.size() != field.list_length) {
        return std::nullopt;
      }
      std::vector<std::int32_t> items;
      items.reserve(value->list_value.size());
      for (const auto &item : value->list_value) {
        if (item.type != DescriptorValue::Type::Integer ||
            item.integer_value < field.minimum ||
            item.integer_value > field.maximum) {
          return std::nullopt;
        }
        items.push_back(item.integer_value);
      }
      return SmartFilterTableValue{std::move(items)};
    }
  }
  return std::nullopt;
}

DescriptorValue integer_value(std::int32_t number) {
  DescriptorValue value;
  value.type = DescriptorValue::Type::Integer;
  value.integer_value = number;
  return value;
}

std::optional<DescriptorValue> authored_value(
    const SmartFilterTableField &field, const SmartFilterTableValue &source) {
  DescriptorValue value;
  switch (field.type) {
    case Type::Integer:
      if (const auto *number = std::get_if<std::int32_t>(&source)) {
        return integer_value(*number);
      }
      return std::nullopt;
    case Type::UnitDouble:
      if (const auto *number = std::get_if<double>(&source)) {
        value.type = DescriptorValue::Type::UnitFloat;
        value.unit = field.unit;
        value.double_value = *number;
        return value;
      }
      return std::nullopt;
    case Type::Enumerated: {
      const auto *token = std::get_if<std::string>(&source);
      const auto *option =
          token != nullptr ? option_for_token(field, *token) : nullptr;
      if (option == nullptr) {
        return std::nullopt;
      }
      value.type = DescriptorValue::Type::Enum;
      value.enum_type = field.enum_type;
      value.enum_type_long_form = field.enum_type_long_form;
      value.enum_value = option->native_value;
      value.enum_value_long_form = option->native_value_long_form;
      return value;
    }
    case Type::Boolean:
      if (const auto *flag = std::get_if<bool>(&source)) {
        value.type = DescriptorValue::Type::Bool;
        value.bool_value = *flag;
        return value;
      }
      return std::nullopt;
    case Type::IntegerList:
      if (const auto *list = std::get_if<std::vector<std::int32_t>>(&source)) {
        value.type = DescriptorValue::Type::List;
        for (const auto item : *list) {
          value.list_value.push_back(integer_value(item));
        }
        return value;
      }
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

bool parse_table_smart_filter(const DescriptorObject *filter,
                              std::uint32_t filter_id, SmartFilterEntry &entry,
                              bool &entry_supported) {
  for (const auto &spec : smart_filter_table()) {
    if (spec.class_id == nullptr) {
      // Photoshop writes no Fltr object for a filter without settings.
      if (filter != nullptr || filter_id != spec.filter_id) {
        continue;
      }
      entry.kind = spec.kind;
      entry.parameters = TableSmartFilter{};
      return true;
    }
    if (filter == nullptr || filter->class_id != spec.class_id ||
        filter->class_id_long_form != spec.class_id_long_form) {
      continue;
    }
    if (filter_id != spec.filter_id) {
      entry_supported = false;
      return true;
    }
    const auto defaults = default_table_smart_filter(spec);
    TableSmartFilter values;
    values.values.reserve(spec.fields.size());
    for (std::size_t index = 0; index < spec.fields.size(); ++index) {
      const auto &field = spec.fields[index];
      auto value = read_field(*filter, field, defaults.values[index]);
      if (!value.has_value()) {
        entry_supported = false;
        return true;
      }
      values.values.push_back(std::move(*value));
    }
    entry.kind = spec.kind;
    entry.parameters = std::move(values);
    return true;
  }
  return false;
}

void append_table_smart_filter_fields(DescriptorObject &filter,
                                      const SmartFilterTableSpec &spec,
                                      const TableSmartFilter &values) {
  for (std::size_t index = 0;
       index < spec.fields.size() && index < values.values.size(); ++index) {
    const auto &field = spec.fields[index];
    auto value = authored_value(field, values.values[index]);
    if (!value.has_value()) {
      continue;
    }
    filter.key_order.push_back({field.key, field.key_long_form});
    filter.values.emplace(field.key, std::move(*value));
  }
}

bool patch_table_smart_filter_fields(DescriptorObject &filter,
                                     const SmartFilterTableSpec &spec,
                                     const TableSmartFilter &values) {
  if (values.values.size() != spec.fields.size()) {
    return false;
  }
  const auto defaults = default_table_smart_filter(spec);
  for (std::size_t index = 0; index < spec.fields.size(); ++index) {
    const auto &field = spec.fields[index];
    auto found = filter.values.find(field.key);
    if (found == filter.values.end()) {
      if (!field.optional) {
        return false;
      }
      // Photoshop omitted the key, which reads as the default: write it only
      // when the value no longer is the default, after the existing keys.
      if (values.values[index] == defaults.values[index]) {
        continue;
      }
      auto value = authored_value(field, values.values[index]);
      if (!value.has_value()) {
        return false;
      }
      filter.key_order.push_back({field.key, field.key_long_form});
      filter.values.emplace(field.key, std::move(*value));
      continue;
    }
    auto &target = found->second;
    const auto &source = values.values[index];
    switch (field.type) {
      case Type::Integer: {
        const auto *number = std::get_if<std::int32_t>(&source);
        if (number == nullptr || target.type != DescriptorValue::Type::Integer) {
          return false;
        }
        target.integer_value = *number;
        break;
      }
      case Type::UnitDouble: {
        const auto *number = std::get_if<double>(&source);
        if (number == nullptr ||
            target.type != DescriptorValue::Type::UnitFloat ||
            target.unit != field.unit) {
          return false;
        }
        target.double_value = *number;
        break;
      }
      case Type::Enumerated: {
        const auto *token = std::get_if<std::string>(&source);
        const auto *option =
            token != nullptr ? option_for_token(field, *token) : nullptr;
        if (option == nullptr || target.type != DescriptorValue::Type::Enum ||
            target.enum_type != field.enum_type) {
          return false;
        }
        // An unchanged value keeps its imported id form.
        if (target.enum_value != option->native_value) {
          target.enum_value = option->native_value;
          target.enum_value_long_form = option->native_value_long_form;
        }
        break;
      }
      case Type::Boolean: {
        const auto *flag = std::get_if<bool>(&source);
        if (flag == nullptr || target.type != DescriptorValue::Type::Bool) {
          return false;
        }
        target.bool_value = *flag;
        break;
      }
      case Type::IntegerList: {
        const auto *list = std::get_if<std::vector<std::int32_t>>(&source);
        if (list == nullptr || target.type != DescriptorValue::Type::List ||
            target.list_value.size() != list->size()) {
          return false;
        }
        for (std::size_t item = 0; item < list->size(); ++item) {
          if (target.list_value[item].type != DescriptorValue::Type::Integer) {
            return false;
          }
          target.list_value[item].integer_value = (*list)[item];
        }
        break;
      }
    }
  }
  return true;
}

}  // namespace patchy::psd
