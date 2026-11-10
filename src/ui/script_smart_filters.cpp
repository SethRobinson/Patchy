// Smart Filters from scripts: layer.smartFilters, addSmartFilter, setSmartFilter,
// removeSmartFilter, moveSmartFilter, clearSmartFilters and smartFiltersVisible
// (docs/scripting.md, docs/smart-objects.md). Every edit builds its candidate stack
// with the same helpers as the Layers panel, the settings dialogs and the Filter
// Gallery (smart_filter_editing.hpp, smart_filter_recipe_mapping.hpp) and commits it
// through MainWindow::commit_smart_filter_stack_edit, so the render, the SoLd
// descriptor and the FEid cache follow the UI's rules. Edits ride the run's undo
// snapshot (prepare_mutation).

#include "core/layer_metadata.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/filter_registry.hpp"
#include "filters/smart_filter_recipe_mapping.hpp"
#include "psd/psd_filter_effects.hpp"
#include "ui/document_session.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_api.hpp"
#include "ui/script_engine.hpp"
#include "ui/smart_filter_editing.hpp"

#include <QCoreApplication>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Photoshop's stored entry name ("Gaussian Blur...", "Dust && Scratches...",
// "Brightness/&Contrast...") without its menu ellipsis and mnemonic.
QString smart_filter_entry_name(const SmartFilterEntry& entry) {
  auto name = QString::fromStdString(entry.native_name);
  name.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
  name.remove(QLatin1Char('&'));
  name.replace(QStringLiteral("\x01"), QStringLiteral("&"));
  name = name.trimmed();
  while (name.endsWith(QLatin1Char('.'))) {
    name.chop(1);
  }
  return name.trimmed();
}

QJSValue parameter_to_js(const FilterParameterValue& value) {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) {
    return QJSValue(static_cast<double>(*integer));
  }
  if (const auto* real = std::get_if<double>(&value)) {
    return QJSValue(*real);
  }
  if (const auto* flag = std::get_if<bool>(&value)) {
    return QJSValue(*flag);
  }
  return QJSValue(QString::fromStdString(std::get<std::string>(value)));
}

QString settings_key_list(const FilterInvocation& settings) {
  QStringList keys;
  for (const auto& [key, value] : settings.parameters) {
    keys << QString::fromStdString(key);
  }
  return keys.isEmpty() ? QStringLiteral("-") : keys.join(QStringLiteral(", "));
}

// Execution order: 0 runs first (the bottom row in the Layers panel).
std::vector<std::optional<std::size_t>> identity_sources(std::size_t count) {
  std::vector<std::optional<std::size_t>> sources;
  sources.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    sources.emplace_back(index);
  }
  return sources;
}

QString cannot_edit_safely_message() {
  return QCoreApplication::translate("patchy::ui::MainWindow",
                                     "This Smart Filter descriptor cannot be edited safely");
}

QString preserved_only_message() {
  return QCoreApplication::translate("patchy::ui::MainWindow",
                                     "This Smart Filter can only be preserved, not edited");
}

}  // namespace

namespace {

// The checks every Smart Filter call makes before touching the stack. Returns the
// session (with a canvas) and the layer, or nulls after throwing.
struct SmartFilterTarget {
  DocumentSession* session{nullptr};
  const Layer* layer{nullptr};
};

}  // namespace

