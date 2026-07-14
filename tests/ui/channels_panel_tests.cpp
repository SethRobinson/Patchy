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

void ui_channels_panel_targets_and_alpha_edits() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer(
      "Locked Color", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(80, 140, 210)));
  patchy::set_layer_lock_flags(layer, patchy::kLayerLockImagePixels);
  const auto add_channel = [&document](std::string name, patchy::DocumentChannelKind kind,
                                       std::uint8_t value) {
    patchy::PixelBuffer pixels(document.width(), document.height(), patchy::PixelFormat::gray8());
    pixels.clear(value);
    const auto id = document.allocate_channel_id();
    document.add_channel(patchy::DocumentChannel(id, std::move(name), kind, std::move(pixels)));
    return id;
  };
  const auto alpha_id = add_channel("Alpha A", patchy::DocumentChannelKind::Alpha, 0);
  const auto spot_id = add_channel("Spot Ink", patchy::DocumentChannelKind::Spot, 96);
  const auto alpha_b_id = add_channel("Alpha B", patchy::DocumentChannelKind::Alpha, 180);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Channels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  auto* channels_dock = window.findChild<QDockWidget*>(QStringLiteral("channelsDock"));
  auto* channels_panel = window.findChild<QWidget*>(QStringLiteral("channelPanel"));
  auto* channels_toggle = window.findChild<QToolButton*>(QStringLiteral("channelsDockCollapseButton"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(layer_list != nullptr);
  CHECK(layers_dock != nullptr);
  CHECK(channels_dock != nullptr);
  CHECK(channels_panel != nullptr);
  CHECK(channels_toggle != nullptr);
  CHECK(channels != nullptr);
  CHECK(window.tabPosition(Qt::RightDockWidgetArea) == QTabWidget::North);
  CHECK(window.tabifiedDockWidgets(layers_dock).contains(channels_dock));
  QTabBar* dock_tabs = nullptr;
  int layers_tab = -1;
  int channels_tab = -1;
  for (auto* candidate : window.findChildren<QTabBar*>()) {
    int candidate_layers = -1;
    int candidate_channels = -1;
    for (int index = 0; index < candidate->count(); ++index) {
      if (candidate->tabText(index) == QStringLiteral("Layers")) {
        candidate_layers = index;
      } else if (candidate->tabText(index) == QStringLiteral("Channels")) {
        candidate_channels = index;
      }
    }
    if (candidate_layers >= 0 && candidate_channels >= 0) {
      dock_tabs = candidate;
      layers_tab = candidate_layers;
      channels_tab = candidate_channels;
      break;
    }
  }
  CHECK(dock_tabs != nullptr);
  CHECK(dock_tabs->currentIndex() == layers_tab);
  CHECK(dock_tabs->mapToGlobal(QPoint()).y() < layers_dock->mapToGlobal(QPoint()).y());
  const auto average_tab_lightness = [dock_tabs](int index) {
    const auto image = dock_tabs->grab().toImage();
    auto sample = dock_tabs->tabRect(index).adjusted(5, 4, -5, -4);
    sample.setRight(std::min(sample.right(), sample.left() + 5));
    std::int64_t total = 0;
    int count = 0;
    for (int y = sample.top(); y <= sample.bottom(); ++y) {
      for (int x = sample.left(); x <= sample.right(); ++x) {
        total += image.pixelColor(x, y).lightness();
        ++count;
      }
    }
    CHECK(count > 0);
    return static_cast<int>(total / count);
  };
  CHECK(average_tab_lightness(layers_tab) >= average_tab_lightness(channels_tab) + 8);
  CHECK(channels_toggle->isChecked());
  CHECK(channels_toggle->text() == QStringLiteral("v"));
  dock_tabs->setCurrentIndex(channels_tab);
  QApplication::processEvents();
  CHECK(dock_tabs->currentIndex() == channels_tab);
  CHECK(channels_panel->isVisible());
  auto* channel_resize_handle = channels_dock->findChild<QWidget*>(
      QStringLiteral("rightDockResizeHandle"), Qt::FindDirectChildrenOnly);
  CHECK(channel_resize_handle != nullptr);
  CHECK(channel_resize_handle->isVisible());
  dock_tabs->setCurrentIndex(layers_tab);
  QApplication::processEvents();
  CHECK(dock_tabs->currentIndex() == layers_tab);
  CHECK(layer_list->isVisible());
  dock_tabs->setCurrentIndex(channels_tab);
  QApplication::processEvents();
  CHECK(dock_tabs->currentIndex() == channels_tab);
  CHECK(channels_panel->isVisible());
  CHECK(channels->count() == 7);
  const QStringList expected_names{QStringLiteral("Composite"), QStringLiteral("Red"),
                                   QStringLiteral("Green"), QStringLiteral("Blue"),
                                   QStringLiteral("Alpha A"), QStringLiteral("Spot Ink"),
                                   QStringLiteral("Alpha B")};
  for (int row_index = 0; row_index < channels->count(); ++row_index) {
    CHECK(channels->item(row_index)->text() == expected_names[row_index]);
  }

  // Saved alphas cannot be moved above the four derived component rows.
  CHECK(channels->model()->moveRow(QModelIndex(), 4, QModelIndex(), 0));
  QApplication::processEvents();
  QApplication::processEvents();
  for (int row_index = 0; row_index < channels->count(); ++row_index) {
    CHECK(channels->item(row_index)->text() == expected_names[row_index]);
  }

  // Alpha rows cannot cross a spot row: the panel restores the committed order
  // before asking MainWindow to mutate the document.
  CHECK(channels->model()->moveRow(QModelIndex(), 6, QModelIndex(), 4));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(channels->item(5)->text() == QStringLiteral("Spot Ink"));
  CHECK(active_document.channels()[0].id() == alpha_id);
  CHECK(active_document.channels()[1].id() == spot_id);
  CHECK(active_document.channels()[2].id() == alpha_b_id);

  channels->setCurrentRow(1);
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::ComponentRed);
  CHECK(!require_action_by_text(window, QStringLiteral("Brush"))->isEnabled());
  CHECK(!require_action_by_text(window, QStringLiteral("Move"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("layer.flip_horizontal"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("layer.flip_vertical"))->isEnabled());
  CHECK(!require_action(window, "filterAction_patchy_filters_edge_detect")->isEnabled());
  const auto red_preview = canvas_pixel(*canvas, QPoint(32, 24));
  CHECK(std::abs(red_preview.red() - 80) <= 4);
  CHECK(std::abs(red_preview.green() - red_preview.red()) <= 2);
  CHECK(std::abs(red_preview.blue() - red_preview.red()) <= 2);
  auto* chip = window.findChild<QToolButton*>(QStringLiteral("maskEditModeChip"));
  CHECK(chip != nullptr);
  CHECK(chip->isVisible());
  CHECK(chip->text().contains(QStringLiteral("Red")));
  chip->click();
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);

  channels->setCurrentRow(5);
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::DocumentChannel);
  CHECK(canvas->active_document_channel_id() == spot_id);
  CHECK(!canvas->document_channel_is_editable());
  CHECK(!require_action(window, "channelRenameAction")->isEnabled());
  CHECK(!require_action(window, "channelInvertAction")->isEnabled());
  CHECK(!require_action(window, "channelDeleteAction")->isEnabled());
  CHECK(require_action(window, "channelLoadSelectionAction")->isEnabled());
  patchy::ui::MainWindowTestAccess::update_document_action_state(window);
  CHECK(!require_action(window, "channelRenameAction")->isEnabled());
  CHECK(!require_action(window, "channelInvertAction")->isEnabled());
  CHECK(!require_action(window, "channelDeleteAction")->isEnabled());
  CHECK(require_action(window, "channelLoadSelectionAction")->isEnabled());

  channels->setCurrentRow(4);
  QApplication::processEvents();
  CHECK(canvas->active_document_channel_id() == alpha_id);
  CHECK(canvas->document_channel_is_editable());
  CHECK(!require_hotkey_action(window, QStringLiteral("layer.flip_horizontal"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("layer.flip_vertical"))->isEnabled());
  CHECK(require_action_by_text(window, QStringLiteral("Brush"))->isEnabled());
  CHECK(require_action(window, "layerFillForegroundAction")->isEnabled());
  CHECK(!require_action(window, "filterAction_patchy_filters_edge_detect")->isEnabled());

  channels->item(4)->setCheckState(Qt::Checked);
  QApplication::processEvents();
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Overlay);
  const auto overlay_preview = canvas_pixel(*canvas, QPoint(32, 24));
  CHECK(overlay_preview.red() > 120);
  CHECK(overlay_preview.blue() < 170);
  channels->item(4)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);

  const auto composite_before = patchy::Compositor{}.flatten_rgb8(active_document);
  canvas->set_primary_color(Qt::white);
  use_solid_fill_settings(canvas);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  const auto brush_point = canvas->widget_position_for_document_point(QPoint(12, 12));
  send_mouse(*canvas, QEvent::MouseButtonPress, brush_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, brush_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(*static_cast<const patchy::Document&>(active_document).find_channel(alpha_id)->pixels().pixel(12, 12) > 0);

  const auto diagnostics_before = canvas->render_cache_diagnostics();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto* filled_channel = static_cast<const patchy::Document&>(active_document).find_channel(alpha_id);
  CHECK(filled_channel != nullptr);
  CHECK(*filled_channel->pixels().pixel(12, 12) == 255);
  const auto diagnostics_after = canvas->render_cache_diagnostics();
  CHECK(diagnostics_after.full_refreshes == diagnostics_before.full_refreshes);
  CHECK(diagnostics_after.partial_patches == diagnostics_before.partial_patches);
  const auto composite_after = patchy::Compositor{}.flatten_rgb8(active_document);
  CHECK(composite_after.data().size() == composite_before.data().size());
  CHECK(std::equal(composite_before.data().begin(), composite_before.data().end(),
                   composite_after.data().begin()));

  channels->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(32, 24)), QColor(80, 140, 210), 4));

  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Alpha A")));
  QApplication::processEvents();
  const auto diagnostics_before_history = canvas->render_cache_diagnostics();
  require_action(window, "channelInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(*static_cast<const patchy::Document&>(active_document).find_channel(alpha_id)->pixels().pixel(12, 12) == 0);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  (void)canvas_pixel(*canvas, QPoint(20, 16));
  const auto diagnostics_after_undo = canvas->render_cache_diagnostics();
  CHECK(diagnostics_after_undo.full_refreshes == diagnostics_before_history.full_refreshes);
  CHECK(diagnostics_after_undo.partial_patches == diagnostics_before_history.partial_patches);
  CHECK(*static_cast<const patchy::Document&>(active_document).find_channel(alpha_id)->pixels().pixel(12, 12) == 255);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  (void)canvas_pixel(*canvas, QPoint(20, 16));
  const auto diagnostics_after_redo = canvas->render_cache_diagnostics();
  CHECK(diagnostics_after_redo.full_refreshes == diagnostics_before_history.full_refreshes);
  CHECK(diagnostics_after_redo.partial_patches == diagnostics_before_history.partial_patches);
  CHECK(*static_cast<const patchy::Document&>(active_document).find_channel(alpha_id)->pixels().pixel(12, 12) == 0);
  CHECK(chip->isVisible());
  CHECK(chip->text().contains(QStringLiteral("Alpha A")));
}

