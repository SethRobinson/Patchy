// Table-described native Smart Filters in the app (core/smart_filter_schema.hpp,
// docs/smart-filters-distort-stylize.md): an imported Photoshop entry renders,
// edits through its settings dialog, and undoes.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_schema.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

// Writes an RGBA8 layer raster to test-artifacts/<name>.png for review shots.
void save_layer_artifact(const patchy::PixelBuffer& pixels, const char* name) {
  ensure_artifact_dir();
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    std::memcpy(image.scanLine(y), pixels.pixel(0, y), static_cast<std::size_t>(pixels.width()) * 4U);
  }
  CHECK(image.save(QStringLiteral("test-artifacts/%1.png").arg(QString::fromLatin1(name))));
}

void ui_smart_filter_imported_find_edges_renders_edits_and_undoes() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-find-edges.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id = select_named_layer(window, QStringLiteral("Layer 1"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto current_layer = [&]() -> const patchy::Layer& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer;
  };
  CHECK(patchy::smart_object_lock_reason(current_layer()).empty());
  CHECK(current_layer().smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(current_layer().smart_filter_stack()->entries.front().kind ==
        patchy::SmartFilterKind::FindEdges);

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  const auto row = [&]() -> QWidget* {
    auto* item = require_layer_item(*layers, QStringLiteral("Layer 1"));
    auto* widget = layers->itemWidget(item);
    CHECK(widget != nullptr);
    return widget;
  };
  bool labelled = false;
  for (auto* label : row()->findChildren<QLabel*>(QStringLiteral("layerSmartFilterEntryLabel"))) {
    labelled = labelled || label->text() == QStringLiteral("Find Edges");
  }
  CHECK(labelled);

  const auto original_pixels = current_layer().pixels();
  save_layer_artifact(original_pixels, "smart_filter_find_edges_photoshop_baked");
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("smartFilterOpacitySpin"));
    CHECK(opacity != nullptr);
    opacity->setValue(50.0);
    dialog_opened = true;
    dialog->accept();
  });
  auto* edit = row()->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] {
    return std::abs(current_layer().smart_filter_stack()->entries.front().opacity - 0.5) < 1e-9;
  }));
  CHECK(dialog_opened);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1U);
  // Half-strength Find Edges is a new render, not Photoshop's full-strength preview.
  CHECK(!patchy::ui::pixel_buffers_equal(current_layer().pixels(), original_pixels));
  save_layer_artifact(current_layer().pixels(), "smart_filter_find_edges_patchy_half_opacity");

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] {
    return std::abs(current_layer().smart_filter_stack()->entries.front().opacity - 1.0) < 1e-9;
  }));
  CHECK(patchy::ui::pixel_buffers_equal(current_layer().pixels(), original_pixels));
}

// A table-described filter's dialog is generated from its schema: integer
// fields become spin boxes, enum fields option combos, and an accepted change
// lands in the native settings in schema order.
void ui_smart_filter_imported_zigzag_dialog_edits_schema_settings() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-zigzag.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id = select_named_layer(window, QStringLiteral("Layer 1"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto settings = [&]() -> std::vector<std::int32_t> {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    const auto& entry = layer->smart_filter_stack()->entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::ZigZag);
    const auto* values = std::get_if<patchy::SchemaSmartFilter>(&entry.parameters);
    CHECK(values != nullptr);
    return values->values;
  };
  CHECK((settings() == std::vector<std::int32_t>{10, 5, 2}));
  save_layer_artifact(std::as_const(document).find_layer(layer_id)->pixels(),
                      "smart_filter_zigzag_photoshop_baked");

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* row = layers->itemWidget(require_layer_item(*layers, QStringLiteral("Layer 1")));
  CHECK(row != nullptr);
  bool dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    auto* ridges = dialog->findChild<QSpinBox*>(QStringLiteral("filterRidgesSpin"));
    auto* style = dialog->findChild<QComboBox*>(QStringLiteral("filterStyleCombo"));
    CHECK(amount != nullptr && ridges != nullptr && style != nullptr);
    CHECK(amount->value() == 10 && ridges->value() == 5);
    CHECK(amount->minimum() == -100 && amount->maximum() == 100);
    CHECK(style->count() == 3);
    CHECK(style->currentData().toString() == QStringLiteral("pond_ripples"));
    amount->setValue(-30);
    style->setCurrentIndex(style->findData(QStringLiteral("around_center")));
    dialog_opened = true;
    dialog->accept();
  });
  auto* edit = row->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return settings() == std::vector<std::int32_t>{-30, 5, 0}; }));
  CHECK(dialog_opened);
  save_layer_artifact(std::as_const(document).find_layer(layer_id)->pixels(),
                      "smart_filter_zigzag_patchy_around_center");
}

// Wave, Ripple, ZigZag and Shear render only approximately (Patchy's own
// pattern or a documented tolerance), so opening a PSD must show Photoshop's
// baked layer until the user edits the stack: no render on open.
void ui_smart_filter_approximate_schema_stacks_keep_photoshop_pixels_on_open() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  for (const char* file : {"photoshop-smart-filter-wave.psd", "photoshop-smart-filter-ripple.psd",
                           "photoshop-smart-filter-zigzag.psd", "photoshop-smart-filter-shear.psd"}) {
    const auto fixture = patchy::test::committed_psd_fixture_path(file);
    const auto baked = patchy::psd::DocumentIo::read_file(fixture);
    const patchy::Layer* baked_layer = nullptr;
    const patchy::Layer* source_layer = nullptr;
    for (const auto& layer : baked.layers()) {
      if (layer.name() == "Layer 1") {
        baked_layer = &layer;
      } else if (layer.name() == "_source") {
        source_layer = &layer;
      }
    }
    CHECK(baked_layer != nullptr && source_layer != nullptr &&
          baked_layer->smart_filter_stack() != nullptr);

    // The check can fail: Patchy's own render of this stack differs from the baked layer.
    const auto own = patchy::render_smart_filter_stack(
        source_layer->pixels(), source_layer->bounds(),
        patchy::Rect{0, 0, baked.width(), baked.height()}, *baked_layer->smart_filter_stack());
    CHECK(!patchy::ui::pixel_buffers_equal(own.pixels, baked_layer->pixels()));

    patchy::ui::MainWindow window;
    show_window(window);
    patchy::ui::MainWindowTestAccess::open_document_path(
        window, QString::fromStdWString(fixture.wstring()));
    QApplication::processEvents();
    const auto layer_id = select_named_layer(window, QStringLiteral("Layer 1"));
    const auto* opened = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
    CHECK(opened != nullptr && opened->smart_filter_stack() != nullptr);
    CHECK(opened->smart_filter_stack()->support == patchy::SmartFilterStackSupport::Supported);
    CHECK(opened->bounds().x == baked_layer->bounds().x && opened->bounds().y == baked_layer->bounds().y &&
          opened->bounds().width == baked_layer->bounds().width &&
          opened->bounds().height == baked_layer->bounds().height);
    CHECK(patchy::ui::pixel_buffers_equal(opened->pixels(), baked_layer->pixels()));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part4() {
  return {
      {"ui_smart_filter_imported_find_edges_renders_edits_and_undoes",
       ui_smart_filter_imported_find_edges_renders_edits_and_undoes},
      {"ui_smart_filter_imported_zigzag_dialog_edits_schema_settings",
       ui_smart_filter_imported_zigzag_dialog_edits_schema_settings},
      {"ui_smart_filter_approximate_schema_stacks_keep_photoshop_pixels_on_open",
       ui_smart_filter_approximate_schema_stacks_keep_photoshop_pixels_on_open},
  };
}