QJSValue ScriptEngineHost::smart_filters_to_js(std::int64_t session_id, LayerId layer_id) {
  const auto* document = session_document_const(session_id);
  const auto* layer = document != nullptr ? document->find_layer(layer_id) : nullptr;
  if (layer == nullptr) {
    throw_js_error(tr("The layer no longer exists."));
    return QJSValue();
  }
  if (!layer_is_smart_object(*layer)) {
    throw_js_error(tr("%1 needs a Smart Object layer.").arg(QStringLiteral("smartFilters")));
    return QJSValue();
  }
  const auto* stack = layer->smart_filter_stack();
  const auto count = stack != nullptr ? stack->entries.size() : 0U;
  auto list = engine()->newArray(static_cast<quint32>(count));
  for (std::size_t index = 0; index < count; ++index) {
    const auto& entry = stack->entries[index];
    const auto settings = smart_filter_entry_settings(entry);
    auto object = engine()->newObject();
    object.setProperty(QStringLiteral("index"), static_cast<int>(index));
    if (const auto command = native_smart_filter_command_id(smart_filter_dialog_kind(entry));
        command.has_value() && entry.kind != SmartFilterKind::FilterGallery) {
      object.setProperty(QStringLiteral("filterId"), QString::fromUtf8(command->data(),
                                                                       static_cast<int>(command->size())));
    } else if (settings.has_value()) {
      object.setProperty(QStringLiteral("filterId"), QString::fromStdString(settings->filter_id));
    } else {
      object.setProperty(QStringLiteral("filterId"), QJSValue(QJSValue::NullValue));
    }
    object.setProperty(QStringLiteral("name"), smart_filter_entry_name(entry));
    if (settings.has_value()) {
      auto params = engine()->newObject();
      for (const auto& [key, value] : settings->parameters) {
        params.setProperty(QString::fromStdString(key), parameter_to_js(value));
      }
      object.setProperty(QStringLiteral("params"), params);
    } else {
      object.setProperty(QStringLiteral("params"), QJSValue(QJSValue::NullValue));
    }
    object.setProperty(QStringLiteral("visible"), entry.enabled);
    object.setProperty(QStringLiteral("opacity"), entry.opacity * 100.0);
    object.setProperty(QStringLiteral("blendMode"), script_blend_mode_id(entry.blend_mode));
    // Patchy renders this filter itself (an unmodeled one can still be hidden or removed).
    object.setProperty(QStringLiteral("modeled"), entry.kind != SmartFilterKind::Unsupported);
    list.setProperty(static_cast<quint32>(index), object);
  }
  return list;
}

bool ScriptEngineHost::smart_filters_visible(std::int64_t session_id, LayerId layer_id) {
  const auto* document = session_document_const(session_id);
  const auto* layer = document != nullptr ? document->find_layer(layer_id) : nullptr;
  if (layer == nullptr) {
    throw_js_error(tr("The layer no longer exists."));
    return false;
  }
  if (!layer_is_smart_object(*layer)) {
    throw_js_error(tr("%1 needs a Smart Object layer.").arg(QStringLiteral("smartFiltersVisible")));
    return false;
  }
  const auto* stack = layer->smart_filter_stack();
  return stack == nullptr || stack->enabled;
}

namespace {

// Resolves and checks a Smart Filter edit target; nulls after `fail` threw.
template <typename Fail>
SmartFilterTarget resolve_smart_filter_target(DocumentSession* session, LayerId layer_id, const char* api,
                                              Fail&& fail) {
  if (session == nullptr) {
    fail(ScriptEngineHost::tr("The document is no longer open."));
    return {};
  }
  const auto* layer = std::as_const(session->document).find_layer(layer_id);
  if (layer == nullptr) {
    fail(ScriptEngineHost::tr("The layer no longer exists."));
    return {};
  }
  if (!layer_is_smart_object(*layer)) {
    fail(ScriptEngineHost::tr("%1 needs a Smart Object layer.").arg(QLatin1String(api)));
    return {};
  }
  if ((layer_effective_lock_flags(std::as_const(session->document).layers(), layer_id) &
       kLayerLockImagePixels) != kLayerLockNone) {
    fail(ScriptEngineHost::tr("Layer pixels are locked."));
    return {};
  }
  if (!smart_object_lock_allows_smart_filter_edits(smart_object_lock_reason(*layer)) ||
      session->canvas == nullptr) {
    fail(QCoreApplication::translate("patchy::ui::MainWindow",
                                     "This Smart Object can only preserve its imported filters"));
    return {};
  }
  return {session, layer};
}

}  // namespace

