// Image > Adjustments stored as native Smart Filters (docs/smart-filters-adjustments.md):
// an imported entry renders in Patchy, shows its adjustment's name, and edits through the
// adjustment's own dialog as one undo step.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

using namespace patchy::test::ui;

namespace {

std::vector<std::uint8_t> bytes_of(const patchy::PixelBuffer& pixels) {
  const auto data = pixels.data();
  return {data.begin(), data.end()};
}

void ui_smart_filter_adjustment_entry_edits_with_its_dialog() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-adjustments.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  // Review shot: the import notice counts every adjustment-filtered layer as editable.
  save_widget_artifact("ui_smart_filter_adjustments_opened", window);

  const auto layer_id = select_named_layer(window, QStringLiteral("Levels master"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() != nullptr);
  CHECK(patchy::smart_object_lock_reason(*original).empty());
  const auto original_pixels = bytes_of(original->pixels());
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  const auto row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* widget = list->itemWidget(require_layer_item(*list, QStringLiteral("Levels master")));
    CHECK(widget != nullptr);
    return widget;
  };
  auto* label = row()->findChild<QLabel*>(QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Levels"));
  save_widget_artifact("ui_smart_filter_adjustment_row", *row());

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLevelsDialog")));
    CHECK(dialog != nullptr);
    auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    auto* gamma = dialog->findChild<QSpinBox*>(QStringLiteral("levelsGammaSpin"));
    auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
    CHECK(black != nullptr && gamma != nullptr && white != nullptr);
    CHECK(black->value() == 8 && gamma->value() == 80 && white->value() == 210);
    gamma->setValue(135);
    edited = true;
    dialog->accept();
  });
  auto* edit = row()->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  QApplication::processEvents();

  const auto settings_now = [&]() {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    const auto& entry = layer->smart_filter_stack()->entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::Adjustment);
    return std::get<patchy::AdjustmentSmartFilter>(entry.parameters).settings;
  };
  CHECK(settings_now().levels.gamma_percent == 135);
  CHECK(settings_now().levels.black_input == 8 && settings_now().levels.white_input == 210);
  const auto* changed = std::as_const(document).find_layer(layer_id);
  CHECK(bytes_of(changed->pixels()) != original_pixels);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1U);

  // The edited stack saves in Photoshop's form and reopens editable.
  ensure_artifact_dir();
  const auto artifact = std::filesystem::absolute(std::filesystem::path("test-artifacts") /
                                                  "ui_smart_filter_adjustment.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact);
  const auto reopened_it = std::find_if(reopened.layers().begin(), reopened.layers().end(),
                                        [](const patchy::Layer& layer) { return layer.name() == "Levels master"; });
  CHECK(reopened_it != reopened.layers().end());
  const auto& reopened_layer = *reopened_it;
  CHECK(reopened_layer.smart_filter_stack() != nullptr &&
        reopened_layer.smart_filter_stack()->support == patchy::SmartFilterStackSupport::Supported);
  CHECK(std::get<patchy::AdjustmentSmartFilter>(reopened_layer.smart_filter_stack()->entries.front().parameters)
            .settings.levels.gamma_percent == 135);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(settings_now().levels.gamma_percent == 80);
  CHECK(bytes_of(std::as_const(document).find_layer(layer_id)->pixels()) == original_pixels);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_adjustments() {
  return {
      {"ui_smart_filter_adjustment_entry_edits_with_its_dialog",
       ui_smart_filter_adjustment_entry_edits_with_its_dialog},
  };
}
