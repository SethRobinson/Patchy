#include <QStatusBar>

#include "ui/canvas_widget.hpp"
#include "color/color_management.hpp"
#include "core/layer_metadata.hpp"
#include "ui/main_window.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "test_harness.hpp"
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QListWidget>
#include <QTemporaryDir>
#include <QMenu>
#include <QMessageBox>
#include <QIODevice>
#include <QSpinBox>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
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
  auto* layers_dock = window.findChild<QWidget*>(QStringLiteral("layersDock"));
  auto* channels_dock = window.findChild<QWidget*>(QStringLiteral("channelsDock"));
  auto* channels_panel = window.findChild<QWidget*>(QStringLiteral("channelPanel"));
  auto* channels_toggle = window.findChild<QToolButton*>(QStringLiteral("channelsDockCollapseButton"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(layer_list != nullptr);
  CHECK(layers_dock != nullptr);
  CHECK(channels_dock != nullptr);
  CHECK(channels_panel != nullptr);
  CHECK(channels_toggle != nullptr);
  CHECK(channels != nullptr);
  // Layers and Channels share the column's tab group, its tab bar above the panels.
  CHECK(patchy::ui::MainWindowTestAccess::panel_column(window)->section_widget(QStringLiteral("layersDock")) ==
        patchy::ui::MainWindowTestAccess::panel_column(window)->section_widget(QStringLiteral("channelsDock")));
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
  // The column edge stays grabbable whichever tab is in front.
  auto* column_edge = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(column_edge != nullptr);
  CHECK(column_edge->isVisible());
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
  auto* channels_dock = window.findChild<QWidget*>(QStringLiteral("channelsDock"));
  auto* panel = window.findChild<QWidget*>(QStringLiteral("channelPanel"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr);
  CHECK(panel != nullptr);
  CHECK(channels != nullptr);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("channelsDock"));
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
                       QStringLiteral("channelDeleteAction"),
                       QStringLiteral("channelNewLayerAction"),
                       QStringLiteral("channelNewColorLayerAction")}));
    // An alpha channel has values but no color of its own.
    CHECK(require_action(window, "channelNewLayerAction")->isEnabled());
    CHECK(!require_action(window, "channelNewColorLayerAction")->isEnabled());
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
  // Tool settings can flush on a delayed timer during the preview checks.
  // Do not leave this test's solid-green gradient in later MainWindows.
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
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
  auto* channels_dock = window.findChild<QWidget*>(QStringLiteral("channelsDock"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels_dock != nullptr);
  CHECK(channels != nullptr);
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("channelsDock"));
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
  // The Layers tab comes to the front first, as the user brings it there to click it.
  patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("layersDock"));
  QApplication::processEvents();
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::DocumentChannel);
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

