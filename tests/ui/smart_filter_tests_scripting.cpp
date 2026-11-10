// Smart Filters from scripts (layer.smartFilters, addSmartFilter, setSmartFilter,
// moveSmartFilter, removeSmartFilter, clearSmartFilters, smartFiltersVisible, and
// applyFilter on a Smart Object; docs/scripting.md). Each script run is one undo step
// and the edits go through the Layers panel's commit, so the render, the PSD
// descriptor and the FEid cache are the UI's. Appended after the historical
// smart_filter_tests parts so the earlier registration order is unchanged.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

struct ScriptResult {
  bool ok{false};
  QStringList messages;
};

ScriptResult run_script(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("smart-filter-script-test");
  options.unattended = true;
  (void)host.run_source(source, std::move(options));
  CHECK(process_events_until([&] { return !host.run_active(); }, 30000));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  ScriptResult result;
  result.ok = !host.last_run_had_error();
  for (const auto& line : host.message_backlog()) {
    result.messages << line;
  }
  return result;
}

bool messages_contain(const ScriptResult& result, const QString& needle) {
  return std::any_of(result.messages.begin(), result.messages.end(),
                     [&](const QString& line) { return line.contains(needle); });
}

const patchy::Layer& active_layer(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto id = document.active_layer_id();
  CHECK(id.has_value());
  const auto* layer = document.find_layer(*id);
  CHECK(layer != nullptr);
  return *layer;
}

std::vector<std::uint8_t> layer_bytes(patchy::ui::MainWindow& window) {
  const auto& layer = active_layer(window);
  const auto data = layer.pixels().data();
  return {data.begin(), data.end()};
}

const patchy::SmartFilterStack& active_stack(patchy::ui::MainWindow& window) {
  const auto* stack = active_layer(window).smart_filter_stack();
  CHECK(stack != nullptr);
  return *stack;
}

double gaussian_radius(const patchy::SmartFilterEntry& entry) {
  const auto* gaussian = std::get_if<patchy::GaussianBlurSmartFilter>(&entry.parameters);
  CHECK(gaussian != nullptr);
  return gaussian->radius_pixels;
}

