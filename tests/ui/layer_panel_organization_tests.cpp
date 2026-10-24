#include <QClipboard>
#include <QScrollBar>
#include <QStatusBar>

#include "ui/canvas_widget.hpp"
#include <QKeySequence>
#include "ui/ui_test_access.hpp"
#include "core/adjustment_layer.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_tree.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_thumbnail_size.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/app_settings.hpp"
#include "ui/theme_palette.hpp"
#include "psd/psd_document_io.hpp"
#include "synthetic_dng.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPainter>
#include <QStackedWidget>
#include <QTest>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QStyleOption>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_tab_switch_layers_follow_the_canvas_after_tab_reorder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* first_canvas = tabs->currentWidget();
  CHECK(!top_level_widget_exists(QStringLiteral("patchyNewLayerDialog")));
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* second_canvas = tabs->currentWidget();
  CHECK(layer_list->count() == 2);

  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  tab_bar->moveTab(tabs->indexOf(first_canvas), tabs->count() - 1);
  QApplication::processEvents();

  tabs->setCurrentWidget(first_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);

  tabs->setCurrentWidget(second_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  save_widget_artifact("ui_tab_session_layers", window);
}

void ui_new_layer_defaults_and_multiselect_layers_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->selectionMode() == QAbstractItemView::ExtendedSelection);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(!top_level_widget_exists(QStringLiteral("patchyNewLayerDialog")));
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  save_widget_artifact("ui_new_layer_result", window);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 4"));

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  save_widget_artifact("ui_multiselect_merge_down", window);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 5);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  save_widget_artifact("ui_multiselect_duplicate_delete", window);
}

void ui_ungroup_layers_releases_children_in_order_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  QStringList before;
  for (int row = 0; row < layer_list->count(); ++row) {
    before << layer_list->item(row)->text();
  }
  CHECK(before.size() >= 3);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto folder_id = document.active_layer_id();
  CHECK(folder_id.has_value());
  CHECK(std::as_const(document).find_layer(*folder_id)->kind() == patchy::LayerKind::Group);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  auto* ungroup = require_hotkey_action(window, QStringLiteral("layer.ungroup"));
  CHECK(ungroup != nullptr);
  CHECK(ungroup->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
  CHECK(require_hotkey_action(window, QStringLiteral("layer.new_folder"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_G));
  ungroup->trigger();
  QApplication::processEvents();
  QStringList after;
  for (int row = 0; row < layer_list->count(); ++row) {
    after << layer_list->item(row)->text();
  }
  CHECK(after == before);
  CHECK(std::as_const(document).find_layer(*folder_id) == nullptr);
  const auto active = document.active_layer_id();
  CHECK(active.has_value());
  CHECK(QString::fromStdString(std::as_const(document).find_layer(*active)->name()) == before.front());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Ungrouped 1 folder")));

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::as_const(document).find_layer(*folder_id) != nullptr);

  // Nothing grouped: a plain layer refuses.
  layer_list->clearSelection();
  document.set_active_layer(*active);
  require_action(window, "layerUngroupAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(QStringLiteral("Select a folder to ungroup")));
}

void ui_merge_down_repeatedly_collapses_to_one_layer() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());
  auto bottom_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(bottom_pixels, QRect(4, 4, 12, 12), QColor(40, 90, 220, 255));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Bottom", std::move(bottom_pixels)));

  auto middle_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(middle_pixels, QRect(16, 8, 12, 12), QColor(40, 180, 80, 192));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Middle", std::move(middle_pixels)));

  auto top_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(top_pixels, QRect(28, 12, 12, 12), QColor(230, 50, 50, 160));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Top", std::move(top_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Repeated"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 3);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Middle"));

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Bottom"));
}

void ui_merge_down_preserves_transparent_pixels() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  auto lower_pixels = solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(lower_pixels, QRect(6, 6, 8, 8), QColor(40, 90, 220, 128));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Lower", std::move(lower_pixels)));

  auto upper_pixels = solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(upper_pixels, QRect(12, 8, 8, 8), QColor(230, 40, 40, 128));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Upper", std::move(upper_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Alpha"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(0, 0).alpha() == 0);
  CHECK(copied.pixelColor(8, 8).alpha() > 0);
  CHECK(copied.pixelColor(14, 10).alpha() > 0);
}

void ui_merge_down_rasterizes_text_target() {
  patchy::Document document(160, 96, patchy::PixelFormat::rgba8());
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Lower", patchy::LayerKind::Text);
  text_layer.set_bounds(patchy::Rect{18, 22, 120, 42});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Lower";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "30";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#2040ff";
  document.add_layer(std::move(text_layer));

  auto paint_pixels = solid_pixels(160, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(paint_pixels, QRect(52, 44, 40, 18), QColor(230, 40, 40, 160));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Paint", std::move(paint_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Down Text"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Text: Lower"));

  auto* row = layer_list->itemWidget(layer_list->item(0));
  CHECK(row != nullptr);
  auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  CHECK(thumbnail->toolTip() == QStringLiteral("Layer thumbnail"));

  auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* text_info = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
  CHECK(layer_info != nullptr);
  CHECK(text_info != nullptr);
  CHECK(layer_info->text().contains(QStringLiteral("Pixel Layer")));
  CHECK(text_info->isHidden() || text_info->text().isEmpty());
}

void ui_merge_down_flattens_single_folder_in_place() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());

  auto base_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(base_pixels, QRect(2, 2, 10, 10), QColor(210, 40, 40, 255));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Base", std::move(base_pixels)));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  auto child_one = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_one, QRect(20, 4, 10, 10), QColor(40, 200, 40, 255));
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Folder Paint A", std::move(child_one)));
  auto child_two = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_two, QRect(34, 20, 10, 10), QColor(40, 40, 200, 255));
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Folder Paint B", std::move(child_two)));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Flatten Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();

  // A single folder flattens in place (Photoshop "Merge Group"); the layer below is untouched.
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Folder")) != nullptr);          // kept, now a pixel layer
  CHECK(find_layer_item(*layer_list, QStringLiteral("Folder Paint A")) == nullptr);  // children flattened away
  CHECK(find_layer_item(*layer_list, QStringLiteral("Base")) != nullptr);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  // Copy yields the active layer -- the flattened folder -- which now holds both former children.
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(24, 8).alpha() > 0);   // folder child A
  CHECK(copied.pixelColor(38, 24).alpha() > 0);  // folder child B
}

void ui_merge_down_discards_hidden_layers() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());

  auto bottom_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(bottom_pixels, QRect(2, 2, 10, 10), QColor(210, 40, 40, 255));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Bottom", std::move(bottom_pixels)));

  auto middle_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(middle_pixels, QRect(20, 4, 10, 10), QColor(40, 200, 40, 255));
  patchy::Layer middle(document.allocate_layer_id(), "Middle", std::move(middle_pixels));
  middle.set_visible(false);
  document.add_layer(std::move(middle));

  auto top_pixels = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(top_pixels, QRect(34, 20, 10, 10), QColor(40, 40, 200, 255));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Top", std::move(top_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Hidden"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 3);

  layer_list->clearSelection();
  for (int i = 0; i < layer_list->count(); ++i) {
    layer_list->item(i)->setSelected(true);
  }
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();

  // The hidden middle layer is erased (not blocking the merge); the visible layers flatten into one.
  CHECK(layer_list->count() == 1);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(6, 6).alpha() > 0);    // bottom rect kept
  CHECK(copied.pixelColor(38, 24).alpha() > 0);  // top rect kept
  CHECK(copied.pixelColor(24, 8).alpha() == 0);  // hidden middle rect erased
}

void ui_merge_down_flattens_entire_psd_to_single_layer() {
  const auto path = patchy::test::local_psd_fixture_path("ipad_main_v04.psd");
  patchy::test::skip_unless_exists(path);
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("iPad Flatten All"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() > 1);

  // Select everything in the panel (folders and their contents) and merge -- this is the reported
  // scenario, including hidden layers that previously aborted the operation.
  layer_list->selectAll();
  QApplication::processEvents();
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();

  // It all collapses to a single flat pixel layer with no folders left.
  auto& merged = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(merged.layers().size() == 1);
  CHECK(merged.layers().front().kind() == patchy::LayerKind::Pixel);
  CHECK(merged.layers().front().children().empty());
  CHECK(layer_list->count() == 1);
}

void ui_merge_down_merges_multiple_folders() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());

  patchy::Layer folder_a(document.allocate_layer_id(), "Folder A", patchy::LayerKind::Group);
  auto child_a = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_a, QRect(2, 2, 10, 10), QColor(210, 40, 40, 255));
  folder_a.add_child(patchy::Layer(document.allocate_layer_id(), "Child A", std::move(child_a)));
  document.add_layer(std::move(folder_a));

  patchy::Layer folder_b(document.allocate_layer_id(), "Folder B", patchy::LayerKind::Group);
  auto child_b = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_b, QRect(30, 20, 10, 10), QColor(40, 40, 210, 255));
  folder_b.add_child(patchy::Layer(document.allocate_layer_id(), "Child B", std::move(child_b)));
  document.add_layer(std::move(folder_b));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Folders"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* folder_a_item = require_layer_item(*layer_list, QStringLiteral("Folder A"));
  auto* folder_b_item = require_layer_item(*layer_list, QStringLiteral("Folder B"));
  layer_list->clearSelection();
  folder_a_item->setSelected(true);
  folder_b_item->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();

  // Both folders collapse together into one pixel layer.
  CHECK(layer_list->count() == 1);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(6, 6).alpha() > 0);     // Folder A child
  CHECK(copied.pixelColor(34, 24).alpha() > 0);   // Folder B child
  CHECK(copied.pixelColor(20, 33).alpha() == 0);  // untouched
}

void ui_merge_down_merges_layers_across_folders() {
  patchy::Document document(48, 36, patchy::PixelFormat::rgba8());

  patchy::Layer folder_a(document.allocate_layer_id(), "Folder A", patchy::LayerKind::Group);
  auto child_a = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_a, QRect(2, 2, 10, 10), QColor(210, 40, 40, 255));
  folder_a.add_child(patchy::Layer(document.allocate_layer_id(), "Child A", std::move(child_a)));
  document.add_layer(std::move(folder_a));

  patchy::Layer folder_b(document.allocate_layer_id(), "Folder B", patchy::LayerKind::Group);
  auto child_b = solid_pixels(48, 36, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(child_b, QRect(30, 20, 10, 10), QColor(40, 40, 210, 255));
  folder_b.add_child(patchy::Layer(document.allocate_layer_id(), "Child B", std::move(child_b)));
  document.add_layer(std::move(folder_b));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Merge Across Folders"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 4);

  auto* child_a_item = require_layer_item(*layer_list, QStringLiteral("Child A"));
  auto* child_b_item = require_layer_item(*layer_list, QStringLiteral("Child B"));
  layer_list->clearSelection();
  child_a_item->setSelected(true);
  child_b_item->setSelected(true);
  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();

  // Child B (in Folder B) merges into Child A (the lower of the two); Folder B is left empty.
  CHECK(find_layer_item(*layer_list, QStringLiteral("Child B")) == nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Child A")) != nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Folder B")) != nullptr);
  CHECK(layer_list->count() == 3);

  QApplication::clipboard()->clear();
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();
  const auto copied = QApplication::clipboard()->image().convertToFormat(QImage::Format_RGBA8888);
  CHECK(!copied.isNull());
  CHECK(copied.pixelColor(6, 6).alpha() > 0);    // Child A rect
  CHECK(copied.pixelColor(34, 24).alpha() > 0);  // Child B rect, now part of Child A
}

void ui_new_layer_button_inserts_above_selected_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  CHECK(layer_list != nullptr);
  CHECK(new_button != nullptr);

  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->setCurrentItem(background_item, QItemSelectionModel::ClearAndSelect);
  new_button->click();
  QApplication::processEvents();

  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Paint Layer"));
  CHECK(layer_list->item(1)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(2)->text() == QStringLiteral("Background"));

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->setCurrentItem(background_item, QItemSelectionModel::ClearAndSelect);
  paint_item->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  new_button->click();
  QApplication::processEvents();

  CHECK(layer_list->count() == 4);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 4"));
  CHECK(layer_list->item(1)->text() == QStringLiteral("Paint Layer"));
  CHECK(layer_list->item(2)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(3)->text() == QStringLiteral("Background"));
}

void ui_document_default_layer_selection_skips_folder() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "All Layers", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::Layer child(document.allocate_layer_id(), "Paint",
                      solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 120, 220)));
  const auto child_id = child.id();
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));
  CHECK(document.active_layer_id().value() == folder_id);
  const auto default_layer_id = patchy::default_non_group_layer_id(document.layers());
  CHECK(default_layer_id.has_value());
  CHECK(*default_layer_id == child_id);
  document.set_active_layer(*default_layer_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Opened With Default Selection"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 3);
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Paint"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("All Layers"));
  CHECK(layer_list->currentItem() == child_item);
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(child_item->isSelected());
  CHECK(!folder_item->isSelected());
  CHECK(static_cast<patchy::LayerId>(child_item->data(patchy::ui::kLayerIdRole).toULongLong()) == child_id);
  CHECK(active_layer_info->text().contains(QStringLiteral("Paint")));
  CHECK(!active_layer_info->text().contains(QStringLiteral("All Layers")));
}

void ui_document_with_only_folders_opens_with_no_layer_selected() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer folder(document.allocate_layer_id(), "All Layers", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group));
  document.add_layer(std::move(folder));
  CHECK(!patchy::default_non_group_layer_id(document.layers()).has_value());
  document.clear_active_layer();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Opened Without Selection"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  CHECK(layer_list != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(layer_list->count() == 2);
  CHECK(layer_list->currentItem() == nullptr);
  CHECK(layer_list->selectedItems().empty());
  CHECK(active_layer_info->text().contains(QStringLiteral("No active layer")));
}

void ui_duplicate_layer_copies_text_and_folder_trees() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Title", patchy::LayerKind::Text);
  text_layer.metadata()[patchy::kLayerMetadataText] = "Title";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(20, 100, 220))));
  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group);
  nested_folder.add_child(patchy::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Duplicate Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Text: Title copy")) != nullptr);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(folder_copy->data(Qt::UserRole + 2).toBool());
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->count() == 11);
  save_widget_artifact("ui_duplicate_text_folder_tree", window);
}

