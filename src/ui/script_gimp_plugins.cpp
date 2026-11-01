// patchy.gimpPlugins and layer.applyGimpPlugin (docs/gimp-plugins.md, "Scripting").

#include "core/layer.hpp"
#include "core/layer_render_utils.hpp"
#include "ui/app_settings.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"

#ifndef Q_OS_WASM
#include "plugins/gimp/gimp_procedure.hpp"
#include "plugins/gimp/gimp_wire_codec.hpp"
#include "ui/gimp_plugin_catalog.hpp"
#include "ui/gimp_plugin_runner.hpp"
#endif

#include <QColor>
#include <QDir>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QSet>

#include <cmath>
#include <cstring>

namespace patchy::ui {

ScriptGimpPluginsObject::ScriptGimpPluginsObject(ScriptEngineHost& host) : host_(host) {}

QStringList ScriptGimpPluginsObject::folders() const { return stored_gimp_plugin_folders(); }

void ScriptGimpPluginsObject::set_folders(const QStringList& folders) {
  const ScriptApiCall api_call(host_);
  set_stored_gimp_plugin_folders(folders);
  host_.rescan_gimp_plugins(false);
}

bool ScriptGimpPluginsObject::enabled() const { return stored_gimp_plugins_enabled(); }

void ScriptGimpPluginsObject::set_enabled(bool enabled) {
  const ScriptApiCall api_call(host_);
  set_stored_gimp_plugins_enabled(enabled);
  host_.rescan_gimp_plugins(false);
}

QString ScriptGimpPluginsObject::install_folder() const { return QDir::fromNativeSeparators(stored_gimp_install_folder()); }

void ScriptGimpPluginsObject::set_install_folder(const QString& folder) {
  const ScriptApiCall api_call(host_);
  set_stored_gimp_install_folder(QDir::toNativeSeparators(folder));
  host_.rescan_gimp_plugins(false);
}

QJSValue ScriptGimpPluginsObject::install() {
  const ScriptApiCall api_call(host_);
  return host_.gimp_plugin_install();
}

QJSValue ScriptGimpPluginsObject::list() {
  const ScriptApiCall api_call(host_);
  return host_.gimp_plugin_list();
}

QJSValue ScriptGimpPluginsObject::rescan(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  bool force = false;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      if (it.name() == QLatin1String("force")) {
        force = it.value().toBool();
      } else {
        host_.throw_js_error(ScriptEngineHost::tr("rescan: unknown option %1.").arg(it.name()));
        return {};
      }
    }
  }
  host_.rescan_gimp_plugins(force);
  return host_.gimp_plugin_list();
}

QJSValue ScriptGimpPluginsObject::geglFilters() {
  const ScriptApiCall api_call(host_);
  return host_.gegl_filter_list();
}

void ScriptLayerObject::applyGimpPlugin(const QString& pluginId, const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  host_.apply_gimp_plugin_to_layer(session_id_, layer_id_, pluginId, options);
}

#ifdef Q_OS_WASM

void ScriptEngineHost::apply_gimp_plugin_to_layer(std::int64_t, LayerId, const QString&, const QJSValue&) {
  throw_js_error(tr("GIMP plug-ins do not run in the web version."));
}
QJSValue ScriptEngineHost::gimp_plugin_list() { return engine()->newArray(); }
QJSValue ScriptEngineHost::gegl_filter_list() { return engine()->newArray(); }
QJSValue ScriptEngineHost::gimp_plugin_install() {
  auto value = engine()->newObject();
  value.setProperty(QStringLiteral("found"), false);
  return value;
}
void ScriptEngineHost::rescan_gimp_plugins(bool) {}

#else

