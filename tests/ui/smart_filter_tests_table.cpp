// Table-described native Smart Filters in the app (docs/smart-filters-pixelate-render.md):
// an imported Photoshop entry opens its generated settings dialog, an edit re-renders the
// layer from its source, and the result saves.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_object.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

namespace {

using namespace patchy::test::ui;

const patchy::Layer &require_layer_named(const patchy::Document &document,
                                         std::string_view name) {
  for (const auto &layer : document.layers()) {
    if (layer.name() == name) {
      return layer;
    }
  }
  CHECK(false);
  return document.layers().front();
}

// Review shots under test-artifacts/smart-filter-table (offscreen grabs of
// Patchy's own widgets and layer pixels).
void save_layer_shot(const patchy::Layer &layer, const char *name) {
  const auto &pixels = layer.pixels();
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    std::copy(pixels.pixel(0, y), pixels.pixel(0, y) + pixels.width() * 4,
              image.scanLine(y));
  }
  std::filesystem::create_directories("test-artifacts/smart-filter-table");
  image.scaled(image.width() * 3, image.height() * 3)
      .save(QStringLiteral("test-artifacts/smart-filter-table/") +
            QString::fromLatin1(name));
}

QWidget *layer_row(patchy::ui::MainWindow &window, const QString &name) {
  auto *list = window.findChild<QListWidget *>(QStringLiteral("layerList"));
  CHECK(list != nullptr);
  auto *item = require_layer_item(*list, name);
  auto *row = list->itemWidget(item);
  CHECK(row != nullptr);
  return row;
}

void ui_smart_filter_table_entry_edits_renders_and_saves() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  QTemporaryDir temporary;
  CHECK(temporary.isValid());

  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-other.psd")
          .wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto &document = patchy::ui::MainWindowTestAccess::document(window);

  // A Roundness Maximum is not modeled: its entry cannot be edited.
  select_named_layer(window, QStringLiteral("Maximum 4 roundness"));
  auto *round_edit = layer_row(window, QStringLiteral("Maximum 4 roundness"))
                         ->findChild<QToolButton *>(
                             QStringLiteral("layerSmartFilterEditButton"));
  CHECK(round_edit == nullptr || !round_edit->isEnabled());

  const auto layer_id =
      select_named_layer(window, QStringLiteral("Offset wrap 20 -7"));
  auto *row = layer_row(window, QStringLiteral("Offset wrap 20 -7"));
  auto *label = row->findChild<QLabel *>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Offset"));
  CHECK(label->toolTip().contains(QStringLiteral("20")));
  auto *edit = row->findChild<QToolButton *>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  std::filesystem::create_directories("test-artifacts/smart-filter-table");
  row->grab().save(QStringLiteral("test-artifacts/smart-filter-table/offset-row.png"));
  save_layer_shot(*std::as_const(document).find_layer(layer_id),
                  "offset-photoshop-preview.png");

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *horizontal =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterHorizontalSpin"));
    auto *vertical =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterVerticalSpin"));
    auto *areas = dialog->findChild<QComboBox *>(
        QStringLiteral("filterUndefinedAreasCombo"));
    CHECK(horizontal != nullptr && vertical != nullptr && areas != nullptr);
    CHECK(horizontal->value() == 20 && vertical->value() == -7);
    CHECK(areas->currentData().toString() == QStringLiteral("wrap"));
    horizontal->setValue(-33);
    areas->setCurrentIndex(areas->findData(QStringLiteral("repeat")));
    edited = true;
    dialog->accept();
  });
  edit->click();
  CHECK(process_events_until([&] { return edited; }, 5000));
  process_events_for(50);

  const auto *layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
  const auto &stack = *layer->smart_filter_stack();
  CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
  const auto *values =
      std::get_if<patchy::TableSmartFilter>(&stack.entries.front().parameters);
  CHECK(values != nullptr &&
        values->values == (std::vector<patchy::SmartFilterTableValue>{
                              std::int32_t{-33}, std::int32_t{-7},
                              std::string("repeat")}));
  // The layer now shows Patchy's render of the edited stack from the source.
  const auto &source =
      require_layer_named(std::as_const(document), "Source");
  const auto expected = patchy::render_smart_filter_stack(
      source.pixels(), source.bounds(),
      patchy::Rect::from_size(document.width(), document.height()), stack);
  CHECK(layer->bounds().x == expected.bounds.x &&
        layer->bounds().y == expected.bounds.y &&
        layer->bounds().width == expected.bounds.width &&
        layer->bounds().height == expected.bounds.height);
  for (std::int32_t y = 0; y < expected.bounds.height; ++y) {
    for (std::int32_t x = 0; x < expected.bounds.width; ++x) {
      const auto *want = expected.pixels.pixel(x, y);
      const auto *got = std::as_const(*layer).pixels().pixel(x, y);
      CHECK(want[3] == got[3]);
      if (want[3] != 0) {
        CHECK(want[0] == got[0] && want[1] == got[1] && want[2] == got[2]);
      }
    }
  }

  save_layer_shot(*layer, "offset-patchy-edited.png");

  const auto saved = temporary.filePath(QStringLiteral("table-edit.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      std::as_const(document), patchy::ui::to_filesystem_path(saved));
  const auto reread = patchy::psd::DocumentIo::read_file(
      patchy::ui::to_filesystem_path(saved));
  const auto &reread_layer = require_layer_named(reread, "Offset wrap 20 -7");
  CHECK(reread_layer.smart_filter_stack() != nullptr);
  const auto *reread_values = std::get_if<patchy::TableSmartFilter>(
      &reread_layer.smart_filter_stack()->entries.front().parameters);
  CHECK(reread_values != nullptr && reread_values->values == values->values);
}

// Opening never re-renders a stack: until the user edits it, a layer whose
// filters Patchy only approximates (Crystallize, Pointillize, Mezzotint,
// Fibers) shows Photoshop's baked pixels, byte for byte
// (docs/smart-filters-native.md, "Approximate renders").
void ui_smart_filter_table_approximate_stacks_keep_photoshop_pixels_on_open() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  for (const char *fixture :
       {"photoshop-smart-filter-random.psd", "photoshop-smart-filter-fibers.psd"}) {
    const auto file = patchy::test::committed_psd_fixture_path(fixture);
    const auto direct = patchy::psd::DocumentIo::read_file(file);
    patchy::ui::MainWindow window;
    show_window(window);
    patchy::ui::MainWindowTestAccess::open_document_path(
        window, QString::fromStdWString(file.wstring()));
    QApplication::processEvents();
    process_events_for(50);
    const auto &opened =
        std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    int compared = 0;
    for (const auto &layer : direct.layers()) {
      if (layer.smart_filter_stack() == nullptr) {
        continue;
      }
      CHECK(layer.smart_filter_stack()->support ==
            patchy::SmartFilterStackSupport::Supported);
      const auto &shown = require_layer_named(opened, layer.name());
      CHECK(shown.bounds().x == layer.bounds().x &&
            shown.bounds().y == layer.bounds().y &&
            shown.bounds().width == layer.bounds().width &&
            shown.bounds().height == layer.bounds().height);
      CHECK(std::equal(layer.pixels().data().begin(), layer.pixels().data().end(),
                       shown.pixels().data().begin(), shown.pixels().data().end()));
      ++compared;
    }
    CHECK(compared >= 2);
  }
}