// GitHub issue 38: Duplicate Layer stacks the copies as one block directly above
// the topmost selected layer, inside its parent and in source order, and the
// copies become the selection. The old behavior appended them to the top.
// After a delete the layer below becomes active, in the same group (GitHub issue 249): the
// one above when the deleted layer was the bottom one, the group when nothing is left in it,
// the layer below the lowest one for a multi-selection. Layer > Delete Layer, the panel's
// delete button and the Delete key share the path; undo brings the deleted layer back active.
void ui_delete_layer_activates_the_layer_below() {
  const auto format = patchy::PixelFormat::rgba8();
  patchy::Document document(64, 48, format);
  document.add_pixel_layer("Bottom", solid_pixels(64, 48, format, QColor(Qt::white)));
  document.add_pixel_layer("Middle", solid_pixels(64, 48, format, QColor(Qt::red)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Inner Low", solid_pixels(8, 8, format, QColor(Qt::blue))));
  // A text layer: the Delete key removes text, smart object and shape layers (it clears pixel layers).
  patchy::Layer inner_mid(document.allocate_layer_id(), "Inner Mid", patchy::LayerKind::Text);
  inner_mid.set_bounds(patchy::Rect{4, 4, 30, 16});
  inner_mid.metadata()[patchy::kLayerMetadataText] = "Mid";
  inner_mid.metadata()[patchy::kLayerMetadataTextSize] = "12";
  inner_mid.metadata()[patchy::kLayerMetadataTextColor] = "#2040ff";
  folder.add_child(std::move(inner_mid));
  folder.add_child(
      patchy::Layer(document.allocate_layer_id(), "Inner High", solid_pixels(8, 8, format, QColor(Qt::green))));
  document.add_layer(std::move(folder));
  patchy::Layer solo(document.allocate_layer_id(), "Solo", patchy::LayerKind::Group);
  solo.add_child(patchy::Layer(document.allocate_layer_id(), "Only", solid_pixels(8, 8, format, QColor(Qt::magenta))));
  document.add_layer(std::move(solo));
  document.add_pixel_layer("Top", solid_pixels(64, 48, format, QColor(Qt::black)));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Delete Selection"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(layer_list != nullptr && delete_button != nullptr);
  if (layer_list == nullptr || delete_button == nullptr) {
    return;
  }
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  const auto rows = [layer_list] {
    QStringList out;
    for (int row = 0; row < layer_list->count(); ++row) {
      out << layer_list->item(row)->text();
    }
    return out;
  };
  const auto selected_rows = [layer_list] {
    QStringList out;
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->isSelected()) {
        out << layer_list->item(row)->text();
      }
    }
    return out;
  };
  const auto active_name = [&doc] {
    const auto id = doc.active_layer_id();
    const auto* layer = id.has_value() ? std::as_const(doc).find_layer(*id) : nullptr;
    return layer != nullptr ? QString::fromStdString(layer->name()) : QString();
  };
  const auto select = [layer_list](std::initializer_list<const char*> names) {
    layer_list->clearSelection();
    for (const auto* name : names) {
      auto* item = require_layer_item(*layer_list, QLatin1String(name));
      layer_list->setCurrentItem(item);
      item->setSelected(true);
    }
    QApplication::processEvents();
  };
  const auto undo = [&window] {
    require_action_by_text(window, QStringLiteral("Undo"))->trigger();
    QApplication::processEvents();
  };
  const QStringList all = {QStringLiteral("Top"),        QStringLiteral("Solo"),       QStringLiteral("Only"),
                           QStringLiteral("Folder"),     QStringLiteral("Inner High"), QStringLiteral("Inner Mid"),
                           QStringLiteral("Inner Low"),  QStringLiteral("Middle"),     QStringLiteral("Bottom")};
  CHECK(rows() == all);

  // A middle layer, through Layer > Delete Layer: the one below takes over.
  select({"Middle"});
  save_widget_artifact("issue_249_before_delete", *layer_list);
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("issue_249_after_delete", *layer_list);
  CHECK(!rows().contains(QStringLiteral("Middle")));
  CHECK(active_name() == QStringLiteral("Bottom"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Bottom")}));
  undo();
  CHECK(rows() == all);
  CHECK(active_name() == QStringLiteral("Middle"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Middle")}));

  // The bottom layer, through the panel's delete button: the one above takes over.
  select({"Bottom"});
  delete_button->click();
  QApplication::processEvents();
  CHECK(active_name() == QStringLiteral("Middle"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Middle")}));
  undo();
  CHECK(rows() == all);

  // Inside a group, through the Delete key (Clear with no pixel selection, which removes a
  // text layer): the layer below in the same group.
  select({"Inner Mid"});
  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(active_name() == QStringLiteral("Inner Low"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Inner Low")}));
  undo();
  CHECK(rows() == all);

  // The bottom layer of a group: the one above it in the group, not the layer below the group.
  select({"Inner Low"});
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(active_name() == QStringLiteral("Inner Mid"));
  undo();

  // The only layer in a group: the group itself.
  select({"Only"});
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(active_name() == QStringLiteral("Solo"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Solo")}));
  undo();
  CHECK(rows() == all);

  // Several layers: the layer below the lowest one deleted.
  select({"Top", "Inner High"});
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(!rows().contains(QStringLiteral("Top")));
  CHECK(!rows().contains(QStringLiteral("Inner High")));
  CHECK(active_name() == QStringLiteral("Inner Mid"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Inner Mid")}));
  undo();
  CHECK(rows() == all);
  CHECK(active_name() == QStringLiteral("Inner High"));
}

void ui_duplicate_layer_inserts_copies_above_source() {
  const auto format = patchy::PixelFormat::rgba8();
  patchy::Document document(64, 48, format);
  document.add_pixel_layer("Background", solid_pixels(64, 48, format, QColor(Qt::white)));
  document.add_pixel_layer("Middle", solid_pixels(64, 48, format, QColor(Qt::red)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Inner Low", solid_pixels(8, 8, format, QColor(Qt::blue))));
  folder.add_child(
      patchy::Layer(document.allocate_layer_id(), "Inner High", solid_pixels(8, 8, format, QColor(Qt::green))));
  document.add_layer(std::move(folder));
  document.add_pixel_layer("Top", solid_pixels(64, 48, format, QColor(Qt::black)));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Duplicate Placement"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  const auto rows = [layer_list] {
    QStringList out;
    for (int row = 0; row < layer_list->count(); ++row) {
      out << layer_list->item(row)->text();
    }
    return out;
  };
  const auto selected_rows = [layer_list] {
    QStringList out;
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->isSelected()) {
        out << layer_list->item(row)->text();
      }
    }
    return out;
  };
  const auto active_name = [&doc] {
    const auto id = doc.active_layer_id();
    const auto* layer = id.has_value() ? std::as_const(doc).find_layer(*id) : nullptr;
    return layer != nullptr ? QString::fromStdString(layer->name()) : QString();
  };
  const auto select_only = [layer_list](QListWidgetItem* item) {
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
  };

  CHECK((rows() == QStringList{QStringLiteral("Top"), QStringLiteral("Folder"), QStringLiteral("Inner High"),
                              QStringLiteral("Inner Low"), QStringLiteral("Middle"), QStringLiteral("Background")}));

  // A single layer in the middle of the stack: the copy sits directly above it.
  select_only(require_layer_item(*layer_list, QStringLiteral("Middle")));
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK((rows() == QStringList{QStringLiteral("Top"), QStringLiteral("Folder"), QStringLiteral("Inner High"),
                              QStringLiteral("Inner Low"), QStringLiteral("Middle copy"), QStringLiteral("Middle"),
                              QStringLiteral("Background")}));
  CHECK(active_name() == QStringLiteral("Middle copy"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Middle copy")}));

  // A child of a folder: the copy stays inside the folder, above its source.
  select_only(require_layer_item(*layer_list, QStringLiteral("Inner Low")));
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK((rows() == QStringList{QStringLiteral("Top"), QStringLiteral("Folder"), QStringLiteral("Inner High"),
                              QStringLiteral("Inner Low copy"), QStringLiteral("Inner Low"),
                              QStringLiteral("Middle copy"), QStringLiteral("Middle"), QStringLiteral("Background")}));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Inner Low copy"))->data(patchy::ui::kLayerDepthRole).toInt() ==
        1);
  CHECK(active_name() == QStringLiteral("Inner Low copy"));

  // A non-contiguous multi-selection: one block above the topmost selected
  // layer, in source order, and the copies are the new selection.
  const auto before_multi = rows();
  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Top"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Background"))->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK((rows() == QStringList{QStringLiteral("Top copy"), QStringLiteral("Background copy"), QStringLiteral("Top"),
                              QStringLiteral("Folder"), QStringLiteral("Inner High"), QStringLiteral("Inner Low copy"),
                              QStringLiteral("Inner Low"), QStringLiteral("Middle copy"), QStringLiteral("Middle"),
                              QStringLiteral("Background")}));
  CHECK(active_name() == QStringLiteral("Top copy"));
  CHECK((selected_rows() == QStringList{QStringLiteral("Top copy"), QStringLiteral("Background copy")}));

  // One undo entry removes the whole block.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(rows() == before_multi);
  save_widget_artifact("ui_duplicate_layer_inserts_copies_above_source", window);
}

void ui_copy_paste_layer_panel_copies_layers_and_folder_trees() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Title", patchy::LayerKind::Text);
  text_layer.metadata()[patchy::kLayerMetadataText] = "Title";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(20, 100, 220))));
  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", patchy::LayerKind::Group);
  nested_folder.add_child(patchy::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Copy Paste Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->row(require_layer_item(*layer_list, QStringLiteral("Text: Title copy"))) + 1 ==
        layer_list->row(require_layer_item(*layer_list, QStringLiteral("Text: Title"))));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->row(require_layer_item(*layer_list, QStringLiteral("Background copy"))) + 1 ==
        layer_list->row(require_layer_item(*layer_list, QStringLiteral("Background"))));
  CHECK(layer_list->count() == 12);
  save_widget_artifact("ui_copy_paste_layer_panel_tree", window);
}

void ui_layer_rows_toggle_visibility_and_drag_reorder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto settle_visibility = [canvas] {
    CHECK(process_events_until([canvas] { return canvas->render_settled(); }, 10000));
  };
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(!layer_list->dragDropOverwriteMode());
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);

  canvas->set_primary_color(QColor(240, 30, 30));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 100, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer 3"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Unchecked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  settle_visibility();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));

  blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
  settle_visibility();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  auto* blue_name = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(blue_name != nullptr);
  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  CHECK(background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(!canvas->has_selection());

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton);
  CHECK(background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton);
  CHECK(!background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* background_name = layer_list->itemWidget(background_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(background_name != nullptr);
  send_mouse(*background_name, QEvent::MouseButtonPress, background_name->rect().center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*background_name, QEvent::MouseButtonRelease, background_name->rect().center(), Qt::LeftButton,
             Qt::NoButton);
  CHECK(background_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ShiftModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ShiftModifier);
  CHECK(background_item->isSelected());
  CHECK(paint_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(layer_list->currentItem() == blue_item);

  // Ctrl+Shift+click adds the anchor..target range to the selection: the
  // clicked row is not toggled off (plain Ctrl would remove Paint) and rows
  // outside the range stay selected (plain Shift would drop Background).
  auto* paint_name = layer_list->itemWidget(paint_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(paint_name != nullptr);
  send_mouse(*paint_name, QEvent::MouseButtonPress, paint_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier | Qt::ShiftModifier);
  send_mouse(*paint_name, QEvent::MouseButtonRelease, paint_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(background_item->isSelected());
  CHECK(paint_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(layer_list->currentItem() == paint_item);

  // The bare-viewport path applies the same additive range with the same
  // anchor semantics.
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  const auto blue_row_center = layer_list->visualItemRect(blue_item).center();
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonPress, blue_row_center, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier | Qt::ShiftModifier);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonRelease, blue_row_center, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(background_item->isSelected());
  CHECK(paint_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(layer_list->currentItem() == blue_item);

  canvas->clear_selection();
  const auto blue_was_checked = blue_item->checkState();
  blue_visibility = layer_list->itemWidget(blue_item)->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  send_mouse(*blue_visibility, QEvent::MouseButtonPress, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_visibility, QEvent::MouseButtonRelease, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(!canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  auto* blue_thumbnail = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(blue_thumbnail != nullptr);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonPress, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonRelease, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  // Shift does not change thumbnail Ctrl-click semantics: it still loads the
  // pixels as a selection and leaves the row selection alone.
  canvas->clear_selection();
  const auto selected_rows_before_thumbnail = layer_list->selectedItems().size();
  send_mouse(*blue_thumbnail, QEvent::MouseButtonPress, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonRelease, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier | Qt::ShiftModifier);
  CHECK(canvas->has_selection());
  CHECK(layer_list->selectedItems().size() == selected_rows_before_thumbnail);

  // Dragged down to just above the Background, which keeps the bottom (GitHub issue 274).
  CHECK(layer_list->model()->moveRow(QModelIndex(), 0, QModelIndex(), layer_list->count() - 1));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(layer_list->count() - 2)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(layer_list->count() - 1)->text() == QStringLiteral("Background"));
  QStringList names_after_drop;
  for (int row = 0; row < layer_list->count(); ++row) {
    names_after_drop << layer_list->item(row)->text();
  }
  CHECK(names_after_drop.contains(QStringLiteral("Background")));
  CHECK(names_after_drop.contains(QStringLiteral("Paint Layer")));
  CHECK(names_after_drop.contains(QStringLiteral("Layer 3")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));
  save_widget_artifact("ui_layer_visibility_drag_reorder", window);
}

void ui_layer_folders_create_with_drag_drop_affordances() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_action(window, "layerNewFolderAction") != nullptr);
  CHECK(window.findChild<QPushButton*>(QStringLiteral("layerNewFolderButton")) != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* selected_blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* selected_paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(selected_blue_item);
  selected_blue_item->setSelected(true);
  selected_paint_item->setSelected(true);
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  const auto folder_row = layer_list->row(folder_item);
  CHECK(folder_row == 0);
  CHECK(folder_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK(folder_item->data(Qt::UserRole + 2).toBool());
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* folder_thumbnail = folder_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(folder_thumbnail != nullptr);
  CHECK(folder_thumbnail->toolTip() == QStringLiteral("Folder layer"));
  CHECK(!folder_thumbnail->pixmap(Qt::ReturnByValue).isNull());
  const auto folder_thumbnail_image = folder_thumbnail->pixmap(Qt::ReturnByValue).toImage();
  int bright_folder_outline_pixels = 0;
  int dark_folder_detail_pixels = 0;
  for (int y = 3; y < folder_thumbnail_image.height() - 3; ++y) {
    for (int x = 3; x < folder_thumbnail_image.width() - 3; ++x) {
      const auto color = folder_thumbnail_image.pixelColor(x, y);
      if (color.red() > 185 && color.green() > 135 && color.blue() < 135) {
        ++bright_folder_outline_pixels;
      }
      if (color.red() < 125 && color.green() < 105 && color.blue() < 95) {
        ++dark_folder_detail_pixels;
      }
    }
  }
  CHECK(bright_folder_outline_pixels > 40);
  CHECK(dark_folder_detail_pixels > 90);
  auto* blue_widget = layer_list->itemWidget(blue_item);
  auto* paint_widget = layer_list->itemWidget(paint_item);
  auto* background_widget = layer_list->itemWidget(background_item);
  CHECK(blue_widget != nullptr);
  CHECK(paint_widget != nullptr);
  CHECK(background_widget != nullptr);
  auto* blue_thumbnail = blue_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* paint_thumbnail = paint_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* background_thumbnail = background_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(blue_thumbnail != nullptr);
  CHECK(paint_thumbnail != nullptr);
  CHECK(background_thumbnail != nullptr);
  auto thumbnail_viewport_left = [viewport = layer_list->viewport()](QWidget* widget) {
    return widget->mapTo(viewport, QPoint()).x();
  };
  const auto folder_thumbnail_left = thumbnail_viewport_left(folder_thumbnail);
  CHECK(thumbnail_viewport_left(blue_thumbnail) >= folder_thumbnail_left + 8);
  CHECK(thumbnail_viewport_left(paint_thumbnail) >= folder_thumbnail_left + 8);
  CHECK(thumbnail_viewport_left(background_thumbnail) < thumbnail_viewport_left(blue_thumbnail));
  CHECK(blue_item->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(paint_item->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(background_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK(layer_list->item(folder_row + 1)->text() == QStringLiteral("Layer 3"));
  CHECK(layer_list->item(folder_row + 2)->text() == QStringLiteral("Paint Layer"));
  CHECK((folder_item->flags() & Qt::ItemIsDropEnabled) != 0);
  CHECK((blue_item->flags() & Qt::ItemIsDragEnabled) != 0);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(layer_list->defaultDropAction() == Qt::MoveAction);
  save_widget_artifact("ui_layer_folder_drag_drop", window);
}

void ui_layer_panel_mixed_folder_visual_cleanup() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Pixel Child",
                                 solid_pixels(18, 18, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255))));

  patchy::Layer text(document.allocate_layer_id(), "Text Child", patchy::LayerKind::Text);
  text.metadata()[patchy::kLayerMetadataText] = "Title";
  text.metadata()[patchy::kLayerMetadataTextSize] = "24";
  text.metadata()[patchy::kLayerMetadataTextColor] = "#f2f6fb";
  folder.add_child(std::move(text));

  patchy::Layer adjustment(document.allocate_layer_id(), "Hue/Saturation", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings adjustment_settings;
  adjustment_settings.kind = patchy::AdjustmentKind::HueSaturation;
  adjustment_settings.hue_saturation = patchy::HueSaturationAdjustment{18, 22, 0};
  patchy::configure_adjustment_layer(adjustment, adjustment_settings);
  folder.add_child(std::move(adjustment));

  patchy::Layer styled(document.allocate_layer_id(), "Masked Styled",
                       solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 110, 230, 255)));
  const auto styled_id = styled.id();
  patchy::PixelBuffer mask_pixels(20, 20, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  styled.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 20, 20}, std::move(mask_pixels), 0, true});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.75F;
  shadow.distance = 4.0F;
  shadow.size = 3.0F;
  styled.layer_style().drop_shadows.push_back(shadow);
  folder.add_child(std::move(styled));

  document.add_layer(std::move(folder));
  document.set_active_layer(styled_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Panel Visual Cleanup"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Assets"));
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* styled_item = require_layer_item(*layer_list, QStringLiteral("Masked Styled"));
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(styled_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);

  auto* folder_row = layer_list->itemWidget(folder_item);
  auto* background_row = layer_list->itemWidget(background_item);
  auto* styled_row = layer_list->itemWidget(styled_item);
  CHECK(folder_row != nullptr);
  CHECK(background_row != nullptr);
  CHECK(styled_row != nullptr);
  auto* folder_name = folder_row->findChild<QLabel*>(QStringLiteral("layerRowName"));
  auto* styled_details = styled_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(folder_name != nullptr);
  CHECK(styled_details != nullptr);
  CHECK(folder_name->text() == QStringLiteral("Assets"));
  CHECK(styled_row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) != nullptr);
  CHECK(styled_details->text().contains(QStringLiteral("mask")));

  auto* folder_visibility = folder_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* styled_visibility = styled_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* folder_thumbnail = folder_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* styled_thumbnail = styled_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* background_thumbnail = background_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(folder_visibility != nullptr);
  CHECK(styled_visibility != nullptr);
  CHECK(folder_thumbnail != nullptr);
  CHECK(styled_thumbnail != nullptr);
  CHECK(background_thumbnail != nullptr);

  auto widget_left = [viewport = layer_list->viewport()](QWidget* widget) {
    return widget->mapTo(viewport, QPoint()).x();
  };
  CHECK(std::abs(widget_left(folder_visibility) - widget_left(styled_visibility)) <= 1);
  CHECK(widget_left(styled_thumbnail) >= widget_left(folder_thumbnail) + 8);
  CHECK(widget_left(background_thumbnail) < widget_left(styled_thumbnail));
  save_widget_artifact("ui_layer_panel_mixed_folder_visual_cleanup", window);
}

void ui_layer_thumbnails_preview_the_whole_document() {
  // Document-mapped previews are the non-default mode now that zoomed
  // thumbnails ship on, so pin the preference off for this test.
  SettingsValueRestorer restore_zoom(QStringLiteral("view/zoomLayerThumbnailsToContent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/zoomLayerThumbnailsToContent"), false);
    settings.sync();
  }

  // 4:1. A square tile would wrap this in checkerboard bars, which claim the
  // opaque layer has transparency it does not have.
  constexpr std::int32_t kWideWidth = 160;
  constexpr std::int32_t kWideHeight = 40;
  const QColor fill(210, 60, 40, 255);
  const QColor badge_fill(40, 190, 90, 255);

  patchy::Document document(kWideWidth, kWideHeight, patchy::PixelFormat::rgba8());
  patchy::Layer banner(document.allocate_layer_id(), "Banner",
                       solid_pixels(kWideWidth, kWideHeight, patchy::PixelFormat::rgba8(), fill));
  patchy::PixelBuffer mask_pixels(kWideWidth, kWideHeight, patchy::PixelFormat::gray8());
  mask_pixels.clear(255);
  banner.set_mask(patchy::LayerMask{patchy::Rect{0, 0, kWideWidth, kWideHeight}, std::move(mask_pixels), 255, false});
  document.add_layer(std::move(banner));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group));

  // A small layer parked on the right. Its thumbnail must still be the whole
  // document, with the content where the layer actually sits. set_bounds comes
  // after the pixels: set_pixels resets bounds to the buffer size.
  patchy::Layer badge(document.allocate_layer_id(), "Badge",
                      solid_pixels(40, 30, patchy::PixelFormat::rgba8(), badge_fill));
  badge.set_bounds(patchy::Rect{100, 5, 40, 30});
  document.add_layer(std::move(badge));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Wide Banner"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto thumbnail_pixmap = [layer_list](const QString& layer_name, const QString& thumbnail_name) {
    auto* row = layer_list->itemWidget(require_layer_item(*layer_list, layer_name));
    CHECK(row != nullptr);
    auto* label = row->findChild<QLabel*>(thumbnail_name);
    CHECK(label != nullptr);
    return label->pixmap(Qt::ReturnByValue);
  };

  const auto content = thumbnail_pixmap(QStringLiteral("Banner"), QStringLiteral("layerContentThumbnail"));
  CHECK(content.size() == QSize(28, 7));
  CHECK(thumbnail_pixmap(QStringLiteral("Banner"), QStringLiteral("layerMaskThumbnail")).size() == QSize(28, 7));
  // Glyph thumbnails stay square: they are icons, not document previews.
  CHECK(thumbnail_pixmap(QStringLiteral("Assets"), QStringLiteral("layerContentThumbnail")).size() == QSize(28, 28));

  // Inside the frame the opaque layer must be nothing but its own color; a
  // single checkerboard pixel means a letterbox margin came back.
  const auto content_image = content.toImage();
  int fill_pixels = 0;
  int checkerboard_pixels = 0;
  for (int y = 1; y + 1 < content_image.height(); ++y) {
    for (int x = 1; x + 1 < content_image.width(); ++x) {
      const auto color = content_image.pixelColor(x, y);
      if (color == QColor(fill.red(), fill.green(), fill.blue())) {
        ++fill_pixels;
      }
      if (color == QColor(204, 204, 204) || color == QColor(255, 255, 255)) {
        ++checkerboard_pixels;
      }
    }
  }
  CHECK(fill_pixels == (content_image.width() - 2) * (content_image.height() - 2));
  CHECK(checkerboard_pixels == 0);

  // The 40x30 badge at (100, 5) is a third of the document wide, so its tile is
  // the document's shape, not its own 4:3, and the content sits on the right.
  const auto badge_thumbnail = thumbnail_pixmap(QStringLiteral("Badge"), QStringLiteral("layerContentThumbnail"));
  CHECK(badge_thumbnail.size() == QSize(28, 7));
  const auto badge_image = badge_thumbnail.toImage();
  const auto inside = badge_image.pixelColor(20, 3);
  const auto outside = badge_image.pixelColor(3, 3);
  CHECK(inside == QColor(badge_fill.red(), badge_fill.green(), badge_fill.blue()));
  CHECK((outside == QColor(204, 204, 204) || outside == QColor(255, 255, 255)));
  save_widget_artifact("ui_layer_thumbnail_document_preview", window);

  // Growing the canvas reshapes every tile even though no layer revision moved,
  // and the Banner stops covering the document, so checkerboard appears beside
  // it. Counted rather than sampled: the anchor the dialog restores is not this
  // test's business, only that both now show up.
  accept_canvas_size_dialog(160, 160);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  const auto grown = thumbnail_pixmap(QStringLiteral("Banner"), QStringLiteral("layerContentThumbnail"));
  CHECK(grown.size() == QSize(28, 28));
  const auto grown_image = grown.toImage();
  int grown_fill_pixels = 0;
  int grown_checkerboard_pixels = 0;
  for (int y = 1; y + 1 < grown_image.height(); ++y) {
    for (int x = 1; x + 1 < grown_image.width(); ++x) {
      const auto color = grown_image.pixelColor(x, y);
      if (color == QColor(fill.red(), fill.green(), fill.blue())) {
        ++grown_fill_pixels;
      }
      if (color == QColor(204, 204, 204) || color == QColor(255, 255, 255)) {
        ++grown_checkerboard_pixels;
      }
    }
  }
  CHECK(grown_fill_pixels > 100);
  CHECK(grown_checkerboard_pixels > 100);

  // A square document still fills the whole slot.
  patchy::Document square(64, 64, patchy::PixelFormat::rgba8());
  square.add_pixel_layer("Square",
                         solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(40, 110, 230, 255)));
  window.add_document_session(std::move(square), QStringLiteral("Square"));
  QApplication::processEvents();
  CHECK(thumbnail_pixmap(QStringLiteral("Square"), QStringLiteral("layerContentThumbnail")).size() ==
        QSize(28, 28));
}

// GitHub issue 386: the layer context menus' Thumbnail Size submenu (None, Small, Medium,
// Large) resizes every row's content and mask tiles and the row height, redraws folder and
// adjustment glyphs at the new size, and is remembered for the next window.
void ui_layer_thumbnail_size_menu_resizes_rows_and_persists() {
  SettingsValueRestorer restore_size{QString::fromLatin1(patchy::ui::kLayerThumbnailSizeSettingsKey)};
  SettingsValueRestorer restore_zoom(QStringLiteral("view/zoomLayerThumbnailsToContent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey));
    settings.setValue(QStringLiteral("view/zoomLayerThumbnailsToContent"), false);
    settings.sync();
  }
  const auto make_document = [] {
    patchy::Document document(96, 96, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer sky(96, 96, patchy::PixelFormat::rgba8());
    for (int y = 0; y < 96; ++y) {
      for (int x = 0; x < 96; ++x) {
        auto* px = sky.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(60 + y);
        px[1] = static_cast<std::uint8_t>(110 + x);
        px[2] = 230;
        px[3] = 255;
      }
    }
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Sky", std::move(sky)));
    patchy::Layer sun(document.allocate_layer_id(), "Sun",
                      solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(250, 200, 40)));
    sun.set_bounds(patchy::Rect{48, 8, 40, 40});
    patchy::PixelBuffer sun_mask(40, 40, patchy::PixelFormat::gray8());
    for (int y = 0; y < 40; ++y) {
      for (int x = 0; x < 40; ++x) {
        const auto dx = x - 20;
        const auto dy = y - 20;
        *sun_mask.pixel(x, y) = dx * dx + dy * dy <= 18 * 18 ? 255 : 0;
      }
    }
    sun.set_mask(patchy::LayerMask{patchy::Rect{48, 8, 40, 40}, std::move(sun_mask), 0, false});
    document.add_layer(std::move(sun));
    patchy::Layer folder(document.allocate_layer_id(), "Hills", patchy::LayerKind::Group);
    patchy::Layer hill(document.allocate_layer_id(), "Hill",
                       solid_pixels(96, 30, patchy::PixelFormat::rgba8(), QColor(60, 150, 70)));
    hill.set_bounds(patchy::Rect{0, 66, 96, 30});
    folder.add_child(std::move(hill));
    document.add_layer(std::move(folder));
    patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
    patchy::AdjustmentSettings settings;
    settings.kind = patchy::AdjustmentKind::Levels;
    settings.levels = patchy::LevelsAdjustment{20, 230, 110};
    patchy::configure_adjustment_layer(levels, settings);
    document.add_layer(std::move(levels));
    return document;
  };

  struct Expected {
    const char* action;
    const char* token;
    int tile;
    int mask_tile;
    int row_height;
    const char* artifact;
  };
  const std::array<Expected, 4> sizes{{
      {"layerThumbnailSizeLargeAction", "large", 48, 48, 60, "issue386_layers_large"},
      {"layerThumbnailSizeSmallAction", "small", 20, 20, 36, "issue386_layers_small"},
      {"layerThumbnailSizeNoneAction", "none", 0, 20, 36, "issue386_layers_none"},
      {"layerThumbnailSizeMediumAction", "medium", 28, 28, 44, "issue386_layers_medium"},
  }};

  {
    patchy::ui::MainWindow window;
    show_window(window);
    window.add_document_session(make_document(), QStringLiteral("Thumbnail Sizes"));
    QApplication::processEvents();
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
    CHECK(layer_list != nullptr && layers_dock != nullptr);
    const auto row_widget = [layer_list](const char* name) {
      auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QString::fromLatin1(name)));
      CHECK(row != nullptr);
      return row;
    };
    const auto pixmap_size = [&](const char* layer_name, const char* label_name) {
      auto* label = row_widget(layer_name)->findChild<QLabel*>(QString::fromLatin1(label_name));
      return label != nullptr ? label->pixmap(Qt::ReturnByValue).size() : QSize();
    };

    // Medium is the default and is today's 28 px tile in a 44 px row.
    CHECK(pixmap_size("Sky", "layerContentThumbnail") == QSize(28, 28));
    CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->sizeHint().height() == 44);
    save_widget_artifact("issue386_layers_default", *layers_dock);

    // The row menu ends with the submenu, Medium checked.
    QStringList size_entries;
    bool medium_checked = false;
    QTimer::singleShot(0, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || !menu->isVisible() || menu->objectName() != QStringLiteral("layerContextMenu")) {
          continue;
        }
        if (auto* size_menu = menu->findChild<QMenu*>(QStringLiteral("layerThumbnailSizeMenu"));
            size_menu != nullptr && menu->actions().back() == size_menu->menuAction()) {
          for (auto* action : size_menu->actions()) {
            size_entries << action->text().remove(QLatin1Char('&'));
            if (action->objectName() == QStringLiteral("layerThumbnailSizeMediumAction")) {
              medium_checked = action->isChecked();
            }
          }
        }
        menu->close();
        return;
      }
    });
    const auto point = layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Sun"))).center();
    QContextMenuEvent context_event(QContextMenuEvent::Mouse, point, layer_list->viewport()->mapToGlobal(point));
    QApplication::sendEvent(layer_list->viewport(), &context_event);
    QApplication::processEvents();
    CHECK(size_entries == (QStringList{QStringLiteral("None"), QStringLiteral("Small"), QStringLiteral("Medium"),
                                       QStringLiteral("Large")}));
    CHECK(medium_checked);

    for (const auto& expected : sizes) {
      auto* action = require_action(window, expected.action);
      action->trigger();
      QApplication::processEvents();
      CHECK(action->isChecked());
      CHECK(patchy::ui::app_settings()
                .value(QLatin1String(patchy::ui::kLayerThumbnailSizeSettingsKey))
                .toString() == QLatin1String(expected.token));
      auto* sky_content = row_widget("Sky")->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
      if (expected.tile == 0) {
        CHECK(sky_content == nullptr);
        CHECK(row_widget("Hills")->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")) == nullptr);
      } else {
        CHECK(sky_content != nullptr && sky_content->size() == QSize(expected.tile + 2, expected.tile + 2));
        // Square document: every tile is square. Glyphs are redrawn at the size, not scaled.
        CHECK(pixmap_size("Sky", "layerContentThumbnail") == QSize(expected.tile, expected.tile));
        CHECK(pixmap_size("Sun", "layerContentThumbnail") == QSize(expected.tile, expected.tile));
        CHECK(pixmap_size("Hills", "layerContentThumbnail") == QSize(expected.tile, expected.tile));
        CHECK(pixmap_size("Levels", "layerContentThumbnail") == QSize(expected.tile, expected.tile));
      }
      CHECK(pixmap_size("Sun", "layerMaskThumbnail") == QSize(expected.mask_tile, expected.mask_tile));
      for (const auto* name : {"Sky", "Sun", "Hills", "Levels"}) {
        auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
        CHECK(item->sizeHint().height() == expected.row_height);
        auto* main_row = row_widget(name)->findChild<QWidget*>(QStringLiteral("layerMainRow"));
        CHECK(main_row != nullptr && main_row->height() == expected.row_height);
        CHECK(layer_list->visualItemRect(item).height() == expected.row_height);
      }
      save_widget_artifact(expected.artifact, *layers_dock);
    }
    require_action(window, "layerThumbnailSizeLargeAction")->trigger();
    QApplication::processEvents();
  }

  // A new window starts at the saved size.
  patchy::ui::MainWindow reopened;
  show_window(reopened);
  reopened.add_document_session(make_document(), QStringLiteral("Thumbnail Sizes Again"));
  QApplication::processEvents();
  auto* layer_list = reopened.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_action(reopened, "layerThumbnailSizeLargeAction")->isChecked());
  auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Sky")));
  auto* content = row != nullptr ? row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")) : nullptr;
  CHECK(content != nullptr && content->pixmap(Qt::ReturnByValue).size() == QSize(48, 48));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Sky"))->sizeHint().height() == 60);
}