int ScriptEngineHost::add_smart_filter_invocation(std::int64_t session_id, LayerId layer_id,
                                                  const FilterRegistry& registry,
                                                  const FilterInvocation& normalized, const char* api,
                                                  const SmartFilterEntryChanges& changes) {
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, api, fail);
  if (target.session == nullptr) {
    return -1;
  }
  const auto* definition = registry.find(normalized.filter_id);
  const auto display_name = definition != nullptr ? filter_display_name(*definition)
                                                  : QString::fromStdString(normalized.filter_id);
  if (!native_smart_filter_kind_for(normalized.filter_id).has_value()) {
    throw_js_error(tr("%1 has no editable Smart Filter mapping. Rasterize the Smart Object to apply it "
                      "destructively.")
                       .arg(display_name));
    return -1;
  }
  auto& document = target.session->document;
  const auto lock = smart_object_lock_reason(*target.layer);
  const auto* current = target.layer->smart_filter_stack();
  if ((!lock.empty() && lock != "external") ||
      (current != nullptr && current->support != SmartFilterStackSupport::Supported)) {
    // The settings dialog's rule: a stack Patchy only preserves takes no new entries.
    throw_js_error(QCoreApplication::translate("patchy::ui::MainWindow",
                                               "This Smart Object can only preserve its imported filters"));
    return -1;
  }
  if (static_cast<std::uint64_t>(document.width()) * static_cast<std::uint64_t>(document.height()) >
      psd::kMaximumEditableSmartFilterMaskPixels) {
    throw_js_error(QCoreApplication::translate(
        "patchy::ui::MainWindow", "Editable Smart Filters currently support documents up to 64 megapixels"));
    return -1;
  }
  FilterRecipe recipe;
  FilterRecipeEntry recipe_entry;
  recipe_entry.invocation = normalized;
  recipe_entry.enabled = changes.visible.value_or(true);
  recipe_entry.opacity = changes.opacity.value_or(1.0);
  recipe_entry.blend_mode = changes.blend_mode.value_or(BlendMode::Normal);
  recipe.entries.push_back(std::move(recipe_entry));
  auto mapped = smart_filter_entries_from_recipe(recipe, registry);
  if (!mapped.has_value() || mapped->size() != 1U) {
    // Radial Blur with a moved center, Emboss with Amount 0: Photoshop's descriptor
    // cannot hold those, so they stay destructive-only (docs/smart-objects.md).
    throw_js_error(tr("%1 cannot be an editable Smart Filter with these settings.").arg(display_name));
    return -1;
  }
  SmartFilterStack candidate;
  std::vector<std::optional<std::size_t>> sources;
  if (current != nullptr) {
    candidate = *current;
    sources = identity_sources(current->entries.size());
  } else {
    candidate.enabled = true;
    candidate.valid_at_position = true;
    candidate.support = SmartFilterStackSupport::Supported;
    candidate.mask = new_smart_filter_stack_mask(std::as_const(document), *target.session->canvas);
  }
  candidate.entries.push_back(std::move(mapped->front()));
  sources.emplace_back(std::nullopt);
  const auto index = static_cast<int>(candidate.entries.size() - 1U);
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::move(candidate), std::move(sources),
          QString(), QString(), MainWindow::SmartFilterCacheEdit::Preserve,
          [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return -1;
  }
  note_structure_changed(session_id);
  return index;
}

