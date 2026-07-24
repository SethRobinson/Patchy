#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
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
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
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
  if (!std::filesystem::exists(path)) {
    return;
  }
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
  CHECK(layer_list->item(0)->text() == QStringLiteral("Text: Title copy"));

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
  CHECK(layer_list->item(0)->text() == QStringLiteral("Background copy"));
  CHECK(layer_list->count() == 12);
  save_widget_artifact("ui_copy_paste_layer_panel_tree", window);
}

void ui_layer_rows_toggle_visibility_and_drag_reorder() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
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
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));

  blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(!blue_visibility->icon().isNull());
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

  CHECK(layer_list->model()->moveRow(QModelIndex(), 0, QModelIndex(), layer_list->count()));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(layer_list->count() - 1)->text() == QStringLiteral("Layer 3"));
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
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
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
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  const auto ids = build_eye_test_document(document, /*folder_visible=*/true, /*child_b_visible=*/true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Eye Sweep Rebuild"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* viewport = layer_list->viewport();
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto selected_before = layer_list->selectedItems().size();

  // Pressing a folder eye rebuilds every row (destroying the pressed button);
  // the sweep must keep going through the viewport afterwards. This is the
  // regression for the old drag-from-eye rubber-band artifact.
  auto* eye = layer_row_eye_button(*layer_list, QStringLiteral("Folder"));
  CHECK(eye != nullptr);
  const auto center = eye->rect().center();
  const auto column_x = viewport->mapFromGlobal(eye->mapToGlobal(center)).x();
  send_mouse(*eye, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(!document_layer_visible(doc, ids.folder));

  QPoint last_position(column_x, 0);
  for (const auto* name : {"Child A", "Child B", "Background"}) {
    auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
    last_position = QPoint(column_x, layer_list->visualItemRect(item).center().y());
    send_mouse(*viewport, QEvent::MouseMove, last_position, Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();
    QApplication::processEvents();
  }
  send_mouse(*viewport, QEvent::MouseButtonRelease, last_position, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!document_layer_visible(doc, ids.folder));
  // The now-disabled children were skipped; the enabled Background eye adopted
  // the sweep state.
  CHECK(document_layer_visible(doc, ids.child_a));
  CHECK(document_layer_visible(doc, ids.child_b));
  CHECK(!document_layer_visible(doc, ids.background));
  CHECK(document_layer_visible(doc, ids.top));
  CHECK(layer_list->selectedItems().size() == selected_before);
  save_widget_artifact("ui_layer_eye_sweep_after_folder_rebuild", window);
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

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests() {
  return {
      {"ui_tab_switch_layers_follow_the_canvas_after_tab_reorder",
       ui_tab_switch_layers_follow_the_canvas_after_tab_reorder},
      {"ui_new_layer_defaults_and_multiselect_layers_work", ui_new_layer_defaults_and_multiselect_layers_work},
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
      {"ui_copy_paste_layer_panel_copies_layers_and_folder_trees",
       ui_copy_paste_layer_panel_copies_layers_and_folder_trees},
      {"ui_layer_rows_toggle_visibility_and_drag_reorder", ui_layer_rows_toggle_visibility_and_drag_reorder},
      {"ui_layer_folders_create_with_drag_drop_affordances", ui_layer_folders_create_with_drag_drop_affordances},
      {"ui_layer_panel_mixed_folder_visual_cleanup", ui_layer_panel_mixed_folder_visual_cleanup},
      {"ui_layer_drag_drops_child_above_parent_folder", ui_layer_drag_drops_child_above_parent_folder},
      {"ui_layer_drag_multiselect_drops_children_above_parent_folder",
       ui_layer_drag_multiselect_drops_children_above_parent_folder},
      {"ui_layer_drag_folder_header_crack_drops_inside_folder",
       ui_layer_drag_folder_header_crack_drops_inside_folder},
      {"ui_layer_drag_shows_insertion_and_folder_drop_previews",
       ui_layer_drag_shows_insertion_and_folder_drop_previews},
      {"ui_layer_list_scrolls_with_wheel_and_drag_autoscroll",
       ui_layer_list_scrolls_with_wheel_and_drag_autoscroll},
      {"ui_layer_list_edge_click_selects_without_scrolling",
       ui_layer_list_edge_click_selects_without_scrolling},
      {"ui_layer_new_folder_button_groups_dropped_layers",
       ui_layer_new_folder_button_groups_dropped_layers},
      {"ui_layer_action_buttons_accept_multiselect_drops", ui_layer_action_buttons_accept_multiselect_drops},
      {"ui_layer_folders_expand_and_contract_children", ui_layer_folders_expand_and_contract_children},
      {"ui_layer_folders_open_with_saved_expansion_state", ui_layer_folders_open_with_saved_expansion_state},
      {"ui_move_auto_select_reveals_layers_in_collapsed_folders",
       ui_move_auto_select_reveals_layers_in_collapsed_folders},
      {"ui_folder_visibility_preserves_layer_panel_scroll", ui_folder_visibility_preserves_layer_panel_scroll},
      {"ui_layer_fill_opacity_control_updates_active_layer",
       ui_layer_fill_opacity_control_updates_active_layer},
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
  };
}