void ui_layer_thumbnails_zoom_to_content_preference() {
  SettingsValueRestorer restore_zoom(QStringLiteral("view/zoomLayerThumbnailsToContent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/zoomLayerThumbnailsToContent"), true);
    settings.sync();
  }

  constexpr std::int32_t kWideWidth = 160;
  constexpr std::int32_t kWideHeight = 40;
  const QColor fill(210, 60, 40, 255);
  const QColor badge_fill(40, 190, 90, 255);

  patchy::Document document(kWideWidth, kWideHeight, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Banner",
                           solid_pixels(kWideWidth, kWideHeight, patchy::PixelFormat::rgba8(), fill));
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group));

  // The 40x30 badge parked on the right, with a full-coverage mask: in zoomed
  // mode the row's tiles take the badge's own 4:3 shape, not the document's.
  patchy::Layer badge(document.allocate_layer_id(), "Badge",
                      solid_pixels(40, 30, patchy::PixelFormat::rgba8(), badge_fill));
  badge.set_bounds(patchy::Rect{100, 5, 40, 30});
  patchy::PixelBuffer badge_mask(40, 30, patchy::PixelFormat::gray8());
  badge_mask.clear(255);
  badge.set_mask(patchy::LayerMask{patchy::Rect{100, 5, 40, 30}, std::move(badge_mask), 255, false});
  document.add_layer(std::move(badge));

  // A canvas-sized buffer whose visible pixels are a 20x10 sticker: the crop
  // must trim the transparent slack, not stop at the declared bounds.
  patchy::PixelBuffer sticker_pixels(kWideWidth, kWideHeight, patchy::PixelFormat::rgba8());
  for (int y = 12; y < 22; ++y) {
    for (int x = 30; x < 50; ++x) {
      auto* px = sticker_pixels.pixel(x, y);
      px[0] = 40;
      px[1] = 80;
      px[2] = 220;
      px[3] = 255;
    }
  }
  document.add_layer(
      patchy::Layer(document.allocate_layer_id(), "Sticker", std::move(sticker_pixels)));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Zoomed Thumbnails"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto thumbnail_pixmap = [layer_list](const QString& layer_name, const QString& thumbnail_name) {
    auto* row = layer_list->itemWidget(require_layer_item(*layer_list, layer_name));
    CHECK(row != nullptr);
    auto* label = row->findChild<QLabel*>(thumbnail_name);
    CHECK(label != nullptr);
    return label->pixmap(Qt::ReturnByValue);
  };

  // Badge: its own aspect (40x30 -> 28x21), nothing but its color inside the
  // frame, and the mask tile shares the shape so the row stays consistent.
  const auto badge_thumbnail =
      thumbnail_pixmap(QStringLiteral("Badge"), QStringLiteral("layerContentThumbnail"));
  CHECK(badge_thumbnail.size() == QSize(28, 21));
  const auto badge_image = badge_thumbnail.toImage();
  int badge_fill_pixels = 0;
  for (int y = 1; y + 1 < badge_image.height(); ++y) {
    for (int x = 1; x + 1 < badge_image.width(); ++x) {
      if (badge_image.pixelColor(x, y) ==
          QColor(badge_fill.red(), badge_fill.green(), badge_fill.blue())) {
        ++badge_fill_pixels;
      }
    }
  }
  CHECK(badge_fill_pixels == (badge_image.width() - 2) * (badge_image.height() - 2));
  CHECK(thumbnail_pixmap(QStringLiteral("Badge"), QStringLiteral("layerMaskThumbnail")).size() ==
        QSize(28, 21));

  // Sticker: alpha-trimmed to 20x10 (28x14 tile), not the 160x40 buffer.
  CHECK(thumbnail_pixmap(QStringLiteral("Sticker"), QStringLiteral("layerContentThumbnail")).size() ==
        QSize(28, 14));

  // A canvas-sized opaque layer crops to itself, so its tile keeps the
  // document's 4:1 shape; the folder glyph stays a square icon.
  CHECK(thumbnail_pixmap(QStringLiteral("Banner"), QStringLiteral("layerContentThumbnail")).size() ==
        QSize(28, 7));
  CHECK(thumbnail_pixmap(QStringLiteral("Assets"), QStringLiteral("layerContentThumbnail")).size() ==
        QSize(28, 28));
  save_widget_artifact("ui_layer_thumbnail_zoom_to_content", window);

  // Unchecking the preference reverts to document mapping without a restart:
  // accepting the dialog clears the pixmap cache and rebuilds the rows.
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesZoomLayerThumbnailsCheck"));
    CHECK(check != nullptr);
    CHECK(check->isChecked());
    check->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(thumbnail_pixmap(QStringLiteral("Badge"), QStringLiteral("layerContentThumbnail")).size() ==
        QSize(28, 7));
  CHECK(thumbnail_pixmap(QStringLiteral("Badge"), QStringLiteral("layerMaskThumbnail")).size() ==
        QSize(28, 7));
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("view/zoomLayerThumbnailsToContent"), true).toBool());
}

void ui_layer_drag_drops_child_above_parent_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  settings.levels = patchy::LevelsAdjustment{20, 230, 110};
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Child Above Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto levels_rect = layer_list->visualItemRect(levels_item);

  send_layer_drop(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id});
  process_events_for(1);

  levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  CHECK(layer_list->row(levels_item) == 0);
  CHECK(layer_list->row(folder_item) == 1);
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_drag_multiselect_drops_children_above_parent_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Multi Child Above Folder"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto paint_id = static_cast<patchy::LayerId>(paint_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto levels_rect = layer_list->visualItemRect(levels_item);

  send_layer_drop(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id, paint_id});
  process_events_for(1);

  levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  CHECK(layer_list->row(levels_item) == 0);
  CHECK(layer_list->row(paint_item) == 1);
  CHECK(layer_list->row(folder_item) == 2);
  CHECK(levels_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
}

void ui_layer_drag_folder_header_crack_drops_inside_folder() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  document.add_layer(std::move(folder));
  patchy::Layer hue_saturation(document.allocate_layer_id(), "Hue/Saturation", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::HueSaturation;
  settings.hue_saturation = patchy::HueSaturationAdjustment{25, 20, 0};
  patchy::configure_adjustment_layer(hue_saturation, settings);
  document.add_layer(std::move(hue_saturation));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Folder Crack Drop"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* hue_item = require_layer_item(*layer_list, QStringLiteral("Hue/Saturation"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  const auto hue_id = static_cast<patchy::LayerId>(hue_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto folder_rect = layer_list->visualItemRect(folder_item);

  send_layer_drop(*layer_list, QPoint(folder_rect.center().x(), folder_rect.bottom() - 2), {hue_id});
  process_events_for(1);

  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  hue_item = require_layer_item(*layer_list, QStringLiteral("Hue/Saturation"));
  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  CHECK(layer_list->row(folder_item) == 0);
  CHECK(layer_list->row(hue_item) == 1);
  CHECK(layer_list->row(paint_item) == 2);
  CHECK(hue_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  CHECK(paint_item->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_drag_shows_insertion_and_folder_drop_previews() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  folder.add_child(std::move(levels));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Drop Preview"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* levels_item = require_layer_item(*layer_list, QStringLiteral("Levels"));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  const auto levels_id = static_cast<patchy::LayerId>(levels_item->data(patchy::ui::kLayerIdRole).toULongLong());
  const auto blue_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.blue() > 190 && color.green() > 115 && color.red() < 95) {
          ++count;
        }
      }
    }
    return count;
  };

  send_layer_drag_move(*layer_list, layer_list->visualItemRect(folder_item).center(), {levels_id});
  process_events_for(1);
  const auto folder_preview = layer_list->viewport()->grab().toImage();
  CHECK(blue_pixels(folder_preview) > 30);

  const auto levels_rect = layer_list->visualItemRect(levels_item);
  send_layer_drag_move(*layer_list, QPoint(2, levels_rect.top() + 4), {levels_id});
  process_events_for(1);
  const auto insertion_preview = layer_list->viewport()->grab().toImage();
  CHECK(blue_pixels(insertion_preview) > 30);
  send_layer_drag_leave(*layer_list);
}

void ui_layer_list_scrolls_with_wheel_and_drag_autoscroll() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  for (int index = 0; index < 24; ++index) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Scrollable " + std::to_string(index + 1),
                                     solid_pixels(6, 6, patchy::PixelFormat::rgba8(), QColor(20, 80, 180))));
  }

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Scroll Drag"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 0);
  scroll->setValue(0);
  QApplication::processEvents();

  auto* top_item = layer_list->item(0);
  CHECK(top_item != nullptr);
  auto* top_name = layer_list->itemWidget(top_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(top_name != nullptr);
  send_wheel(*top_name, top_name->rect().center(), -120);
  CHECK(scroll->value() > 0);

  scroll->setValue(0);
  QApplication::processEvents();
  qApp->installEventFilter(layer_list);
  const auto list_global_position = layer_list->viewport()->mapToGlobal(layer_list->viewport()->rect().center());
  send_wheel(window, window.mapFromGlobal(list_global_position), -120);
  qApp->removeEventFilter(layer_list);
  CHECK(scroll->value() > 0);

  scroll->setValue(0);
  QApplication::processEvents();
  const auto id = static_cast<patchy::LayerId>(top_item->data(patchy::ui::kLayerIdRole).toULongLong());
  send_layer_drag_enter(*layer_list, QPoint(layer_list->viewport()->width() / 2, layer_list->viewport()->height() - 3),
                        {id});
  send_layer_drag_move(*layer_list, QPoint(layer_list->viewport()->width() / 2, layer_list->viewport()->height() - 3),
                       {id});
  process_events_for(320);
  CHECK(scroll->value() > 0);
  const auto scroll_after_auto = scroll->value();
  send_wheel(*top_name, top_name->rect().center(), -120);
  CHECK(scroll->value() > scroll_after_auto);
  send_layer_drag_leave(*layer_list);
}

void ui_layer_list_pixel_wheel_scrolls_in_row_steps() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  for (int index = 0; index < 24; ++index) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Pixel Scroll " + std::to_string(index + 1),
                                     solid_pixels(6, 6, patchy::PixelFormat::rgba8(), QColor(20, 80, 180))));
  }

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Pixel Scroll"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 5);
  scroll->setValue(0);
  QApplication::processEvents();

  auto* top_item = layer_list->item(0);
  CHECK(top_item != nullptr);
  const auto row_height = layer_list->visualItemRect(top_item).height();
  CHECK(row_height > 0);
  auto* top_name = layer_list->itemWidget(top_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(top_name != nullptr);

  // One browser wheel notch arrives as a ~120 px pixelDelta on wasm; it must
  // step a few rows, not jump the scrollbar's whole range.
  send_pixel_wheel(*top_name, top_name->rect().center(), -120);
  CHECK(scroll->value() == 120 / row_height);

  // Sub-row trackpad deltas accumulate across events instead of being dropped.
  const auto half_row = row_height / 2;
  for (int repeat = 0; repeat < 6; ++repeat) {
    send_pixel_wheel(*top_name, top_name->rect().center(), -half_row);
  }
  CHECK(scroll->value() == (120 + 6 * half_row) / row_height);
}

void ui_layer_list_edge_click_selects_without_scrolling() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  for (int index = 0; index < 24; ++index) {
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Edge Click " + std::to_string(index + 1),
                                     solid_pixels(6, 6, patchy::PixelFormat::rgba8(), QColor(20, 80, 180))));
  }

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Edge Click"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(170);
  QApplication::processEvents();

  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 4);
  scroll->setValue(3);
  QApplication::processEvents();

  QListWidgetItem* target_item = nullptr;
  QRect target_visible_rect;
  const auto viewport_rect = layer_list->viewport()->rect();
  for (int row = layer_list->count() - 1; row >= 0; --row) {
    auto* item = layer_list->item(row);
    if (item == nullptr || item->isSelected()) {
      continue;
    }
    const auto visible_rect = layer_list->visualItemRect(item).intersected(viewport_rect);
    if (visible_rect.isValid() && visible_rect.bottom() >= viewport_rect.bottom() - 6) {
      target_item = item;
      target_visible_rect = visible_rect;
      break;
    }
  }
  CHECK(target_item != nullptr);
  auto* row_widget = layer_list->itemWidget(target_item);
  CHECK(row_widget != nullptr);

  const auto scroll_before = scroll->value();
  const auto viewport_click =
      QPoint(std::min(target_visible_rect.left() + 80, viewport_rect.right() - 24), target_visible_rect.center().y());
  const auto row_click = row_widget->mapFrom(layer_list->viewport(), viewport_click);
  send_mouse(*row_widget, QEvent::MouseButtonPress, row_click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*row_widget, QEvent::MouseButtonRelease, row_click, Qt::LeftButton, Qt::NoButton);
  process_events_for(120);

  CHECK(scroll->value() == scroll_before);
  CHECK(target_item->isSelected());
  CHECK(layer_list->currentItem() == target_item);
}

void ui_layer_new_folder_button_groups_dropped_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* folder_button = window.findChild<QPushButton*>(QStringLiteral("layerNewFolderButton"));
  CHECK(layer_list != nullptr);
  CHECK(folder_button != nullptr);

  require_action(window, "layerNewAction")->trigger();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  const std::vector<patchy::LayerId> ids{
      static_cast<patchy::LayerId>(layer4->data(patchy::ui::kLayerIdRole).toULongLong()),
      static_cast<patchy::LayerId>(layer3->data(patchy::ui::kLayerIdRole).toULongLong())};

  send_layer_button_drop(*folder_button, ids);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  CHECK(layer_list->row(folder_item) == 0);
  CHECK(layer_list->row(layer4) == 1);
  CHECK(layer_list->row(layer3) == 2);
  CHECK(folder_item->data(patchy::ui::kLayerDepthRole).toInt() == 0);
  CHECK(layer4->data(patchy::ui::kLayerDepthRole).toInt() == 1);
  CHECK(layer3->data(patchy::ui::kLayerDepthRole).toInt() == 1);
}

void ui_layer_action_buttons_accept_multiselect_drops() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
  auto* duplicate_button = window.findChild<QPushButton*>(QStringLiteral("layerDuplicateButton"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  CHECK(layer_list != nullptr);
  CHECK(new_button != nullptr);
  CHECK(duplicate_button != nullptr);
  CHECK(delete_button != nullptr);

  require_action(window, "layerNewAction")->trigger();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  const std::vector<patchy::LayerId> ids{
      static_cast<patchy::LayerId>(layer4->data(patchy::ui::kLayerIdRole).toULongLong()),
      static_cast<patchy::LayerId>(layer3->data(patchy::ui::kLayerIdRole).toULongLong())};
  const auto count_before = layer_list->count();

  send_layer_button_drop(*new_button, ids);
  CHECK(layer_list->count() == count_before + 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4 copy")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3 copy")) != nullptr);

  send_layer_button_drop(*duplicate_button, ids);
  CHECK(layer_list->count() == count_before + 4);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4 copy 2")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3 copy 2")) != nullptr);

  send_layer_button_drop(*delete_button, ids);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Layer 4")) == nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Layer 3")) == nullptr);
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 4")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Layer 3")) != nullptr);
}

// The Layers panel footer's Add Layer Mask button: enabled only for exactly
// one selected layer that can take a new mask, and it adds the mask.
void ui_layer_add_mask_button_tracks_selection_and_adds_mask() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* mask_button = window.findChild<QPushButton*>(QStringLiteral("layerAddMaskButton"));
  CHECK(layer_list != nullptr);
  CHECK(mask_button != nullptr);
  CHECK(!mask_button->toolTip().isEmpty());

  require_action(window, "layerNewAction")->trigger();
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* layer3 = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  auto* layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  const auto layer4_id = static_cast<patchy::LayerId>(layer4->data(patchy::ui::kLayerIdRole).toULongLong());

  // One selected pixel layer without a mask: enabled.
  layer_list->clearSelection();
  layer_list->setCurrentItem(layer4);
  layer4->setSelected(true);
  QApplication::processEvents();
  CHECK(mask_button->isEnabled());

  // Two selected layers: disabled.
  layer3->setSelected(true);
  QApplication::processEvents();
  CHECK(!mask_button->isEnabled());

  layer_list->clearSelection();
  layer4 = require_layer_item(*layer_list, QStringLiteral("Layer 4"));
  layer_list->setCurrentItem(layer4);
  layer4->setSelected(true);
  QApplication::processEvents();
  CHECK(mask_button->isEnabled());

  mask_button->click();
  QApplication::processEvents();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* masked = doc.find_layer(layer4_id);
  CHECK(masked != nullptr && masked->mask().has_value());
  // The layer has its mask now, so there is nothing left to add.
  CHECK(!mask_button->isEnabled());

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(doc.find_layer(layer4_id) != nullptr && !doc.find_layer(layer4_id)->mask().has_value());
  CHECK(mask_button->isEnabled());

  // A pixel-locked layer cannot take a mask.
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  lock_all->click();
  QApplication::processEvents();
  CHECK(!mask_button->isEnabled());
}