namespace {

// A script value for one GIMP argument, typed by its registration.
std::optional<gimp::Param> param_from_script(const gimp::ParamDef& def, const QJSValue& value, QString* error) {
  using gimp::ParamDefKind;
  using gimp::ParamKind;
  const auto name = QString::fromStdString(def.name.value_or(""));
  gimp::Param p;
  p.type_name = def.value_type_name;
  switch (def.kind) {
    case ParamDefKind::Int:
    case ParamDefKind::Unit:
    case ParamDefKind::Enum: {
      if (!value.isNumber()) {
        *error = ScriptEngineHost::tr("%1 needs a number.").arg(name);
        return std::nullopt;
      }
      const double v = value.toNumber();
      if (def.kind == ParamDefKind::Int && (v < static_cast<double>(def.int_min) || v > static_cast<double>(def.int_max))) {
        *error = ScriptEngineHost::tr("%1 must be between %2 and %3.").arg(name).arg(def.int_min).arg(def.int_max);
        return std::nullopt;
      }
      p.kind = ParamKind::Int;
      p.int_value = static_cast<std::int32_t>(std::lround(v));
      return p;
    }
    case ParamDefKind::Boolean:
      p.kind = ParamKind::Int;
      p.int_value = value.toBool() ? 1 : 0;
      return p;
    case ParamDefKind::Double: {
      if (!value.isNumber()) {
        *error = ScriptEngineHost::tr("%1 needs a number.").arg(name);
        return std::nullopt;
      }
      const double v = value.toNumber();
      if (v < def.double_min || v > def.double_max) {
        *error = ScriptEngineHost::tr("%1 must be between %2 and %3.").arg(name).arg(def.double_min).arg(def.double_max);
        return std::nullopt;
      }
      p.kind = ParamKind::Double;
      p.double_value = v;
      return p;
    }
    case ParamDefKind::String:
      p.kind = ParamKind::String;
      p.string_value = value.toString().toStdString();
      return p;
    case ParamDefKind::Choice: {
      const auto nick = value.toString().toStdString();
      QStringList valid;
      for (const auto& c : def.choices) {
        valid << QString::fromStdString(c.nick.value_or(""));
        if (c.nick.value_or("") == nick) {
          p.kind = ParamKind::String;
          p.string_value = nick;
          return p;
        }
      }
      *error = ScriptEngineHost::tr("%1 must be one of: %2.").arg(name, valid.join(QStringLiteral(", ")));
      return std::nullopt;
    }
    case ParamDefKind::GeglColor: {
      double rgba[4] = {0, 0, 0, 1};
      if (value.isArray()) {
        const int n = std::min(4, value.property(QStringLiteral("length")).toInt());
        for (int i = 0; i < n; ++i) rgba[i] = value.property(static_cast<quint32>(i)).toNumber();
      } else {
        const QColor color(value.toString());
        if (!color.isValid()) {
          *error = ScriptEngineHost::tr("%1 needs a color such as \"#ff8800\" or [1, 0.5, 0].").arg(name);
          return std::nullopt;
        }
        rgba[0] = color.redF();
        rgba[1] = color.greenF();
        rgba[2] = color.blueF();
        rgba[3] = color.alphaF();
      }
      gimp::Color c;
      c.bytes.resize(sizeof rgba);
      std::memcpy(c.bytes.data(), rgba, sizeof rgba);
      c.encoding = std::string("R'G'B'A double");
      p.kind = ParamKind::GeglColor;
      p.color = std::move(c);
      return p;
    }
    default:
      *error = ScriptEngineHost::tr("%1 cannot be set from a script.").arg(name);
      return std::nullopt;
  }
}

}  // namespace