namespace {

// Reads {visible, opacity, blendMode} (and, with `params`, the params object) from a
// script object. False after `fail` threw.
template <typename Fail>
bool read_smart_filter_changes(const QJSValue& object, const char* api, bool allow_params,
                               ScriptEngineHost::SmartFilterEntryChanges& changes, QJSValue* params,
                               Fail&& fail) {
  if (object.isUndefined() || object.isNull()) {
    return true;
  }
  if (!object.isObject()) {
    fail(ScriptEngineHost::tr("%1: options must be an object.").arg(QLatin1String(api)));
    return false;
  }
  QJSValueIterator it(object);
  while (it.hasNext()) {
    it.next();
    const auto key = it.name();
    const auto value = it.value();
    if (key == QLatin1String("visible")) {
      if (!value.isBool()) {
        fail(ScriptEngineHost::tr("%1: %2 must be true or false.").arg(QLatin1String(api), key));
        return false;
      }
      changes.visible = value.toBool();
    } else if (key == QLatin1String("opacity")) {
      if (!value.isNumber() || !std::isfinite(value.toNumber())) {
        fail(ScriptEngineHost::tr("%1: %2 must be a number.").arg(QLatin1String(api), key));
        return false;
      }
      // Whole percent, as Photoshop stores and the settings dialog edits it.
      changes.opacity = std::round(std::clamp(value.toNumber(), 0.0, 100.0)) / 100.0;
    } else if (key == QLatin1String("blendMode")) {
      BlendMode mode = BlendMode::Normal;
      if (!script_blend_mode_from_id(value.toString(), &mode)) {
        fail(ScriptEngineHost::tr("Unknown blend mode: %1").arg(value.toString()));
        return false;
      }
      if (!recipe_blend_mode_supported(mode)) {
        fail(ScriptEngineHost::tr("%1: Smart Filters cannot use blend mode %2.")
                 .arg(QLatin1String(api), value.toString()));
        return false;
      }
      changes.blend_mode = mode;
    } else if (allow_params && key == QLatin1String("params")) {
      *params = value;
    } else {
      fail(ScriptEngineHost::tr("%1: unknown option %2.").arg(QLatin1String(api), key));
      return false;
    }
  }
  return true;
}

void apply_entry_changes(SmartFilterEntry& entry, const ScriptEngineHost::SmartFilterEntryChanges& changes) {
  if (changes.visible.has_value()) {
    entry.enabled = *changes.visible;
  }
  if (changes.opacity.has_value()) {
    entry.opacity = *changes.opacity;
  }
  if (changes.blend_mode.has_value()) {
    entry.blend_mode = *changes.blend_mode;
  }
}

}  // namespace

int ScriptEngineHost::add_smart_filter(std::int64_t session_id, LayerId layer_id, const QString& filter_id,
                                       const QJSValue& params, const QJSValue& options) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  SmartFilterEntryChanges changes;
  if (!read_smart_filter_changes(options, "addSmartFilter", false, changes, nullptr, fail)) {
    return -1;
  }
  const auto normalized = script_filter_invocation(window_.filters_, filter_id, params);
  if (!normalized.has_value()) {
    return -1;
  }
  return add_smart_filter_invocation(session_id, layer_id, window_.filters_, *normalized, "addSmartFilter",
                                     changes);
}