// GitHub issue 94: the Layers panel footer's Layer Styles button. Its menu holds
// Edit Layer Styles... and every effect in the dialog's own order, and it is
// enabled while a selected layer can take styles (not locked).
void ui_layer_styles_button_tracks_selection_and_lists_effects() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  document.add_pixel_layer("Badge", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(220, 90, 40)));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Styles Button"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* styles_button = window.findChild<QToolButton*>(QStringLiteral("layerStylesButton"));
  CHECK(layer_list != nullptr);
  CHECK(styles_button != nullptr);
  CHECK(!styles_button->toolTip().isEmpty());
  CHECK(!styles_button->icon().isNull());
  CHECK(styles_button->property("layerActionButton").toBool());
  CHECK(styles_button->popupMode() == QToolButton::InstantPopup);
  CHECK(styles_button->focusPolicy() == Qt::NoFocus);

  auto* menu = styles_button->menu();
  CHECK(menu != nullptr);
  CHECK(menu->objectName() == QStringLiteral("layerStylesButtonMenu"));
  const auto actions = menu->actions();
  const std::array<std::pair<const char*, const char*>, 10> effects{{
      {"layerStylesBevelEmbossAction", "Bevel && Emboss..."},
      {"layerStylesStrokeAction", "Stroke..."},
      {"layerStylesInnerShadowAction", "Inner Shadow..."},
      {"layerStylesInnerGlowAction", "Inner Glow..."},
      {"layerStylesSatinAction", "Satin..."},
      {"layerStylesColorOverlayAction", "Color Overlay..."},
      {"layerStylesGradientOverlayAction", "Gradient Overlay..."},
      {"layerStylesPatternOverlayAction", "Pattern Overlay..."},
      {"layerStylesOuterGlowAction", "Outer Glow..."},
      {"layerStylesDropShadowAction", "Drop Shadow..."},
  }};
  CHECK(actions.size() == static_cast<qsizetype>(effects.size() + 2));
  // The full dialog first, through the same action as Layer > Edit Layer Styles...
  CHECK(actions[0] == require_action(window, "layerBlendingOptionsAction"));
  CHECK(actions[1]->isSeparator());
  for (std::size_t index = 0; index < effects.size(); ++index) {
    const auto* action = actions[static_cast<qsizetype>(index + 2)];
    CHECK(action->objectName() == QLatin1String(effects[index].first));
    CHECK(action->text() == QLatin1String(effects[index].second));
  }

  // The active layer can take styles.
  auto* badge = require_layer_item(*layer_list, QStringLiteral("Badge"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(badge);
  badge->setSelected(true);
  QApplication::processEvents();
  CHECK(styles_button->isEnabled());

  // The footer with its menu open: the issue's AFTER picture.
  auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
  CHECK(panel != nullptr);
  menu->popup(styles_button->mapToGlobal(QPoint(0, styles_button->height())));
  QApplication::processEvents();
  CHECK(menu->isVisible());
  const auto panel_image = panel->grab().toImage();
  const auto menu_image = menu->grab().toImage();
  menu->hide();
  QApplication::processEvents();
  const QRect button_rect(styles_button->mapTo(panel, QPoint()), styles_button->size());
  const auto ratio = panel_image.devicePixelRatio();
  const auto menu_size = (QSizeF(menu_image.size()) / menu_image.devicePixelRatio()).toSize();
  // Drawn where Qt places it at the bottom of a window: above the button.
  const auto menu_top = button_rect.top() - menu_size.height();
  const auto lift = std::max(0, -menu_top);
  const QSize composed_size(std::max(panel->width(), button_rect.left() + menu_size.width()), panel->height() + lift);
  QImage composed((QSizeF(composed_size) * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
  composed.setDevicePixelRatio(ratio);
  composed.fill(panel_image.pixelColor(0, 0));
  {
    QPainter painter(&composed);
    painter.drawImage(QPoint(0, lift), panel_image);
    painter.drawImage(QPoint(button_rect.left(), menu_top + lift), menu_image);
  }
  save_image_artifact("ui_layer_styles_button_menu", composed);

  // Two selected layers still take styles together.
  require_layer_item(*layer_list, QStringLiteral("Background"))->setSelected(true);
  QApplication::processEvents();
  CHECK(styles_button->isEnabled());

  // A locked layer cannot.
  layer_list->clearSelection();
  badge = require_layer_item(*layer_list, QStringLiteral("Badge"));
  layer_list->setCurrentItem(badge);
  badge->setSelected(true);
  QApplication::processEvents();
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  lock_all->click();
  QApplication::processEvents();
  CHECK(!styles_button->isEnabled());
  lock_all->click();
  QApplication::processEvents();
  CHECK(styles_button->isEnabled());
}

// GitHub issue 94: an effect entry in the footer's Layer Styles menu opens the
// one Layer Style dialog on that effect, turned on; OK keeps it, Cancel leaves
// the layer as it was, and an existing turned-off effect is turned on rather
// than duplicated.
// GitHub issue 354: every button in a panel footer (Layers, Channels, Paths) is flat at
// rest, like the Layers footer's adjustment and styles QToolButtons, and shows its outline
// only on hover. Saves each footer at rest over the same footer with one button hovered,
// Dark and Light, for the issue's pictures.
void ui_panel_footer_buttons_outline_only_on_hover() {
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    const std::string scheme_name = scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light";
    patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
    document.add_pixel_layer("Layer 1", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(220, 90, 40)));
    patchy::ui::MainWindow window;
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Footer Buttons"));
    QApplication::processEvents();

    const auto check_footer = [&](const char* bar_name, QWidget* bar, const char* property, int expected_count,
                                  const QString& hover_name) {
      CHECK(bar != nullptr);
      if (bar == nullptr) {
        return;
      }
      std::vector<QAbstractButton*> buttons;
      QRect footer_rect;
      for (auto* button : bar->findChildren<QAbstractButton*>()) {
        if (button->property(property).toBool() && button->isVisibleTo(bar)) {
          buttons.push_back(button);
          footer_rect |= QRect(button->mapTo(bar, QPoint()), button->size());
        }
      }
      CHECK(static_cast<int>(buttons.size()) == expected_count);
      auto* hovered_button = bar->findChild<QAbstractButton*>(hover_name);
      CHECK(hovered_button != nullptr);
      if (buttons.empty() || hovered_button == nullptr) {
        return;
      }
      footer_rect = footer_rect.adjusted(-4, -4, 4, 4).intersected(bar->rect());
      const auto rest = bar->grab(footer_rect).toImage();
      // The hovered look, drawn by the button's own (style sheet) style with the
      // mouse-over state set: a pointer simulated offscreen does not reliably reach the
      // button once earlier tests have run.
      const auto paint_hovered = [&](QImage& image) {
        QPainter painter(&image);
        painter.translate(hovered_button->mapTo(bar, QPoint()) - footer_rect.topLeft());
        painter.setClipRect(hovered_button->rect());
        painter.fillRect(hovered_button->rect(), image.pixelColor(QPoint(0, 0)));
        auto* style = hovered_button->style();
        if (auto* tool_button = qobject_cast<QToolButton*>(hovered_button); tool_button != nullptr) {
          QStyleOptionToolButton option;
          option.initFrom(tool_button);
          option.state |= QStyle::State_MouseOver | QStyle::State_Raised;
          option.subControls = QStyle::SC_ToolButton;
          option.icon = tool_button->icon();
          option.iconSize = tool_button->iconSize();
          option.toolButtonStyle = tool_button->toolButtonStyle();
          style->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, tool_button);
        } else {
          QStyleOptionButton option;
          option.initFrom(hovered_button);
          option.state |= QStyle::State_MouseOver | QStyle::State_Raised;
          option.icon = hovered_button->icon();
          option.iconSize = hovered_button->iconSize();
          style->drawControl(QStyle::CE_PushButton, &option, &painter, hovered_button);
        }
      };
      auto hovered = rest.copy();
      hovered.setDevicePixelRatio(rest.devicePixelRatio());
      paint_hovered(hovered);

      const auto ratio = rest.devicePixelRatio();
      QImage composed(QSize(rest.width(), rest.height() * 2), QImage::Format_ARGB32_Premultiplied);
      composed.setDevicePixelRatio(ratio);
      {
        QPainter painter(&composed);
        painter.drawImage(QPoint(0, 0), rest);
        painter.drawImage(QPointF(0, rest.height() / ratio), hovered);
      }
      save_image_artifact(std::string("ui_issue_354_") + bar_name + "_footer_" + scheme_name, composed);

      // The left border column of a button against the panel just beside it.
      const auto pixel_at = [&](const QImage& image, QPoint bar_point) {
        const auto local = bar_point - footer_rect.topLeft();
        return image.pixelColor(QPoint(static_cast<int>(local.x() * ratio), static_cast<int>(local.y() * ratio)));
      };
      for (auto* button : buttons) {
        const auto edge = button->mapTo(bar, QPoint(0, button->height() / 2));
        const auto beside = edge - QPoint(2, 0);
        if (!footer_rect.contains(beside)) {
          continue;
        }
        const auto edge_color = pixel_at(rest, edge);
        const auto beside_color = pixel_at(rest, beside);
        if (edge_color != beside_color) {
          std::fprintf(stderr, "  %s %s at rest: edge %s, beside %s\n", bar_name, qPrintable(button->objectName()),
                       qPrintable(edge_color.name()), qPrintable(beside_color.name()));
        }
        CHECK(edge_color == beside_color);
      }
      const auto hover_edge = hovered_button->mapTo(bar, QPoint(0, hovered_button->height() / 2));
      if (pixel_at(hovered, hover_edge) == pixel_at(rest, hover_edge)) {
        std::fprintf(stderr, "  %s hovered edge unchanged: %s\n", bar_name,
                     qPrintable(pixel_at(hovered, hover_edge).name()));
      }
      CHECK(pixel_at(hovered, hover_edge) != pixel_at(rest, hover_edge));
    };

    auto* layers_panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
    check_footer("layers", layers_panel, "layerActionButton", 8, QStringLiteral("layerNewButton"));
    check_footer("channels", window.findChild<QWidget*>(QStringLiteral("channelActionBar")), "channelActionButton", 6,
                 QStringLiteral("channelNewButton"));
    check_footer("paths", window.findChild<QWidget*>(QStringLiteral("pathsActionBar")), "channelActionButton", 6,
                 QStringLiteral("pathsNewButton"));
  }
}

void ui_layer_styles_menu_entry_opens_dialog_on_effect() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  document.add_pixel_layer("Badge", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(220, 90, 40)));
  const auto badge_id = document.layers().back().id();
  patchy::LayerStroke stroke;
  stroke.enabled = false;
  document.find_layer(badge_id)->layer_style().strokes.push_back(stroke);
  document.set_active_layer(badge_id);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Styles Menu"));
  QApplication::processEvents();

  auto* styles_button = window.findChild<QToolButton*>(QStringLiteral("layerStylesButton"));
  CHECK(styles_button != nullptr && styles_button->menu() != nullptr);
  CHECK(styles_button->isEnabled());
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto badge_style = [&]() -> const patchy::LayerStyle& {
    const auto* layer = doc.find_layer(badge_id);
    CHECK(layer != nullptr);
    return layer->layer_style();
  };

  struct Seen {
    bool found{false};
    QString current_label;
    QString current_page;
    bool checked{false};
  };
  // Triggers a menu entry. The dialog is read, then closed before any CHECK
  // runs, so a failure can never leave it open.
  const auto run_entry = [&](const char* action_name, const char* check_name, bool accept) {
    QAction* entry = nullptr;
    for (auto* action : styles_button->menu()->actions()) {
      if (action->objectName() == QLatin1String(action_name)) {
        entry = action;
      }
    }
    CHECK(entry != nullptr);
    Seen seen;
    QTimer::singleShot(0, [&seen, check_name, accept] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("patchyLayerStyleDialog") ||
            !dialog->isVisible()) {
          continue;
        }
        seen.found = true;
        auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
        auto* stack = dialog->findChild<QStackedWidget*>(QStringLiteral("layerStyleOptionsStack"));
        if (categories != nullptr && categories->currentItem() != nullptr) {
          seen.current_label = categories->currentItem()->text();
        }
        if (stack != nullptr && stack->currentWidget() != nullptr) {
          seen.current_page = stack->currentWidget()->objectName();
        }
        if (check_name != nullptr) {
          auto* check = dialog->findChild<QCheckBox*>(QLatin1String(check_name));
          seen.checked = check != nullptr && check->isChecked();
        }
        if (accept) {
          dialog->accept();
        } else {
          dialog->reject();
        }
        return;
      }
    });
    entry->trigger();
    QApplication::processEvents();
    CHECK(seen.found);
    return seen;
  };

  // Drop Shadow: opens on its page, ticked; OK adds one enabled drop shadow.
  auto seen = run_entry("layerStylesDropShadowAction", "layerStyleDropShadowCategoryCheck", true);
  CHECK(seen.current_label == QStringLiteral("Drop Shadow"));
  CHECK(seen.current_page == QStringLiteral("layerStyleDropShadowPage"));
  CHECK(seen.checked);
  CHECK(badge_style().drop_shadows.size() == 1U);
  CHECK(badge_style().drop_shadows.front().enabled);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(badge_style().drop_shadows.empty());

  // Cancel leaves the turned-off stroke as it was.
  seen = run_entry("layerStylesStrokeAction", "layerStyleStrokeCategoryCheck", false);
  CHECK(seen.current_label == QStringLiteral("Stroke"));
  CHECK(seen.current_page == QStringLiteral("layerStyleStrokePage"));
  CHECK(seen.checked);
  CHECK(badge_style().strokes.size() == 1U);
  CHECK(!badge_style().strokes.front().enabled);

  // OK turns that stroke on instead of adding a second one.
  seen = run_entry("layerStylesStrokeAction", "layerStyleStrokeCategoryCheck", true);
  CHECK(seen.checked);
  CHECK(badge_style().strokes.size() == 1U);
  CHECK(badge_style().strokes.front().enabled);

  // Bevel & Emboss (not stackable) opens on its page too.
  seen = run_entry("layerStylesBevelEmbossAction", "layerStyleBevelEmbossCategoryCheck", false);
  CHECK(seen.current_page == QStringLiteral("layerStyleBevelEmbossPage"));
  CHECK(seen.checked);
  CHECK(badge_style().bevels.empty());

  // Edit Layer Styles... opens the dialog on Blending Options, nothing turned on.
  seen = run_entry("layerBlendingOptionsAction", nullptr, false);
  CHECK(seen.current_page == QStringLiteral("layerStyleBlendingPage"));
}

// GitHub issue 244: Layer > Color and the row's context menu label the selected
// layers (groups too) in one undo step, and a labeled row shows a strip down its
// left edge in the label's theme role, inside the row's left margin.
void ui_layer_color_labels_menu_undo_and_row_strip() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(96, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Inside",
                                solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(40, 90, 200))));
  document.add_layer(std::move(group));
  document.add_pixel_layer("Badge", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(220, 90, 40)));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Colors"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto label_of = [&](const char* name) {
    const patchy::Layer* found = nullptr;
    const std::function<void(const std::vector<patchy::Layer>&)> walk = [&](const std::vector<patchy::Layer>& layers) {
      for (const auto& layer : layers) {
        if (layer.name() == name) {
          found = &layer;
        }
        walk(layer.children());
      }
    };
    walk(doc.layers());
    CHECK(found != nullptr);
    return found != nullptr ? found->color_label() : patchy::LayerColorLabel{0xffffU};
  };
  const auto strip_of = [&](const char* name) -> QWidget* {
    auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QString::fromLatin1(name)));
    CHECK(row != nullptr);
    return row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerRowColorStrip")) : nullptr;
  };
  const auto select_only = [&](std::initializer_list<const char*> names) {
    layer_list->clearSelection();
    bool first = true;
    for (const auto* name : names) {
      auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
      if (first) {
        layer_list->setCurrentItem(item);
        first = false;
      }
      item->setSelected(true);
    }
    QApplication::processEvents();
  };

  // Layer > Color: None, a separator, then the seven colors in Photoshop's order.
  auto* color_menu = window.findChild<QMenu*>(QStringLiteral("layerColorMenu"));
  CHECK(color_menu != nullptr);
  CHECK(color_menu->menuAction()->menuRole() == QAction::NoRole);
  const std::array<const char*, 8> names{{"layerColorNoneAction", "layerColorRedAction", "layerColorOrangeAction",
                                          "layerColorYellowAction", "layerColorGreenAction", "layerColorBlueAction",
                                          "layerColorVioletAction", "layerColorGrayAction"}};
  const auto actions = color_menu->actions();
  CHECK(actions.size() == 9);
  CHECK(actions[1]->isSeparator());
  for (std::size_t index = 0; index < names.size(); ++index) {
    auto* action = actions[static_cast<qsizetype>(index == 0 ? 0 : index + 1)];
    CHECK(action->objectName() == QLatin1String(names[index]));
    CHECK(action->isCheckable());
    CHECK(action->menuRole() == QAction::NoRole);
    CHECK(index == 0 || !action->icon().isNull());
  }
  auto* red = require_action(window, "layerColorRedAction");
  auto* blue = require_action(window, "layerColorBlueAction");
  auto* none = require_action(window, "layerColorNoneAction");

  // One layer: labeled, its strip painted in the theme role, inside the margin.
  select_only({"Badge"});
  CHECK(strip_of("Badge") == nullptr);
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  red->trigger();
  QApplication::processEvents();
  CHECK(label_of("Badge") == patchy::kLayerColorRed);
  CHECK(label_of("Background") == patchy::kLayerColorNone);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1);
  auto* strip = strip_of("Badge");
  CHECK(strip != nullptr);
  CHECK(strip->property("layerColor").toString() == QStringLiteral("red"));
  auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Badge")));
  auto* eye = row->findChild<QWidget*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(eye != nullptr);
  CHECK(strip->x() == 0 && strip->width() <= 4 && strip->height() >= 40);
  CHECK(strip->geometry().right() < eye->mapTo(strip->parentWidget(), QPoint()).x());
  const auto row_image = row->grab().toImage();
  const auto painted = row_image.pixelColor(QPoint(1, row->height() / 2) * row_image.devicePixelRatio());
  CHECK(painted.rgb() == patchy::ui::theme().layer_color_red.rgb());
  CHECK(strip_of("Background") == nullptr);
  // The selection's label is the checked entry.
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  emit color_menu->aboutToShow();
  CHECK(red->isChecked() && !none->isChecked());

  // Undo takes the label off again; redo brings it back.
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(label_of("Badge") == patchy::kLayerColorNone);
  CHECK(strip_of("Badge") == nullptr);

  // A group and a layer together, from the row's context menu.
  select_only({"Folder", "Background"});
  bool saw_color_menu = false;
  bool saw_blue = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      if (auto* submenu = menu->findChild<QMenu*>(QStringLiteral("layerContextColorMenu")); submenu != nullptr) {
        saw_color_menu = true;
        saw_blue = submenu->actions().contains(blue);
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Folder"))).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_color_menu);
  CHECK(saw_blue);
  select_only({"Folder", "Background"});
  blue->trigger();
  QApplication::processEvents();
  CHECK(label_of("Folder") == patchy::kLayerColorBlue);
  CHECK(label_of("Background") == patchy::kLayerColorBlue);
  CHECK(label_of("Inside") == patchy::kLayerColorNone);
  CHECK(strip_of("Folder") != nullptr && strip_of("Background") != nullptr);

  // A mixed selection checks nothing; None clears every selected label.
  select_only({"Folder", "Badge"});
  emit color_menu->aboutToShow();
  CHECK(!blue->isChecked() && !none->isChecked());
  none->trigger();
  QApplication::processEvents();
  CHECK(label_of("Folder") == patchy::kLayerColorNone);
  CHECK(label_of("Background") == patchy::kLayerColorBlue);
}

// GitHub issue 276: a right-click on a row's eye opens Photoshop's short color menu (None,
// then the seven colors: the Layer > Color actions) instead of the layer menu. It labels
// the selected layers in one undo step; an unselected row's eye selects that row first.
void ui_layer_eye_right_click_opens_color_menu() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Base", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  document.add_pixel_layer("Middle", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(40, 90, 200)));
  document.add_pixel_layer("Top", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(220, 90, 40)));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Colors"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto label_of = [&](const char* name) {
    for (const auto& layer : doc.layers()) {
      if (layer.name() == name) {
        return layer.color_label();
      }
    }
    CHECK(false);
    return patchy::LayerColorLabel{0xffffU};
  };
  const auto select_only = [&](std::initializer_list<const char*> names) {
    layer_list->clearSelection();
    bool first = true;
    for (const auto* name : names) {
      auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
      if (first) {
        layer_list->setCurrentItem(item);
        first = false;
      }
      item->setSelected(true);
    }
    QApplication::processEvents();
  };
  // Right-clicks the eye of `row_name` and picks `pick` (an action objectName) from the menu
  // that opens; returns that menu's objectName and its action names.
  const auto right_click_eye = [&](const char* row_name, const char* pick) {
    std::pair<QString, QStringList> seen;
    QTimer::singleShot(0, [&seen, pick] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* menu = qobject_cast<QMenu*>(widget);
        if (menu == nullptr || !menu->isVisible()) {
          continue;
        }
        seen.first = menu->objectName();
        QAction* chosen = nullptr;
        for (auto* action : menu->actions()) {
          seen.second.push_back(action->isSeparator() ? QStringLiteral("-") : action->objectName());
          if (action->objectName() == QLatin1String(pick)) {
            chosen = action;
          }
          if (!action->isSeparator() && action->objectName() != QStringLiteral("layerColorNoneAction") &&
              (action->icon().isNull() || !action->isIconVisibleInMenu())) {
            seen.second.push_back(QStringLiteral("no swatch"));
          }
        }
        if (chosen != nullptr) {
          menu->setActiveAction(chosen);
          save_widget_artifact("issue276_eye_color_menu", *menu);
          QTest::keyClick(menu, Qt::Key_Return);
        } else {
          menu->close();
        }
        return;
      }
    });
    auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QString::fromLatin1(row_name)));
    CHECK(row != nullptr);
    auto* eye = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerVisibilityCheck")) : nullptr;
    CHECK(eye != nullptr);
    if (eye != nullptr) {
      const auto local = eye->rect().center();
      QContextMenuEvent context_event(QContextMenuEvent::Mouse, local, eye->mapToGlobal(local));
      QApplication::sendEvent(eye, &context_event);
    }
    QApplication::processEvents();
    return seen;
  };

  select_only({"Top", "Middle"});
  const auto undo_depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto seen = right_click_eye("Middle", "layerColorRedAction");
  CHECK(seen.first == QStringLiteral("layerEyeColorMenu"));
  CHECK(seen.second == (QStringList{QStringLiteral("layerColorNoneAction"), QStringLiteral("-"),
                                    QStringLiteral("layerColorRedAction"), QStringLiteral("layerColorOrangeAction"),
                                    QStringLiteral("layerColorYellowAction"), QStringLiteral("layerColorGreenAction"),
                                    QStringLiteral("layerColorBlueAction"), QStringLiteral("layerColorVioletAction"),
                                    QStringLiteral("layerColorGrayAction")}));
  CHECK(label_of("Top") == patchy::kLayerColorRed);
  CHECK(label_of("Middle") == patchy::kLayerColorRed);
  CHECK(label_of("Base") == patchy::kLayerColorNone);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth + 1);
  // The layers stay visible: a right-click never toggles the eye.
  CHECK(std::all_of(doc.layers().begin(), doc.layers().end(), [](const patchy::Layer& layer) { return layer.visible(); }));

  // An unselected row's eye: that row alone takes the color.
  const auto green = right_click_eye("Base", "layerColorGreenAction");
  CHECK(green.first == QStringLiteral("layerEyeColorMenu"));
  CHECK(label_of("Base") == patchy::kLayerColorGreen);
  CHECK(label_of("Top") == patchy::kLayerColorRed);
  save_widget_artifact("issue276_layers_after_eye_colors", *window.findChild<QWidget*>(QStringLiteral("layersDock")));

  // The rest of the row still opens the layer menu.
  const auto row_menu = [&] {
    QString name;
    QTimer::singleShot(0, [&name] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* menu = qobject_cast<QMenu*>(widget); menu != nullptr && menu->isVisible()) {
          name = menu->objectName();
          menu->close();
          return;
        }
      }
    });
    const auto point = layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Top"))).center();
    QContextMenuEvent context_event(QContextMenuEvent::Mouse, point, layer_list->viewport()->mapToGlobal(point));
    QApplication::sendEvent(layer_list->viewport(), &context_event);
    QApplication::processEvents();
    return name;
  }();
  CHECK(row_menu == QStringLiteral("layerContextMenu"));
  // The row menu hides the shared actions' icons; the eye menu still shows its swatches.
  const auto after_row_menu = right_click_eye("Top", "layerColorNoneAction");
  CHECK(after_row_menu.first == QStringLiteral("layerEyeColorMenu"));
  CHECK(!after_row_menu.second.contains(QStringLiteral("no swatch")));
  CHECK(label_of("Top") == patchy::kLayerColorNone);
}

// GitHub issue 244: the reporter's sample in the Layers panel, Dark and Light, for
// the issue's before and after pictures, and every sample row's strip. The
// "before" pictures came from this test without the strip checks on an older build.
void ui_issue_244_sample_layers_panel_shots_if_available() {
  const auto path =
      patchy::test::source_root_path() / "local-test-fixtures" / "psd" / "issue-244" / "Test - Layer Lock.psd";
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  for (const auto scheme : {patchy::ui::ColorSchemePreference::Dark, patchy::ui::ColorSchemePreference::Light}) {
    ColorSchemeRestorer restore(scheme);
    patchy::ui::MainWindow window;
    show_window(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
    QApplication::processEvents();
    QApplication::processEvents();
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(layer_list != nullptr && layer_list->count() == 5);
    for (const auto& [name, color] : std::array<std::pair<const char*, const char*>, 5>{{
             {"Full Lock", "blue"},
             {"Transparent Pixel Lock", "red"},
             {"Lock Image Pixels", "orange"},
             {"Lock Position", "yellow"},
             {"Prevent Auto Nesting", "green"},
         }}) {
      auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QString::fromLatin1(name)));
      auto* strip = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerRowColorStrip")) : nullptr;
      CHECK(strip != nullptr && strip->property("layerColor").toString() == QLatin1String(color));
    }
    auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
    CHECK(panel != nullptr);
    save_widget_artifact(std::string("ui_issue_244_layers_panel_") +
                             (scheme == patchy::ui::ColorSchemePreference::Dark ? "dark" : "light"),
                         *panel);
  }
}

// GitHub issue 246: the same sample's "Prevent Auto Nesting" layer opens with the lock. Its
// row shows the badge, the right-click Lock submenu shows the registered action checked, and
// a resave keeps lspf 0x8 for the Photoshop open check
// (test-artifacts/issue_246_sample_resaved.psd).
const patchy::Layer* top_level_layer_named(const patchy::Document& document, const char* name) {
  const auto& layers = document.layers();
  const auto found = std::find_if(layers.begin(), layers.end(),
                                  [name](const patchy::Layer& layer) { return layer.name() == name; });
  return found != layers.end() ? &*found : nullptr;
}