void ScriptEngineHost::apply_gimp_plugin_to_layer(std::int64_t session_id, LayerId layer_id, const QString& plugin_id,
                                                  const QJSValue& options) {
  pump_progress_indicator();
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    throw_js_error(tr("The document is no longer open."));
    return;
  }
  window_.ensure_gimp_plugins_scanned();
  const auto* entry = window_.find_gimp_plugin(plugin_id);
  if (entry == nullptr) {
    throw_js_error(tr("Unknown GIMP plug-in id: %1").arg(plugin_id));
    return;
  }
  if (entry->use != QLatin1String("layer")) {
    throw_js_error(tr("GIMP plug-in %1 cannot run: %2").arg(plugin_id, MainWindow::gimp_plugin_reason_text(entry->reason)));
    return;
  }
  MainWindow::GimpPluginApplyOptions run;
  run.run_mode = 1;  // non-interactive
  run.timeout_ms = 120000;
  QJSValue args;
  if (options.isObject()) {
    QJSValueIterator it(options);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name();
      if (key == QLatin1String("dialog")) {
        if (it.value().toBool()) {
          run.run_mode = 0;
          run.timeout_ms = 0;
        }
      } else if (key == QLatin1String("lastValues")) {
        if (it.value().toBool() && run.run_mode == 1) run.run_mode = 2;
      } else if (key == QLatin1String("args")) {
        args = it.value();
      } else if (key == QLatin1String("timeoutMs")) {
        run.timeout_ms = static_cast<std::uint32_t>(std::max(0.0, it.value().toNumber()));
      } else if (key == QLatin1String("trace")) {
        run.trace = it.value().toBool();
      } else if (key == QLatin1String("wireLog")) {
        run.wire_log = QDir::toNativeSeparators(it.value().toString());
      } else if (key == QLatin1String("captureDialog")) {
        run.capture_dialog = QDir::toNativeSeparators(it.value().toString());
      } else if (key == QLatin1String("answerDialog")) {
        const auto answer = it.value().toString();
        if (answer == QLatin1String("ok")) {
          run.answer_dialog = 1;
        } else if (answer == QLatin1String("cancel")) {
          run.answer_dialog = 2;
        } else {
          throw_js_error(tr("applyGimpPlugin: answerDialog is \"ok\" or \"cancel\"."));
          return;
        }
      } else {
        throw_js_error(tr("applyGimpPlugin: unknown option %1.").arg(key));
        return;
      }
    }
  }
  if (run.run_mode == 0 && unattended_run() && run.answer_dialog == 0) {
    // Unattended runs never wait for a person: without answerDialog the dialog is
    // answered with OK once it has settled (after its image, when captureDialog asks).
    run.answer_dialog = 1;
  }
  if (run.run_mode == 0 && run.answer_dialog != 0 && run.timeout_ms == 0) {
    run.timeout_ms = 120000;
  }
  if (args.isObject()) {
    gimp::Message install;
    try {
      gimp::SpanSource source({reinterpret_cast<const std::uint8_t*>(entry->install.constData()),
                               static_cast<std::size_t>(entry->install.size())});
      install = gimp::read_message(source);
    } catch (const gimp::WireError& e) {
      throw_js_error(tr("GIMP plug-in %1: its registration could not be read (%2).").arg(plugin_id, QString::fromUtf8(e.what())));
      return;
    }
    const auto& defs = install.install.params;
    QSet<QString> used;
    gimp::Message call;
    call.type = gimp::MessageType::ProcRun;
    call.call.name = entry->procedure.toStdString();
    for (std::size_t i = 3; i < defs.size(); ++i) {
      const auto name = QString::fromStdString(defs[i].name.value_or(""));
      const auto value = args.property(name);
      if (value.isUndefined()) {
        std::string why;
        auto fallback = gimp::default_argument(defs[i], &why);
        if (!fallback.has_value()) {
          throw_js_error(tr("GIMP plug-in %1: %2").arg(plugin_id, QString::fromStdString(why)));
          return;
        }
        call.call.params.push_back(std::move(*fallback));
        continue;
      }
      used.insert(name);
      QString error;
      auto param = param_from_script(defs[i], value, &error);
      if (!param.has_value()) {
        throw_js_error(tr("applyGimpPlugin: %1").arg(error));
        return;
      }
      call.call.params.push_back(std::move(*param));
    }
    QJSValueIterator it(args);
    while (it.hasNext()) {
      it.next();
      if (!used.contains(it.name())) {
        throw_js_error(tr("applyGimpPlugin: %1 has no argument %2.").arg(plugin_id, it.name()));
        return;
      }
    }
    const auto bytes = gimp::encode_message(call);
    run.arguments = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    run.argument_count = static_cast<std::uint32_t>(call.call.params.size());
  }
  auto* layer = session->document.find_layer(layer_id);
  if (layer == nullptr) {
    throw_js_error(tr("The layer no longer exists."));
    return;
  }
  if (layer->kind() != LayerKind::Pixel) {
    throw_js_error(tr("applyGimpPlugin needs a pixel layer."));
    return;
  }
  const auto before_bounds = to_qrect(layer_render_bounds(std::as_const(*layer)));
  QString error;
  const auto status = window_.apply_gimp_plugin(
      *session, layer_id, *entry, run, [this, session_id] { (void)prepare_mutation(session_id); }, &error);
  switch (status) {
    case MainWindow::GimpPluginApplyStatus::Applied:
      note_pixels_changed(session_id, before_bounds);
      return;
    case MainWindow::GimpPluginApplyStatus::NoChange:
      return;
    case MainWindow::GimpPluginApplyStatus::Cancelled:
      throw_js_error(tr("GIMP plug-in %1 was cancelled.").arg(plugin_id));
      return;
    case MainWindow::GimpPluginApplyStatus::Error:
      throw_js_error(tr("GIMP plug-in %1 failed: %2").arg(plugin_id, error));
      return;
  }
}

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