void ScriptEngineHost::set_smart_filter(std::int64_t session_id, LayerId layer_id, int index,
                                        const QJSValue& change_object) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  SmartFilterEntryChanges changes;
  QJSValue params;
  if (!read_smart_filter_changes(change_object, "setSmartFilter", true, changes, &params, fail)) {
    return;
  }
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, "setSmartFilter", fail);
  if (target.session == nullptr) {
    return;
  }
  const auto* current = target.layer->smart_filter_stack();
  const auto count = current != nullptr ? current->entries.size() : 0U;
  if (index < 0 || static_cast<std::size_t>(index) >= count) {
    throw_js_error(tr("%1: no Smart Filter at index %2 (the stack has %3).")
                       .arg(QStringLiteral("setSmartFilter"))
                       .arg(index)
                       .arg(count));
    return;
  }
  const auto position = static_cast<std::size_t>(index);
  const bool edits_settings = (!params.isUndefined() && !params.isNull()) || changes.opacity.has_value() ||
                              changes.blend_mode.has_value();
  // Showing or hiding works on any stack Patchy can render afterwards (GitHub issue 243);
  // settings and blending need a stack Patchy fully models, like the settings dialog.
  if (!current->native_shape_supported ||
      (edits_settings && current->support != SmartFilterStackSupport::Supported)) {
    throw_js_error(preserved_only_message());
    return;
  }
  auto candidate = *current;
  const auto entry_name = smart_filter_entry_name(candidate.entries[position]);
  if (!params.isUndefined() && !params.isNull()) {
    if (!params.isObject()) {
      throw_js_error(tr("%1: %2 must be an object.").arg(QStringLiteral("setSmartFilter"), QStringLiteral("params")));
      return;
    }
    auto settings = smart_filter_entry_settings(candidate.entries[position]);
    if (!settings.has_value()) {
      throw_js_error(tr("%1: Smart Filter %2 has no settings a script can change.")
                         .arg(QStringLiteral("setSmartFilter"), entry_name));
      return;
    }
    std::vector<std::pair<std::string, std::string>> requested_options;
    QJSValueIterator it(params);
    while (it.hasNext()) {
      it.next();
      const auto key = it.name().toStdString();
      const auto found = settings->parameters.find(key);
      if (found == settings->parameters.end()) {
        throw_js_error(tr("%1: Smart Filter %2 has no setting named %3 (settings: %4).")
                           .arg(QStringLiteral("setSmartFilter"), entry_name, it.name(),
                                settings_key_list(*settings)));
        return;
      }
      const auto value = it.value();
      auto& slot = found->second;
      if (std::holds_alternative<bool>(slot)) {
        if (!value.isBool()) {
          throw_js_error(tr("%1: %2 must be true or false.").arg(QStringLiteral("setSmartFilter"), it.name()));
          return;
        }
        slot = value.toBool();
      } else if (std::holds_alternative<std::string>(slot)) {
        slot = value.toString().toStdString();
        requested_options.emplace_back(key, std::get<std::string>(slot));
      } else {
        if (!value.isNumber() || !std::isfinite(value.toNumber())) {
          throw_js_error(tr("%1: %2 must be a number.").arg(QStringLiteral("setSmartFilter"), it.name()));
          return;
        }
        if (std::holds_alternative<std::int64_t>(slot)) {
          slot = static_cast<std::int64_t>(std::llround(value.toNumber()));
        } else {
          slot = value.toNumber();
        }
      }
    }
    try {
      candidate = smart_filter_stack_with_entry_settings(std::move(candidate), position, *settings);
    } catch (const std::invalid_argument&) {
      throw_js_error(preserved_only_message());
      return;
    }
    // Numbers clamp to the filter's ranges like the dialog; a choice the filter does not
    // offer is an error, not a silent fallback.
    const auto applied = smart_filter_entry_settings(candidate.entries[position]);
    for (const auto& [key, option] : requested_options) {
      const std::string* stored = nullptr;
      if (applied.has_value()) {
        if (const auto found = applied->parameters.find(key); found != applied->parameters.end()) {
          stored = std::get_if<std::string>(&found->second);
        }
      }
      if (stored == nullptr || *stored != option) {
        throw_js_error(tr("Filter %1 rejected those parameters.").arg(entry_name));
        return;
      }
    }
  }
  apply_entry_changes(candidate.entries[position], changes);
  if (smart_filter_stack_support(candidate) != SmartFilterStackSupport::Supported) {
    throw_js_error(changes.visible.value_or(true) ? smart_filter_unmodeled_show_message()
                                                  : smart_filter_unmodeled_left_running_message());
    return;
  }
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::move(candidate), {}, QString(), QString(),
          MainWindow::SmartFilterCacheEdit::Preserve, [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return;
  }
  note_structure_changed(session_id);
}

void ScriptEngineHost::remove_smart_filter(std::int64_t session_id, LayerId layer_id, int index) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, "removeSmartFilter", fail);
  if (target.session == nullptr) {
    return;
  }
  const auto* current = target.layer->smart_filter_stack();
  const auto count = current != nullptr ? current->entries.size() : 0U;
  if (index < 0 || static_cast<std::size_t>(index) >= count) {
    throw_js_error(tr("%1: no Smart Filter at index %2 (the stack has %3).")
                       .arg(QStringLiteral("removeSmartFilter"))
                       .arg(index)
                       .arg(count));
    return;
  }
  if (!current->native_shape_supported) {
    throw_js_error(preserved_only_message());
    return;
  }
  const auto position = static_cast<std::size_t>(index);
  std::optional<SmartFilterStack> candidate;
  std::vector<std::optional<std::size_t>> sources;
  if (count > 1U) {
    // The Layers panel's Delete: the last entry takes the whole stack with it.
    candidate = *current;
    candidate->entries.erase(candidate->entries.begin() + static_cast<std::ptrdiff_t>(position));
    if (smart_filter_stack_support(*candidate) != SmartFilterStackSupport::Supported) {
      throw_js_error(smart_filter_unmodeled_left_after_delete_message());
      return;
    }
    for (std::size_t source = 0; source < count; ++source) {
      if (source != position) {
        sources.emplace_back(source);
      }
    }
  }
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::move(candidate), std::move(sources), QString(),
          QString(), MainWindow::SmartFilterCacheEdit::Preserve,
          [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return;
  }
  note_structure_changed(session_id);
}

