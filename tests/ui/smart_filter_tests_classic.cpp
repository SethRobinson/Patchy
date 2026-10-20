// Photoshop's parameterless Smart Filters (Blur, Blur More, Sharpen, Sharpen
// More) in the Layers panel: their rows name the filter and their settings
// dialog holds only the Blending section. Appended after the historical
// smart_filter_tests parts so the earlier registration order is unchanged.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_smart_filter_parameterless_entry_edits_blending_only() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-classic.psd")
          .wstring());
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_name = QStringLiteral("Blur More");
  const auto layer_id = select_named_layer(window, layer_name);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  layers->scrollToItem(require_layer_item(*layers, layer_name),
                       QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();
  const auto current_entry = [&]() -> const patchy::SmartFilterEntry& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    CHECK(layer->smart_filter_stack()->entries.size() == 1U);
    return layer->smart_filter_stack()->entries.front();
  };
  CHECK(current_entry().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(current_entry().opacity == 1.0);

  auto* row = layers->itemWidget(require_layer_item(*layers, layer_name));
  CHECK(row != nullptr);
  bool labelled = false;
  for (auto* label : row->findChildren<QLabel*>(
           QStringLiteral("layerSmartFilterEntryLabel"))) {
    labelled = labelled || label->text() == QStringLiteral("Blur More");
  }
  CHECK(labelled);
  ensure_artifact_dir();
  layers->grab().save(
      QStringLiteral("test-artifacts/smart-filter-classic-layers.png"));
  auto* edit = row->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());

  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_seen = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    // No filter parameters: the Blending section is the only content.
    CHECK(dialog->findChildren<QSpinBox*>().empty());
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("smartFilterBlendModeCombo"));
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("smartFilterOpacitySpin"));
    CHECK(mode != nullptr && opacity != nullptr);
    opacity->setValue(40.0);
    dialog_seen = true;
    dialog->accept();
  });
  edit->click();
  CHECK(process_events_until([&] {
    return std::abs(current_entry().opacity - 0.40) < 0.0000001;
  }));
  CHECK(dialog_seen);
  CHECK(current_entry().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] { return current_entry().opacity == 1.0; }));
}

void ui_smart_filter_parameterless_menu_command_appends_without_dialog() {
  // Filter > Blur > Blur More on a Smart Object appends Photoshop's native
  // entry at once, like Photoshop: no settings dialog, one undo step.
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, QString::fromStdWString(patchy::test::committed_psd_fixture_path(
                                          "photoshop-smart-filter-classic.psd")
                                          .wstring()));
  QApplication::processEvents();
  const auto layer_id = select_named_layer(window, QStringLiteral("_Source"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(document).find_layer(layer_id)->smart_filter_stack() ==
        nullptr);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_seen = false;
  QTimer::singleShot(50, [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
        dialog != nullptr) {
      dialog_seen = true;
      dialog->reject();
    }
  });
  auto* action = window.findChild<QAction*>(
      QStringLiteral("filterAction_patchy_filters_blur_more"));
  CHECK(action != nullptr && action->isEnabled());
  action->trigger();
  CHECK(process_events_until([&] {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    return layer != nullptr && layer->smart_filter_stack() != nullptr;
  }));
  process_events_for(100);
  CHECK(!dialog_seen);
  const auto& stack =
      *std::as_const(document).find_layer(layer_id)->smart_filter_stack();
  CHECK(stack.entries.size() == 1U);
  CHECK(stack.entries.front().kind == patchy::SmartFilterKind::BlurMore);
  CHECK(!stack.entries.front().has_options);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_classic() {
  return {
      {"ui_smart_filter_parameterless_entry_edits_blending_only",
       ui_smart_filter_parameterless_entry_edits_blending_only},
      {"ui_smart_filter_parameterless_menu_command_appends_without_dialog",
       ui_smart_filter_parameterless_menu_command_appends_without_dialog},
  };
}