void ui_channel_ctrl_click_context_menu_and_compact_actions() {
  patchy::Document document(3, 1, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(3, 1, patchy::PixelFormat::rgba8());
  const std::array<std::array<std::uint8_t, 4>, 3> samples{{
      {64, 128, 192, 255},
      {200, 100, 50, 128},
      {10, 20, 30, 0},
  }};
  for (int x = 0; x < pixels.width(); ++x) {
    auto* pixel = pixels.pixel(x, 0);
    std::copy(samples[static_cast<std::size_t>(x)].begin(),
              samples[static_cast<std::size_t>(x)].end(), pixel);
  }
  document.add_pixel_layer("Pixels", std::move(pixels));

  patchy::PixelBuffer alpha_pixels(3, 1, patchy::PixelFormat::gray8());
  *alpha_pixels.pixel(0, 0) = 0;
  *alpha_pixels.pixel(1, 0) = 73;
  *alpha_pixels.pixel(2, 0) = 255;
  const auto alpha_id = document.allocate_channel_id();
  document.add_channel(patchy::DocumentChannel(alpha_id, "Alpha Test",
                                                patchy::DocumentChannelKind::Alpha,
                                                std::move(alpha_pixels)));
  patchy::PixelBuffer spot_pixels(3, 1, patchy::PixelFormat::gray8());
  *spot_pixels.pixel(0, 0) = 12;
  *spot_pixels.pixel(1, 0) = 128;
  *spot_pixels.pixel(2, 0) = 244;
  const auto spot_id = document.allocate_channel_id();
  document.add_channel(patchy::DocumentChannel(spot_id, "Spot Test",
                                                patchy::DocumentChannelKind::Spot,
                                                std::move(spot_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Channel Gestures"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* channels_dock = window.findChild<QDockWidget*>(QStringLiteral("channelsDock"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("channelPanel"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr);
  CHECK(panel != nullptr);
  CHECK(channels != nullptr);
  channels_dock->raise();
  QApplication::processEvents();
  CHECK(channels->currentRow() == 0);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);

  struct ButtonExpectation {
    const char* button_name;
    const char* action_name;
    QString text;
  };
  const std::array<ButtonExpectation, 6> button_expectations{{
      {"channelNewButton", "channelNewAction", QStringLiteral("New Channel")},
      {"channelSaveSelectionButton", "channelSaveSelectionAction", QStringLiteral("Save Selection as Channel")},
      {"channelLoadSelectionButton", "channelLoadSelectionAction", QStringLiteral("Load Channel as Selection")},
      {"channelRenameButton", "channelRenameAction", QStringLiteral("Rename Channel")},
      {"channelInvertButton", "channelInvertAction", QStringLiteral("Invert Channel")},
      {"channelDeleteButton", "channelDeleteAction", QStringLiteral("Delete Channel")},
  }};
  auto* action_bar = panel->findChild<QWidget*>(QStringLiteral("channelActionBar"));
  CHECK(action_bar != nullptr);
  int row_y = -1;
  int previous_x = -1;
  std::vector<qint64> icon_keys;
  for (const auto& expectation : button_expectations) {
    auto* button = panel->findChild<QToolButton*>(QLatin1String(expectation.button_name));
    auto* action = require_action(window, expectation.action_name);
    CHECK(button != nullptr);
    CHECK(button->defaultAction() == action);
    CHECK(button->text() == expectation.text);
    const auto position = button->mapTo(panel, QPoint());
    CHECK(row_y < 0 || position.y() == row_y);
    CHECK(position.x() > previous_x);
    CHECK(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
    CHECK(!button->icon().isNull());
    CHECK(!button->icon().pixmap(button->iconSize()).isNull());
    CHECK(button->toolTip() == action->toolTip());
    CHECK(button->accessibleName() == expectation.text);
    CHECK(button->width() <= 36);
    CHECK(button->height() <= 32);
    row_y = position.y();
    previous_x = position.x();
    icon_keys.push_back(button->icon().cacheKey());
  }
  CHECK(action_bar->height() <= 32);
  std::sort(icon_keys.begin(), icon_keys.end());
  CHECK(std::adjacent_find(icon_keys.begin(), icon_keys.end()) == icon_keys.end());

  const auto ctrl_click_row = [channels](int row) {
    const auto point = channels->visualItemRect(channels->item(row)).center();
    send_mouse(*channels->viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton,
               Qt::ControlModifier);
    send_mouse(*channels->viewport(), QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton,
               Qt::ControlModifier);
    QApplication::processEvents();
  };

  ctrl_click_row(1);
  CHECK(channels->currentRow() == 0);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(canvas->selection_alpha_at(QPoint(0, 0)) == 64);
  CHECK(canvas->selection_alpha_at(QPoint(1, 0)) == 227);
  CHECK(canvas->selection_alpha_at(QPoint(2, 0)) == 255);

  ctrl_click_row(0);
  CHECK(channels->currentRow() == 0);
  CHECK(canvas->selection_alpha_at(QPoint(0, 0)) == 115);
  CHECK(canvas->selection_alpha_at(QPoint(1, 0)) == 189);
  CHECK(canvas->selection_alpha_at(QPoint(2, 0)) == 255);

  ctrl_click_row(4);
  CHECK(channels->currentRow() == 0);
  CHECK(canvas->active_document_channel_id() == std::nullopt);
  CHECK(canvas->selection_alpha_at(QPoint(0, 0)) == 0);
  CHECK(canvas->selection_alpha_at(QPoint(1, 0)) == 73);
  CHECK(canvas->selection_alpha_at(QPoint(2, 0)) == 255);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(QPoint(0, 0)) == 115);
  CHECK(canvas->selection_alpha_at(QPoint(1, 0)) == 189);

  ctrl_click_row(5);
  CHECK(channels->currentRow() == 0);
  CHECK(canvas->selection_alpha_at(QPoint(0, 0)) == 12);
  CHECK(canvas->selection_alpha_at(QPoint(1, 0)) == 128);
  CHECK(canvas->selection_alpha_at(QPoint(2, 0)) == 244);

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&window, &saw_context_menu] {
    auto* menu = window.findChild<QMenu*>(QStringLiteral("channelContextMenu"));
    CHECK(menu != nullptr);
    CHECK(menu->objectName() == QStringLiteral("channelContextMenu"));
    QStringList action_names;
    for (auto* action : menu->actions()) {
      if (!action->isSeparator()) {
        action_names.push_back(action->objectName());
      }
    }
    CHECK(action_names ==
          QStringList({QStringLiteral("channelNewAction"),
                       QStringLiteral("channelSaveSelectionAction"),
                       QStringLiteral("channelLoadSelectionAction"),
                       QStringLiteral("channelRenameAction"),
                       QStringLiteral("channelInvertAction"),
                       QStringLiteral("channelDeleteAction")}));
    CHECK(require_action(window, "channelNewAction")->isEnabled());
    CHECK(require_action(window, "channelSaveSelectionAction")->isEnabled());
    CHECK(require_action(window, "channelLoadSelectionAction")->isEnabled());
    CHECK(require_action(window, "channelRenameAction")->isEnabled());
    CHECK(require_action(window, "channelInvertAction")->isEnabled());
    CHECK(require_action(window, "channelDeleteAction")->isEnabled());
    saw_context_menu = true;
    menu->close();
  });
  const auto context_point = channels->visualItemRect(channels->item(4)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  channels->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(channels->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(channels->currentItem()->text() == QStringLiteral("Alpha Test"));
  CHECK(canvas->active_document_channel_id() == alpha_id);
  CHECK(static_cast<const patchy::Document&>(patchy::ui::MainWindowTestAccess::document(window))
            .find_channel(spot_id) != nullptr);
  save_widget_artifact("ui_channel_compact_actions", *panel);
}

void ui_channel_shape_previews_match_committed_grayscale_and_overlay() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgb8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(80, 140, 210)));
  patchy::PixelBuffer channel_pixels(96, 72, patchy::PixelFormat::gray8());
  channel_pixels.clear(0);
  const auto channel_id = document.allocate_channel_id();
  document.add_channel(patchy::DocumentChannel(channel_id, "Alpha 1",
                                                patchy::DocumentChannelKind::Alpha,
                                                std::move(channel_pixels)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Channel Shape Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels != nullptr);
  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Alpha 1")));
  QApplication::processEvents();
  CHECK(canvas->active_document_channel_id() == channel_id);

  canvas->set_primary_color(QColor(0, 255, 0));  // mask_value_from_color = 150.
  canvas->set_brush_size(8);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  canvas->set_fill_shapes(true);
  canvas->set_gradient_method(patchy::GradientMethod::Linear);
  canvas->set_gradient_opacity(100);
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{0, 255, 0, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{0, 255, 0, 255}},
  });

  const QRect full_document(0, 0, active_document.width(), active_document.height());
  const auto reset_channel = [&](std::uint8_t value) {
    auto* channel = active_document.find_channel(channel_id);
    CHECK(channel != nullptr);
    channel->pixels().clear(value);
    canvas->grayscale_target_changed(full_document);
    QApplication::processEvents();
  };
  const auto check_solo_preview = [&](patchy::ui::CanvasTool tool, QPoint from, QPoint to,
                                      QPoint sample) {
    reset_channel(0);
    canvas->set_mask_display_mode(patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);
    canvas->set_tool(tool);
    const auto widget_from = canvas->widget_position_for_document_point(from);
    const auto widget_to = canvas->widget_position_for_document_point(to);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_from, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, widget_to, Qt::NoButton, Qt::LeftButton);
    const auto preview = canvas_pixel_center(*canvas, sample);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_to, Qt::LeftButton, Qt::NoButton);
    const auto committed = canvas_pixel_center(*canvas, sample);
    CHECK(color_close(preview, committed, 5));
    CHECK(std::abs(committed.red() - 150) <= 5);
    CHECK(std::abs(committed.green() - committed.red()) <= 2);
    CHECK(std::abs(committed.blue() - committed.red()) <= 2);
  };

  check_solo_preview(patchy::ui::CanvasTool::Line, QPoint(12, 12), QPoint(76, 12), QPoint(44, 12));
  check_solo_preview(patchy::ui::CanvasTool::Rectangle, QPoint(18, 18), QPoint(76, 54), QPoint(44, 34));
  check_solo_preview(patchy::ui::CanvasTool::Ellipse, QPoint(18, 18), QPoint(76, 54), QPoint(47, 36));
  check_solo_preview(patchy::ui::CanvasTool::Gradient, QPoint(12, 36), QPoint(80, 36), QPoint(46, 36));

  // A white Masked Areas edit removes the existing red overlay. The live
  // rectangle must reveal the composite immediately and match the release.
  reset_channel(0);
  canvas->set_mask_display_mode(patchy::ui::CanvasWidget::MaskDisplayMode::Overlay);
  canvas->set_primary_color(Qt::white);
  canvas->set_tool(patchy::ui::CanvasTool::Rectangle);
  const QPoint overlay_from(18, 18);
  const QPoint overlay_to(76, 54);
  const QPoint overlay_sample(44, 34);
  const auto overlaid_before = canvas_pixel_center(*canvas, overlay_sample);
  const auto widget_from = canvas->widget_position_for_document_point(overlay_from);
  const auto widget_to = canvas->widget_position_for_document_point(overlay_to);
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_to, Qt::NoButton, Qt::LeftButton);
  const auto overlay_preview = canvas_pixel_center(*canvas, overlay_sample);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_to, Qt::LeftButton, Qt::NoButton);
  const auto overlay_committed = canvas_pixel_center(*canvas, overlay_sample);
  CHECK(!color_close(overlaid_before, QColor(80, 140, 210), 10));
  CHECK(color_close(overlay_preview, QColor(80, 140, 210), 5));
  CHECK(color_close(overlay_preview, overlay_committed, 5));
}