void ui_issue_246_sample_prevent_auto_nesting_if_available() {
  const auto path =
      patchy::test::source_root_path() / "local-test-fixtures" / "psd" / "issue-244" / "Test - Layer Lock.psd";
  patchy::test::skip_unless_exists(path);
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 5);
  auto* item = require_layer_item(*layer_list, QStringLiteral("Prevent Auto Nesting"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(item);
  item->setSelected(true);
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = top_level_layer_named(document, "Prevent Auto Nesting");
  CHECK(layer != nullptr && patchy::layer_lock_flags(*layer) == patchy::kLayerLockArtboardAutonest);
  auto* row = layer_list->itemWidget(item);
  const auto badges = row != nullptr ? row->findChildren<QLabel*>(QStringLiteral("layerLockBadge")) : QList<QLabel*>{};
  CHECK(badges.size() == 1);
  CHECK(!badges.isEmpty() &&
        badges.front()->toolTip() == QStringLiteral("Auto-nesting into and out of artboards prevented"));
  // Not an editing lock: none of the Layers panel lock buttons is on.
  for (const char* name :
       {"layerLockTransparentButton", "layerLockPixelsButton", "layerLockPositionButton", "layerLockAllButton"}) {
    auto* button = window.findChild<QToolButton*>(QLatin1String(name));
    CHECK(button != nullptr && !button->isChecked());
  }
  auto* action = require_action(window, "layerPreventAutoNestingAction");
  CHECK(action->isChecked());
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
    save_widget_artifact("issue_246_layers_panel_badge", *panel);
  }

  bool saw_context_entry = false;
  bool context_entry_checked = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      for (auto* lock_menu : menu->findChildren<QMenu*>()) {
        if (!lock_menu->actions().contains(action)) {
          continue;
        }
        saw_context_entry = true;
        context_entry_checked = action->isCheckable() && action->isChecked();
        lock_menu->popup(menu->geometry().topRight());
        QApplication::processEvents();
        save_widget_artifact("issue_246_context_lock_menu", *lock_menu);
        lock_menu->hide();
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(item).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_entry);
  CHECK(context_entry_checked);

  // Set in Patchy on "Lock Position": the row gains the badge and the save writes 0x4 | 0x8.
  auto* position_item = require_layer_item(*layer_list, QStringLiteral("Lock Position"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(position_item);
  position_item->setSelected(true);
  QApplication::processEvents();
  CHECK(!action->isChecked());
  action->trigger();
  QApplication::processEvents();
  CHECK(action->isChecked());
  auto* position_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Lock Position")));
  CHECK(position_row != nullptr && position_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge")).size() == 2);

  ensure_artifact_dir();
  const auto saved_path =
      QDir(QStringLiteral("test-artifacts")).absoluteFilePath(QStringLiteral("issue_246_sample_resaved.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, saved_path));
  const auto saved = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(saved_path));
  const auto* saved_layer = top_level_layer_named(saved, "Prevent Auto Nesting");
  CHECK(saved_layer != nullptr && patchy::layer_lock_flags(*saved_layer) == patchy::kLayerLockArtboardAutonest);
  const auto* full = top_level_layer_named(saved, "Full Lock");
  CHECK(full != nullptr && patchy::layer_locks_all(*full) && !patchy::layer_prevents_auto_nesting(*full));
  const auto* position = top_level_layer_named(saved, "Lock Position");
  CHECK(position != nullptr &&
        patchy::layer_lock_flags(*position) == (patchy::kLayerLockPosition | patchy::kLayerLockArtboardAutonest));
}

// Photoshop saves its Background with lspf 0xd (transparency, position and Prevent
// Auto-Nesting) and shows it one lock icon: the row keeps its two lock badges and draws no
// auto-nesting badge, though the lock is kept and the action shows it (GitHub issue 246).
// Another layer with 0x8 still shows the badge.
void ui_psd_background_hides_prevent_auto_nesting_badge() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto psd_path = temp.filePath(QStringLiteral("background-0xd.psd"));
  {
    patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
    auto& background = document.add_pixel_layer(
        "Background", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
    patchy::set_layer_lock_flags(background, patchy::kLayerLockTransparentPixels);
    auto& nesting = document.add_pixel_layer("Nesting",
                                             solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(40, 90, 200)));
    patchy::set_layer_lock_flags(nesting, patchy::kLayerLockTransparentPixels);
    auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
    // Records run bottom to top: the first lspf is the Background's, the last the top layer's.
    const std::array<std::uint8_t, 8> tag{'8', 'B', 'I', 'M', 'l', 's', 'p', 'f'};
    const auto first = std::search(bytes.begin(), bytes.end(), tag.begin(), tag.end());
    const auto last = std::find_end(bytes.begin(), bytes.end(), tag.begin(), tag.end());
    CHECK(first != bytes.end() && last != bytes.end() && first != last);
    const auto write_value = [&bytes](std::vector<std::uint8_t>::iterator at, std::uint32_t value) {
      const auto offset = static_cast<std::size_t>(std::distance(bytes.begin(), at)) + 12U;
      for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (24U - 8U * index));
      }
    };
    write_value(first, 0xDU);
    write_value(last, 0x8U);
    QFile file(psd_path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
          static_cast<qint64>(bytes.size()));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr && layer_list->count() == 2);
  const auto badge_tips = [layer_list](const char* name) {
    auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QLatin1String(name)));
    QStringList tips;
    if (row != nullptr) {
      for (const auto* badge : row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"))) {
        tips << badge->toolTip();
      }
    }
    return tips;
  };
  // The Background's one padlock (GitHub issue 274) stands for its auto-nest bit too.
  const auto background_tips = badge_tips("Background");
  CHECK(background_tips.size() == 1);
  CHECK(background_tips.size() == 1 && background_tips.front().startsWith(QStringLiteral("Background")));
  CHECK(badge_tips("Nesting") == QStringList{QStringLiteral("Auto-nesting into and out of artboards prevented")});

  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* background = top_level_layer_named(document, "Background");
  CHECK(background != nullptr && patchy::layer_prevents_auto_nesting(*background));
  auto* item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(item);
  item->setSelected(true);
  QApplication::processEvents();
  CHECK(require_action(window, "layerPreventAutoNestingAction")->isChecked());
  if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
    save_widget_artifact("issue_246_background_badges", *panel);
  }
}

void ui_layer_folders_expand_and_contract_children() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Nested 1",
                                   solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Nested 2",
                                   solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Folder Disclosure"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  CHECK(layer_list->count() == 4);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Nested 1")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(disclosure->isChecked());
  CHECK(disclosure->arrowType() == Qt::DownArrow);

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(QStringLiteral("Nested 1")) == nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) == nullptr);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(!folder_item->data(Qt::UserRole + 3).toBool());
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(!disclosure->isChecked());
  CHECK(disclosure->arrowType() == Qt::RightArrow);

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 1"))->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 2"))->data(Qt::UserRole + 1).toInt() == 1);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  save_widget_artifact("ui_layer_folder_expand_contract", window);
}

void ui_layer_folders_open_with_saved_expansion_state() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));

  patchy::Layer closed_group(document.allocate_layer_id(), "Closed Folder", patchy::LayerKind::Group);
  closed_group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  closed_group.add_child(patchy::Layer(document.allocate_layer_id(), "Closed Child",
                                          solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  document.add_layer(std::move(closed_group));

  patchy::Layer open_group(document.allocate_layer_id(), "Open Folder", patchy::LayerKind::Group);
  open_group.metadata()[patchy::kLayerMetadataGroupExpanded] = "true";
  open_group.add_child(patchy::Layer(document.allocate_layer_id(), "Open Child",
                                        solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(open_group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Saved Folder State"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  auto* open_item = require_layer_item(*layer_list, QStringLiteral("Open Folder"));
  auto* closed_item = require_layer_item(*layer_list, QStringLiteral("Closed Folder"));
  CHECK(open_item->data(Qt::UserRole + 3).toBool());
  CHECK(!closed_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Open Child")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Closed Child")) == nullptr);

  auto* closed_widget = layer_list->itemWidget(closed_item);
  CHECK(closed_widget != nullptr);
  auto* closed_disclosure = closed_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(closed_disclosure != nullptr);
  CHECK(closed_disclosure->arrowType() == Qt::RightArrow);
  save_widget_artifact("ui_layer_folder_saved_state", window);
}

// Issue 179: opening or closing a folder is panel state, so like Photoshop it does not mark
// the document modified, but every layered save writes the folders as the panel shows them
// (PSD section divider 1 open / 2 closed, the Aseprite "collapsed" flag), a plain Save of
// the unmodified document included.
void ui_layer_folder_toggle_saves_without_marking_modified() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto psd_path = temp.filePath(QStringLiteral("folders.psd"));
  {
    patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background",
                             solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
    patchy::Layer closed_group(document.allocate_layer_id(), "Closed Folder", patchy::LayerKind::Group);
    patchy::set_layer_group_expanded(closed_group, false);
    closed_group.add_child(patchy::Layer(document.allocate_layer_id(), "Closed Child",
                                         solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
    document.add_layer(std::move(closed_group));
    patchy::Layer open_group(document.allocate_layer_id(), "Open Folder", patchy::LayerKind::Group);
    open_group.add_child(patchy::Layer(document.allocate_layer_id(), "Open Child",
                                       solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
    document.add_layer(std::move(open_group));
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(psd_path));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  QApplication::processEvents();
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  // Rows are rebuilt on every toggle, so refetch the disclosure button per click.
  auto click_disclosure = [layer_list](const QString& name) {
    auto* row_widget = layer_list->itemWidget(require_layer_item(*layer_list, name));
    CHECK(row_widget != nullptr);
    auto* disclosure = row_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
    CHECK(disclosure != nullptr);
    disclosure->click();
    QApplication::processEvents();
    QApplication::processEvents();
  };
  CHECK(!require_layer_item(*layer_list, QStringLiteral("Closed Folder"))->data(Qt::UserRole + 3).toBool());
  CHECK(require_layer_item(*layer_list, QStringLiteral("Open Folder"))->data(Qt::UserRole + 3).toBool());
  click_disclosure(QStringLiteral("Closed Folder"));
  click_disclosure(QStringLiteral("Open Folder"));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Closed Folder"))->data(Qt::UserRole + 3).toBool());
  CHECK(!require_layer_item(*layer_list, QStringLiteral("Open Folder"))->data(Qt::UserRole + 3).toBool());
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  const auto group_expanded = [](const patchy::Document& document, const std::string& name) {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        CHECK(layer.kind() == patchy::LayerKind::Group);
        return patchy::layer_group_expanded(layer);
      }
    }
    CHECK(false);
    return false;
  };
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  const auto saved = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  CHECK(group_expanded(saved, "Closed Folder"));
  CHECK(!group_expanded(saved, "Open Folder"));

  const auto aseprite_path = temp.filePath(QStringLiteral("folders.aseprite"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, aseprite_path));
  const auto aseprite = patchy::aseprite::DocumentIo::read_file(patchy::ui::to_filesystem_path(aseprite_path));
  CHECK(group_expanded(aseprite, "Closed Folder"));
  CHECK(!group_expanded(aseprite, "Open Folder"));

  // A reopened file shows the folders as they were saved.
  patchy::ui::MainWindow reopened;
  show_window(reopened);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened, psd_path);
  QApplication::processEvents();
  auto* reopened_list = reopened.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(reopened_list != nullptr);
  CHECK(require_layer_item(*reopened_list, QStringLiteral("Closed Folder"))->data(Qt::UserRole + 3).toBool());
  CHECK(require_layer_item(*reopened_list, QStringLiteral("Closed Child")) != nullptr);
  CHECK(!require_layer_item(*reopened_list, QStringLiteral("Open Folder"))->data(Qt::UserRole + 3).toBool());
}

// An empty folder's arrow works like any other (GitHub issue 229): a PSD's closed empty
// folder opens, an open one closes, Alt and Ctrl+Alt clicks keep their scope, and the
// state saves to PSD.
void ui_layer_empty_folder_disclosure_toggles_and_saves() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto psd_path = temp.filePath(QStringLiteral("empty-folders.psd"));
  {
    patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background",
                             solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
    patchy::Layer closed_group(document.allocate_layer_id(), "Closed Empty", patchy::LayerKind::Group);
    patchy::set_layer_group_expanded(closed_group, false);
    document.add_layer(std::move(closed_group));
    document.add_layer(patchy::Layer(document.allocate_layer_id(), "Open Empty", patchy::LayerKind::Group));
    patchy::psd::DocumentIo::write_layered_rgb8_file(document, patchy::ui::to_filesystem_path(psd_path));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, psd_path);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  // Rows are rebuilt on every toggle, so refetch the disclosure button per click.
  const auto disclosure_of = [layer_list](const QString& name) {
    auto* row_widget = layer_list->itemWidget(require_layer_item(*layer_list, name));
    CHECK(row_widget != nullptr);
    auto* disclosure = row_widget == nullptr
                           ? nullptr
                           : row_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
    CHECK(disclosure != nullptr);
    return disclosure;
  };
  const auto click_disclosure = [&disclosure_of](const QString& name, Qt::KeyboardModifiers modifiers) {
    auto* disclosure = disclosure_of(name);
    if (disclosure == nullptr) {
      return;
    }
    CHECK(disclosure->isEnabled());
    const auto center = disclosure->rect().center();
    send_mouse(*disclosure, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*disclosure, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
    QApplication::processEvents();
  };
  const auto collapsed = [layer_list](const QString& name) {
    return !require_layer_item(*layer_list, name)->data(patchy::ui::kLayerGroupExpandedRole).toBool();
  };

  CHECK(collapsed(QStringLiteral("Closed Empty")));
  CHECK(!collapsed(QStringLiteral("Open Empty")));
  for (const auto& name : {QStringLiteral("Closed Empty"), QStringLiteral("Open Empty")}) {
    auto* disclosure = disclosure_of(name);
    CHECK(disclosure != nullptr && disclosure->isEnabled());
  }
  CHECK(disclosure_of(QStringLiteral("Closed Empty"))->toolTip().startsWith(QStringLiteral("Expand folder")));
  CHECK(disclosure_of(QStringLiteral("Open Empty"))->toolTip().startsWith(QStringLiteral("Collapse folder")));

  click_disclosure(QStringLiteral("Closed Empty"), Qt::NoModifier);
  save_widget_artifact("issue_229_empty_folder_arrows", *layer_list);
  CHECK(!collapsed(QStringLiteral("Closed Empty")));
  CHECK(disclosure_of(QStringLiteral("Closed Empty"))->arrowType() == Qt::DownArrow);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Folder expanded"));
  click_disclosure(QStringLiteral("Open Empty"), Qt::AltModifier);
  CHECK(collapsed(QStringLiteral("Open Empty")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Folder and nested folders collapsed"));
  // Ctrl+Alt sets every folder to the clicked empty folder's new state.
  click_disclosure(QStringLiteral("Open Empty"), Qt::ControlModifier | Qt::AltModifier);
  CHECK(!collapsed(QStringLiteral("Open Empty")));
  CHECK(!collapsed(QStringLiteral("Closed Empty")));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("All folders expanded"));
  click_disclosure(QStringLiteral("Closed Empty"), Qt::ControlModifier | Qt::AltModifier);
  CHECK(collapsed(QStringLiteral("Open Empty")));
  CHECK(collapsed(QStringLiteral("Closed Empty")));
  click_disclosure(QStringLiteral("Closed Empty"), Qt::NoModifier);
  CHECK(!collapsed(QStringLiteral("Closed Empty")));
  CHECK(collapsed(QStringLiteral("Open Empty")));

  const auto group_expanded = [](const patchy::Document& document, const std::string& name) {
    for (const auto& layer : document.layers()) {
      if (layer.name() == name) {
        CHECK(layer.kind() == patchy::LayerKind::Group);
        CHECK(layer.children().empty());
        return patchy::layer_group_expanded(layer);
      }
    }
    CHECK(false);
    return false;
  };
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  const auto saved = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  CHECK(group_expanded(saved, "Closed Empty"));
  CHECK(!group_expanded(saved, "Open Empty"));

  patchy::ui::MainWindow reopened;
  show_window(reopened);
  patchy::ui::MainWindowTestAccess::open_document_path(reopened, psd_path);
  QApplication::processEvents();
  auto* reopened_list = reopened.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(reopened_list != nullptr);
  if (reopened_list != nullptr) {
    // Saved the other way round from how the file came in.
    CHECK(require_layer_item(*reopened_list, QStringLiteral("Closed Empty"))
              ->data(patchy::ui::kLayerGroupExpandedRole)
              .toBool());
    CHECK(!require_layer_item(*reopened_list, QStringLiteral("Open Empty"))
               ->data(patchy::ui::kLayerGroupExpandedRole)
               .toBool());
  }
}

void ui_layer_folder_alt_click_toggles_nested_folders() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer outer(document.allocate_layer_id(), "Outer Folder", patchy::LayerKind::Group);
  patchy::Layer inner(document.allocate_layer_id(), "Inner Folder", patchy::LayerKind::Group);
  inner.add_child(patchy::Layer(document.allocate_layer_id(), "Inner Child",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  outer.add_child(std::move(inner));
  outer.add_child(patchy::Layer(document.allocate_layer_id(), "Outer Leaf",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(outer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Alt Click Folder Branch"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };
  // Rows are rebuilt on every toggle, so refetch the disclosure button per click.
  auto click_disclosure = [layer_list](const QString& name, Qt::KeyboardModifiers modifiers) {
    auto* item = require_layer_item(*layer_list, name);
    auto* row_widget = layer_list->itemWidget(item);
    CHECK(row_widget != nullptr);
    auto* disclosure = row_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
    CHECK(disclosure != nullptr);
    const auto center = disclosure->rect().center();
    send_mouse(*disclosure, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*disclosure, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
    QApplication::processEvents();
  };

  CHECK(layer_list->count() == 5);

  // Alt+click collapses the folder and every folder nested inside it.
  click_disclosure(QStringLiteral("Outer Folder"), Qt::AltModifier);
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(QStringLiteral("Inner Folder")) == nullptr);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Folder and nested folders collapsed"));

  // A plain click expands only the clicked folder; the nested one stays collapsed.
  click_disclosure(QStringLiteral("Outer Folder"), Qt::NoModifier);
  CHECK(layer_list->count() == 4);
  auto* inner_item = require_layer_item(*layer_list, QStringLiteral("Inner Folder"));
  CHECK(!inner_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Inner Child")) == nullptr);
  CHECK(find_layer_item(QStringLiteral("Outer Leaf")) != nullptr);

  // Alt+click on the collapsed branch expands the folder and everything nested.
  click_disclosure(QStringLiteral("Outer Folder"), Qt::NoModifier);
  CHECK(layer_list->count() == 2);
  click_disclosure(QStringLiteral("Outer Folder"), Qt::AltModifier);
  CHECK(layer_list->count() == 5);
  inner_item = require_layer_item(*layer_list, QStringLiteral("Inner Folder"));
  CHECK(inner_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Inner Child")) != nullptr);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Folder and nested folders expanded"));
  save_widget_artifact("ui_layer_folder_alt_click_nested", window);
}

void ui_layer_folder_ctrl_alt_click_toggles_all_folders() {
  patchy::Document document(32, 32, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer first(document.allocate_layer_id(), "First Folder", patchy::LayerKind::Group);
  patchy::Layer inner(document.allocate_layer_id(), "Inner Folder", patchy::LayerKind::Group);
  inner.add_child(patchy::Layer(document.allocate_layer_id(), "Inner Child",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  first.add_child(std::move(inner));
  first.add_child(patchy::Layer(document.allocate_layer_id(), "First Leaf",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(first));
  patchy::Layer second(document.allocate_layer_id(), "Second Folder", patchy::LayerKind::Group);
  second.add_child(patchy::Layer(document.allocate_layer_id(), "Second Leaf",
                                 solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 180, 80))));
  document.add_layer(std::move(second));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Ctrl Alt Click All Folders"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };
  // Rows are rebuilt on every toggle, so refetch the disclosure button per click.
  auto click_disclosure = [layer_list](const QString& name, Qt::KeyboardModifiers modifiers) {
    auto* item = require_layer_item(*layer_list, name);
    auto* row_widget = layer_list->itemWidget(item);
    CHECK(row_widget != nullptr);
    auto* disclosure = row_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
    CHECK(disclosure != nullptr);
    const auto center = disclosure->rect().center();
    send_mouse(*disclosure, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*disclosure, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
    QApplication::processEvents();
  };

  CHECK(layer_list->count() == 7);

  // Ctrl+Alt+click collapses every folder in the document, the sibling
  // folder included.
  click_disclosure(QStringLiteral("First Folder"), Qt::ControlModifier | Qt::AltModifier);
  CHECK(layer_list->count() == 3);
  auto* second_item = require_layer_item(*layer_list, QStringLiteral("Second Folder"));
  CHECK(!second_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Inner Folder")) == nullptr);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("All folders collapsed"));

  // Ctrl+Alt+click on the collapsed folder expands everything again.
  click_disclosure(QStringLiteral("First Folder"), Qt::ControlModifier | Qt::AltModifier);
  CHECK(layer_list->count() == 7);
  auto* inner_item = require_layer_item(*layer_list, QStringLiteral("Inner Folder"));
  CHECK(inner_item->data(Qt::UserRole + 3).toBool());
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("All folders expanded"));

  // Mixed state: the clicked folder's toggled state wins. First Folder is
  // expanded, so Ctrl+Alt still collapses all even though Second Folder
  // already is.
  click_disclosure(QStringLiteral("Second Folder"), Qt::NoModifier);
  CHECK(layer_list->count() == 6);
  click_disclosure(QStringLiteral("First Folder"), Qt::ControlModifier | Qt::AltModifier);
  CHECK(layer_list->count() == 3);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("All folders collapsed"));
  save_widget_artifact("ui_layer_folder_ctrl_alt_click_all", window);
}

void ui_layer_thumbnail_double_click_zooms_canvas_to_layer() {
  patchy::Document document(800, 600, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(800, 600, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer badge(document.allocate_layer_id(), "Badge",
                      solid_pixels(40, 30, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  badge.set_bounds(patchy::Rect{600, 100, 40, 30});
  document.add_layer(std::move(badge));
  patchy::Layer folder(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  patchy::Layer first_child(document.allocate_layer_id(), "First",
                            solid_pixels(40, 30, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  first_child.set_bounds(patchy::Rect{100, 200, 40, 30});
  folder.add_child(std::move(first_child));
  patchy::Layer second_child(document.allocate_layer_id(), "Second",
                             solid_pixels(40, 30, patchy::PixelFormat::rgba8(), QColor(40, 180, 80)));
  second_child.set_bounds(patchy::Rect{300, 400, 40, 30});
  folder.add_child(std::move(second_child));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Thumbnail Zoom"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);

  auto double_click_thumbnail = [layer_list](const QString& name) {
    // The press can rebuild the row, so refetch the label per event, the
    // click_layer_row_thumbnail pattern.
    click_layer_row_thumbnail(*layer_list, name, QStringLiteral("layerContentThumbnail"));
    auto* item = require_layer_item(*layer_list, name);
    auto* row = layer_list->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    send_double_click(*thumbnail, thumbnail->rect().center());
    QApplication::processEvents();
  };

  const auto zoom_before = canvas->zoom();
  double_click_thumbnail(QStringLiteral("Badge"));
  CHECK(canvas->zoom() > zoom_before);
  // zoom_to_document_rect centers the rect: the layer's center lands at the
  // canvas widget's center.
  const auto badge_center = canvas->widget_position_for_document_point(QPoint(620, 115));
  CHECK(std::abs(badge_center.x() - canvas->width() / 2) <= 2);
  CHECK(std::abs(badge_center.y() - canvas->height() / 2) <= 2);

  // A folder zooms to its children's union (center of (100,200)-(340,430)).
  double_click_thumbnail(QStringLiteral("Assets"));
  const auto union_center = canvas->widget_position_for_document_point(QPoint(220, 315));
  CHECK(std::abs(union_center.x() - canvas->width() / 2) <= 2);
  CHECK(std::abs(union_center.y() - canvas->height() / 2) <= 2);
  save_widget_artifact("ui_layer_thumbnail_double_click_zoom", window);
}

namespace {

QLabel* require_layer_row_label(QListWidget& layer_list, const QString& layer_name, const QString& label_name) {
  auto* item = require_layer_item(layer_list, layer_name);
  auto* row = layer_list.itemWidget(item);
  CHECK(row != nullptr);
  auto* label = row->findChild<QLabel*>(label_name);
  CHECK(label != nullptr);
  return label;
}

QLineEdit* find_inline_rename_edit(QListWidget& layer_list) {
  return layer_list.findChild<QLineEdit*>(QStringLiteral("layerRowNameEdit"));
}

// The name label is stretched across the row; only its text extent renames.
QPoint name_text_center(const QLabel& name) {
  return QPoint(std::max(2, name.fontMetrics().horizontalAdvance(name.text()) / 2), name.height() / 2);
}

}  // namespace

// Photoshop's split: a double-click on the name edits it in place (Return
// commits as one undo step, Escape cancels, focus loss commits), while a
// double-click on the rest of the row still opens the layer's editor dialog.
void ui_layer_row_name_double_click_renames_inline() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  document.add_pixel_layer("Sketch", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  const auto sketch_id = *document.active_layer_id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Inline Rename"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_name = [&doc, sketch_id] {
    return QString::fromStdString(std::as_const(doc).find_layer(sketch_id)->name());
  };
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Return commits.
  auto* name = require_layer_row_label(*layer_list, QStringLiteral("Sketch"), QStringLiteral("layerRowName"));
  const auto name_height = name->height();
  const auto row_height = layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Sketch"))).height();
  send_double_click(*name, name_text_center(*name));
  auto* edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  CHECK(edit->text() == QStringLiteral("Sketch"));
  CHECK(edit->selectedText() == QStringLiteral("Sketch"));
  // The editor takes the label's slot: same height, so the row does not move.
  CHECK(edit->height() == std::max(name_height, edit->fontMetrics().height() + 2));
  CHECK(layer_list->visualItemRect(require_layer_item(*layer_list, QStringLiteral("Sketch"))).height() == row_height);
  CHECK(!name->isVisible());
  save_widget_artifact("ui_layer_row_name_inline_rename_editor", window);
  edit->setText(QStringLiteral("  Line art  "));
  send_key(*edit, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  CHECK(layer_name() == QStringLiteral("Line art"));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Line art")) != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);

  // Escape cancels without an undo entry.
  name = require_layer_row_label(*layer_list, QStringLiteral("Line art"), QStringLiteral("layerRowName"));
  send_double_click(*name, name_text_center(*name));
  edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  edit->setText(QStringLiteral("Discarded"));
  send_key(*edit, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  CHECK(layer_name() == QStringLiteral("Line art"));
  CHECK(name->isVisible());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  // The list keeps its own Escape (deselect all layers) out of the editor's
  // Escape: the row stays selected.
  CHECK(require_layer_item(*layer_list, QStringLiteral("Line art"))->isSelected());

  // Focus loss commits (the click-elsewhere case), keyed by layer id so a
  // click that selects another row still renames the edited one.
  send_double_click(*name, name_text_center(*name));
  edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  edit->setText(QStringLiteral("Inks"));
  // The commit rebuilds the rows between press and release, so the label is
  // refetched per event (the click_layer_row_thumbnail pattern).
  auto* background_name =
      require_layer_row_label(*layer_list, QStringLiteral("Background"), QStringLiteral("layerRowName"));
  send_mouse(*background_name, QEvent::MouseButtonPress, background_name->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::NoModifier);
  layer_list->setFocus();
  QApplication::processEvents();
  background_name =
      require_layer_row_label(*layer_list, QStringLiteral("Background"), QStringLiteral("layerRowName"));
  send_mouse(*background_name, QEvent::MouseButtonRelease, background_name->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  CHECK(layer_name() == QStringLiteral("Inks"));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background"))->isSelected());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);

  // An unchanged commit records nothing.
  name = require_layer_row_label(*layer_list, QStringLiteral("Inks"), QStringLiteral("layerRowName"));
  send_double_click(*name, name_text_center(*name));
  edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  send_key(*edit, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 2);

  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(layer_name() == QStringLiteral("Sketch"));

  // The rest of the row keeps the editor dialog: the details line, and the
  // stretched name label's empty space right of the text.
  const auto expect_style_dialog = [&](QWidget& target, QPoint position) {
    bool saw_style_dialog = false;
    QTimer::singleShot(0, [&] {
      if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")); dialog != nullptr) {
        saw_style_dialog = true;
        dialog->reject();
      }
    });
    send_double_click(target, position);
    QApplication::processEvents();
    CHECK(saw_style_dialog);
    CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  };
  auto* details = require_layer_row_label(*layer_list, QStringLiteral("Sketch"), QStringLiteral("layerRowDetails"));
  expect_style_dialog(*details, details->rect().center());
  name = require_layer_row_label(*layer_list, QStringLiteral("Sketch"), QStringLiteral("layerRowName"));
  CHECK(name->width() > name->fontMetrics().horizontalAdvance(name->text()) + 40);
  expect_style_dialog(*name, QPoint(name->width() - 4, name->height() / 2));
}

// A shape row's double-click opens Layer Style like every other row (layer
// styles apply to shapes too); Shape Appearance stays on the vector badge.
void ui_layer_shape_row_double_click_opens_layer_style() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  QApplication::processEvents();
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto shape_id = doc.active_layer_id();
  CHECK(shape_id.has_value());
  CHECK(patchy::layer_is_vector_shape(*std::as_const(doc).find_layer(*shape_id)));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->currentItem();
  CHECK(item != nullptr);
  auto* details = layer_list->itemWidget(item)->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  bool saw_style_dialog = false;
  QTimer::singleShot(0, [&] {
    CHECK(find_top_level_dialog(QStringLiteral("shapeAppearanceDialog")) == nullptr);
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")); dialog != nullptr) {
      saw_style_dialog = true;
      dialog->reject();
    }
  });
  send_double_click(*details, details->rect().center());
  QApplication::processEvents();
  CHECK(saw_style_dialog);
}

// GitHub issue 134: the row badges (fx, smart object, vector) are centered on
// the row, not seated on its second text line.
void ui_layer_row_badges_center_on_the_row() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->currentItem();
  CHECK(item != nullptr);
  auto* row = layer_list->itemWidget(item);
  CHECK(row != nullptr);
  auto* badge = row->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton"));
  auto* main_row = row->findChild<QWidget*>(QStringLiteral("layerMainRow"));
  CHECK(badge != nullptr && main_row != nullptr);
  if (badge == nullptr || main_row == nullptr) {
    return;
  }
  const auto badge_center = badge->mapTo(main_row, badge->rect().center());
  const int row_center = main_row->rect().center().y();
  CHECK(std::abs(badge_center.y() - row_center) <= 2);
}

// The vector badge owns its clicks: a real press is not a row drag start, and
// a double-click on it never falls through to the row's Layer Style editor
// (regression: double-clicking the badge opened Layer Style, September 2026).
void ui_layer_vector_badge_double_click_opens_shape_appearance() {
  VectorSettingsGuard settings_guard;
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(400, 300)));
  QApplication::processEvents();
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto shape_id = doc.active_layer_id();
  CHECK(shape_id.has_value());
  CHECK(patchy::layer_is_vector_shape(*std::as_const(doc).find_layer(*shape_id)));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->currentItem();
  CHECK(item != nullptr);
  auto* badge = layer_list->itemWidget(item)->findChild<QToolButton*>(QStringLiteral("layerVectorBadgeButton"));
  CHECK(badge != nullptr);

  // The badge opens Shape Appearance one timer tick after its release, so the
  // closer polls until the dialog exists (it fires inside the dialog's loop).
  bool saw_style_dialog = false;
  bool saw_appearance_dialog = false;
  int attempts = 0;
  std::function<void()> close_appearance_dialog = [&] {
    if (find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")) != nullptr) {
      saw_style_dialog = true;
    }
    if (auto* dialog = find_top_level_dialog(QStringLiteral("shapeAppearanceDialog")); dialog != nullptr) {
      saw_appearance_dialog = true;
      dialog->reject();
      return;
    }
    if (++attempts < 20) {
      QTimer::singleShot(0, close_appearance_dialog);
    }
  };

  // A real double-click arrives as press, release, double-click, release.
  const auto center = badge->rect().center();
  send_mouse(*badge, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  CHECK(badge->isDown());
  QTimer::singleShot(0, close_appearance_dialog);
  send_mouse(*badge, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(saw_appearance_dialog);
  CHECK(!saw_style_dialog);

  saw_appearance_dialog = false;
  attempts = 0;
  send_double_click(*badge, center);
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  QTimer::singleShot(0, close_appearance_dialog);
  send_mouse(*badge, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::processEvents();
  CHECK(saw_appearance_dialog);
  CHECK(!saw_style_dialog);
  CHECK(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")) == nullptr);
}

// Layer ids restart per document: a commit whose focus loss went to another
// document's tab is dropped rather than renaming that document's same-id layer.
void ui_layer_inline_rename_drops_commit_after_document_switch() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  const auto background_id = *document.active_layer_id();

  patchy::ui::MainWindow window;
  show_window(window);  // Untitled-1: its Background shares layer id 1.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto& untitled = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(std::as_const(untitled).find_layer(background_id) != nullptr);
  const auto untitled_name = std::as_const(untitled).find_layer(background_id)->name();
  window.add_document_session(std::move(document), QStringLiteral("Second"));
  QApplication::processEvents();
  CHECK(tabs->currentIndex() == 1);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* name = require_layer_row_label(*layer_list, QStringLiteral("Background"), QStringLiteral("layerRowName"));
  send_double_click(*name, name_text_center(*name));
  auto* edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  edit->setText(QStringLiteral("Wrong document"));
  // Focus loss queues the commit; the tab switch lands before it runs.
  layer_list->setFocus();
  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  auto& active = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(&active == &untitled);
  CHECK(std::as_const(active).find_layer(background_id)->name() == untitled_name);
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(background_id)->name() ==
        "Background");
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background")) != nullptr);
}

// Rename Layer (F2 by default) edits in place when the active layer's row is on
// screen and it is the only selected layer; a multi-selection or a filtered-out
// row falls back to the rename dialog.
void ui_layer_rename_hotkey_enters_inline_edit() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  document.add_pixel_layer("Alpha", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  document.add_pixel_layer("Beta", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(220, 80, 40)));
  const auto beta_id = *document.active_layer_id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Rename Hotkey"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* rename = require_action(window, "layerRenameAction");
  CHECK(rename->shortcut() == QKeySequence(Qt::Key_F2));
  CHECK(require_hotkey_action(window, QStringLiteral("layer.rename")) == rename);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  rename->trigger();
  QApplication::processEvents();
  auto* edit = find_inline_rename_edit(*layer_list);
  CHECK(edit != nullptr);
  CHECK(edit->text() == QStringLiteral("Beta"));
  // A second F2 while editing keeps the same editor instead of restarting it.
  rename->trigger();
  QApplication::processEvents();
  CHECK(find_inline_rename_edit(*layer_list) == edit);
  edit->setText(QStringLiteral("Gamma"));
  send_key(*edit, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(QString::fromStdString(std::as_const(doc).find_layer(beta_id)->name()) == QStringLiteral("Gamma"));

  // Two selected rows: the dialog.
  auto* gamma_item = require_layer_item(*layer_list, QStringLiteral("Gamma"));
  auto* alpha_item = require_layer_item(*layer_list, QStringLiteral("Alpha"));
  layer_list->setCurrentItem(gamma_item);
  gamma_item->setSelected(true);
  alpha_item->setSelected(true);
  QApplication::processEvents();
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyRenameLayerDialog")); dialog != nullptr) {
      saw_dialog = true;
      dialog->reject();
    }
  });
  rename->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);

  // The active row hidden by the name filter: the dialog again.
  layer_list->clearSelection();
  layer_list->setCurrentItem(gamma_item);
  gamma_item->setSelected(true);
  QApplication::processEvents();
  auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(filter != nullptr);
  filter->setText(QStringLiteral("Alpha"));
  QApplication::processEvents();
  CHECK(doc.active_layer_id() == beta_id);
  saw_dialog = false;
  QTimer::singleShot(0, [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyRenameLayerDialog")); dialog != nullptr) {
      saw_dialog = true;
      dialog->reject();
    }
  });
  rename->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(find_inline_rename_edit(*layer_list) == nullptr);
  filter->clear();
  QApplication::processEvents();
}

void ui_move_auto_select_reveals_layers_in_collapsed_folders() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Collapsed Folder", patchy::LayerKind::Group);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  auto child = patchy::Layer(document.allocate_layer_id(), "Hidden Child",
                                solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{12, 12, 12, 12});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  document.set_active_layer(child_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Reveal Collapsed Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"))->data(Qt::UserRole + 3).toBool() == false);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hidden Child")) == nullptr);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"));
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Hidden Child"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(child_item->isSelected());
  CHECK(layer_list->currentItem() == child_item);
  CHECK(layer_list->visualItemRect(child_item).intersects(layer_list->viewport()->rect()));
  save_widget_artifact("ui_auto_select_reveals_collapsed_folder", window);
}

void ui_folder_visibility_preserves_layer_panel_scroll() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Scrollable Folder", patchy::LayerKind::Group);
  for (int index = 0; index < 42; ++index) {
    auto child = patchy::Layer(
        document.allocate_layer_id(), "Child " + std::to_string(index + 1),
        solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40 + index * 3 % 180, 80, 220, 255)));
    child.set_bounds(patchy::Rect{index % 16, index % 16, 8, 8});
    group.add_child(std::move(child));
  }
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Panel Scroll"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 2);
  QApplication::processEvents();
  const auto scroll_before = scroll->value();
  CHECK(scroll_before > 0);

  QListWidgetItem* folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);

  folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);
}