void ScriptEngineHost::move_smart_filter(std::int64_t session_id, LayerId layer_id, int from, int to) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, "moveSmartFilter", fail);
  if (target.session == nullptr) {
    return;
  }
  const auto* current = target.layer->smart_filter_stack();
  const auto count = current != nullptr ? current->entries.size() : 0U;
  for (const auto index : {from, to}) {
    if (index < 0 || static_cast<std::size_t>(index) >= count) {
      throw_js_error(tr("%1: no Smart Filter at index %2 (the stack has %3).")
                         .arg(QStringLiteral("moveSmartFilter"))
                         .arg(index)
                         .arg(count));
      return;
    }
  }
  if (current->support != SmartFilterStackSupport::Supported) {
    throw_js_error(preserved_only_message());
    return;
  }
  if (from == to) {
    return;
  }
  auto candidate = *current;
  auto sources = identity_sources(count);
  const auto source = static_cast<std::size_t>(from);
  const auto destination = static_cast<std::size_t>(to);
  auto moved = std::move(candidate.entries[source]);
  candidate.entries.erase(candidate.entries.begin() + static_cast<std::ptrdiff_t>(source));
  candidate.entries.insert(candidate.entries.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
  const auto moved_source = sources[source];
  sources.erase(sources.begin() + static_cast<std::ptrdiff_t>(source));
  sources.insert(sources.begin() + static_cast<std::ptrdiff_t>(destination), moved_source);
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::move(candidate), std::move(sources), QString(),
          QString(), MainWindow::SmartFilterCacheEdit::Preserve,
          [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return;
  }
  note_structure_changed(session_id);
}

void ScriptEngineHost::clear_smart_filters(std::int64_t session_id, LayerId layer_id) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, "clearSmartFilters", fail);
  if (target.session == nullptr) {
    return;
  }
  const auto* current = target.layer->smart_filter_stack();
  if (current == nullptr) {
    return;  // nothing to remove
  }
  if (!current->native_shape_supported) {
    throw_js_error(preserved_only_message());
    return;
  }
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::nullopt, {}, QString(), QString(),
          MainWindow::SmartFilterCacheEdit::Preserve, [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return;
  }
  note_structure_changed(session_id);
}

void ScriptEngineHost::set_smart_filters_visible(std::int64_t session_id, LayerId layer_id, bool visible) {
  pump_progress_indicator();
  const auto fail = [this](const QString& message) { throw_js_error(message); };
  const auto target = resolve_smart_filter_target(window_.session_with_id(session_id), layer_id, "smartFiltersVisible", fail);
  if (target.session == nullptr) {
    return;
  }
  const auto* current = target.layer->smart_filter_stack();
  if (current == nullptr) {
    throw_js_error(tr("This Smart Object has no Smart Filters."));
    return;
  }
  if (current->enabled == visible) {
    return;
  }
  if (!current->native_shape_supported) {
    throw_js_error(preserved_only_message());
    return;
  }
  auto candidate = *current;
  candidate.enabled = visible;
  if (smart_filter_stack_support(candidate) != SmartFilterStackSupport::Supported) {
    throw_js_error(smart_filter_unmodeled_show_message());
    return;
  }
  if (!window_.commit_smart_filter_stack_edit(
          *target.session, target.session->canvas, layer_id, std::move(candidate), {}, QString(), QString(),
          MainWindow::SmartFilterCacheEdit::Preserve, [this, session_id] { return prepare_mutation(session_id); })) {
    throw_js_error(cannot_edit_safely_message());
    return;
  }
  note_structure_changed(session_id);
}

}  // namespace patchy::ui