void ui_channels_soft_selection_crud_history_and_layer_exit() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Pixels",
                           solid_pixels(96, 72, patchy::PixelFormat::rgb8(), QColor(210, 80, 45)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Channel History"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* channels_dock = window.findChild<QDockWidget*>(QStringLiteral("channelsDock"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr);
  CHECK(channels != nullptr);
  channels_dock->raise();
  QApplication::processEvents();
  CHECK(channels->isVisible());

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  auto* feather = window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin"));
  CHECK(feather != nullptr);
  feather->setValue(12);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 18)),
       canvas->widget_position_for_document_point(QPoint(72, 54)));
  QApplication::processEvents();
  std::optional<QPoint> soft_point;
  for (int y = 0; y < active_document.height() && !soft_point.has_value(); ++y) {
    for (int x = 0; x < active_document.width(); ++x) {
      const auto alpha = canvas->selection_alpha_at(QPoint(x, y));
      if (alpha > 20 && alpha < 230) {
        soft_point = QPoint(x, y);
        break;
      }
    }
  }
  CHECK(soft_point.has_value());
  const auto saved_alpha = canvas->selection_alpha_at(*soft_point);
  require_action(window, "channelSaveSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 1);
  const auto first_id = active_document.channels().front().id();
  CHECK(*active_document.channels().front().pixels().pixel(soft_point->x(), soft_point->y()) == saved_alpha);
  CHECK(!require_action_by_text(window, QStringLiteral("Move"))->isEnabled());
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().empty());
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(require_action_by_text(window, QStringLiteral("Move"))->isEnabled());
  CHECK(require_hotkey_action(window, QStringLiteral("layer.flip_horizontal"))->isEnabled());
  CHECK(require_action(window, "filterAction_patchy_filters_edge_detect")->isEnabled());
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 1);
  CHECK(active_document.channels().front().id() == first_id);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Alpha 1")));
  QApplication::processEvents();

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action(window, "channelLoadSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(*soft_point) == saved_alpha);

  require_action(window, "channelNewAction")->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 2);
  const auto second_id = active_document.channels().back().id();
  CHECK(canvas->active_document_channel_id() == second_id);

  bool rename_dialog_seen = false;
  QTimer::singleShot(0, [&rename_dialog_seen] {
    auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
    CHECK(dialog != nullptr);
    rename_dialog_seen = true;
    dialog->setTextValue(QStringLiteral("Custom Alpha"));
    dialog->accept();
  });
  require_action(window, "channelRenameAction")->trigger();
  QApplication::processEvents();
  CHECK(rename_dialog_seen);
  CHECK(static_cast<const patchy::Document&>(active_document).find_channel(second_id)->name() == "Custom Alpha");
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(static_cast<const patchy::Document&>(active_document).find_channel(second_id)->name() == "Alpha 2");
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(static_cast<const patchy::Document&>(active_document).find_channel(second_id)->name() == "Custom Alpha");

  auto* custom_item = require_layer_item(*channels, QStringLiteral("Custom Alpha"));
  const auto custom_row = channels->row(custom_item);
  CHECK(custom_row > 4);
  CHECK(channels->model()->moveRow(QModelIndex(), custom_row, QModelIndex(), 4));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(active_document.channels().front().id() == second_id);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().front().id() == first_id);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().front().id() == second_id);

  require_action(window, "channelDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 1);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 2);
  CHECK(static_cast<const patchy::Document&>(active_document).find_channel(second_id) != nullptr);

  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("Alpha 1")));
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::DocumentChannel);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  click_layer_row_thumbnail(*layers, QStringLiteral("Pixels"), QStringLiteral("layerContentThumbnail"));
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
}

