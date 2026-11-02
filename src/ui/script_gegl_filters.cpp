// patchy.gegl (docs/gegl-filters.md, "Scripting"): the GEGL filters Patchy offers.

#include "filters/filter_registry.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/main_window.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"

#include <QJSEngine>

namespace patchy::ui {

ScriptGeglObject::ScriptGeglObject(ScriptEngineHost& host) : host_(host) {}

QJSValue ScriptGeglObject::filters() {
  const ScriptApiCall api_call(host_);
  return host_.gegl_filter_list();
}

#ifdef Q_OS_WASM

QJSValue ScriptEngineHost::gegl_filter_list() { return engine()->newArray(); }

#else

QJSValue ScriptEngineHost::gegl_filter_list() {
  window_.ensure_gegl_filters_scanned();
  auto array = engine()->newArray();
  const auto* scan = window_.gegl_filters_.get();
  if (scan == nullptr || scan->registry == nullptr) {
    return array;
  }
  // Type tokens and keys are permanent API identifiers (docs/scripting.md).
  const auto type_token = [](FilterParameterKind kind) {
    switch (kind) {
      case FilterParameterKind::Integer:
        return QStringLiteral("integer");
      case FilterParameterKind::Double:
        return QStringLiteral("number");
      case FilterParameterKind::Boolean:
        return QStringLiteral("boolean");
      case FilterParameterKind::Option:
        return QStringLiteral("choice");
    }
    return QStringLiteral("number");
  };
  const auto value_of = [](const FilterParameterValue& value) -> QJSValue {
    if (const auto* i = std::get_if<std::int64_t>(&value)) return QJSValue(static_cast<double>(*i));
    if (const auto* d = std::get_if<double>(&value)) return QJSValue(*d);
    if (const auto* b = std::get_if<bool>(&value)) return QJSValue(*b);
    return QJSValue(QString::fromStdString(std::get<std::string>(value)));
  };
  quint32 index = 0;
  for (const auto& filter : scan->registry->filters()) {
    auto item = engine()->newObject();
    item.setProperty(QStringLiteral("id"), QString::fromStdString(filter.identifier));
    item.setProperty(QStringLiteral("name"), QString::fromStdString(filter.display_name));
    const auto category = scan->categories.find(filter.identifier);
    item.setProperty(QStringLiteral("category"), category != scan->categories.end() ? category->second : QString());
    const auto description = scan->descriptions.find(filter.identifier);
    item.setProperty(QStringLiteral("description"),
                     description != scan->descriptions.end() ? description->second : QString());
    auto parameters = engine()->newArray();
    quint32 p_index = 0;
    for (const auto& definition : filter.catalog.parameters) {
      auto described = engine()->newObject();
      described.setProperty(QStringLiteral("name"), QString::fromStdString(definition.key));
      described.setProperty(QStringLiteral("label"), QString::fromStdString(definition.display_name));
      described.setProperty(QStringLiteral("type"), type_token(definition.kind));
      described.setProperty(QStringLiteral("default"), value_of(definition.default_value));
      if (definition.minimum.has_value()) {
        described.setProperty(QStringLiteral("minimum"), *definition.minimum);
      }
      if (definition.maximum.has_value()) {
        described.setProperty(QStringLiteral("maximum"), *definition.maximum);
      }
      if (definition.kind == FilterParameterKind::Option) {
        auto choices = engine()->newArray();
        quint32 c_index = 0;
        for (const auto& choice : definition.options) {
          choices.setProperty(c_index++, QString::fromStdString(choice.value));
        }
        described.setProperty(QStringLiteral("choices"), choices);
      }
      parameters.setProperty(p_index++, described);
    }
    item.setProperty(QStringLiteral("parameters"), parameters);
    array.setProperty(index++, item);
  }
  return array;
}

#endif

}  // namespace patchy::ui
