#include "smart_filter_test_support.hpp"

#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

namespace patchy::test::ui {

std::size_t smart_filter_effect_record_count(const patchy::Document& document) {
  std::size_t count = 0;
  for (const auto& block : document.metadata().smart_filter_effects.blocks) {
    count += block.records.size();
  }
  return count;
}

patchy::LayerId select_named_layer(patchy::ui::MainWindow& window, const QString& name) {
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, name);
  layer_list->clearSelection();
  layer_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
  item->setSelected(true);
  QApplication::processEvents();

  const auto id = static_cast<patchy::LayerId>(
      item->data(patchy::ui::kLayerIdRole).toULongLong());
  CHECK(id != 0);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).active_layer_id() == id);
  return id;
}

void open_smart_filter_instances_fixture(patchy::ui::MainWindow& window) {
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path(
          "photoshop-smart-filter-instances-base.psd")
          .wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto& document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(std::any_of(document.layers().begin(), document.layers().end(),
                    [](const patchy::Layer& layer) {
                      return layer.name() == kSmartFilterInstanceAName;
                    }));
  CHECK(std::any_of(document.layers().begin(), document.layers().end(),
                    [](const patchy::Layer& layer) {
                      return layer.name() == kSmartFilterInstanceBName;
                    }));
}

}  // namespace patchy::test::ui