QJSValue ScriptEngineHost::gimp_plugin_list() {
  window_.ensure_gimp_plugins_scanned();
  auto array = engine()->newArray();
  const auto* scan = window_.gimp_plugins_.get();
  if (scan == nullptr) {
    return array;
  }
  quint32 index = 0;
  for (const auto& p : scan->procedures) {
    auto value = engine()->newObject();
    value.setProperty(QStringLiteral("id"), p.identifier);
    value.setProperty(QStringLiteral("procedure"), p.procedure);
    value.setProperty(QStringLiteral("name"), p.label);
    value.setProperty(QStringLiteral("menu"), p.menu.join(QLatin1Char('/')));
    value.setProperty(QStringLiteral("supported"), p.use == QLatin1String("layer"));
    value.setProperty(QStringLiteral("hidden"), p.use == QLatin1String("hidden"));
    value.setProperty(QStringLiteral("reasonCode"), p.reason);
    value.setProperty(QStringLiteral("reason"),
                      p.use == QLatin1String("layer") ? QString() : MainWindow::gimp_plugin_reason_text(p.reason));
    value.setProperty(QStringLiteral("file"), QDir::fromNativeSeparators(p.plugin_file));
    value.setProperty(QStringLiteral("blurb"), p.blurb);
    value.setProperty(QStringLiteral("imageTypes"), p.image_types);
    auto params = engine()->newArray();
    quint32 n = 0;
    for (std::size_t i = 3; i < p.params.size(); ++i) {
      const auto& param = p.params[i];
      auto pv = engine()->newObject();
      pv.setProperty(QStringLiteral("name"), param.name);
      pv.setProperty(QStringLiteral("label"), param.nick);
      pv.setProperty(QStringLiteral("description"), param.blurb);
      pv.setProperty(QStringLiteral("kind"), param.kind);
      pv.setProperty(QStringLiteral("type"), param.value_type);
      params.setProperty(n++, pv);
    }
    value.setProperty(QStringLiteral("params"), params);
    array.setProperty(index++, value);
  }
  return array;
}

QJSValue ScriptEngineHost::gimp_plugin_install() {
  window_.ensure_gimp_plugins_scanned();
  auto value = engine()->newObject();
  const auto* scan = window_.gimp_plugins_.get();
  const bool found = scan != nullptr && scan->install.has_value();
  value.setProperty(QStringLiteral("found"), found);
  value.setProperty(QStringLiteral("problem"), scan != nullptr ? scan->problem : QString());
  if (found) {
    const auto& install = *scan->install;
    value.setProperty(QStringLiteral("folder"), QDir::fromNativeSeparators(to_qstring(install.prefix)));
    value.setProperty(QStringLiteral("version"), QString::fromStdString(install.version));
    value.setProperty(QStringLiteral("series"), QString::fromStdString(install.series));
    value.setProperty(QStringLiteral("source"), QString::fromStdString(install.source));
    value.setProperty(QStringLiteral("flatpak"), install.flatpak);
  }
  auto folders = engine()->newArray();
  quint32 n = 0;
  if (scan != nullptr) {
    for (const auto& folder : scan->folders) {
      folders.setProperty(n++, QDir::fromNativeSeparators(folder));
    }
  }
  value.setProperty(QStringLiteral("pluginFolders"), folders);
  auto failed = engine()->newArray();
  quint32 f = 0;
  if (scan != nullptr) {
    for (const auto& file : scan->files) {
      if (!file.ok) {
        auto entry = engine()->newObject();
        entry.setProperty(QStringLiteral("file"), QDir::fromNativeSeparators(file.path));
        entry.setProperty(QStringLiteral("error"), file.error);
        failed.setProperty(f++, entry);
      }
    }
  }
  value.setProperty(QStringLiteral("failedFiles"), failed);
  return value;
}

void ScriptEngineHost::rescan_gimp_plugins(bool force) { window_.rescan_gimp_plugins(force); }

#endif

}  // namespace patchy::ui
