// GIMP 3 procedure settings for Patchy's own dialog (gimp_plugin_arguments.hpp).

#include "ui/gimp_plugin_arguments.hpp"

#include "plugins/gimp/gimp_procedure.hpp"
#include "plugins/gimp/gimp_wire_codec.hpp"
#include "ui/gimp_plugin_catalog.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <climits>
#include <cstring>

namespace patchy::ui {

namespace {

// Plug-ins whose dialog holds settings that are not among their arguments (presets,
// drawings, parameter files, a random pattern picked in the dialog): they always open
// their own dialog.
constexpr const char* kOwnDialogProcedures[] = {
    "plug-in-gimpressionist", "plug-in-gfig",   "plug-in-cml-explorer", "plug-in-spheredesigner",
    "plug-in-ifscompose",     "plug-in-qbist",  "plug-in-imagemap",
};

QString name_of(const gimp::ParamDef& def) { return QString::fromStdString(def.name.value_or("")); }

bool is_layer_type(const std::string& value_type) {
  return value_type == "GimpDrawable" || value_type == "GimpLayer";
}

}  // namespace

GimpPluginArgumentSet gimp_plugin_arguments(const GimpPluginProcedure& entry) {
  GimpPluginArgumentSet set;
  const auto procedure = entry.procedure.toStdString();
  for (const auto* name : kOwnDialogProcedures) {
    if (procedure == name) {
      set.own_dialog_reason = QStringLiteral("its dialog holds settings that are not among its arguments");
    }
  }
  try {
    gimp::SpanSource source({reinterpret_cast<const std::uint8_t*>(entry.install.constData()),
                             static_cast<std::size_t>(entry.install.size())});
    const auto install = gimp::read_message(source);
    set.defs = install.install.params;
  } catch (const gimp::WireError& e) {
    set.own_dialog_reason = QStringLiteral("its registration could not be read (%1)").arg(QString::fromUtf8(e.what()));
    return set;
  }
  for (std::size_t i = 3; i < set.defs.size(); ++i) {
    const auto& def = set.defs[i];
    GimpPluginArgument argument;
    argument.def = def;
    argument.key = name_of(def);
    // GIMP's labels carry mnemonic underscores ("E_ffect Channel").
    argument.label = QString::fromStdString(gimp::strip_mnemonic(def.nick.value_or(def.name.value_or(""))));
    argument.help = QString::fromStdString(def.blurb.value_or(""));
    const auto value_type = def.value_type_name.value_or("");
    bool shown = true;
    switch (def.kind) {
      case gimp::ParamDefKind::Int:
        argument.widget = GimpArgumentWidget::Integer;
        break;
      case gimp::ParamDefKind::Boolean:
        argument.widget = GimpArgumentWidget::Boolean;
        break;
      case gimp::ParamDefKind::Double:
        argument.widget = GimpArgumentWidget::Double;
        break;
      case gimp::ParamDefKind::Choice:
        argument.widget = GimpArgumentWidget::Choice;
        shown = !def.choices.empty();
        break;
      case gimp::ParamDefKind::GeglColor:
        argument.widget = GimpArgumentWidget::Color;
        break;
      case gimp::ParamDefKind::String:
        argument.widget = GimpArgumentWidget::Text;
        break;
      case gimp::ParamDefKind::Id:
        argument.widget = GimpArgumentWidget::Layer;
        shown = is_layer_type(value_type);
        break;
      default:
        shown = false;
        break;
    }
    if (!shown) {
      if (set.own_dialog_reason.isEmpty()) {
        set.own_dialog_reason =
            QStringLiteral("its argument %1 (%2) is not a setting Patchy's dialog can show")
                .arg(argument.key, QString::fromStdString(value_type));
      }
      continue;
    }
    set.editable.push_back(std::move(argument));
  }
  if (set.defs.size() < 3 && set.own_dialog_reason.isEmpty()) {
    set.own_dialog_reason = QStringLiteral("it does not take GIMP's image-procedure arguments");
  }
  return set;
}

GimpPluginValues gimp_plugin_default_values(const GimpPluginArgumentSet& arguments) {
  GimpPluginValues values;
  for (const auto& a : arguments.editable) {
    const auto& def = a.def;
    switch (a.widget) {
      case GimpArgumentWidget::Integer:
        values[a.key] = static_cast<int>(std::clamp<std::int64_t>(def.int_default, INT_MIN, INT_MAX));
        break;
      case GimpArgumentWidget::Boolean:
        values[a.key] = def.int32_default != 0;
        break;
      case GimpArgumentWidget::Double:
        values[a.key] = def.double_default;
        break;
      case GimpArgumentWidget::Choice: {
        auto nick = QString::fromStdString(def.string_default.value_or(""));
        if (nick.isEmpty() && !def.choices.empty()) {
          nick = QString::fromStdString(def.choices.front().nick.value_or(""));
        }
        values[a.key] = nick;
        break;
      }
      case GimpArgumentWidget::Color: {
        double rgba[4] = {0, 0, 0, 1};
        QColor color(Qt::black);
        if (gimp::color_to_srgb(def.color_default, rgba)) {
          color = QColor::fromRgbF(static_cast<float>(rgba[0]), static_cast<float>(rgba[1]), static_cast<float>(rgba[2]),
                                   static_cast<float>(rgba[3]));
        }
        values[a.key] = color;
        break;
      }
      case GimpArgumentWidget::Text:
        values[a.key] = QString::fromStdString(def.string_default.value_or(""));
        break;
      case GimpArgumentWidget::Layer:
        // As the plug-ins' own dialogs do: the layer the filter runs on.
        values[a.key] = QStringLiteral("layer");
        break;
    }
  }
  return values;
}

GimpPluginValues gimp_plugin_merge_values(const GimpPluginArgumentSet& arguments, const GimpPluginValues& stored) {
  auto values = gimp_plugin_default_values(arguments);
  for (const auto& a : arguments.editable) {
    const auto it = stored.find(a.key);
    if (it == stored.end()) {
      continue;
    }
    const auto& v = it->second;
    switch (a.widget) {
      case GimpArgumentWidget::Integer:
        if (v.canConvert<int>()) {
          values[a.key] = static_cast<int>(std::clamp<std::int64_t>(v.toLongLong(), a.def.int_min, a.def.int_max));
        }
        break;
      case GimpArgumentWidget::Double:
        if (v.canConvert<double>()) {
          values[a.key] = std::clamp(v.toDouble(), a.def.double_min, a.def.double_max);
        }
        break;
      case GimpArgumentWidget::Boolean:
        values[a.key] = v.toBool();
        break;
      case GimpArgumentWidget::Choice: {
        const auto nick = v.toString().toStdString();
        for (const auto& c : a.def.choices) {
          if (c.nick.value_or("") == nick) {
            values[a.key] = v.toString();
          }
        }
        break;
      }
      case GimpArgumentWidget::Color: {
        const auto color = v.typeId() == QMetaType::QColor ? v.value<QColor>() : QColor(v.toString());
        if (color.isValid()) {
          values[a.key] = color;
        }
        break;
      }
      case GimpArgumentWidget::Text:
        values[a.key] = v.toString();
        break;
      case GimpArgumentWidget::Layer:
        if (v.toString() == QLatin1String("layer") || v.toString() == QLatin1String("none")) {
          values[a.key] = v.toString();
        }
        break;
    }
  }
  return values;
}

bool encode_gimp_plugin_arguments(const GimpPluginProcedure& entry, const GimpPluginArgumentSet& arguments,
                                  const GimpPluginValues& values, QByteArray* message, std::uint32_t* count,
                                  QString* error) {
  gimp::Message call;
  call.type = gimp::MessageType::ProcRun;
  call.call.name = entry.procedure.toStdString();
  for (std::size_t i = 3; i < arguments.defs.size(); ++i) {
    const auto& def = arguments.defs[i];
    const auto key = name_of(def);
    const auto it = values.find(key);
    const GimpPluginArgument* argument = nullptr;
    for (const auto& a : arguments.editable) {
      if (a.key == key) {
        argument = &a;
      }
    }
    if (it == values.end() || argument == nullptr) {
      std::string why;
      auto fallback = gimp::default_argument(def, &why);
      if (!fallback.has_value()) {
        if (error != nullptr) *error = QString::fromStdString(why);
        return false;
      }
      call.call.params.push_back(std::move(*fallback));
      continue;
    }
    const auto& v = it->second;
    gimp::Param p;
    p.type_name = def.value_type_name;
    switch (argument->widget) {
      case GimpArgumentWidget::Integer:
        p.kind = gimp::ParamKind::Int;
        p.int_value = static_cast<std::int32_t>(std::clamp<std::int64_t>(v.toLongLong(), std::max<std::int64_t>(def.int_min, INT_MIN),
                                                                         std::min<std::int64_t>(def.int_max, INT_MAX)));
        break;
      case GimpArgumentWidget::Boolean:
        p.kind = gimp::ParamKind::Int;
        p.int_value = v.toBool() ? 1 : 0;
        break;
      case GimpArgumentWidget::Double:
        p.kind = gimp::ParamKind::Double;
        p.double_value = std::clamp(v.toDouble(), def.double_min, def.double_max);
        break;
      case GimpArgumentWidget::Choice:
      case GimpArgumentWidget::Text:
        p.kind = gimp::ParamKind::String;
        p.string_value = v.toString().toStdString();
        break;
      case GimpArgumentWidget::Color: {
        const auto color = v.value<QColor>();
        const double rgba[4] = {color.redF(), color.greenF(), color.blueF(), color.alphaF()};
        p.kind = gimp::ParamKind::GeglColor;
        p.color.bytes.resize(sizeof rgba);
        std::memcpy(p.color.bytes.data(), rgba, sizeof rgba);
        p.color.encoding = std::string("R'G'B'A double");
        // The registration's own default color carries the profile plug-ins expect.
        p.color.profile = def.color_default.profile;
        break;
      }
      case GimpArgumentWidget::Layer:
        p.kind = gimp::ParamKind::Int;
        p.int_value = v.toString() == QLatin1String("none") ? -1 : kGimpRunLayerId;
        break;
    }
    call.call.params.push_back(std::move(p));
  }
  const auto bytes = gimp::encode_message(call);
  *message = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
  *count = static_cast<std::uint32_t>(call.call.params.size());
  return true;
}

QString gimp_plugin_values_to_json(const GimpPluginValues& values) {
  QJsonObject object;
  for (const auto& [key, value] : values) {
    if (value.typeId() == QMetaType::QColor) {
      object.insert(key, value.value<QColor>().name(QColor::HexArgb));
    } else if (value.typeId() == QMetaType::Bool) {
      object.insert(key, value.toBool());
    } else if (value.typeId() == QMetaType::Int) {
      object.insert(key, value.toInt());
    } else if (value.typeId() == QMetaType::Double) {
      object.insert(key, value.toDouble());
    } else {
      object.insert(key, value.toString());
    }
  }
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

GimpPluginValues gimp_plugin_values_from_json(const QString& json) {
  GimpPluginValues values;
  const auto object = QJsonDocument::fromJson(json.toUtf8()).object();
  for (auto it = object.begin(); it != object.end(); ++it) {
    values[it.key()] = it.value().toVariant();
  }
  return values;
}

}  // namespace patchy::ui
