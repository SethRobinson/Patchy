#include "ui/smart_filter_table_ui.hpp"

#include "ui/localization.hpp"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <string>

namespace patchy::ui {

namespace {

using Type = SmartFilterTableFieldType;

bool field_has_control(const SmartFilterTableField &field) {
  return field.parameter_key != nullptr && field.parameter_key[0] != '\0' &&
         field.type != Type::IntegerList;
}

// Whole-number fields travel as integers through the dialog.
bool field_is_integral(const SmartFilterTableField &field) {
  return field.type == Type::Integer ||
         (field.type == Type::UnitDouble && field.whole_numbers);
}

std::optional<double> invocation_number(const FilterInvocation &invocation,
                                        const char *key) {
  const auto found = invocation.parameters.find(key);
  if (found == invocation.parameters.end()) {
    return std::nullopt;
  }
  if (const auto *integer = std::get_if<std::int64_t>(&found->second)) {
    return static_cast<double>(*integer);
  }
  if (const auto *number = std::get_if<double>(&found->second)) {
    return *number;
  }
  return std::nullopt;
}

QString number_text(double value) {
  auto text = QString::number(value, 'f', 2);
  while (text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(QLatin1Char('.'))) {
    text.chop(1);
  }
  return text;
}

}  // namespace

FilterInvocation table_smart_filter_invocation(const SmartFilterTableSpec &spec,
                                               const TableSmartFilter &values) {
  FilterInvocation invocation;
  invocation.schema_version = 1U;
  invocation.filter_id = spec.invocation_id;
  for (std::size_t index = 0;
       index < spec.fields.size() && index < values.values.size(); ++index) {
    const auto &field = spec.fields[index];
    if (!field_has_control(field)) {
      continue;
    }
    const auto &value = values.values[index];
    if (const auto *integer = std::get_if<std::int32_t>(&value)) {
      invocation.parameters[field.parameter_key] =
          static_cast<std::int64_t>(*integer);
    } else if (const auto *number = std::get_if<double>(&value)) {
      if (field_is_integral(field)) {
        invocation.parameters[field.parameter_key] =
            static_cast<std::int64_t>(std::llround(*number));
      } else {
        invocation.parameters[field.parameter_key] = *number;
      }
    } else if (const auto *token = std::get_if<std::string>(&value)) {
      invocation.parameters[field.parameter_key] = *token;
    } else if (const auto *flag = std::get_if<bool>(&value)) {
      invocation.parameters[field.parameter_key] = *flag;
    }
  }
  return invocation;
}

FilterDialogSpec table_smart_filter_dialog_spec(
    const SmartFilterTableSpec &spec) {
  FilterDialogSpec dialog;
  dialog.identifier = QString::fromUtf8(spec.invocation_id);
  dialog.display_name = table_smart_filter_display_name(spec);
  const auto defaults = default_table_smart_filter(spec);
  for (std::size_t index = 0; index < spec.fields.size(); ++index) {
    const auto &field = spec.fields[index];
    if (!field_has_control(field)) {
      continue;
    }
    FilterControlSpec control;
    control.label = field.label != nullptr ? translate_data_text(field.label)
                                           : QString();
    const auto key = std::string(field.parameter_key);
    // Object names follow the existing filter dialogs: "filter" plus the
    // CamelCase parameter key ("cell_size" -> "filterCellSize").
    QString object_name = QStringLiteral("filter");
    bool upper = true;
    for (const auto character : key) {
      if (character == '_') {
        upper = true;
        continue;
      }
      object_name += upper ? QChar(character).toUpper() : QChar(character);
      upper = false;
    }
    control.object_name = object_name;
    control.parameter_key = key;
    if (field.suffix != nullptr) {
      control.suffix = translate_data_text(field.suffix);
    }
    switch (field.type) {
      case Type::Integer:
      case Type::UnitDouble:
        control.minimum = static_cast<int>(std::floor(field.practical_minimum));
        control.maximum = static_cast<int>(std::ceil(field.practical_maximum));
        control.value = static_cast<int>(std::lround(field.default_value));
        control.typed_minimum = field.minimum;
        control.typed_maximum = field.maximum;
        if (field_is_integral(field)) {
          control.kind = FilterParameterKind::Integer;
          control.default_value =
              static_cast<std::int64_t>(std::llround(field.default_value));
          control.step = 1.0;
        } else {
          control.kind = FilterParameterKind::Double;
          control.default_value = field.default_value;
          control.step = 0.01;
        }
        break;
      case Type::Enumerated:
        control.kind = FilterParameterKind::Option;
        control.default_value = std::string(
            field.default_token != nullptr ? field.default_token : "");
        for (const auto &option : field.options) {
          control.options.push_back({option.token, option.label});
        }
        break;
      case Type::Boolean:
        control.kind = FilterParameterKind::Boolean;
        control.default_value =
            std::get_if<bool>(&defaults.values[index]) != nullptr &&
            std::get<bool>(defaults.values[index]);
        break;
      case Type::IntegerList:
        break;
    }
    dialog.controls.push_back(std::move(control));
  }
  return dialog;
}

TableSmartFilter table_smart_filter_from_invocation(
    const SmartFilterTableSpec &spec, const FilterInvocation &invocation,
    const TableSmartFilter &base) {
  auto result = base;
  if (result.values.size() != spec.fields.size()) {
    result = default_table_smart_filter(spec);
  }
  for (std::size_t index = 0; index < spec.fields.size(); ++index) {
    const auto &field = spec.fields[index];
    if (!field_has_control(field)) {
      continue;
    }
    auto &value = result.values[index];
    switch (field.type) {
      case Type::Integer:
        if (const auto number = invocation_number(invocation,
                                                  field.parameter_key)) {
          value = static_cast<std::int32_t>(std::clamp(
              std::round(*number), field.minimum, field.maximum));
        }
        break;
      case Type::UnitDouble:
        if (const auto number = invocation_number(invocation,
                                                  field.parameter_key)) {
          auto clamped = std::clamp(*number, field.minimum, field.maximum);
          if (field.whole_numbers) {
            clamped = std::round(clamped);
          }
          value = clamped;
        }
        break;
      case Type::Enumerated: {
        const auto found = invocation.parameters.find(field.parameter_key);
        if (found != invocation.parameters.end()) {
          if (const auto *token = std::get_if<std::string>(&found->second);
              token != nullptr &&
              std::any_of(field.options.begin(), field.options.end(),
                          [token](const SmartFilterTableOption &option) {
                            return *token == option.token;
                          })) {
            value = *token;
          }
        }
        break;
      }
      case Type::Boolean: {
        const auto found = invocation.parameters.find(field.parameter_key);
        if (found != invocation.parameters.end()) {
          if (const auto *flag = std::get_if<bool>(&found->second)) {
            value = *flag;
          }
        }
        break;
      }
      case Type::IntegerList:
        break;
    }
  }
  return result;
}

QString table_smart_filter_display_name(const SmartFilterTableSpec &spec) {
  return translate_data_text(spec.display_name);
}

QString table_smart_filter_tooltip_details(const SmartFilterTableSpec &spec,
                                           const TableSmartFilter &values) {
  QStringList parts;
  for (std::size_t index = 0;
       index < spec.fields.size() && index < values.values.size(); ++index) {
    const auto &field = spec.fields[index];
    if (!field_has_control(field) || field.label == nullptr) {
      continue;
    }
    const auto &value = values.values[index];
    QString text;
    if (const auto *integer = std::get_if<std::int32_t>(&value)) {
      text = QString::number(*integer);
    } else if (const auto *number = std::get_if<double>(&value)) {
      text = number_text(*number);
    } else if (const auto *token = std::get_if<std::string>(&value)) {
      for (const auto &option : field.options) {
        if (*token == option.token) {
          text = translate_data_text(option.label);
        }
      }
    } else if (const auto *flag = std::get_if<bool>(&value)) {
      text = *flag ? QObject::tr("On") : QObject::tr("Off");
    }
    if (field.suffix != nullptr &&
        (field.type == Type::Integer || field.type == Type::UnitDouble)) {
      text += translate_data_text(field.suffix);
    }
    //: Layer-row tooltip detail of a Smart Filter setting: "Radius 3 px".
    parts << QObject::tr("%1 %2").arg(translate_data_text(field.label), text);
  }
  if (parts.isEmpty()) {
    return {};
  }
  return QStringLiteral(" (") + parts.join(QStringLiteral(", ")) +
         QStringLiteral(")");
}

}  // namespace patchy::ui