// Photoshop re-randomizes Crystallize and Mezzotint, so an edit shows
// Patchy's own deterministic rendering; the generated dialogs still carry the
// imported values.
void ui_smart_filter_table_random_entries_edit_from_dialogs() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-random.psd")
          .wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto &document = patchy::ui::MainWindowTestAccess::document(window);

  const auto edit_layer = [&](const QString &name, const char *before_shot,
                              const char *after_shot,
                              const std::function<void(QDialog &)> &change) {
    const auto layer_id = select_named_layer(window, name);
    save_layer_shot(*std::as_const(document).find_layer(layer_id), before_shot);
    auto *edit = layer_row(window, name)->findChild<QToolButton *>(
        QStringLiteral("layerSmartFilterEditButton"));
    CHECK(edit != nullptr && edit->isEnabled());
    bool edited = false;
    QTimer::singleShot(20, [&] {
      auto *dialog = qobject_cast<QDialog *>(
          find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
      CHECK(dialog != nullptr);
      change(*dialog);
      edited = true;
      dialog->accept();
    });
    edit->click();
    CHECK(process_events_until([&] { return edited; }, 5000));
    process_events_for(50);
    const auto *layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    save_layer_shot(*layer, after_shot);
    return std::get<patchy::TableSmartFilter>(
        layer->smart_filter_stack()->entries.front().parameters);
  };

  const auto crystallize = edit_layer(
      QStringLiteral("Crystallize 10"), "crystallize-photoshop-preview.png",
      "crystallize-patchy-20.png", [](QDialog &dialog) {
        auto *cell = dialog.findChild<QSpinBox *>(QStringLiteral("filterCellSizeSpin"));
        CHECK(cell != nullptr && cell->value() == 10);
        cell->setValue(20);
      });
  CHECK(std::get<std::int32_t>(crystallize.values[0]) == 20);

  const auto mezzotint = edit_layer(
      QStringLiteral("Mezzotint fine dots"), "mezzotint-photoshop-preview.png",
      "mezzotint-patchy-medium-lines.png", [](QDialog &dialog) {
        auto *type = dialog.findChild<QComboBox *>(QStringLiteral("filterTypeCombo"));
        CHECK(type != nullptr &&
              type->currentData().toString() == QStringLiteral("fine_dots"));
        type->setCurrentIndex(type->findData(QStringLiteral("medium_lines")));
      });
  CHECK(std::get<std::string>(mezzotint.values[0]) == "medium_lines");
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part5() {
  return {
      {"ui_smart_filter_table_entry_edits_renders_and_saves",
       ui_smart_filter_table_entry_edits_renders_and_saves},
      {"ui_smart_filter_table_approximate_stacks_keep_photoshop_pixels_on_open",
       ui_smart_filter_table_approximate_stacks_keep_photoshop_pixels_on_open},
      {"ui_smart_filter_table_random_entries_edit_from_dialogs",
       ui_smart_filter_table_random_entries_edit_from_dialogs},
  };
}