void ui_layer_row_selected_highlight_paints() {
  // Pins the painted row visuals: the selected-layer highlight silently never
  // rendered for a long time (the plain-QWidget containers inside each row
  // matched the theme's global QWidget rule and painted opaque #262626 over
  // the row background), leaving selection marked only by a 1px item border
  // leak. The rows now paint through app-stylesheet rules keyed on the
  // layerRowSelected/layerRowGroup dynamic properties, with the inner
  // containers forced transparent.
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Red Layer",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(200, 40, 40)));
  document.add_pixel_layer("Blue Layer",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(40, 40, 200)));
  patchy::Layer group(document.allocate_layer_id(), "Highlight Folder", patchy::LayerKind::Group);
  auto child = patchy::Layer(document.allocate_layer_id(), "Folder Child",
                             solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(40, 200, 40, 255)));
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Row Highlight"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* selected_item = require_layer_item(*layer_list, QStringLiteral("Blue Layer"));
  layer_list->setCurrentItem(selected_item, QItemSelectionModel::ClearAndSelect);
  QApplication::processEvents();
  CHECK(selected_item->isSelected());

  const auto row_background = [layer_list](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    auto* row_widget = layer_list->itemWidget(item);
    CHECK(row_widget != nullptr);
    const auto image = row_widget->grab().toImage();
    // Top-right corner of the row: inside the layout's top margin, clear of
    // the visibility button, thumbnails, labels, and the bottom divider.
    return image.pixelColor(image.width() - 6, 2);
  };
  save_widget_artifact("ui_layer_row_selected_highlight", *layer_list);
  CHECK(row_background(QStringLiteral("Blue Layer")) == QColor(0x2d, 0x4c, 0x6d));
  CHECK(row_background(QStringLiteral("Red Layer")) == QColor(0x24, 0x26, 0x28));
  CHECK(row_background(QStringLiteral("Highlight Folder")) == QColor(0x29, 0x2d, 0x31));

  // The selected row also gets the blue bottom edge.
  auto* selected_row = layer_list->itemWidget(selected_item);
  CHECK(selected_row != nullptr);
  const auto selected_image = selected_row->grab().toImage();
  CHECK(selected_image.pixelColor(selected_image.width() - 6, selected_image.height() - 1) ==
        QColor(0x4f, 0x91, 0xca));

  save_widget_artifact("ui_layer_row_selected_highlight", *layer_list);
}

void ui_layer_fill_opacity_control_updates_active_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* fill = window.findChild<QSpinBox*>(QStringLiteral("layerFillOpacitySpin"));
  CHECK(fill != nullptr);
  CHECK(fill->isEnabled());
  fill->setValue(37);
  QMetaObject::invokeMethod(fill, "editingFinished", Qt::DirectConnection);
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.active_layer_id().has_value());
  const auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  CHECK(std::abs(layer->fill_opacity() - 0.37F) < 0.001F);
}

// GitHub issue 96: until the user picks a width, the right column opens at
// the narrowest width its panels fit without scrolling, so a long translated
// row leaves no gap beside the footer buttons. Polish wraps the blend row.
void ui_right_dock_opens_at_content_width_without_gap() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto restore_language = qScopeGuard([&manager] { manager.set_language(QStringLiteral("en"), false); });
  CHECK(manager.set_language(QStringLiteral("pl"), false));
  patchy::ui::MainWindow window;
  show_window(window);
  QApplication::processEvents();
  QApplication::processEvents();
  QWidget* dock = patchy::ui::MainWindowTestAccess::panel_column(window);
  auto* palette_dock = patchy::ui::MainWindowTestAccess::panel_section(window, QStringLiteral("paletteDock"));
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
  auto* layers_page = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  auto* overflow =
      layers_page != nullptr ? layers_page->findChild<QScrollArea*>(QStringLiteral("rightDockOverflowScroll")) : nullptr;
  CHECK(dock != nullptr && palette_dock != nullptr && combo != nullptr && opacity != nullptr);
  CHECK(filter != nullptr && delete_button != nullptr && overflow != nullptr);
  CHECK(dock->width() == patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window));
  CHECK(palette_dock->width() == dock->width());
  CHECK(!overflow->horizontalScrollBar()->isVisible());
  CHECK(opacity->y() > combo->geometry().bottom());
  // The footer buttons reach the panel's right inset (the name filter's
  // right edge), with no dead strip after them.
  CHECK(std::abs(delete_button->geometry().right() - filter->geometry().right()) <= 8);
  save_widget_artifact("ui_right_dock_opens_at_content_width_pl", *dock);
}

// GitHub issue 96: Opacity and Fill wrap under the blend combo when the stack
// is narrower than the one-line row, so long translated prefixes (Russian
// "Непрозрачность:", Polish "Wypełnienie:") no longer hold the right dock
// wider than the rest of the panel needs.
void ui_layer_blend_row_wraps_in_narrow_panel() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto restore_language = qScopeGuard([&manager] { manager.set_language(QStringLiteral("en"), false); });
  for (const auto& code : {QStringLiteral("ru"), QStringLiteral("pl"), QStringLiteral("en")}) {
    CHECK(manager.set_language(code, false));
    patchy::ui::MainWindow window;
    show_window(window);
    QApplication::processEvents();
    QWidget* dock = patchy::ui::MainWindowTestAccess::panel_column(window);
    auto* combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
    auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
    auto* fill = window.findChild<QSpinBox*>(QStringLiteral("layerFillOpacitySpin"));
    auto* filter = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
    CHECK(dock != nullptr && combo != nullptr && opacity != nullptr && fill != nullptr && filter != nullptr);
    const auto one_line_width = combo->width() + opacity->width() + fill->width() + 12;

    const auto content_minimum = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, content_minimum);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(dock->width() == content_minimum);
    // The name filter stretches to the panel's right inset, the edge every row
    // must stay inside.
    const auto content_right = filter->geometry().right();
    const auto content_width = content_right + 1 - combo->x();
    const auto wrapped = one_line_width > content_width;
    if (code != QStringLiteral("en")) {
      CHECK(wrapped);
    }
    if (wrapped) {
      CHECK(opacity->y() > combo->geometry().bottom());
      CHECK(opacity->x() == combo->x());
      CHECK(fill->y() == opacity->y());
    }
    CHECK(combo->geometry().right() <= content_right);
    CHECK(fill->geometry().right() <= content_right);
    CHECK(filter->y() > fill->geometry().bottom());
    save_widget_artifact("ui_layer_blend_row_wraps_" + code.toStdString(), *dock);

    // Wide enough for the whole row: back on one line.
    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(
        window, content_minimum + std::max(0, one_line_width - content_width) + 40);
    QApplication::processEvents();
    QApplication::processEvents();
    const auto wide_right = filter->geometry().right();
    CHECK(std::abs(opacity->geometry().center().y() - combo->geometry().center().y()) <= 1);
    CHECK(fill->geometry().center().y() == opacity->geometry().center().y());
    CHECK(opacity->x() > combo->geometry().right());
    CHECK(fill->geometry().right() <= wide_right);
    CHECK(filter->y() > combo->geometry().bottom());
  }
}