// Issue 90: while View > Proof Colors is on, the Channels panel lists the CMYK proof's ink
// plates. They are read-only views like the color components.
void ui_channels_panel_shows_cmyk_plates_under_proof_colors() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  const auto space = patchy::CmykWorkingSpace::from_icc_profile(patchy::ui::bundled_cmyk_profile(),
                                                                patchy::RenderingIntent::RelativeColorimetric, true);

  // Left half a blue, right half black.
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(0, 160, 224));
  fill_pixel_rect(pixels, QRect(32, 0, 32, 48), QColor(0, 0, 0));
  document.add_pixel_layer("Artwork", std::move(pixels));
  patchy::ui::MainWindow window;
  // After the window exists: its constructor installs the stored profile choice.
  patchy::set_working_cmyk_space(space);
  window.add_document_session(std::move(document), QStringLiteral("Plates"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  auto* proof = window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"));
  CHECK(channels != nullptr && proof != nullptr);
  CHECK(channels->count() == 4);

  proof->trigger();
  QApplication::processEvents();
  CHECK(proof->isChecked());
  CHECK(channels->count() == 8);
  const QStringList plates{QStringLiteral("Cyan"), QStringLiteral("Magenta"), QStringLiteral("Yellow"),
                           QStringLiteral("Black")};
  for (int index = 0; index < 4; ++index) {
    CHECK(channels->item(4 + index)->text() == plates[index]);
    CHECK(channels->item(4 + index)->toolTip().contains(QStringLiteral("ink plate")));
  }

  // What each plate should show: 255 - ink, from the working space's separation.
  const auto inks = space->separate_rgba8(std::vector<std::uint8_t>{0, 160, 224, 255, 0, 0, 0, 255});
  const auto shown = [&](QPoint document_point) {
    QApplication::processEvents();
    return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(document_point));
  };
  const std::array<patchy::ui::CanvasWidget::LayerEditTarget, 4> targets{
      patchy::ui::CanvasWidget::LayerEditTarget::ComponentCyan, patchy::ui::CanvasWidget::LayerEditTarget::ComponentMagenta,
      patchy::ui::CanvasWidget::LayerEditTarget::ComponentYellow, patchy::ui::CanvasWidget::LayerEditTarget::ComponentBlack};
  for (std::size_t plate = 0; plate < 4; ++plate) {
    channels->setCurrentRow(4 + static_cast<int>(plate));
    QApplication::processEvents();
    CHECK(canvas->layer_edit_target() == targets[plate]);
    const auto blue = shown(QPoint(10, 20));
    const auto black = shown(QPoint(50, 20));
    CHECK(blue.red() == blue.green() && blue.green() == blue.blue());
    CHECK(std::abs(blue.red() - (255 - inks[plate])) <= 1);
    CHECK(std::abs(black.red() - (255 - inks[4 + plate])) <= 1);
  }
  save_widget_artifact("ui_channels_black_plate", *canvas);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Black plate")));
  auto* chip = window.findChild<QWidget*>(QStringLiteral("maskEditModeChip"));
  CHECK(chip != nullptr && chip->isVisible());

  // A plate is a view: the painting and moving tools are unavailable, as for Red/Green/Blue.
  CHECK(!require_action_by_text(window, QStringLiteral("Brush"))->isEnabled());
  CHECK(!require_action_by_text(window, QStringLiteral("Move"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("layer.flip_horizontal"))->isEnabled());
  CHECK(!require_action(window, "filterAction_patchy_filters_edge_detect")->isEnabled());

  // Ctrl-click loads where the plate prints no ink as a selection.
  const auto point = channels->visualItemRect(channels->item(4)).center();
  send_mouse(*channels->viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*channels->viewport(), QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(std::abs(canvas->selection_alpha_at(QPoint(10, 20)) - (255 - inks[0])) <= 1);
  CHECK(std::abs(canvas->selection_alpha_at(QPoint(50, 20)) - (255 - inks[4])) <= 1);

  // Turning the proof off removes the plates and returns to the composite.
  channels->setCurrentRow(7);
  QApplication::processEvents();
  proof->trigger();
  QApplication::processEvents();
  CHECK(!proof->isChecked());
  CHECK(channels->count() == 4);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);
  CHECK(channels->currentRow() == 0);
}


// The channel context menu's New Layer from Channel copies a row's values into a
// grayscale layer; New Layer from Channel Color copies a component or ink plate in its own
// color (Screen for components, Multiply for plates).
void ui_channel_rows_become_new_layers() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  const auto space = patchy::CmykWorkingSpace::from_icc_profile(patchy::ui::bundled_cmyk_profile(),
                                                                patchy::RenderingIntent::RelativeColorimetric, true);
  patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Artwork", solid_pixels(32, 24, patchy::PixelFormat::rgb8(), QColor(200, 120, 40)));
  patchy::ui::MainWindow window;
  patchy::set_working_cmyk_space(space);
  window.add_document_session(std::move(document), QStringLiteral("Channel layers"));
  show_window(window);
  QApplication::processEvents();
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  auto* gray_action = window.findChild<QAction*>(QStringLiteral("channelNewLayerAction"));
  auto* color_action = window.findChild<QAction*>(QStringLiteral("channelNewColorLayerAction"));
  CHECK(channels != nullptr && gray_action != nullptr && color_action != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto top_layer = [&doc]() -> const patchy::Layer& { return std::as_const(doc).layers().back(); };
  const auto top_pixel = [&](int x, int y) {
    const auto* pixel = top_layer().pixels().pixel(x, y);
    return QColor(pixel[0], pixel[1], pixel[2], pixel[3]);
  };

  // Composite has no color of its own.
  channels->setCurrentRow(0);
  QApplication::processEvents();
  CHECK(gray_action->isEnabled());
  CHECK(!color_action->isEnabled());

  channels->setCurrentRow(1);  // Red
  QApplication::processEvents();
  CHECK(color_action->isEnabled());
  gray_action->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(doc).layers().size() == 2);
  CHECK(top_layer().name() == "Red channel");
  CHECK(top_layer().blend_mode() == patchy::BlendMode::Normal);
  CHECK(top_pixel(5, 5) == QColor(200, 200, 200, 255));
  // The canvas left the channel view for the new layer.
  CHECK(require_canvas(window)->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Content);

  channels->setCurrentRow(1);
  QApplication::processEvents();
  color_action->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(doc).layers().size() == 3);
  CHECK(top_layer().name() == "Red");
  CHECK(top_layer().blend_mode() == patchy::BlendMode::Screen);
  CHECK(top_pixel(5, 5) == QColor(200, 0, 0, 255));
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::as_const(doc).layers().size() == 2);

  // An ink plate in color: what that ink alone prints, opaque, in Multiply.
  auto* proof = window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"));
  CHECK(proof != nullptr);
  proof->trigger();
  QApplication::processEvents();
  CHECK(channels->count() == 8);
  channels->setCurrentRow(5);  // Magenta
  QApplication::processEvents();
  color_action->trigger();
  QApplication::processEvents();
  CHECK(top_layer().name() == "Magenta");
  CHECK(top_layer().blend_mode() == patchy::BlendMode::Multiply);
  // The plate is separated from the composite as it now stands (the gray layer on top).
  const auto ink = space->separate_rgba8(std::vector<std::uint8_t>{200, 200, 200, 255})[1];
  patchy::CmykPercent magenta;
  magenta.magenta = static_cast<double>(ink) * 100.0 / 255.0;
  const auto expected = space->rgb_from_cmyk(magenta);
  const auto printed = top_pixel(5, 5);
  CHECK(printed.red() == expected.red && printed.green() == expected.green && printed.blue() == expected.blue);
  CHECK(printed.alpha() == 255);
  proof->trigger();
  QApplication::processEvents();
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
      {"ui_channels_panel_shows_cmyk_plates_under_proof_colors", ui_channels_panel_shows_cmyk_plates_under_proof_colors},
      {"ui_channel_rows_become_new_layers", ui_channel_rows_become_new_layers},
      {"ui_non_psd_save_warns_before_discarding_channels",
       ui_non_psd_save_warns_before_discarding_channels},
      {"ui_save_layered_flat_format_routes_to_save_as_with_psd_default",
       ui_save_layered_flat_format_routes_to_save_as_with_psd_default},
  };
}
