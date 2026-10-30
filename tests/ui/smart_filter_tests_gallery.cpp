// Photoshop's Filter Gallery Smart Filter entry (GEfc, docs/smart-filters-native.md): an
// imported single-effect Plastic Wrap entry edits through the Plastic Wrap dialog and stays
// a gallery entry; a multi-effect entry renders but keeps its settings.

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
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

using namespace patchy::test::ui;

namespace {

void ui_smart_filter_gallery_plastic_wrap_edits_in_place() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-gallery.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  save_widget_artifact("ui_smart_filter_gallery_opened", window);

  const auto layer_id = select_named_layer(window, QStringLiteral("Gallery Plastic Wrap"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto row = [&](const QString& name) -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* widget = list->itemWidget(require_layer_item(*list, name));
    CHECK(widget != nullptr);
    return widget;
  };
  auto* label = row(QStringLiteral("Gallery Plastic Wrap"))->findChild<QLabel*>(QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Filter Gallery"));
  CHECK(label->toolTip().contains(QStringLiteral("Plastic Wrap")));
  CHECK(label->toolTip().contains(QStringLiteral("Highlight 13")));
  save_widget_artifact("ui_smart_filter_gallery_row", *row(QStringLiteral("Gallery Plastic Wrap")));
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool edited = false;
  when_dialog_visible(QStringLiteral("patchyFilterDialog"), [&](QDialog& shown) {
    auto* dialog = &shown;
    auto* highlight = dialog->findChild<QSpinBox*>(QStringLiteral("filterHighlightStrengthSpin"));
    auto* detail = dialog->findChild<QSpinBox*>(QStringLiteral("filterDetailSpin"));
    auto* smoothness = dialog->findChild<QSpinBox*>(QStringLiteral("filterSmoothnessSpin"));
    CHECK(highlight != nullptr && detail != nullptr && smoothness != nullptr);
    CHECK(highlight->value() == 13 && detail->value() == 9 && smoothness->value() == 7);
    highlight->setValue(4);
    smoothness->setValue(15);
    edited = true;
    dialog->accept();
  });
  auto* edit = row(QStringLiteral("Gallery Plastic Wrap"))->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  QApplication::processEvents();

  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
  const auto& entry = layer->smart_filter_stack()->entries.front();
  CHECK(entry.kind == patchy::SmartFilterKind::FilterGallery);
  const auto* plastic = patchy::editable_plastic_wrap(entry);
  CHECK(plastic != nullptr && plastic->highlight_strength == 4 && plastic->detail == 9 && plastic->smoothness == 15);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1U);

  ensure_artifact_dir();
  const auto artifact =
      std::filesystem::absolute(std::filesystem::path("test-artifacts") / "ui_smart_filter_gallery.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact);
  const auto reopened_it = std::find_if(reopened.layers().begin(), reopened.layers().end(),
                                        [](const patchy::Layer& candidate) {
                                          return candidate.name() == "Gallery Plastic Wrap";
                                        });
  CHECK(reopened_it != reopened.layers().end() && reopened_it->smart_filter_stack() != nullptr);
  const auto& reopened_entry = reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::FilterGallery);
  CHECK(patchy::editable_plastic_wrap(reopened_entry) != nullptr &&
        patchy::editable_plastic_wrap(reopened_entry)->highlight_strength == 4);

  // A two-effect entry renders but its settings do not open a dialog.
  select_named_layer(window, QStringLiteral("Gallery two Plastic Wraps"));
  auto* listed_edit =
      row(QStringLiteral("Gallery two Plastic Wraps"))->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(listed_edit != nullptr);
  const auto before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  listed_edit->click();
  QApplication::processEvents();
  CHECK(find_top_level_dialog(QStringLiteral("patchyFilterDialog")) == nullptr);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("one effect")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == before);
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_gallery() {
  return {
      {"ui_smart_filter_gallery_plastic_wrap_edits_in_place", ui_smart_filter_gallery_plastic_wrap_edits_in_place},
  };
}