// GitHub issue 199: below the width the column opens at, the Layers panel
// rows wrap instead of clipping: the footer buttons narrow and then wrap four
// over four, Fill drops under Opacity, and the name filter takes a line of its
// own. The compact width (every row wrapped) sits far under the opening width,
// and there every control is whole, apart, and inside the panel, with no
// sideways scroll. Narrower than that the panel scrolls sideways.
void ui_layer_panel_compact_width_keeps_controls_whole() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto restore_language = qScopeGuard([&manager] { manager.set_language(QStringLiteral("en"), false); });
  for (const auto& code : {QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("pl")}) {
    CHECK(manager.set_language(code, false));
    patchy::ui::MainWindow window;
    show_window(window);
    patchy::Document document(320, 240, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(320, 240, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
    document.add_pixel_layer("Sky", solid_pixels(320, 240, patchy::PixelFormat::rgba8(), QColor(90, 150, 220)));
    document.add_pixel_layer("Hills", solid_pixels(320, 240, patchy::PixelFormat::rgba8(), QColor(70, 140, 60)));
    window.add_document_session(std::move(document), QStringLiteral("Compact Layers"));
    QApplication::processEvents();
    QWidget* column = patchy::ui::MainWindowTestAccess::panel_column(window);
    auto* layers_page = window.findChild<QWidget*>(QStringLiteral("layersDock"));
    auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel"));
    auto* overflow =
        layers_page != nullptr ? layers_page->findChild<QScrollArea*>(QStringLiteral("rightDockOverflowScroll")) : nullptr;
    auto* new_button = window.findChild<QPushButton*>(QStringLiteral("layerNewButton"));
    auto* delete_button = window.findChild<QPushButton*>(QStringLiteral("layerDeleteButton"));
    CHECK(column != nullptr && panel != nullptr && overflow != nullptr);
    CHECK(new_button != nullptr && delete_button != nullptr);

    const auto opening = patchy::ui::MainWindowTestAccess::right_dock_content_minimum_width(window);
    const auto compact = patchy::ui::MainWindowTestAccess::right_dock_compact_width(window);
    std::cout << "[INFO] " << code.toStdString() << ": Layers column opens at " << opening << " px, compact width "
              << compact << " px\n";
    CHECK(compact <= opening - 90);
    if (code == QStringLiteral("en")) {
      CHECK(compact <= 270);
    }

    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, compact);
    // Three passes: each wrapped row re-requests the layout once after the pass
    // that wrapped it.
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::processEvents();
    }
    CHECK(column->width() == compact);
    CHECK(!overflow->horizontalScrollBar()->isVisible());
    CHECK(panel->width() == overflow->viewport()->width());
    // The footer wrapped rather than squeezing its buttons under the compact width.
    CHECK(delete_button->y() > new_button->geometry().bottom());

    struct Control {
      const char* name;
      bool natural_width;  // laid out at its own width hint, never narrower
    };
    const std::array<Control, 17> controls{{
        {"layerBlendModeCombo", true},     {"layerOpacitySpin", true},         {"layerFillOpacitySpin", true},
        {"layerLockTransparentButton", true}, {"layerLockPixelsButton", true}, {"layerLockPositionButton", true},
        {"layerLockAllButton", true},      {"layerNameFilterEdit", false},     {"layerList", false},
        {"layerNewButton", false},         {"layerNewFolderButton", false},    {"layerNewAdjustmentButton", false},
        {"layerStylesButton", false},      {"layerAddMaskButton", false},      {"layerDuplicateButton", false},
        {"layerAnimationButton", false},   {"layerDeleteButton", false},
    }};
    std::vector<QRect> rects;
    for (const auto& control : controls) {
      auto* widget = panel->findChild<QWidget*>(QLatin1String(control.name));
      CHECK(widget != nullptr);
      CHECK(widget->isVisible());
      const QRect rect(widget->mapTo(panel, QPoint()), widget->size());
      if (!panel->rect().contains(rect)) {
        std::cout << "[INFO] " << control.name << " leaves the panel: " << rect.x() << "," << rect.y() << " "
                  << rect.width() << "x" << rect.height() << " in " << panel->width() << "x" << panel->height()
                  << "\n";
      }
      CHECK(panel->rect().contains(rect));
      if (control.natural_width) {
        CHECK(rect.width() >= std::min(widget->sizeHint().width(), widget->maximumWidth()));
      }
      if (auto* button = qobject_cast<QAbstractButton*>(widget); button != nullptr) {
        CHECK(rect.width() >= button->iconSize().width() + 4);
      }
      for (const auto& other : rects) {
        CHECK(!rect.intersects(other));
      }
      rects.push_back(rect);
    }
    auto* filter = panel->findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
    auto* layer_list = panel->findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(filter != nullptr && layer_list != nullptr);
    CHECK(filter->width() >= 90);
    CHECK(layer_list->height() >= 48);
    save_widget_artifact("ui_layer_panel_compact_width_" + code.toStdString(), *column);

    // Narrower than the compact width the panel keeps it and scrolls sideways.
    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, compact - 20);
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::processEvents();
    }
    CHECK(overflow->horizontalScrollBar()->isVisible());

    // Back at the opening width, the footer is one row of full-width buttons.
    patchy::ui::MainWindowTestAccess::set_right_dock_stack_width(window, opening);
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::processEvents();
    }
    CHECK(!overflow->horizontalScrollBar()->isVisible());
    CHECK(delete_button->y() == new_button->y());
    CHECK(delete_button->width() == delete_button->maximumWidth());
    // The name filter is back beside the Lock buttons.
    auto* lock_all = panel->findChild<QWidget*>(QStringLiteral("layerLockAllButton"));
    CHECK(lock_all != nullptr);
    CHECK(std::abs(filter->geometry().center().y() - lock_all->geometry().center().y()) <= 1);
  }
}

patchy::Document make_name_filter_document() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Hero",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  document.add_layer(std::move(group));
  return document;
}

void ui_layer_filter_shows_matching_rows_and_ancestor_folders() {
  auto document = make_name_filter_document();
  const auto background_id = document.layers()[0].id();
  document.set_active_layer(background_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Name Filter"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);
  CHECK(filter_edit->isEnabled());
  CHECK(layer_list->count() == 3);

  filter_edit->setText(QStringLiteral("hero"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Hero")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Assets")) != nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Background")) == nullptr);
  // The filtered-out active layer stays active in the document; the list just
  // shows no selection.
  CHECK(layer_list->selectedItems().isEmpty());
  const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.active_layer_id() == background_id);

  filter_edit->clear();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background")) != nullptr);
  save_widget_artifact("ui_layer_name_filter", window);
}

void ui_layer_filter_reveals_matches_inside_collapsed_folders() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  patchy::Layer group(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  group.add_child(patchy::Layer(document.allocate_layer_id(), "Hero",
                                solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(220, 40, 40))));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Filter Collapsed Folder"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hero")) == nullptr);

  filter_edit->setText(QStringLiteral("hero"));
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Hero")) != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Assets"))
            ->data(patchy::ui::kLayerGroupExpandedRole)
            .toBool());

  filter_edit->clear();
  QApplication::processEvents();
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hero")) == nullptr);
  CHECK(!require_layer_item(*layer_list, QStringLiteral("Assets"))
             ->data(patchy::ui::kLayerGroupExpandedRole)
             .toBool());
}

void ui_layer_filter_reapplies_after_layer_rename() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(make_name_filter_document(), QStringLiteral("Filter Rename"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  filter_edit->setText(QStringLiteral("hero"));
  QApplication::processEvents();
  auto* hero_item = require_layer_item(*layer_list, QStringLiteral("Hero"));
  const auto hero_id = static_cast<patchy::LayerId>(hero_item->data(patchy::ui::kLayerIdRole).toULongLong());
  CHECK(layer_list->count() == 2);

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  doc.find_layer(hero_id)->set_name("Sky");
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  CHECK(layer_list->count() == 0);

  doc.find_layer(hero_id)->set_name("Hero");
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Hero")) != nullptr);
}

void ui_layer_filter_blocks_layer_drag_reorder() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Red Layer",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(220, 40, 40)));
  document.add_pixel_layer("Green Layer",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 180, 80)));
  document.add_pixel_layer("Blue Sky",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 90, 220)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Filter Drag Block"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  filter_edit->setText(QStringLiteral("layer"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Red Layer"));
  const auto red_id = static_cast<patchy::LayerId>(red_item->data(patchy::ui::kLayerIdRole).toULongLong());
  auto* green_item = require_layer_item(*layer_list, QStringLiteral("Green Layer"));
  const auto green_rect = layer_list->visualItemRect(green_item);

  send_layer_drop(*layer_list, QPoint(2, green_rect.top() + 2), {red_id});
  process_events_for(1);
  const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.layers()[0].name() == "Red Layer");
  CHECK(doc.layers()[1].name() == "Green Layer");
  CHECK(doc.layers()[2].name() == "Blue Sky");
  CHECK(layer_list->count() == 2);

  // Control: the same drop reorders once the filter is cleared.
  filter_edit->clear();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  const auto top_rect = layer_list->visualItemRect(layer_list->item(0));
  send_layer_drop(*layer_list, QPoint(2, top_rect.top() + 2), {red_id});
  process_events_for(1);
  CHECK(layer_list->row(require_layer_item(*layer_list, QStringLiteral("Red Layer"))) == 0);
}

void ui_layer_filter_persists_across_document_tabs() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  window.add_document_session(make_name_filter_document(), QStringLiteral("Filter Tab A"));
  QApplication::processEvents();
  auto* hero_canvas = tabs->currentWidget();

  patchy::Document boat_document(48, 48, patchy::PixelFormat::rgb8());
  boat_document.add_pixel_layer("Boat",
                                solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 90, 220)));
  window.add_document_session(std::move(boat_document), QStringLiteral("Filter Tab B"));
  QApplication::processEvents();
  auto* boat_canvas = tabs->currentWidget();

  filter_edit->setText(QStringLiteral("hero"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 0);

  tabs->setCurrentWidget(hero_canvas);
  QApplication::processEvents();
  CHECK(filter_edit->text() == QStringLiteral("hero"));
  CHECK(layer_list->count() == 2);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Hero")) != nullptr);

  tabs->setCurrentWidget(boat_canvas);
  QApplication::processEvents();
  CHECK(filter_edit->text() == QStringLiteral("hero"));
  CHECK(layer_list->count() == 0);
}

void ui_layer_filter_empty_match_keeps_document_state() {
  auto document = make_name_filter_document();
  const auto background_id = document.layers()[0].id();
  document.set_active_layer(background_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Filter Empty Match"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  filter_edit->setText(QStringLiteral("zzz"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 0);
  const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.active_layer_id() == background_id);

  filter_edit->clear();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->currentItem() != nullptr);
  CHECK(static_cast<patchy::LayerId>(
            layer_list->currentItem()->data(patchy::ui::kLayerIdRole).toULongLong()) == background_id);
}

void ui_layer_filter_reveal_clears_filter_for_hidden_target() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  const auto background_id = document.layers()[0].id();
  patchy::Layer group(document.allocate_layer_id(), "Assets", patchy::LayerKind::Group);
  auto boat = patchy::Layer(document.allocate_layer_id(), "Boat",
                            solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  boat.set_bounds(patchy::Rect{12, 12, 12, 12});
  group.add_child(std::move(boat));
  document.add_layer(std::move(group));
  document.set_active_layer(background_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Filter Reveal Target"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  filter_edit->setText(QStringLiteral("background"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Boat")) == nullptr);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();

  CHECK(filter_edit->text().isEmpty());
  auto* boat_item = require_layer_item(*layer_list, QStringLiteral("Boat"));
  CHECK(boat_item->isSelected());
  CHECK(layer_list->currentItem() == boat_item);
}

void ui_layer_filter_drag_attempt_reports_error_without_selection_sweep() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Red Layer",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(220, 40, 40)));
  document.add_pixel_layer("Green Layer",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 180, 80)));
  document.add_pixel_layer("Blue Sky",
                           solid_pixels(48, 48, patchy::PixelFormat::rgb8(), QColor(40, 90, 220)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Filter Drag Feedback"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("layerNameFilterEdit"));
  CHECK(layer_list != nullptr);
  CHECK(filter_edit != nullptr);

  filter_edit->setText(QStringLiteral("layer"));
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);

  auto* green_item = require_layer_item(*layer_list, QStringLiteral("Green Layer"));
  auto* green_name = layer_list->itemWidget(green_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(green_name != nullptr);
  const auto press_point = green_name->rect().center();
  send_mouse(*green_name, QEvent::MouseButtonPress, press_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*green_name, QEvent::MouseMove, press_point + QPoint(0, 60), Qt::NoButton, Qt::LeftButton);
  CHECK(window.statusBar()->currentMessage() ==
        QStringLiteral("Clear the layer name filter to reorder layers"));

  // The button is still down; the leftover moves the platform would deliver to
  // the viewport must not turn into a drag-selection sweep across the rows.
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Red Layer"));
  const auto red_center = layer_list->visualItemRect(red_item).center();
  send_mouse(*layer_list->viewport(), QEvent::MouseMove, red_center, Qt::NoButton, Qt::LeftButton);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonRelease, red_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() <= 1);
  CHECK(!red_item->isSelected());

  const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.layers()[0].name() == "Red Layer");
  CHECK(doc.layers()[1].name() == "Green Layer");
  CHECK(doc.layers()[2].name() == "Blue Sky");
}

// Bottom-to-top: Background, Folder [Child A visible, Child B hidden], Top Layer.
struct EyeTestLayerIds {
  patchy::LayerId background{0};
  patchy::LayerId folder{0};
  patchy::LayerId child_a{0};
  patchy::LayerId child_b{0};
  patchy::LayerId top{0};
};

EyeTestLayerIds build_eye_test_document(patchy::Document& document, bool folder_visible = true,
                                        bool child_b_visible = false) {
  EyeTestLayerIds ids;
  ids.background = document
                       .add_pixel_layer("Background",
                                        solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)))
                       .id();
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  ids.folder = group.id();
  group.set_visible(folder_visible);
  auto child_a = patchy::Layer(document.allocate_layer_id(), "Child A",
                               solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(40, 80, 220)));
  ids.child_a = child_a.id();
  group.add_child(std::move(child_a));
  auto child_b = patchy::Layer(document.allocate_layer_id(), "Child B",
                               solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(220, 80, 40)));
  child_b.set_visible(child_b_visible);
  ids.child_b = child_b.id();
  group.add_child(std::move(child_b));
  document.add_layer(std::move(group));
  ids.top = document
                .add_pixel_layer("Top Layer",
                                 solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(60, 200, 90)))
                .id();
  return ids;
}

QToolButton* layer_row_eye_button(QListWidget& layer_list, const QString& layer_name) {
  auto* item = require_layer_item(layer_list, layer_name);
  auto* row_widget = layer_list.itemWidget(item);
  CHECK(row_widget != nullptr);
  auto* eye = row_widget != nullptr
                  ? row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"))
                  : nullptr;
  CHECK(eye != nullptr);
  return eye;
}

// A real press/release pair on the eye (not QToolButton::click), so the list's
// Alt-isolate / sweep interception runs. The deferred handlers rebuild rows, so
// callers must re-fetch widgets afterwards.
void click_layer_row_eye(QListWidget& layer_list, const QString& layer_name,
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  auto* eye = layer_row_eye_button(layer_list, layer_name);
  if (eye == nullptr) {
    return;
  }
  const auto center = eye->rect().center();
  send_mouse(*eye, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(*eye, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, modifiers);
  QApplication::processEvents();
  QApplication::processEvents();
}

bool document_layer_visible(patchy::Document& document, patchy::LayerId id) {
  const auto* layer = std::as_const(document).find_layer(id);
  CHECK(layer != nullptr);
  return layer != nullptr && layer->visible();
}

void ui_layer_eye_alt_click_isolates_and_restores() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Isolate"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(!document_layer_visible(doc, ids.background));
  CHECK(!document_layer_visible(doc, ids.folder));
  CHECK(!document_layer_visible(doc, ids.child_a));
  CHECK(!document_layer_visible(doc, ids.child_b));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Top Layer"))->checkState() == Qt::Checked);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background"))->checkState() == Qt::Unchecked);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Folder"))->checkState() == Qt::Unchecked);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Hid other layers"));
  save_widget_artifact("ui_layer_eye_alt_click_isolated", window);

  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(document_layer_visible(doc, ids.background));
  CHECK(document_layer_visible(doc, ids.folder));
  CHECK(document_layer_visible(doc, ids.child_a));
  // Child B was hidden before the isolation and must come back hidden.
  CHECK(!document_layer_visible(doc, ids.child_b));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Background"))->checkState() == Qt::Checked);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Restored layer visibility"));
}

void ui_layer_eye_alt_click_folder_isolates_group() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Isolate Folder"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  click_layer_row_eye(*layer_list, QStringLiteral("Folder"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.folder));
  // The isolated folder's contents keep their own flags.
  CHECK(document_layer_visible(doc, ids.child_a));
  CHECK(!document_layer_visible(doc, ids.child_b));
  CHECK(!document_layer_visible(doc, ids.background));
  CHECK(!document_layer_visible(doc, ids.top));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Hid other layers"));

  click_layer_row_eye(*layer_list, QStringLiteral("Folder"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.folder));
  CHECK(document_layer_visible(doc, ids.child_a));
  CHECK(!document_layer_visible(doc, ids.child_b));
  CHECK(document_layer_visible(doc, ids.background));
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Restored layer visibility"));
}

void ui_layer_eye_alt_click_reisolate_keeps_original_snapshot() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Reisolate"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(!document_layer_visible(doc, ids.background));

  // Alt-clicking another eye while isolated switches the isolation target.
  click_layer_row_eye(*layer_list, QStringLiteral("Background"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.background));
  CHECK(!document_layer_visible(doc, ids.top));
  CHECK(!document_layer_visible(doc, ids.folder));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Hid other layers"));

  // The restore still returns to the true pre-isolation state, not to the
  // intermediate only-Top-visible state.
  click_layer_row_eye(*layer_list, QStringLiteral("Background"), Qt::AltModifier);
  CHECK(document_layer_visible(doc, ids.background));
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(document_layer_visible(doc, ids.folder));
  CHECK(document_layer_visible(doc, ids.child_a));
  CHECK(!document_layer_visible(doc, ids.child_b));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Restored layer visibility"));
}

void ui_layer_eye_alt_click_after_external_change_starts_fresh() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Fresh Isolate"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(!document_layer_visible(doc, ids.background));

  // A manual visibility change invalidates the stored isolation snapshot.
  click_layer_row_eye(*layer_list, QStringLiteral("Background"));
  CHECK(document_layer_visible(doc, ids.background));

  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Hid other layers"));
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(!document_layer_visible(doc, ids.background));

  // The restore returns to the state captured at the fresh isolation (Top and
  // Background visible, the folder still hidden from the first isolation).
  click_layer_row_eye(*layer_list, QStringLiteral("Top Layer"), Qt::AltModifier);
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Restored layer visibility"));
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(document_layer_visible(doc, ids.background));
  CHECK(!document_layer_visible(doc, ids.folder));
  CHECK(!document_layer_visible(doc, ids.child_a));
}