// A checkerboard pixel layer converted for Smart Filters (Filter > Convert for Smart
// Filters), so a blur visibly changes it.
void make_checker_smart_object(patchy::ui::MainWindow& window) {
  const auto setup = run_script(window, QStringLiteral(R"JS(
    var doc = app.newDocument(96, 64, {name: 'smart filters'});
    var art = doc.addLayer('art');
    art.fill('#ffffff');
    for (var y = 0; y < 64; y += 16) {
      for (var x = 0; x < 96; x += 16) {
        art.fillRect(x, y, 8, 8, '#d02030');
        art.fillRect(x + 8, y + 8, 8, 8, '#2040c0');
      }
    }
    doc.activeLayer = art;
    if (!app.runCommand('filter.convert_for_smart_filters')) { throw new Error('convert failed'); }
    if (!doc.activeLayer.isSmartObject) { throw new Error('not a smart object'); }
    if (doc.activeLayer.smartFilters.length !== 0) { throw new Error('unexpected filters'); }
    if (!doc.activeLayer.smartFiltersVisible) { throw new Error('stack switch off'); }
  )JS"));
  CHECK(setup.ok);
  CHECK(patchy::smart_object_lock_reason(active_layer(window)).empty());
  CHECK(active_layer(window).smart_filter_stack() == nullptr);
}

void ui_smart_filter_script_edits_render_like_the_layers_panel() {
  patchy::ui::MainWindow window;
  show_window(window);
  make_checker_smart_object(window);
  const auto unfiltered = layer_bytes(window);
  const auto undo_base = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Add: applyFilter's id and params; one run is one undo step.
  auto added = run_script(window, QStringLiteral(R"JS(
    var l = app.activeDocument.activeLayer;
    var index = l.addSmartFilter('patchy.filters.gaussian_blur', {radius: 3});
    var list = l.smartFilters;
    if (index !== 0 || list.length !== 1) { throw new Error('bad index ' + index); }
    var f = list[0];
    console.log('entry ' + f.filterId + ' ' + f.name + ' ' + f.params.radius + ' ' + f.visible + ' ' +
                f.opacity + ' ' + f.blendMode + ' ' + f.modeled + ' ' + f.index);
  )JS"));
  CHECK(added.ok);
  CHECK(messages_contain(added, QStringLiteral("entry patchy.filters.gaussian_blur Gaussian Blur 3 true 100 normal true 0")));
  CHECK(active_stack(window).entries.size() == 1U);
  CHECK(active_stack(window).entries[0].kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK(gaussian_radius(active_stack(window).entries[0]) == 3.0);
  const auto blurred = layer_bytes(window);
  CHECK(blurred != unfiltered);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_base + 1U);

  // Hide the filter, then the whole stack, and show both again.
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {visible: false});")).ok);
  CHECK(!active_stack(window).entries[0].enabled);
  CHECK(layer_bytes(window) == unfiltered);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {visible: true});")).ok);
  CHECK(layer_bytes(window) == blurred);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var l = app.activeDocument.activeLayer;
    l.smartFiltersVisible = false;
    if (l.smartFiltersVisible) { throw new Error('still visible'); }
  )JS")).ok);
  CHECK(!active_stack(window).enabled);
  CHECK(layer_bytes(window) == unfiltered);
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.smartFiltersVisible = true;")).ok);
  CHECK(active_stack(window).enabled);
  CHECK(layer_bytes(window) == blurred);

  // Settings, then opacity and blend mode on a second filter.
  CHECK(run_script(window, QStringLiteral(
                               "app.activeDocument.activeLayer.setSmartFilter(0, {params: {radius: 6}});"))
            .ok);
  CHECK(gaussian_radius(active_stack(window).entries[0]) == 6.0);
  const auto wider = layer_bytes(window);
  CHECK(wider != blurred);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var l = app.activeDocument.activeLayer;
    l.addSmartFilter('patchy.filters.pixelate', {block_size: 6});
    l.setSmartFilter(1, {opacity: 50.4, blendMode: 'multiply'});
    var f = l.smartFilters[1];
    console.log('mosaic ' + f.filterId + ' ' + f.params.cell_size + ' ' + f.opacity + ' ' + f.blendMode);
  )JS")).ok);
  {
    const auto& stack = active_stack(window);
    CHECK(stack.entries.size() == 2U);
    CHECK(stack.entries[1].kind == patchy::SmartFilterKind::Mosaic);
    CHECK(std::abs(stack.entries[1].opacity - 0.5) < 0.0000001);
    CHECK(stack.entries[1].blend_mode == patchy::BlendMode::Multiply);
  }
  const auto two_filters = layer_bytes(window);
  CHECK(two_filters != wider);

  // Reorder: Mosaic first, then the blur over it.
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.moveSmartFilter(1, 0);")).ok);
  CHECK(active_stack(window).entries[0].kind == patchy::SmartFilterKind::Mosaic);
  CHECK(active_stack(window).entries[1].kind == patchy::SmartFilterKind::GaussianBlur);
  const auto reordered = layer_bytes(window);
  CHECK(reordered != two_filters);

  // Remove the Mosaic: only the 6 px blur is left.
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.removeSmartFilter(0);")).ok);
  CHECK(active_stack(window).entries.size() == 1U);
  CHECK(layer_bytes(window) == wider);

  // Undo and redo step through the runs.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(active_stack(window).entries.size() == 2U);
  CHECK(layer_bytes(window) == reordered);
  patchy::ui::MainWindowTestAccess::redo(window);
  QApplication::processEvents();
  CHECK(active_stack(window).entries.size() == 1U);
  CHECK(layer_bytes(window) == wider);

  // Clearing the stack renders the source again.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var l = app.activeDocument.activeLayer;
    l.clearSmartFilters();
    if (l.smartFilters.length !== 0) { throw new Error('filters left'); }
  )JS")).ok);
  CHECK(active_layer(window).smart_filter_stack() == nullptr);
  CHECK(layer_bytes(window) == unfiltered);
}