void ui_save_fragmented_selection_as_channel_stays_responsive() {
  patchy::Document document(256, 256, patchy::PixelFormat::rgb8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(256, 256, patchy::PixelFormat::rgb8(), QColor(220, 80, 45)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Fragmented Selection"));
  show_window(window);
  auto* canvas = require_canvas(window);

  patchy::PixelBuffer checkerboard(256, 256, patchy::PixelFormat::gray8());
  for (int y = 0; y < checkerboard.height(); ++y) {
    auto row = checkerboard.row(y);
    for (int x = 0; x < checkerboard.width(); ++x) {
      row[static_cast<std::size_t>(x)] = ((x + y) & 1) == 0 ? 255 : 0;
    }
  }
  canvas->replace_selection_from_grayscale(checkerboard, QStringLiteral("Checkerboard selection"));
  CHECK(canvas->has_selection());
  CHECK(!canvas->selection_has_partial_alpha());

  QElapsedTimer timer;
  timer.start();
  require_action(window, "channelSaveSelectionAction")->trigger();
  const auto elapsed_ms = timer.elapsed();
  QApplication::processEvents();
  CHECK(elapsed_ms < 1500);
  const auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(active_document.channels().size() == 1);
  CHECK(active_document.channels().front().pixels().data().size() == checkerboard.data().size());
  CHECK(std::equal(active_document.channels().front().pixels().data().begin(),
                   active_document.channels().front().pixels().data().end(), checkerboard.data().begin()));
}

void ui_channels_target_survives_document_switch() {
  const auto make_document = [](QString channel_name, std::uint8_t value) {
    patchy::Document document(48, 36, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Pixels",
                             solid_pixels(48, 36, patchy::PixelFormat::rgb8(), QColor(90, 150, 210)));
    patchy::PixelBuffer pixels(48, 36, patchy::PixelFormat::gray8());
    pixels.clear(value);
    document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), channel_name.toStdString(),
                                                  patchy::DocumentChannelKind::Alpha, std::move(pixels)));
    return document;
  };

  patchy::ui::MainWindow window;
  window.add_document_session(make_document(QStringLiteral("First Alpha"), 40), QStringLiteral("First Channels"));
  show_window(window);
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(channels != nullptr);
  CHECK(tabs != nullptr);
  auto* first_canvas = require_canvas(window);
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(first_canvas->free_transform_active());
  channels->setCurrentItem(require_layer_item(*channels, QStringLiteral("First Alpha")));
  QApplication::processEvents();
  CHECK(!first_canvas->free_transform_active());
  const auto first_channel_id = first_canvas->active_document_channel_id();
  CHECK(first_channel_id.has_value());

  window.add_document_session(make_document(QStringLiteral("Second Alpha"), 210), QStringLiteral("Second Channels"));
  auto* second_canvas = require_canvas(window);
  CHECK(second_canvas != first_canvas);
  channels->setCurrentRow(1);
  QApplication::processEvents();
  CHECK(second_canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::ComponentRed);

  tabs->setCurrentIndex(tabs->indexOf(first_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == first_canvas);
  CHECK(first_canvas->active_document_channel_id() == first_channel_id);
  CHECK(channels->currentItem() != nullptr);
  CHECK(channels->currentItem()->text() == QStringLiteral("First Alpha"));

  tabs->setCurrentIndex(tabs->indexOf(second_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == second_canvas);
  CHECK(second_canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::ComponentRed);
  CHECK(channels->currentItem() != nullptr);
  CHECK(channels->currentItem()->text() == QStringLiteral("Red"));
}

void ui_non_psd_save_warns_before_discarding_channels() {
  patchy::Document document(24, 18, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Pixels",
                           solid_pixels(24, 18, patchy::PixelFormat::rgb8(), QColor(60, 120, 180)));
  patchy::PixelBuffer alpha(24, 18, patchy::PixelFormat::gray8());
  alpha.clear(128);
  document.add_channel(patchy::DocumentChannel(document.allocate_channel_id(), "Saved Alpha",
                                                patchy::DocumentChannelKind::Alpha, std::move(alpha)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Channel Save Warning"));
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("channels.png"));
  patchy::ui::ImageSaveOptions options;

  bool cancel_prompt_seen = false;
  QTimer::singleShot(0, [&cancel_prompt_seen] {
    auto* box = qobject_cast<QMessageBox*>(
        find_top_level_dialog(QStringLiteral("discardSavedChannelsMessageBox")));
    CHECK(box != nullptr);
    cancel_prompt_seen = true;
    box->button(QMessageBox::Cancel)->click();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document_to_path(window, path, options));
  CHECK(cancel_prompt_seen);
  CHECK(!QFileInfo::exists(path));

  bool save_prompt_seen = false;
  QTimer::singleShot(0, [&save_prompt_seen] {
    auto* box = qobject_cast<QMessageBox*>(
        find_top_level_dialog(QStringLiteral("discardSavedChannelsMessageBox")));
    CHECK(box != nullptr);
    save_prompt_seen = true;
    box->button(QMessageBox::Save)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, path, options));
  CHECK(save_prompt_seen);
  CHECK(QFileInfo::exists(path));
}

void ui_save_layered_flat_format_routes_to_save_as_with_psd_default() {
  patchy::Document document(24, 18, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Pixels",
                           solid_pixels(24, 18, patchy::PixelFormat::rgb8(), QColor(60, 120, 180)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layered Save Routing"));
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto jpg_path = temp.filePath(QStringLiteral("photo.jpg"));

  // A flat single-layer document saves to JPEG silently and becomes that file.
  patchy::ui::ImageSaveOptions options;
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, jpg_path, options));
  CHECK(QFileInfo::exists(jpg_path));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == jpg_path);

  // Once the document grows layers, Save must not silently flatten over the JPEG:
  // it routes to Save As with the file name defaulted to .psd (Photoshop behavior).
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  QByteArray jpg_bytes_before;
  {
    QFile jpg_file(jpg_path);
    CHECK(jpg_file.open(QIODevice::ReadOnly));
    jpg_bytes_before = jpg_file.readAll();
  }

  bool saw_dialog = false;
  QString default_name;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    const auto selected = dialog->selectedFiles();
    if (!selected.isEmpty()) {
      default_name = QFileInfo(selected.first()).fileName();
    }
    saw_dialog = true;
    dialog->reject();
  });
  CHECK(!patchy::ui::MainWindowTestAccess::save_document(window));
  CHECK(saw_dialog);
  CHECK(default_name == QStringLiteral("photo.psd"));

  // The canceled Save As left the JPEG untouched and the document unsaved.
  {
    QFile jpg_file(jpg_path);
    CHECK(jpg_file.open(QIODevice::ReadOnly));
    CHECK(jpg_file.readAll() == jpg_bytes_before);
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == jpg_path);
}

}  // namespace

std::vector<patchy::test::TestCase> channels_panel_tests() {
  return {
      {"ui_channels_panel_targets_and_alpha_edits", ui_channels_panel_targets_and_alpha_edits},
      {"ui_channel_ctrl_click_context_menu_and_compact_actions",
       ui_channel_ctrl_click_context_menu_and_compact_actions},
      {"ui_channel_shape_previews_match_committed_grayscale_and_overlay",
       ui_channel_shape_previews_match_committed_grayscale_and_overlay},
      {"ui_channels_soft_selection_crud_history_and_layer_exit",
       ui_channels_soft_selection_crud_history_and_layer_exit},
      {"ui_save_fragmented_selection_as_channel_stays_responsive",
       ui_save_fragmented_selection_as_channel_stays_responsive},
      {"ui_channels_target_survives_document_switch", ui_channels_target_survives_document_switch},
      {"ui_non_psd_save_warns_before_discarding_channels",
       ui_non_psd_save_warns_before_discarding_channels},
      {"ui_save_layered_flat_format_routes_to_save_as_with_psd_default",
       ui_save_layered_flat_format_routes_to_save_as_with_psd_default},
  };
}