void ui_layer_eye_sweep_sets_crossed_rows_to_first_state() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  std::vector<patchy::LayerId> ids;
  ids.push_back(document
                    .add_pixel_layer("Keep Layer",
                                     solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)))
                    .id());
  for (const auto* name : {"Row Four", "Row Three", "Row Two", "Row One"}) {
    ids.push_back(document
                      .add_pixel_layer(name, solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                                          QColor(40, 80, 220)))
                      .id());
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Sweep"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* viewport = layer_list->viewport();
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto selected_before = layer_list->selectedItems().size();
  auto* current_before = layer_list->currentItem();

  const auto sweep_over = [&](const QString& start_name, std::initializer_list<const char*> crossed) {
    auto* eye = layer_row_eye_button(*layer_list, start_name);
    if (eye == nullptr) {
      return;
    }
    const auto center = eye->rect().center();
    const auto column_x = viewport->mapFromGlobal(eye->mapToGlobal(center)).x();
    send_mouse(*eye, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
    QApplication::processEvents();
    auto last_position = QPoint(column_x, 0);
    for (const auto* name : crossed) {
      auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
      last_position = QPoint(column_x, layer_list->visualItemRect(item).center().y());
      send_mouse(*viewport, QEvent::MouseMove, last_position, Qt::NoButton, Qt::LeftButton);
      QApplication::processEvents();
      QApplication::processEvents();
    }
    send_mouse(*viewport, QEvent::MouseButtonRelease, last_position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  // Downward sweep from the top row hides every crossed eye.
  sweep_over(QStringLiteral("Row One"), {"Row Two", "Row Three", "Row Four"});
  CHECK(!document_layer_visible(doc, ids[4]));
  CHECK(!document_layer_visible(doc, ids[3]));
  CHECK(!document_layer_visible(doc, ids[2]));
  CHECK(!document_layer_visible(doc, ids[1]));
  CHECK(document_layer_visible(doc, ids[0]));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Row Three"))->checkState() == Qt::Unchecked);
  // The press-drag must not rubber-band rows into the selection.
  CHECK(layer_list->selectedItems().size() == selected_before);
  CHECK(layer_list->currentItem() == current_before);

  // Upward sweep from a hidden eye shows everything it crosses again.
  sweep_over(QStringLiteral("Row Four"), {"Row Three", "Row Two", "Row One"});
  CHECK(document_layer_visible(doc, ids[4]));
  CHECK(document_layer_visible(doc, ids[3]));
  CHECK(document_layer_visible(doc, ids[2]));
  CHECK(document_layer_visible(doc, ids[1]));
  CHECK(document_layer_visible(doc, ids[0]));
  CHECK(layer_list->selectedItems().size() == selected_before);
}

void ui_layer_eye_sweep_skips_disabled_and_off_column() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document, /*folder_visible=*/false, /*child_b_visible=*/true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Sweep Skips"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* viewport = layer_list->viewport();
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  // Children of the hidden folder have disabled eyes; sweeping across them
  // leaves their stored flags alone.
  auto* eye = layer_row_eye_button(*layer_list, QStringLiteral("Background"));
  CHECK(eye != nullptr);
  auto center = eye->rect().center();
  auto column_x = viewport->mapFromGlobal(eye->mapToGlobal(center)).x();
  send_mouse(*eye, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(!document_layer_visible(doc, ids.background));
  auto* child_a_item = require_layer_item(*layer_list, QStringLiteral("Child A"));
  const auto child_a_y = layer_list->visualItemRect(child_a_item).center().y();
  send_mouse(*viewport, QEvent::MouseMove, QPoint(column_x, child_a_y), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  QApplication::processEvents();
  send_mouse(*viewport, QEvent::MouseButtonRelease, QPoint(column_x, child_a_y), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(document_layer_visible(doc, ids.child_a));
  CHECK(document_layer_visible(doc, ids.child_b));
  CHECK(!document_layer_visible(doc, ids.folder));

  // A drag that strays out of the eye column stops toggling: the folder row is
  // crossed at thumbnail x and its (enabled) eye must not flip.
  eye = layer_row_eye_button(*layer_list, QStringLiteral("Background"));
  CHECK(eye != nullptr);
  center = eye->rect().center();
  column_x = viewport->mapFromGlobal(eye->mapToGlobal(center)).x();
  send_mouse(*eye, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(document_layer_visible(doc, ids.background));
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  const auto off_column = QPoint(column_x + 60, layer_list->visualItemRect(folder_item).center().y());
  send_mouse(*viewport, QEvent::MouseMove, off_column, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  QApplication::processEvents();
  send_mouse(*viewport, QEvent::MouseButtonRelease, off_column, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!document_layer_visible(doc, ids.folder));
  CHECK(document_layer_visible(doc, ids.background));
}

void ui_layer_eye_sweep_survives_folder_row_rebuild() {
  EnvironmentVariableRestorer restore_delay{"PATCHY_PROCESSING_RENDER_TEST_DELAY_MS"};
  EnvironmentVariableRestorer restore_threshold{"PATCHY_PROCESSING_OVERLAY_MIN_PIXELS"};
  for (const bool collapsed : {false, true}) {
    for (const auto* start_name : {"Folder", "Top Layer"}) {
      patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
      const auto ids = build_eye_test_document(document, /*folder_visible=*/true, /*child_b_visible=*/true);

      patchy::ui::MainWindow window;
      show_window(window);
      // These rows need the Layers list's height, which the expanded Color panel takes in the
      // 780 px test window.
      patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
      window.add_document_session(std::move(document), QStringLiteral("Eye Sweep Rebuild"));
      QApplication::processEvents();

      auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(layer_list != nullptr);
      auto* viewport = layer_list->viewport();
      auto& doc = patchy::ui::MainWindowTestAccess::document(window);
      if (collapsed) {
        auto* folder_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Folder")));
        auto* disclosure = folder_row->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
        CHECK(disclosure != nullptr);
        disclosure->click();
        QApplication::processEvents();
        QApplication::processEvents();
      }
      CHECK(layer_list->count() == (collapsed ? 3 : 5));
      auto* canvas = require_canvas(window);
      canvas->force_refresh();
      const auto overlays_before = canvas->render_cache_diagnostics().processing_overlays_shown;
      qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("600"));
      qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("1"));
      const auto selected_before = layer_list->selectedItems().size();
      const auto current_id_before = layer_list->currentItem()->data(Qt::UserRole).toULongLong();

      // Use Qt's window event routing, including its implicit mouse grab. Sending
      // moves straight to the viewport hides the lost grab when a folder toggle
      // rebuilds and destroys the pressed eye button.
      auto* window_handle = window.windowHandle();
      CHECK(window_handle != nullptr);
      auto* eye = layer_row_eye_button(*layer_list, QString::fromLatin1(start_name));
      QPointer<QToolButton> pressed_eye = eye;
      CHECK(eye != nullptr);
      const auto center = eye->rect().center();
      const auto column_x = viewport->mapFromGlobal(eye->mapToGlobal(center)).x();
      const auto press_started = std::chrono::steady_clock::now();
      QTest::mousePress(window_handle, Qt::LeftButton, Qt::NoModifier, eye->mapTo(&window, center));
      QApplication::processEvents();
      QApplication::processEvents();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      const auto press_ms = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - press_started).count();
      CHECK(press_ms < 400.0);  // the injected render takes at least 600 ms

      QPoint last_position(column_x, 0);
      for (const auto* name : {"Folder", "Child B", "Child A", "Background"}) {
        if (collapsed && (QString::fromLatin1(name) == QStringLiteral("Child A") ||
                          QString::fromLatin1(name) == QStringLiteral("Child B"))) {
          continue;
        }
        auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
        last_position = QPoint(column_x, layer_list->visualItemRect(item).center().y());
        QTest::mouseMove(window_handle, viewport->mapTo(&window, last_position));
        QApplication::processEvents();
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      }
      // Releasing outside the list must also retire the sweep's explicit grab.
      QTest::mouseRelease(window_handle, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
      QApplication::processEvents();

      CHECK(pressed_eye.isNull());
      CHECK(QWidget::mouseGrabber() == nullptr);
      CHECK(!document_layer_visible(doc, ids.folder));
      // The now-disabled children were skipped; the enabled Background eye adopted
      // the sweep state.
      CHECK(document_layer_visible(doc, ids.child_a));
      CHECK(document_layer_visible(doc, ids.child_b));
      CHECK(!document_layer_visible(doc, ids.background));
      CHECK(document_layer_visible(doc, ids.top) == (QString::fromLatin1(start_name) == QStringLiteral("Folder")));
      CHECK(layer_list->selectedItems().size() == selected_before);
      CHECK(layer_list->currentItem()->data(Qt::UserRole).toULongLong() == current_id_before);
      save_widget_artifact("ui_layer_eye_sweep_after_folder_rebuild", window);

      // Sweep upward through the group to show the rows again, with the same real
      // event routing. A plain hover after release must leave the eyes alone.
      eye = layer_row_eye_button(*layer_list, QStringLiteral("Background"));
      QTest::mousePress(window_handle, Qt::LeftButton, Qt::NoModifier, eye->mapTo(&window, eye->rect().center()));
      for (const auto* name : {"Folder", "Top Layer"}) {
        auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
        last_position = QPoint(column_x, layer_list->visualItemRect(item).center().y());
        QTest::mouseMove(window_handle, viewport->mapTo(&window, last_position));
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      }
      QTest::mouseRelease(window_handle, Qt::LeftButton, Qt::NoModifier, viewport->mapTo(&window, last_position));
      QTest::mouseMove(window_handle, viewport->mapTo(&window, QPoint(column_x, viewport->height() - 1)));
      CHECK(QWidget::mouseGrabber() == nullptr);
      CHECK(document_layer_visible(doc, ids.top));
      CHECK(document_layer_visible(doc, ids.folder));
      CHECK(document_layer_visible(doc, ids.background));
      CHECK(document_layer_visible(doc, ids.child_a));
      CHECK(document_layer_visible(doc, ids.child_b));
      CHECK(layer_list->selectedItems().size() == selected_before);
      CHECK(layer_list->currentItem()->data(Qt::UserRole).toULongLong() == current_id_before);
      CHECK(canvas->render_cache_diagnostics().processing_overlays_shown == overlays_before);
      qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("0"));
      CHECK(process_events_until([canvas] { return canvas->render_settled(); }, 10000));
      const auto settled_image = render_widget_image(*canvas);
      canvas->force_refresh();
      CHECK(images_equal_rgba(settled_image, render_widget_image(*canvas)));
    }
  }
}

void ui_layer_eye_double_click_toggles_each_click() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(245, 245, 245)));
  const auto blink_id = document
                            .add_pixel_layer("Blink Layer",
                                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                                          QColor(40, 80, 220)))
                            .id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Double Click"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  click_layer_row_eye(*layer_list, QStringLiteral("Blink Layer"));
  CHECK(!document_layer_visible(doc, blink_id));

  // Rapid clicking turns the second press into a double-click; it must still
  // toggle like a press.
  auto* eye = layer_row_eye_button(*layer_list, QStringLiteral("Blink Layer"));
  CHECK(eye != nullptr);
  const auto center = eye->rect().center();
  send_mouse(*eye, QEvent::MouseButtonDblClick, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*eye, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(document_layer_visible(doc, blink_id));
  CHECK(require_layer_item(*layer_list, QStringLiteral("Blink Layer"))->checkState() == Qt::Checked);
}

// Canvas-driven layer activation must not rebuild the Layers panel when the
// target row already exists: every Move-tool auto-select click reconstructed
// every row widget (about 5 s on a 2000-layer document, September 2026). Only
// a collapsed ancestor or a name filter that has to clear changes the row set,
// and those still rebuild. Covers both entry points: the single activation
// (reveal_layer_in_layer_list) and the rectangle/multi selection
// (select_layers_in_layer_list).
void ui_move_auto_select_click_keeps_existing_layer_rows() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  auto& background =
      document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::set_layer_locks_position(background, true);
  patchy::Layer group(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  patchy::Layer nested_1(document.allocate_layer_id(), "Nested 1",
                         solid_pixels(18, 18, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  nested_1.set_bounds(patchy::Rect{20, 20, 18, 18});
  group.add_child(std::move(nested_1));
  patchy::Layer nested_2(document.allocate_layer_id(), "Nested 2",
                         solid_pixels(18, 18, patchy::PixelFormat::rgba8(), QColor(40, 90, 220)));
  nested_2.set_bounds(patchy::Rect{65, 20, 18, 18});
  group.add_child(std::move(nested_2));
  document.add_layer(std::move(group));
  patchy::Layer solo(document.allocate_layer_id(), "Solo",
                     solid_pixels(18, 18, patchy::PixelFormat::rgba8(), QColor(40, 180, 90)));
  solo.set_bounds(patchy::Rect{110, 70, 18, 18});
  const auto solo_id = solo.id();
  document.add_layer(std::move(solo));
  document.set_active_layer(solo_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Auto Select Keeps Rows"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);

  const auto row_widgets = [layer_list] {
    std::vector<QWidget*> widgets;
    for (int row = 0; row < layer_list->count(); ++row) {
      widgets.push_back(layer_list->itemWidget(layer_list->item(row)));
    }
    return widgets;
  };
  const auto click = [&](QPoint document_point) {
    const auto position = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    QApplication::processEvents();
  };

  // Every row exists: the click activates the layer without a rebuild.
  CHECK(layer_list->count() == 5);
  const auto before_click = row_widgets();
  click(QPoint(74, 29));
  CHECK(row_widgets() == before_click);
  auto* nested_2_item = require_layer_item(*layer_list, QStringLiteral("Nested 2"));
  CHECK(nested_2_item->isSelected());
  CHECK(layer_list->currentItem() == nested_2_item);
  CHECK(canvas->active_layer_document_rect() == QRect(65, 20, 18, 18));

  // Collapse the folder, then click a hidden child: its row must come back,
  // which is the one case that still rebuilds.
  auto* folder_widget = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Folder")));
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  click(QPoint(29, 29));
  CHECK(layer_list->count() == 5);
  auto* nested_1_item = require_layer_item(*layer_list, QStringLiteral("Nested 1"));
  CHECK(nested_1_item->isSelected());
  CHECK(layer_list->currentItem() == nested_1_item);
  CHECK(canvas->active_layer_document_rect() == QRect(20, 20, 18, 18));

  // A Ctrl-drag rectangle over existing rows selects them without a rebuild.
  const auto before_rectangle = row_widgets();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(10, 10)),
       canvas->widget_position_for_document_point(QPoint(90, 45)), Qt::ControlModifier);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(row_widgets() == before_rectangle);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 1"))->isSelected());
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 2"))->isSelected());
}

// Row masks keep rows from painting over the scroll bars. They used to be
// mapped through the scroll bar's container, which is not an ancestor of the
// rows, so Qt warned once per row on every scroll and resize (140k formatted
// stderr writes in one short session on a 2000-layer document, September
// 2026); the pass now maps through global coordinates and touches only rows
// inside the viewport.
void ui_layer_list_row_masks_map_scroll_bars_without_warnings() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  for (int index = 0; index < 60; ++index) {
    document.add_pixel_layer("Row " + std::to_string(index),
                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(index * 4, 80, 120)));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Row Masks"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->count() == 60);
  auto* scroll_bar = layer_list->verticalScrollBar();
  CHECK(scroll_bar != nullptr);
  CHECK(scroll_bar->maximum() > scroll_bar->minimum());

  static int map_from_warnings = 0;
  static QtMessageHandler previous_handler = nullptr;
  map_from_warnings = 0;
  previous_handler = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context,
                                               const QString& message) {
    if (message.contains(QStringLiteral("mapFrom"))) {
      ++map_from_warnings;
    }
    if (previous_handler != nullptr) {
      previous_handler(type, context, message);
    }
  });
  const auto restore_handler = qScopeGuard([] { qInstallMessageHandler(previous_handler); });

  scroll_bar->setValue((scroll_bar->minimum() + scroll_bar->maximum()) / 2);
  QApplication::processEvents();
  QApplication::processEvents();
  layer_list->resize(layer_list->width() - 8, layer_list->height());
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(map_from_warnings == 0);

  // Visible rows under the bar are masked away from it (rows beside a bar
  // that does not overlay them simply have nothing to mask).
  auto* viewport = layer_list->viewport();
  CHECK(viewport != nullptr);
  const QRect scroll_rect(viewport->mapFromGlobal(scroll_bar->mapToGlobal(QPoint(0, 0))), scroll_bar->size());
  for (int row = 0; row < layer_list->count(); ++row) {
    auto* row_widget = layer_list->itemWidget(layer_list->item(row));
    if (row_widget == nullptr) {
      continue;
    }
    const auto row_rect = row_widget->geometry();
    if (!row_rect.intersects(viewport->rect()) || !row_rect.intersects(scroll_rect)) {
      continue;
    }
    const auto probe = row_rect.intersected(scroll_rect).center() - row_rect.topLeft();
    CHECK(!row_widget->mask().isEmpty());
    CHECK(!row_widget->mask().contains(probe));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests_animation_part();
std::vector<patchy::test::TestCase> layer_panel_organization_tests_cross_document_part();
std::vector<patchy::test::TestCase> layer_panel_organization_tests_files_as_layers_part();

std::vector<patchy::test::TestCase> layer_panel_organization_tests() {
  std::vector<patchy::test::TestCase> tests{
      {"ui_tab_switch_layers_follow_the_canvas_after_tab_reorder",
       ui_tab_switch_layers_follow_the_canvas_after_tab_reorder},
      {"ui_new_layer_defaults_and_multiselect_layers_work", ui_new_layer_defaults_and_multiselect_layers_work},
      {"ui_ungroup_layers_releases_children_in_order_and_undoes",
       ui_ungroup_layers_releases_children_in_order_and_undoes},
      {"ui_merge_down_repeatedly_collapses_to_one_layer", ui_merge_down_repeatedly_collapses_to_one_layer},
      {"ui_merge_down_preserves_transparent_pixels", ui_merge_down_preserves_transparent_pixels},
      {"ui_merge_down_rasterizes_text_target", ui_merge_down_rasterizes_text_target},
      {"ui_merge_down_flattens_single_folder_in_place", ui_merge_down_flattens_single_folder_in_place},
      {"ui_merge_down_discards_hidden_layers", ui_merge_down_discards_hidden_layers},
      {"ui_merge_down_flattens_entire_psd_to_single_layer", ui_merge_down_flattens_entire_psd_to_single_layer},
      {"ui_merge_down_merges_multiple_folders", ui_merge_down_merges_multiple_folders},
      {"ui_merge_down_merges_layers_across_folders", ui_merge_down_merges_layers_across_folders},
      {"ui_new_layer_button_inserts_above_selected_layer", ui_new_layer_button_inserts_above_selected_layer},
      {"ui_document_default_layer_selection_skips_folder",
       ui_document_default_layer_selection_skips_folder},
      {"ui_document_with_only_folders_opens_with_no_layer_selected",
       ui_document_with_only_folders_opens_with_no_layer_selected},
      {"ui_duplicate_layer_copies_text_and_folder_trees", ui_duplicate_layer_copies_text_and_folder_trees},
      {"ui_duplicate_layer_inserts_copies_above_source", ui_duplicate_layer_inserts_copies_above_source},
      {"ui_delete_layer_activates_the_layer_below", ui_delete_layer_activates_the_layer_below},
      {"ui_copy_paste_layer_panel_copies_layers_and_folder_trees",
       ui_copy_paste_layer_panel_copies_layers_and_folder_trees},
      {"ui_layer_rows_toggle_visibility_and_drag_reorder", ui_layer_rows_toggle_visibility_and_drag_reorder},
      {"ui_layer_folders_create_with_drag_drop_affordances", ui_layer_folders_create_with_drag_drop_affordances},
      {"ui_layer_panel_mixed_folder_visual_cleanup", ui_layer_panel_mixed_folder_visual_cleanup},
      {"ui_layer_thumbnails_preview_the_whole_document", ui_layer_thumbnails_preview_the_whole_document},
      {"ui_layer_thumbnail_size_menu_resizes_rows_and_persists",
       ui_layer_thumbnail_size_menu_resizes_rows_and_persists},
      {"ui_layer_thumbnails_zoom_to_content_preference", ui_layer_thumbnails_zoom_to_content_preference},
      {"ui_layer_drag_drops_child_above_parent_folder", ui_layer_drag_drops_child_above_parent_folder},
      {"ui_layer_drag_multiselect_drops_children_above_parent_folder",
       ui_layer_drag_multiselect_drops_children_above_parent_folder},
      {"ui_layer_drag_folder_header_crack_drops_inside_folder",
       ui_layer_drag_folder_header_crack_drops_inside_folder},
      {"ui_layer_drag_shows_insertion_and_folder_drop_previews",
       ui_layer_drag_shows_insertion_and_folder_drop_previews},
      {"ui_layer_list_scrolls_with_wheel_and_drag_autoscroll",
       ui_layer_list_scrolls_with_wheel_and_drag_autoscroll},
      {"ui_layer_list_pixel_wheel_scrolls_in_row_steps", ui_layer_list_pixel_wheel_scrolls_in_row_steps},
      {"ui_layer_list_edge_click_selects_without_scrolling",
       ui_layer_list_edge_click_selects_without_scrolling},
      {"ui_layer_new_folder_button_groups_dropped_layers",
       ui_layer_new_folder_button_groups_dropped_layers},
      {"ui_layer_action_buttons_accept_multiselect_drops", ui_layer_action_buttons_accept_multiselect_drops},
      {"ui_layer_add_mask_button_tracks_selection_and_adds_mask",
       ui_layer_add_mask_button_tracks_selection_and_adds_mask},
      {"ui_layer_styles_button_tracks_selection_and_lists_effects",
       ui_layer_styles_button_tracks_selection_and_lists_effects},
      {"ui_layer_styles_menu_entry_opens_dialog_on_effect", ui_layer_styles_menu_entry_opens_dialog_on_effect},
      {"ui_panel_footer_buttons_outline_only_on_hover", ui_panel_footer_buttons_outline_only_on_hover},
      {"ui_layer_color_labels_menu_undo_and_row_strip", ui_layer_color_labels_menu_undo_and_row_strip},
      {"ui_layer_eye_right_click_opens_color_menu", ui_layer_eye_right_click_opens_color_menu},
      {"ui_issue_244_sample_layers_panel_shots_if_available",
       ui_issue_244_sample_layers_panel_shots_if_available},
      {"ui_issue_246_sample_prevent_auto_nesting_if_available",
       ui_issue_246_sample_prevent_auto_nesting_if_available},
      {"ui_psd_background_hides_prevent_auto_nesting_badge", ui_psd_background_hides_prevent_auto_nesting_badge},
      {"ui_layer_folders_expand_and_contract_children", ui_layer_folders_expand_and_contract_children},
      {"ui_layer_folders_open_with_saved_expansion_state", ui_layer_folders_open_with_saved_expansion_state},
      {"ui_layer_folder_toggle_saves_without_marking_modified", ui_layer_folder_toggle_saves_without_marking_modified},
      {"ui_layer_empty_folder_disclosure_toggles_and_saves", ui_layer_empty_folder_disclosure_toggles_and_saves},
      {"ui_move_auto_select_reveals_layers_in_collapsed_folders",
       ui_move_auto_select_reveals_layers_in_collapsed_folders},
      {"ui_folder_visibility_preserves_layer_panel_scroll", ui_folder_visibility_preserves_layer_panel_scroll},
      {"ui_layer_fill_opacity_control_updates_active_layer",
       ui_layer_fill_opacity_control_updates_active_layer},
      {"ui_layer_blend_row_wraps_in_narrow_panel", ui_layer_blend_row_wraps_in_narrow_panel},
      {"ui_layer_panel_compact_width_keeps_controls_whole", ui_layer_panel_compact_width_keeps_controls_whole},
      {"ui_right_dock_opens_at_content_width_without_gap", ui_right_dock_opens_at_content_width_without_gap},
      {"ui_layer_filter_shows_matching_rows_and_ancestor_folders",
       ui_layer_filter_shows_matching_rows_and_ancestor_folders},
      {"ui_layer_filter_reveals_matches_inside_collapsed_folders",
       ui_layer_filter_reveals_matches_inside_collapsed_folders},
      {"ui_layer_filter_reapplies_after_layer_rename", ui_layer_filter_reapplies_after_layer_rename},
      {"ui_layer_filter_blocks_layer_drag_reorder", ui_layer_filter_blocks_layer_drag_reorder},
      {"ui_layer_filter_persists_across_document_tabs", ui_layer_filter_persists_across_document_tabs},
      {"ui_layer_filter_empty_match_keeps_document_state",
       ui_layer_filter_empty_match_keeps_document_state},
      {"ui_layer_filter_reveal_clears_filter_for_hidden_target",
       ui_layer_filter_reveal_clears_filter_for_hidden_target},
      {"ui_layer_filter_drag_attempt_reports_error_without_selection_sweep",
       ui_layer_filter_drag_attempt_reports_error_without_selection_sweep},
      {"ui_layer_row_selected_highlight_paints", ui_layer_row_selected_highlight_paints},
      {"ui_layer_folder_alt_click_toggles_nested_folders",
       ui_layer_folder_alt_click_toggles_nested_folders},
      {"ui_layer_folder_ctrl_alt_click_toggles_all_folders",
       ui_layer_folder_ctrl_alt_click_toggles_all_folders},
      {"ui_layer_thumbnail_double_click_zooms_canvas_to_layer",
       ui_layer_thumbnail_double_click_zooms_canvas_to_layer},
      {"ui_layer_row_name_double_click_renames_inline", ui_layer_row_name_double_click_renames_inline},
      {"ui_layer_rename_hotkey_enters_inline_edit", ui_layer_rename_hotkey_enters_inline_edit},
      {"ui_layer_inline_rename_drops_commit_after_document_switch",
       ui_layer_inline_rename_drops_commit_after_document_switch},
      {"ui_layer_shape_row_double_click_opens_layer_style", ui_layer_shape_row_double_click_opens_layer_style},
      {"ui_layer_row_badges_center_on_the_row", ui_layer_row_badges_center_on_the_row},
      {"ui_layer_vector_badge_double_click_opens_shape_appearance",
       ui_layer_vector_badge_double_click_opens_shape_appearance},
      {"ui_layer_eye_alt_click_isolates_and_restores", ui_layer_eye_alt_click_isolates_and_restores},
      {"ui_layer_eye_alt_click_folder_isolates_group", ui_layer_eye_alt_click_folder_isolates_group},
      {"ui_layer_eye_alt_click_reisolate_keeps_original_snapshot",
       ui_layer_eye_alt_click_reisolate_keeps_original_snapshot},
      {"ui_layer_eye_alt_click_after_external_change_starts_fresh",
       ui_layer_eye_alt_click_after_external_change_starts_fresh},
      {"ui_layer_eye_sweep_sets_crossed_rows_to_first_state",
       ui_layer_eye_sweep_sets_crossed_rows_to_first_state},
      {"ui_layer_eye_sweep_skips_disabled_and_off_column", ui_layer_eye_sweep_skips_disabled_and_off_column},
      {"ui_layer_eye_sweep_survives_folder_row_rebuild", ui_layer_eye_sweep_survives_folder_row_rebuild},
      {"ui_layer_eye_double_click_toggles_each_click", ui_layer_eye_double_click_toggles_each_click},
      {"ui_move_auto_select_click_keeps_existing_layer_rows",
       ui_move_auto_select_click_keeps_existing_layer_rows},
      {"ui_layer_list_row_masks_map_scroll_bars_without_warnings",
       ui_layer_list_row_masks_map_scroll_bars_without_warnings},
  };
  auto animation_part = layer_panel_organization_tests_animation_part();
  tests.insert(tests.end(), animation_part.begin(), animation_part.end());
  auto cross_document_part = layer_panel_organization_tests_cross_document_part();
  tests.insert(tests.end(), cross_document_part.begin(), cross_document_part.end());
  auto files_as_layers_part = layer_panel_organization_tests_files_as_layers_part();
  tests.insert(tests.end(), files_as_layers_part.begin(), files_as_layers_part.end());
  return tests;
}