void ui_smart_filter_script_apply_filter_adds_a_smart_filter_and_refusals_throw() {
  patchy::ui::MainWindow window;
  show_window(window);
  make_checker_smart_object(window);
  const auto unfiltered = layer_bytes(window);

  // applyFilter on a Smart Object follows the Filter menu: a mapped filter is added.
  CHECK(run_script(window, QStringLiteral(
                               "app.activeDocument.activeLayer.applyFilter('patchy.filters.blur_more');"))
            .ok);
  CHECK(active_stack(window).entries.size() == 1U);
  CHECK(active_stack(window).entries[0].kind == patchy::SmartFilterKind::BlurMore);
  CHECK(layer_bytes(window) != unfiltered);
  auto filtered = layer_bytes(window);
  std::size_t entries = 1U;

  // Each refusal throws a clear error and leaves the layer untouched.
  const auto refuses = [&](const QString& source, const QString& needle) {
    const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
    const auto result = run_script(window, source);
    CHECK(!result.ok);
    if (!messages_contain(result, needle)) {
      for (const auto& line : result.messages) {
        std::cerr << "  script: " << line.toStdString() << std::endl;
      }
    }
    CHECK(messages_contain(result, needle));
    CHECK(active_stack(window).entries.size() == entries);
    CHECK(layer_bytes(window) == filtered);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth);
  };
  refuses(QStringLiteral("app.activeDocument.activeLayer.applyFilter('patchy.filters.invert');"),
          QStringLiteral("Invert has no editable Smart Filter mapping"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.addSmartFilter('patchy.filters.radial_blur', "
                         "{center_x: 20});"),
          QStringLiteral("cannot be an editable Smart Filter with these settings"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.addSmartFilter('patchy.filters.nope');"),
          QStringLiteral("Unknown filter id: patchy.filters.nope"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(3, {visible: false});"),
          QStringLiteral("no Smart Filter at index 3 (the stack has 1)"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {params: {radius: 2}});"),
          QStringLiteral("has no setting named radius (settings: -)"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {blendMode: 'pass-through'});"),
          QStringLiteral("Smart Filters cannot use blend mode pass-through"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {opacity: 'half'});"),
          QStringLiteral("opacity must be a number"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(0, {glow: 1});"),
          QStringLiteral("unknown option glow"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.moveSmartFilter(0, 1);"),
          QStringLiteral("no Smart Filter at index 1"));

  // Radial Blur's own settings are amount and quality (its descriptor has no center).
  CHECK(run_script(window, QStringLiteral(R"JS(
    var l = app.activeDocument.activeLayer;
    l.addSmartFilter('patchy.filters.radial_blur', {amount: 20, samples: 32});
    var f = l.smartFilters[1];
    console.log('radial ' + f.params.amount + ' ' + f.params.quality);
  )JS")).ok);
  entries = 2U;
  filtered = layer_bytes(window);
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(1, {params: {samples: 8}});"),
          QStringLiteral("has no setting named samples (settings: amount, quality)"));
  refuses(QStringLiteral("app.activeDocument.activeLayer.setSmartFilter(1, {params: {quality: 'superb'}});"),
          QStringLiteral("rejected those parameters"));
  CHECK(run_script(window, QStringLiteral(
                               "app.activeDocument.activeLayer.setSmartFilter(1, {params: {quality: 'draft'}});"))
            .ok);
  CHECK(std::get<patchy::RadialBlurSmartFilter>(active_stack(window).entries[1].parameters).quality ==
        patchy::RadialBlurQuality::Draft);

  // A plain pixel layer has no Smart Filters.
  const auto plain = run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var p = doc.addLayer('plain');
    var n = p.smartFilters;
  )JS"));
  CHECK(!plain.ok);
  CHECK(messages_contain(plain, QStringLiteral("smartFilters needs a Smart Object layer")));
}

void ui_smart_filter_script_stack_survives_a_psd_round_trip() {
  patchy::ui::MainWindow window;
  show_window(window);
  make_checker_smart_object(window);
  ensure_artifact_dir();
  const auto path = QFileInfo(QStringLiteral("test-artifacts/smart-filter-script-round-trip.psd")).absoluteFilePath();
  QFile::remove(path);
  const auto saved = run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var l = doc.activeLayer;
    l.addSmartFilter('patchy.filters.gaussian_blur', {radius: 2.5});
    l.addSmartFilter('patchy.filters.motion_blur', {angle: 30, distance: 9}, {opacity: 70, blendMode: 'screen'});
    l.addSmartFilter('patchy.filters.add_noise', {amount: 20, monochromatic: true}, {visible: false});
    l.moveSmartFilter(2, 0);
    if (!doc.saveAs(%1)) { throw new Error('save failed'); }
  )JS")
                                                    .arg(QStringLiteral("'") + QDir::fromNativeSeparators(path) +
                                                         QStringLiteral("'")));
  CHECK(saved.ok);
  const auto expected = active_stack(window);
  const auto expected_pixels = layer_bytes(window);

  const auto reopened = patchy::psd::DocumentIo::read_file(std::filesystem::path(path.toStdWString()));
  const patchy::Layer* layer = nullptr;
  for (const auto& candidate : reopened.layers()) {
    if (patchy::layer_is_smart_object(candidate)) {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*layer).empty());
  const auto* stack = layer->smart_filter_stack();
  CHECK(stack != nullptr);
  CHECK(stack->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(stack->entries.size() == 3U);
  CHECK(stack->entries[0].kind == patchy::SmartFilterKind::AddNoise);
  CHECK(!stack->entries[0].enabled);
  CHECK(stack->entries[1].kind == patchy::SmartFilterKind::GaussianBlur);
  CHECK(gaussian_radius(stack->entries[1]) == 2.5);
  CHECK(stack->entries[2].kind == patchy::SmartFilterKind::MotionBlur);
  CHECK(stack->entries[2].blend_mode == patchy::BlendMode::Screen);
  CHECK(std::abs(stack->entries[2].opacity - 0.7) < 0.0000001);
  for (std::size_t index = 0; index < expected.entries.size(); ++index) {
    CHECK(stack->entries[index].parameters == expected.entries[index].parameters);
  }
  const auto data = layer->pixels().data();
  CHECK(std::vector<std::uint8_t>(data.begin(), data.end()) == expected_pixels);

  // The reopened document reads back the same list through the script API.
  const auto listed = run_script(window, QStringLiteral(R"JS(
    var doc = app.open(%1);
    var found = null;
    doc.layers.forEach(function (layer) { if (layer.isSmartObject) { found = layer; } });
    var list = found.smartFilters;
    console.log('listed ' + list.map(function (f) { return f.filterId + ':' + f.visible; }).join(','));
    console.log('motion ' + list[2].params.angle + ' ' + list[2].params.distance + ' ' + list[2].opacity + ' ' +
                list[2].blendMode);
  )JS")
                                                     .arg(QStringLiteral("'") + QDir::fromNativeSeparators(path) +
                                                          QStringLiteral("'")));
  CHECK(listed.ok);
  CHECK(messages_contain(listed, QStringLiteral("listed patchy.filters.add_noise:false,patchy.filters.gaussian_blur:true,"
                                                "patchy.filters.motion_blur:true")));
  CHECK(messages_contain(listed, QStringLiteral("motion 30 9 70 screen")));
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_scripting() {
  return {
      {"ui_smart_filter_script_edits_render_like_the_layers_panel",
       ui_smart_filter_script_edits_render_like_the_layers_panel},
      {"ui_smart_filter_script_apply_filter_adds_a_smart_filter_and_refusals_throw",
       ui_smart_filter_script_apply_filter_adds_a_smart_filter_and_refusals_throw},
      {"ui_smart_filter_script_stack_survives_a_psd_round_trip",
       ui_smart_filter_script_stack_survives_a_psd_round_trip},
  };
}
