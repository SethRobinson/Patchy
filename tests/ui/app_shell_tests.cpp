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

void ui_main_window_renders_color_controls() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  auto* background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  CHECK(foreground != nullptr);
  CHECK(background != nullptr);
  CHECK(foreground->text() == QStringLiteral("FG"));
  CHECK(background->text() == QStringLiteral("BG"));
  CHECK(!foreground->text().contains('#'));
  CHECK(!background->text().contains('#'));
  CHECK(window.findChild<QDockWidget*>(QStringLiteral("swatchesDock")) == nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton")) == nullptr);
  CHECK(window.findChildren<QPushButton*>(QStringLiteral("swatchButton")).isEmpty());
  CHECK(window.findChild<QDockWidget*>(QStringLiteral("paletteDock")) != nullptr);
  const QStringList expected_menus = {QStringLiteral("File"),   QStringLiteral("Edit"),   QStringLiteral("Image"),
                                      QStringLiteral("Layer"),  QStringLiteral("Type"),   QStringLiteral("Select"),
                                      QStringLiteral("Filter"), QStringLiteral("Plugins"), QStringLiteral("View"),
                                      QStringLiteral("Window"), QStringLiteral("Help")};
  QStringList actual_menus;
  for (auto* action : window.menuBar()->actions()) {
    actual_menus << action->text().remove('&');
  }
  CHECK(actual_menus == expected_menus);
  CHECK(window.menuBar()->height() >= 30);
  // Frameless + custom chrome (badge, window buttons) exist only where Patchy draws its
  // own frame (Windows); macOS/Linux use the native frame and must NOT have them.
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint) ==
        patchy::ui::MainWindow::use_custom_window_chrome());
  auto* app_badge = window.menuBar()->findChild<QLabel*>(QStringLiteral("patchyBadge"));
  auto* window_close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  if (patchy::ui::MainWindow::use_custom_window_chrome()) {
    CHECK(app_badge != nullptr);
    CHECK(app_badge->pixmap(Qt::ReturnByValue).isNull() == false);
    CHECK(window_close != nullptr);
    CHECK(window_close->mapTo(&window, QPoint(window_close->width(), 0)).x() >= window.width() - 1);
  } else {
    CHECK(app_badge == nullptr);
    CHECK(window_close == nullptr);
  }
  CHECK(window.findChild<QAction*>(QStringLiteral("workspaceHomeAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("helpHomepageAction")) == nullptr);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  QStringList filter_action_texts;
  const std::function<void(QMenu*)> collect_filter_actions = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action->isSeparator()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_filter_actions(submenu);
        continue;
      }
      auto text = action->text();
      text.remove('&');
      filter_action_texts << text;
    }
  };
  collect_filter_actions(filter_menu);
  CHECK(filter_action_texts.contains(QStringLiteral("Soft Glow")));
  CHECK(filter_action_texts.contains(QStringLiteral("Punchy Color")));
  CHECK(filter_action_texts.contains(QStringLiteral("Noir")));
  CHECK(filter_action_texts.contains(QStringLiteral("Cinematic Matte")));
  CHECK(filter_action_texts.contains(QStringLiteral("Vintage Fade")));
  CHECK(filter_action_texts.contains(QStringLiteral("Twirl")));
  CHECK(filter_action_texts.contains(QStringLiteral("Clouds")));
  CHECK(filter_action_texts.contains(QStringLiteral("Pixel Mosaic")));
  CHECK(filter_action_texts.contains(QStringLiteral("Unsharp Mask")));
  CHECK(filter_action_texts.contains(QStringLiteral("Motion Blur")));
  CHECK(filter_action_texts.contains(QStringLiteral("Color Halftone")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness +24")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast +25%")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Desaturate")));
  for (auto* action : filter_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* adjustments_menu = window.findChild<QMenu*>(QStringLiteral("imageAdjustmentsMenu"));
  CHECK(adjustments_menu != nullptr);
  QStringList adjustment_action_texts;
  for (auto* action : adjustments_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
    if (!action->isSeparator()) {
      auto text = action->text();
      text.remove('&');
      adjustment_action_texts << text;
    }
  }
  CHECK(adjustment_action_texts.contains(QStringLiteral("Brightness/Contrast...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Brightness...")));
  CHECK(!adjustment_action_texts.contains(QStringLiteral("Contrast...")));
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustBrightnessAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("imageAdjustContrastAction")) == nullptr);
  auto* new_adjustments_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(new_adjustments_menu != nullptr);
  CHECK(require_action(window, "layerNewLevelsAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewCurvesAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewHueSaturationAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewColorBalanceAdjustmentAction") != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton")) != nullptr);
  CHECK(window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin")) != nullptr);
  for (auto* button : window.findChildren<QPushButton*>()) {
    CHECK(button->text() != QStringLiteral("Select and Mask..."));
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  CHECK(options_bar->height() >= 36);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  CHECK(tool_palette->width() <= 45);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(marquee_button->menu()->actions().size() == 2);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Marquee")));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(shape_button->menu()->actions().size() == 3);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Rect")));

  save_widget_artifact("ui_main_window", window);
}

void ui_window_force_refresh_action_rebuilds_cache() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Blue Block",
                      solid_pixels(44, 28, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  layer.set_bounds(patchy::Rect{42, 38, 44, 28});
  const auto expected = patchy::ui::qimage_from_document(document, true);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Force Refresh"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* action = require_action(window, "windowForceRefreshAction");
  CHECK(action->isEnabled());
  CHECK(action->shortcuts().contains(QKeySequence(Qt::Key_F5)));

  const auto before = canvas->render_cache_diagnostics();
  action->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();
  CHECK(after.full_refreshes == before.full_refreshes + 1);
  CHECK(after.forced_refreshes == before.forced_refreshes + 1);

  CHECK(color_close(canvas_pixel(*canvas, QPoint(48, 44)), expected.pixelColor(48, 44), 0));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(16, 16)), expected.pixelColor(16, 16), 0));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Forced refresh"));
  save_widget_artifact("ui_window_force_refresh", window);
}

void ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  auto layer_pixels = solid_pixels(40, 30, patchy::PixelFormat::rgba8(), Qt::transparent);
  fill_pixel_rect(layer_pixels, QRect(16, 12, 12, 10), QColor(230, 20, 30, 255));
  document.add_pixel_layer("Transparent Layer", std::move(layer_pixels));

  auto flat_composite = solid_pixels(40, 30, patchy::PixelFormat::rgb8(), Qt::black);
  fill_pixel_rect(flat_composite, QRect(16, 12, 12, 10), QColor(230, 20, 30));
  document.metadata().psd_flat_composite = std::move(flat_composite);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(140, 100);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(canvas, QPoint(2, 2)), QColor(188, 188, 188), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(14, 2)), QColor(236, 236, 236), 1));
  CHECK(color_close(canvas_pixel(canvas, QPoint(18, 14)), QColor(230, 20, 30), 1));
  CHECK(canvas.render_cache_diagnostics().full_refreshes == 1);
}

void ui_top_menu_items_highlight_on_hover() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* file_menu = window.menuBar()->actions().front()->menu();
  CHECK(file_menu != nullptr);
  auto* open_action = require_action(window, "fileOpenAction");
  CHECK(file_menu->actions().contains(open_action));

  file_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  const auto open_rect = file_menu->actionGeometry(open_action);
  CHECK(open_rect.isValid());
  const QPoint sample_point(open_rect.left() + 8, open_rect.center().y());
  const auto idle_color = file_menu->grab().toImage().pixelColor(sample_point);

  send_mouse(*file_menu, QEvent::MouseMove, open_rect.center(), Qt::NoButton, Qt::NoButton);
  if (file_menu->activeAction() != open_action) {
    file_menu->setActiveAction(open_action);
    QApplication::processEvents();
  }

  const auto hover_color = file_menu->grab().toImage().pixelColor(sample_point);
  CHECK(color_close(idle_color, QColor(58, 58, 58), 6));
  CHECK(color_close(hover_color, QColor(78, 111, 149), 6));
  CHECK(!color_close(idle_color, hover_color, 10));
  file_menu->close();
}

void ui_save_as_dialog_lists_recent_files() {
  ensure_artifact_dir();
  const auto first_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-target.psd")).absoluteFilePath();
  const auto second_path =
      QFileInfo(QStringLiteral("test-artifacts/recent-save-backup.png")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
    QFile second(second_path);
    CHECK(second.open(QIODevice::WriteOnly));
    CHECK(second.write("patchy png placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path, second_path});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->actions().size() >= 2);
  CHECK(recent_menu->actions()[0]->data().toString() == first_path);
  CHECK(recent_menu->actions()[0]->text().remove('&') == QStringLiteral("1 %1").arg(QDir::toNativeSeparators(first_path)));
  CHECK(recent_menu->actions()[1]->data().toString() == second_path);
  CHECK(recent_menu->actions()[1]->text().remove('&') == QStringLiteral("2 %1").arg(QDir::toNativeSeparators(second_path)));

  auto* save_as_action = require_action(window, "fileSaveAsAction");
  CHECK(save_as_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("saveAsRecentFileNameCombo"));
    CHECK(combo != nullptr);
    CHECK(combo->isEditable());
    CHECK(combo->count() == 2);
    CHECK(combo->itemText(0) == first_path);
    CHECK(combo->itemData(0).toString() == first_path);
    CHECK(combo->itemData(0, Qt::ToolTipRole).toString() == first_path);
    CHECK(combo->itemText(1) == second_path);
    CHECK(combo->itemData(1).toString() == second_path);
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absoluteFilePath() == second_path);
    saw_dialog = true;
    dialog->reject();
  });
  save_as_action->trigger();
  CHECK(saw_dialog);
}

void ui_open_recent_keeps_two_hundred_files_in_grouped_menu() {
  ensure_artifact_dir();
  QStringList recent_files;
  for (int i = 0; i < 205; ++i) {
    const auto path =
        QFileInfo(QStringLiteral("test-artifacts/recent-file-%1.psd").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("patchy recent placeholder") > 0);
    recent_files << path;
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), recent_files);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  QList<QAction*> direct_file_actions;
  QList<QMenu*> page_menus;
  for (auto* action : recent_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      direct_file_actions << action;
    }
    if (auto* submenu = action == nullptr ? nullptr : action->menu();
        submenu != nullptr && submenu->objectName().startsWith(QStringLiteral("fileOpenRecentRangeMenu"))) {
      page_menus << submenu;
    }
  }
  CHECK(direct_file_actions.size() == 50);
  CHECK(page_menus.size() == 3);
  for (int i = 0; i < direct_file_actions.size(); ++i) {
    CHECK(direct_file_actions[i]->data().toString() == recent_files[i]);
  }
  CHECK(direct_file_actions.front()->text().remove('&') ==
        QStringLiteral("1 %1").arg(QDir::toNativeSeparators(recent_files.front())));
  CHECK(direct_file_actions.back()->text().remove('&') ==
        QStringLiteral("50 %1").arg(QDir::toNativeSeparators(recent_files[49])));
  CHECK(page_menus[0]->title() == QStringLiteral("Recent Files 51-100"));
  CHECK(page_menus[1]->title() == QStringLiteral("Recent Files 101-150"));
  CHECK(page_menus[2]->title() == QStringLiteral("Recent Files 151-200"));

  QStringList all_menu_paths;
  const std::function<void(QMenu*)> collect_file_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        all_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_file_paths(submenu);
      }
    }
  };
  collect_file_paths(recent_menu);
  CHECK(all_menu_paths.size() == 200);
  for (int i = 0; i < all_menu_paths.size(); ++i) {
    CHECK(all_menu_paths[i] == recent_files[i]);
  }
  CHECK(!all_menu_paths.contains(recent_files[200]));
  CHECK(recent_menu->actions().contains(require_action(window, "fileClearRecentAction")));

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(recent_menu->height() <= screen->availableGeometry().height());
  }
  recent_menu->close();

  auto* first_older_action = page_menus.front()->actions().front();
  CHECK(first_older_action->data().toString() == recent_files[50]);
  CHECK(first_older_action->text().remove('&') ==
        QStringLiteral("51 %1").arg(QDir::toNativeSeparators(recent_files[50])));

  QApplication::clipboard()->clear();
  page_menus.front()->popup(window.mapToGlobal(QPoint(80, 80)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = page_menus.front()->actionGeometry(first_older_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  page_menus.front()->mapToGlobal(context_point));
  QApplication::sendEvent(page_menus.front(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_files[50]));

  CHECK(QFile::remove(recent_files[50]));
  first_older_action->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Recent file is missing"));

  QStringList refreshed_menu_paths;
  const std::function<void(QMenu*)> collect_refreshed_file_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        refreshed_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_refreshed_file_paths(submenu);
      }
    }
  };
  collect_refreshed_file_paths(recent_menu);
  CHECK(refreshed_menu_paths.size() == 199);
  CHECK(!refreshed_menu_paths.contains(recent_files[50]));
}

void ui_open_recent_keeps_two_hundred_folders_in_grouped_menu() {
  ensure_artifact_dir();
  QStringList recent_folders;
  for (int i = 0; i < 205; ++i) {
    const auto folder =
        QFileInfo(QStringLiteral("test-artifacts/recent-folder-%1").arg(i, 3, 10, QLatin1Char('0')))
            .absoluteFilePath();
    CHECK(QDir().mkpath(folder));
    recent_folders << folder;
  }

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), recent_folders);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  QList<QAction*> direct_folder_actions;
  QList<QMenu*> page_menus;
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      direct_folder_actions << action;
    }
    if (auto* submenu = action == nullptr ? nullptr : action->menu();
        submenu != nullptr && submenu->objectName().startsWith(QStringLiteral("fileOpenRecentFolderRangeMenu"))) {
      page_menus << submenu;
    }
  }
  CHECK(direct_folder_actions.size() == 50);
  CHECK(page_menus.size() == 3);
  for (int i = 0; i < direct_folder_actions.size(); ++i) {
    CHECK(direct_folder_actions[i]->data().toString() == recent_folders[i]);
  }
  CHECK(direct_folder_actions.front()->text().remove('&') ==
        QStringLiteral("1 %1").arg(QDir::toNativeSeparators(recent_folders.front())));
  CHECK(page_menus[0]->title() == QStringLiteral("Recent Folders 51-100"));
  CHECK(page_menus[1]->title() == QStringLiteral("Recent Folders 101-150"));
  CHECK(page_menus[2]->title() == QStringLiteral("Recent Folders 151-200"));

  QStringList all_menu_paths;
  const std::function<void(QMenu*)> collect_folder_paths = [&](QMenu* menu) {
    for (auto* action : menu->actions()) {
      if (action == nullptr || action->isSeparator()) {
        continue;
      }
      const auto path = action->data().toString();
      if (!path.isEmpty()) {
        all_menu_paths << path;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        collect_folder_paths(submenu);
      }
    }
  };
  collect_folder_paths(folders_menu);
  CHECK(all_menu_paths.size() == 200);
  for (int i = 0; i < all_menu_paths.size(); ++i) {
    CHECK(all_menu_paths[i] == recent_folders[i]);
  }
  CHECK(!all_menu_paths.contains(recent_folders[200]));
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();
  if (auto* screen = QApplication::primaryScreen(); screen != nullptr) {
    CHECK(folders_menu->height() <= screen->availableGeometry().height());
  }
  folders_menu->close();

  auto* first_older_action = page_menus.front()->actions().front();
  CHECK(first_older_action->data().toString() == recent_folders[50]);
  CHECK(first_older_action->text().remove('&') ==
        QStringLiteral("51 %1").arg(QDir::toNativeSeparators(recent_folders[50])));

  QApplication::clipboard()->clear();
  page_menus.front()->popup(window.mapToGlobal(QPoint(80, 80)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = page_menus.front()->actionGeometry(first_older_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  page_menus.front()->mapToGlobal(context_point));
  QApplication::sendEvent(page_menus.front(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(recent_folders[50]));
  page_menus.front()->close();
}

void ui_recent_file_context_menu_copies_path() {
  ensure_artifact_dir();
  const auto first_path = QFileInfo(QStringLiteral("test-artifacts/recent-copy-target.psd")).absoluteFilePath();
  {
    QFile first(first_path);
    CHECK(first.open(QIODevice::WriteOnly));
    CHECK(first.write("patchy psd placeholder") > 0);
  }

  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{first_path});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  CHECK(recent_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!recent_menu->actions().isEmpty());
  auto* recent_action = recent_menu->actions().front();
  CHECK(recent_action->data().toString() == first_path);

  recent_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy File Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFileCopyPathAction"));
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFileOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = recent_menu->actionGeometry(recent_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  recent_menu->mapToGlobal(context_point));
  QApplication::sendEvent(recent_menu, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(first_path));
  recent_menu->close();
}

void ui_recent_folder_context_menu_copies_path_and_offers_explorer() {
  ensure_artifact_dir();
  const auto folder = QFileInfo(QStringLiteral("test-artifacts/recent-folder-context")).absoluteFilePath();
  CHECK(QDir().mkpath(folder));

  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder});
    settings.sync();
  }

  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  CHECK(folders_menu->contextMenuPolicy() == Qt::CustomContextMenu);
  CHECK(!folders_menu->actions().isEmpty());
  auto* folder_action = folders_menu->actions().front();
  CHECK(folder_action->data().toString() == folder);

  folders_menu->popup(window.mapToGlobal(QPoint(40, 40)));
  QApplication::processEvents();

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* copy_action = find_menu_action_by_text(*menu, QStringLiteral("Copy Folder Path"));
      CHECK(copy_action != nullptr);
      CHECK(copy_action->objectName() == QStringLiteral("recentFolderCopyPathAction"));
      // Confirm the Explorer option is present, but do not trigger it (it would spawn explorer.exe).
      auto* explorer_action = find_menu_action_by_text(*menu, QStringLiteral("Open in File Explorer"));
      CHECK(explorer_action != nullptr);
      CHECK(explorer_action->objectName() == QStringLiteral("recentFolderOpenInExplorerAction"));
      copy_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = folders_menu->actionGeometry(folder_action).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  folders_menu->mapToGlobal(context_point));
  QApplication::sendEvent(folders_menu, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(QApplication::clipboard()->text() == QDir::toNativeSeparators(folder));
  folders_menu->close();
}

void ui_save_as_remembers_last_save_directory_between_windows() {
  ensure_artifact_dir();
  const auto remembered_dir = QFileInfo(QStringLiteral("test-artifacts/remembered-save-dir")).absoluteFilePath();
  CHECK(QDir().mkpath(remembered_dir));
  const auto saved_path = QDir(remembered_dir).filePath(QStringLiteral("remembered-save.psd"));
  QFile::remove(saved_path);

  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));

  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool saved = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
      CHECK(dialog != nullptr);
      dialog->setDirectory(remembered_dir);
      dialog->selectFile(saved_path);
      saved = true;
      static_cast<QDialog*>(dialog)->accept();
    });
    require_action(window, "fileSaveAsAction")->trigger();
    CHECK(saved);
    CHECK(QFileInfo::exists(saved_path));
  }

  {
    auto settings = patchy::ui::app_settings();
    CHECK(QFileInfo(settings.value(QStringLiteral("lastSaveDirectory")).toString()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
  }

  patchy::ui::MainWindow next_window;
  show_window(next_window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("saveAsFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(remembered_dir).absoluteFilePath());
    const auto selected_files = dialog->selectedFiles();
    CHECK(!selected_files.isEmpty());
    CHECK(QFileInfo(selected_files.first()).absolutePath() == QFileInfo(remembered_dir).absoluteFilePath());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(next_window, "fileSaveAsAction")->trigger();
  CHECK(saw_dialog);
}

void ui_open_remembers_last_directory_and_lists_recent_folders() {
  ensure_artifact_dir();
  const auto folder_a = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-a")).absoluteFilePath();
  const auto folder_b = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-b")).absoluteFilePath();
  const auto missing_folder = QFileInfo(QStringLiteral("test-artifacts/recent-open-dir-missing")).absoluteFilePath();
  CHECK(QDir().mkpath(folder_a));
  CHECK(QDir().mkpath(folder_b));
  QDir(missing_folder).removeRecursively();

  SettingsValueRestorer last_open_directory_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("lastOpenDirectory"), folder_a);
    // Include a stale entry to confirm self-healing drops folders that no longer exist.
    settings.setValue(QStringLiteral("recentFolders"), QStringList{folder_a, missing_folder, folder_b});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  auto* folders_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentFolderMenu"));
  CHECK(folders_menu != nullptr);
  QStringList listed_folders;
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && !action->isSeparator() && !action->data().toString().isEmpty()) {
      listed_folders << action->data().toString();
    }
  }
  CHECK(listed_folders == QStringList({folder_a, folder_b}));
  CHECK(folders_menu->actions().contains(require_action(window, "fileClearRecentFoldersAction")));

  // The Open dialog starts in the remembered directory.
  bool saw_open_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(folder_a).absoluteFilePath());
    saw_open_dialog = true;
    dialog->reject();
  });
  require_action(window, "fileOpenAction")->trigger();
  CHECK(saw_open_dialog);

  // Picking a recent folder opens the dialog pointed at that folder.
  bool saw_recent_folder_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("openFileDialog")));
    CHECK(dialog != nullptr);
    CHECK(QFileInfo(dialog->directory().absolutePath()).absoluteFilePath() ==
          QFileInfo(folder_b).absoluteFilePath());
    saw_recent_folder_dialog = true;
    dialog->reject();
  });
  listed_folders.clear();
  for (auto* action : folders_menu->actions()) {
    if (action != nullptr && action->data().toString() == folder_b) {
      action->trigger();
      break;
    }
  }
  CHECK(saw_recent_folder_dialog);

  // Clearing empties both the menu and the persisted list.
  require_action(window, "fileClearRecentFoldersAction")->trigger();
  QApplication::processEvents();
  for (auto* action : folders_menu->actions()) {
    CHECK(action->data().toString().isEmpty());
  }
  CHECK(!folders_menu->isEnabled());
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("recentFolders")).toStringList().isEmpty());
  }
}

QStringList top_level_menu_texts(QMenuBar& menu_bar) {
  QStringList texts;
  for (auto* action : menu_bar.actions()) {
    texts << action->text().remove('&');
  }
  return texts;
}

void choose_preferences_language(patchy::ui::MainWindow& window, const QString& language_code) {
  auto* preferences = require_action(window, "filePreferencesAction");
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesLanguageCombo"));
    CHECK(combo != nullptr);
    const auto index = combo->findData(language_code);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    dialog->accept();
  });
  preferences->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void update_manifest_parser_handles_supported_cases() {
  const QByteArray newer_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      },
      "macos": {
        "version": "0.3.0",
        "download_url": "https://rtsoft.com/patchy/PatchyMacOS.dmg"
      }
    }
  })";
  const auto update = patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("windows"),
                                                        QStringLiteral("0.1.0"));
  CHECK(update.has_value());
  CHECK(update->platform == QStringLiteral("windows"));
  CHECK(update->version == QStringLiteral("0.2"));
  CHECK(update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/PatchyWindowsInstaller.exe")));
  const auto update_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(update_result.status == patchy::ui::UpdateCheckStatus::UpdateAvailable);
  CHECK(update_result.update.has_value());
  CHECK(update_result.latest_version == QStringLiteral("0.2"));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2.0"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.2"), QStringLiteral("0.2.0")));
  CHECK(patchy::ui::update_version_is_newer(QStringLiteral("0.10"), QStringLiteral("0.2")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")));
  CHECK(!patchy::ui::update_version_is_newer(QStringLiteral("0.0.9"), QStringLiteral("0.1.0")));

  const QByteArray equal_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.1.0",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto equal_result =
      patchy::ui::inspect_update_manifest(equal_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(equal_result.status == patchy::ui::UpdateCheckStatus::NoUpdateAvailable);
  CHECK(equal_result.latest_version == QStringLiteral("0.1.0"));
  const QByteArray lower_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.0.9",
        "download_url": "https://rtsoft.com/patchy/PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(lower_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  CHECK(!patchy::ui::parse_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"))
             .has_value());
  const auto missing_platform_result =
      patchy::ui::inspect_update_manifest(newer_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(missing_platform_result.status == patchy::ui::UpdateCheckStatus::MissingPlatform);
  // A manifest that does carry a linux entry parses for the linux platform id.
  const QByteArray linux_manifest = R"({
    "platforms": {
      "linux": {
        "version": "0.2",
        "download_url": "https://rtsoft.com/patchy/Patchy.flatpak"
      }
    }
  })";
  const auto linux_update =
      patchy::ui::parse_update_manifest(linux_manifest, QStringLiteral("linux"), QStringLiteral("0.1.0"));
  CHECK(linux_update.has_value());
  CHECK(linux_update->platform == QStringLiteral("linux"));
  CHECK(linux_update->download_url == QUrl(QStringLiteral("https://rtsoft.com/patchy/Patchy.flatpak")));
  const auto invalid_manifest_result =
      patchy::ui::inspect_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(invalid_manifest_result.status == patchy::ui::UpdateCheckStatus::InvalidManifest);
  CHECK(!patchy::ui::parse_update_manifest(QByteArray("{"), QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());

  const QByteArray empty_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": ""
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
  const auto empty_url_result =
      patchy::ui::inspect_update_manifest(empty_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"));
  CHECK(empty_url_result.status == patchy::ui::UpdateCheckStatus::InvalidDownloadUrl);

  const QByteArray relative_url_manifest = R"({
    "platforms": {
      "windows": {
        "version": "0.2.0",
        "download_url": "PatchyWindowsInstaller.exe"
      }
    }
  })";
  CHECK(!patchy::ui::parse_update_manifest(relative_url_manifest, QStringLiteral("windows"), QStringLiteral("0.1.0"))
             .has_value());
}

void ui_update_available_dialog_warns_to_close_patchy_before_installing() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("updateAvailableMessageBox")));
    CHECK(dialog != nullptr);
    // The install advice is per-platform (installer exe / DMG / Flatpak bundle).
#if defined(Q_OS_MACOS)
    CHECK(dialog->text().contains(QStringLiteral("drag the new Patchy into Applications")));
#elif defined(Q_OS_LINUX)
    CHECK(dialog->text().contains(QStringLiteral("flatpak install")));
    CHECK(dialog->text().contains(QStringLiteral("curl -L -o")));
    CHECK(dialog->findChild<QAbstractButton*>(QStringLiteral("updateCopyCommandButton")) != nullptr);
#else
    CHECK(dialog->text().contains(
        QStringLiteral("Save your work and close Patchy before running the installer.")));
#endif
    saw_dialog = true;
    dialog->reject();
  });

  window.show_update_available({QStringLiteral("windows"), QStringLiteral("9.9"),
                                QUrl(QStringLiteral("https://rtsoft.com/files/PatchyWindowsInstaller.exe"))});
  CHECK(saw_dialog);
}

void ui_update_preference_persists_startup_check_setting() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), false);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("updates/checkOnStartup"), true);
    settings.sync();
  }

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesCheckForUpdatesCheck"));
    CHECK(check != nullptr);
    CHECK(check->isChecked());
    check->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_gui_scale_preference_persists_setting() {
  SettingsValueRestorer restore_gui_scale(QStringLiteral("preferences/guiScalePercent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/guiScalePercent"), 100);
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  bool dismissed_message = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesGuiScaleCombo"));
    CHECK(combo != nullptr);
    const int index = combo->findData(150);
    CHECK(index >= 0);
    combo->setCurrentIndex(index);
    saw_dialog = true;
    // Accepting with a changed scale shows a modal restart-required message box; dismiss it.
    QTimer::singleShot(0, [&] {
      auto* message = qobject_cast<QMessageBox*>(
          find_top_level_dialog(QStringLiteral("preferencesInterfaceScaleMessageBox")));
      CHECK(message != nullptr);
      if (message != nullptr) {
        message->accept();
        dismissed_message = true;
      }
    });
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(dismissed_message);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt() == 150);
}

void ui_main_window_persists_window_geometry() {
  SettingsValueRestorer restore_geometry(QStringLiteral("window/normalGeometry"));
  SettingsValueRestorer restore_maximized(QStringLiteral("window/maximized"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("window/normalGeometry"));
    settings.remove(QStringLiteral("window/maximized"));
    settings.sync();
  }

  // Derive the target from the available screen so the on-screen clamp performed during restore is
  // an identity operation; otherwise a small offscreen test screen would shrink/move the geometry.
  const QScreen* primary = QApplication::primaryScreen();
  CHECK(primary != nullptr);
  const QRect available = primary->availableGeometry();
  CHECK(available.isValid());
  const QRect target = available.adjusted(20, 20, -120, -100);
  CHECK(target.width() > 0 && target.height() > 0);
  {
    patchy::ui::MainWindow window;
    window.show();
    QApplication::processEvents();
    window.setGeometry(target);
    QApplication::processEvents();
    // Closing a window with no modified documents accepts the close and persists geometry.
    window.close();
    QApplication::processEvents();
  }

  QRect stored;
  {
    auto settings = patchy::ui::app_settings();
    stored = settings.value(QStringLiteral("window/normalGeometry")).toRect();
    CHECK(stored.isValid());
    CHECK(stored.size() == target.size());
    CHECK(!settings.value(QStringLiteral("window/maximized"), false).toBool());
  }

  patchy::ui::MainWindow restored;
  restored.show();
  QApplication::processEvents();
  CHECK(restored.size() == stored.size());
}

void ui_update_preference_defaults_startup_check_setting_to_enabled() {
  SettingsValueRestorer restore_update_check(QStringLiteral("updates/checkOnStartup"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("updates/checkOnStartup"));
    settings.sync();
  }

  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("updates/checkOnStartup")));
  CHECK(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
}

void ui_psd_import_warning_preference_defaults_to_hidden() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(check->text() == QStringLiteral("Show import warnings and notes in a popup (status bar otherwise)"));
    CHECK(!check->isChecked());
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_psd_import_warning_preference_persists_enabled_setting() {
  SettingsValueRestorer restore_psd_warning_check(QStringLiteral("imports/showPsdWarningsAndInfo"));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
    CHECK(check != nullptr);
    CHECK(!check->isChecked());
    check->setChecked(true);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);

  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
}

void ui_language_switch_updates_existing_window() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  const auto initial_tab_count = tabs->count();

  choose_preferences_language(window, QStringLiteral("ja"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto japanese_menus = top_level_menu_texts(*window.menuBar());
  CHECK(japanese_menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!japanese_menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());

  choose_preferences_language(window, QStringLiteral("en"));

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto english_menus = top_level_menu_texts(*window.menuBar());
  CHECK(english_menus.contains(QStringLiteral("File")));
  CHECK(!english_menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "fileSaveAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(tabs->count() == initial_tab_count);
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_preference_applies_at_startup() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("ja"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(!menus.contains(QStringLiteral("環境設定(P)")));
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());
}

void ui_language_missing_preference_uses_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("preferences/language"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("ja"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("ファイル(F)")));
  CHECK(require_action(window, "preferencesLanguageJapaneseAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(!settings.contains(QStringLiteral("preferences/language")));
}

void ui_language_saved_preference_overrides_system_language() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("en"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language(QLocale(QLocale::Japanese, QLocale::Japan));

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("preferences/language")).toString() == QStringLiteral("en"));
}

void ui_language_invalid_preference_falls_back_to_english() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("preferences/language"), QStringLiteral("zz"));
    settings.sync();
  }
  patchy::ui::LocalizationManager::instance().load_saved_language();

  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(patchy::ui::LocalizationManager::instance().current_language() == QStringLiteral("en"));
  const auto menus = top_level_menu_texts(*window.menuBar());
  CHECK(menus.contains(QStringLiteral("File")));
  CHECK(!menus.contains(QStringLiteral("Preferences")));
  CHECK(require_action(window, "preferencesLanguageEnglishAction")->isChecked());
}

void ui_language_catalog_covers_dialog_status_and_properties() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();

  const auto canvas_status = QCoreApplication::translate(
      "patchy::ui::CanvasWidget", "Select a normal pixel layer before painting on text");
  CHECK(canvas_status == QStringLiteral("テキスト上に描画する前に通常のピクセルレイヤーを選択してください"));

  const auto save_title = QCoreApplication::translate("patchy::ui::MainWindow", "Save changes?");
  CHECK(save_title == QStringLiteral("変更を保存しますか?"));
  const auto save_prompt =
      QCoreApplication::translate("patchy::ui::MainWindow", "Save changes to %1 before closing?");
  CHECK(save_prompt == QStringLiteral("閉じる前に %1 への変更を保存しますか?"));

  const auto no_layer = QCoreApplication::translate("patchy::ui::MainWindow", "Layer: No active layer");
  CHECK(no_layer == QStringLiteral("レイヤー: アクティブレイヤーなし"));
  const auto document_info = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Document: %1 x %2 px | %3 x %4 %5 | %6 ppi | %7 | %8 layers | Zoom %9% | %10");
  CHECK(document_info.startsWith(QStringLiteral("ドキュメント:")));
  const auto bmp_depth = QCoreApplication::translate("QObject", "Color depth");
  CHECK(bmp_depth == QStringLiteral("色深度"));
  const auto bmp_quantize = QCoreApplication::translate("QObject", "Reduce colors automatically");
  CHECK(bmp_quantize == QStringLiteral("色数を自動的に減らす"));
  const auto bmp_palette_file = QCoreApplication::translate("QObject", "Use palette file");
  CHECK(bmp_palette_file == QStringLiteral("パレットファイルを使用"));
  const auto settings_file = QCoreApplication::translate("QObject", "Settings file:");
  CHECK(settings_file == QStringLiteral("設定ファイル:"));
  const auto open_settings_folder = QCoreApplication::translate("QObject", "Open Settings Folder");
  CHECK(open_settings_folder == QStringLiteral("設定フォルダーを開く"));
  const auto settings_folder_failed = QCoreApplication::translate("QObject", "Could not open settings folder.");
  CHECK(settings_folder_failed == QStringLiteral("設定フォルダーを開けませんでした。"));
  const auto checking_updates = QCoreApplication::translate("QObject", "Checking for updates...");
  CHECK(checking_updates == QStringLiteral("更新を確認しています..."));
  const auto up_to_date = QCoreApplication::translate("QObject", "Patchy is up to date (%1).");
  CHECK(up_to_date == QStringLiteral("Patchy は最新です (%1)。"));
  const auto update_failed =
      QCoreApplication::translate("QObject", "Update check failed: invalid update manifest.");
  CHECK(update_failed == QStringLiteral("更新確認に失敗しました: 更新マニフェストが無効です。"));
  const auto show_effects = QCoreApplication::translate("QObject", "Show Effects");
  CHECK(show_effects == QStringLiteral("効果を表示"));
  const auto gloss_contour = QCoreApplication::translate("QObject", "Gloss Contour");
  CHECK(gloss_contour == QStringLiteral("光沢輪郭"));
  const auto link_with_layer = QCoreApplication::translate("QObject", "Link with Layer");
  CHECK(link_with_layer == QStringLiteral("レイヤーにリンク"));
  const auto pattern_basketweave = QCoreApplication::translate("QObject", "Basketweave");
  CHECK(pattern_basketweave == QStringLiteral("バスケット編み"));
  const auto contour_ring_double = QCoreApplication::translate("QObject", "Ring - Double");
  CHECK(contour_ring_double == QStringLiteral("リング - 二重"));
  const auto curves_graph = QCoreApplication::translate("QObject", "Curves graph");
  CHECK(curves_graph == QStringLiteral("トーンカーブグラフ"));
  const auto curves_input = QCoreApplication::translate("QObject", "Input:");
  CHECK(curves_input == QStringLiteral("入力:"));
  const auto curves_auto =
      QCoreApplication::translate("QObject", "Set the active channel from its histogram");
  CHECK(curves_auto == QStringLiteral("ヒストグラムに基づいて選択中のチャンネルを自動調整"));
  const auto curves_summary =
      QCoreApplication::translate("QObject", "Curves: RGB %1, Red %2, Green %3, Blue %4 points");
  CHECK(curves_summary == QStringLiteral("トーンカーブ: RGB %1、赤 %2、緑 %3、青 %4 ポイント"));
  const auto curves_presets = QCoreApplication::translate("QObject", "Curves presets");
  CHECK(curves_presets == QStringLiteral("トーンカーブのプリセット"));
  const auto medium_contrast = QCoreApplication::translate("QObject", "Medium Contrast");
  CHECK(medium_contrast == QStringLiteral("中程度のコントラスト"));
  const auto soft_glow = QCoreApplication::translate("QObject", "Soft Glow");
  CHECK(soft_glow == QStringLiteral("ソフトグロー"));
  const auto filter_radius = QCoreApplication::translate("QObject", "Radius");
  CHECK(filter_radius == QStringLiteral("半径"));
  CHECK(patchy::ui::filter_progress_stage_text(patchy::FilterProgressStage::GeneratingClouds) ==
        QStringLiteral("雲模様を生成しています"));
  const auto filter_gallery_action =
      QCoreApplication::translate("patchy::ui::MainWindow", "&Visual Filters && Looks...");
  CHECK(filter_gallery_action == QStringLiteral("ビジュアルフィルター && ルック(&V)..."));
  const auto filter_gallery_tip = QCoreApplication::translate(
      "patchy::ui::MainWindow", "Preview and apply visual filters and photo looks");
  CHECK(filter_gallery_tip == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  const auto filter_gallery_cancelled =
      QCoreApplication::translate("patchy::ui::MainWindow", "Cancelled Visual Filters & Looks");
  CHECK(filter_gallery_cancelled == QStringLiteral("ビジュアルフィルターとルックをキャンセルしました"));
  const auto filter_gallery_none =
      QCoreApplication::translate("patchy::ui::MainWindow", "No visual filter applied");
  CHECK(filter_gallery_none == QStringLiteral("ビジュアルフィルターは適用されませんでした"));
  const auto filter_gallery_title = QCoreApplication::translate("QObject", "Visual Filters & Looks");
  CHECK(filter_gallery_title == QStringLiteral("ビジュアルフィルターとルック"));
  const auto filter_gallery_original = QCoreApplication::translate("QObject", "Original");
  CHECK(filter_gallery_original == QStringLiteral("元画像"));
  const auto filter_gallery_canvas = QCoreApplication::translate("QObject", "Live Canvas Preview");
  CHECK(filter_gallery_canvas == QStringLiteral("キャンバスでライブプレビュー"));
  const auto filter_gallery_apply = QCoreApplication::translate("QObject", "Apply");
  CHECK(filter_gallery_apply == QStringLiteral("適用"));
  const auto filter_gallery_rendering = QCoreApplication::translate("QObject", "Rendering preview...");
  CHECK(filter_gallery_rendering == QStringLiteral("プレビューを描画しています..."));
  const auto filter_gallery_ready = QCoreApplication::translate("QObject", "Ready");
  CHECK(filter_gallery_ready == QStringLiteral("準備完了"));
  const auto curves_load = QCoreApplication::translate("QObject", "Load...");
  CHECK(curves_load == QStringLiteral("読み込み..."));
  const auto curves_save = QCoreApplication::translate("QObject", "Save...");
  CHECK(curves_save == QStringLiteral("保存..."));
  const auto curves_load_title = QCoreApplication::translate("QObject", "Load Curves Preset");
  CHECK(curves_load_title == QStringLiteral("トーンカーブプリセットを読み込み"));
  const auto curves_save_title = QCoreApplication::translate("QObject", "Save Curves Preset");
  CHECK(curves_save_title == QStringLiteral("トーンカーブプリセットを保存"));
  const auto curves_preset_filter =
      QCoreApplication::translate("QObject", "Photoshop Curves Preset (*.acv)");
  CHECK(curves_preset_filter == QStringLiteral("Photoshop トーンカーブプリセット (*.acv)"));
  const auto curves_load_error = QCoreApplication::translate(
      "QObject", "The Curves preset could not be loaded. The file may be damaged or unsupported.");
  CHECK(curves_load_error == QStringLiteral(
                                  "トーンカーブプリセットを読み込めませんでした。ファイルが破損しているか、対応していない可能性があります。"));
  const auto curves_save_error =
      QCoreApplication::translate("QObject", "The Curves preset could not be saved.");
  CHECK(curves_save_error == QStringLiteral("トーンカーブプリセットを保存できませんでした。"));
  const auto curves_target = QCoreApplication::translate("QObject", "Target");
  CHECK(curves_target == QStringLiteral("画像内調整"));
  const auto curves_before = QCoreApplication::translate("QObject", "Before");
  CHECK(curves_before == QStringLiteral("調整前"));
  const auto curves_clipping =
      QCoreApplication::translate("QObject", "Show shadow and highlight clipping together");
  CHECK(curves_clipping == QStringLiteral("シャドウとハイライトのクリッピングを同時に表示"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
}

void ui_filter_gallery_action_retranslates() {
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* action = require_action(window, "filterGalleryAction");
  CHECK(action->text() == QStringLiteral("&Visual Filters && Looks..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("ja"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("ビジュアルフィルター && ルック(&V)..."));
  CHECK(action->statusTip() == QStringLiteral("ビジュアルフィルターとフォトルックをプレビューして適用"));
  patchy::ui::ZoomableImagePreview translated_preview;
  CHECK(translated_preview.toolTip() ==
        QStringLiteral("ドラッグで表示位置を移動できます。マウスホイールでズームします。"));

  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  CHECK(action->text() == QStringLiteral("&Visual Filters && Looks..."));
  CHECK(action->statusTip() == QStringLiteral("Preview and apply visual filters and photo looks"));
}

void ui_about_dialog_shows_labeled_external_links() {
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchySplashScreen"));
    CHECK(dialog != nullptr);

    const auto link_labels = dialog->findChildren<QLabel*>(QStringLiteral("splashHome"));
    CHECK(link_labels.size() == 2);
    QString combined_text;
    for (const auto* label : link_labels) {
      CHECK(label->textFormat() == Qt::RichText);
      CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
      CHECK(label->openExternalLinks());
      combined_text += label->text();
      combined_text += QLatin1Char('\n');
    }

    CHECK(combined_text.contains(QStringLiteral("GitHub: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://github.com/SethRobinson/Patchy\"")));
    CHECK(combined_text.contains(QStringLiteral(">SethRobinson/Patchy</a>")));
    CHECK(combined_text.contains(QStringLiteral("Seth's site: ")));
    CHECK(combined_text.contains(QStringLiteral("href=\"https://rtsoft.com\"")));
    CHECK(combined_text.contains(QStringLiteral(">rtsoft.com</a>")));

    auto* settings_caption = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsCaption"));
    CHECK(settings_caption != nullptr);
    CHECK(settings_caption->text() == QStringLiteral("Settings file:"));
    auto* settings_path = dialog->findChild<QLabel*>(QStringLiteral("splashSettingsPath"));
    CHECK(settings_path != nullptr);
    CHECK(settings_path->text() == QDir::toNativeSeparators(patchy::ui::app_settings().fileName()));
    CHECK(settings_path->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    CHECK(settings_path->wordWrap());
    auto* open_settings_folder = dialog->findChild<QPushButton*>(QStringLiteral("splashOpenSettingsFolderButton"));
    CHECK(open_settings_folder != nullptr);
    CHECK(open_settings_folder->text() == QStringLiteral("Open Settings Folder"));

    save_widget_artifact("ui_about_dialog_links", *dialog);
    inspected = true;
    dialog->accept();
  });

  patchy::ui::show_about_splash();
  CHECK(inspected);
}

void ui_frameless_window_edges_resize() {
  if (!patchy::ui::MainWindow::use_custom_window_chrome()) {
    // macOS/Linux use the native frame; the OS owns the resize borders and the Qt-level
    // edge machinery under test here is deliberately inert.
    std::cout << "[SKIP] native window frame owns resize borders on this platform\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(980, 720);
  QApplication::processEvents();

#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == QStringLiteral("windows")) {
    const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_CAPTION) == 0);
  }
#endif

  const auto start = window.geometry();
  const QPoint right_edge(window.width() - 2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, right_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, right_edge + QPoint(90, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, right_edge + QPoint(90, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= start.width() + 70);
  CHECK(window.height() == start.height());

  const auto widened = window.geometry();
  const QPoint bottom_right(window.width() - 2, window.height() - 2);
  send_mouse(window, QEvent::MouseButtonPress, bottom_right, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, bottom_right + QPoint(45, 55), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, bottom_right + QPoint(45, 55), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.width() >= widened.width() + 30);
  CHECK(window.height() >= widened.height() + 40);

  const auto expanded = window.geometry();
  const QPoint left_edge(2, window.height() / 2);
  send_mouse(window, QEvent::MouseButtonPress, left_edge, Qt::LeftButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseMove, left_edge - QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(window, QEvent::MouseButtonRelease, left_edge - QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.x() <= expanded.x() - 45);
  CHECK(window.width() >= expanded.width() + 45);
}

void ui_right_edge_scrollbars_remain_draggable() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  for (int index = 0; index < 48; ++index) {
    document.add_pixel_layer("Scrollable Layer " + std::to_string(index + 1),
                             solid_pixels(64, 64, patchy::PixelFormat::rgba8(),
                                          QColor(40 + index * 3 % 180, 80, 220, 255)));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Scrollbar Edge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();

  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->isVisible());
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 3);
  QApplication::processEvents();

  QStyleOptionSlider option;
  option.initFrom(scroll);
  option.orientation = scroll->orientation();
  option.minimum = scroll->minimum();
  option.maximum = scroll->maximum();
  option.singleStep = scroll->singleStep();
  option.pageStep = scroll->pageStep();
  option.sliderPosition = scroll->sliderPosition();
  option.sliderValue = scroll->value();
  option.upsideDown = scroll->invertedAppearance();
  const auto handle = scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                      QStyle::SC_ScrollBarSlider, scroll);
  CHECK(handle.isValid());
  const auto start_x = std::clamp(scroll->width() - 2, handle.left(), handle.right());
  const QPoint start(start_x, handle.center().y());
  CHECK(handle.contains(start));
  CHECK(scroll->mapTo(&window, start).x() >= window.width() - 10);

  const auto geometry_before = window.geometry();
  const auto scroll_before = scroll->value();
  const auto end = start + QPoint(0, 55);
  send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*scroll, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(window.geometry() == geometry_before);
  CHECK(scroll->value() > scroll_before);
}

void ui_svg_icon_resources_are_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/new.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/mask.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/selection-add.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-save-selection.svg")));
  CHECK(QFile::exists(QStringLiteral(":/patchy/icons/channel-load-selection.svg")));

  const QIcon icon(QStringLiteral(":/patchy/icons/new.svg"));
  CHECK(!icon.isNull());
  CHECK(!icon.pixmap(QSize(32, 32)).isNull());

  CHECK(!require_action(window, "layerNewAction")->icon().isNull());
  CHECK(!require_action(window, "layerAddMaskAction")->icon().isNull());
}

}  // namespace

std::vector<patchy::test::TestCase> app_shell_tests() {
  return {
      {"ui_main_window_renders_color_controls", ui_main_window_renders_color_controls},
      {"ui_window_force_refresh_action_rebuilds_cache", ui_window_force_refresh_action_rebuilds_cache},
      {"ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency",
       ui_canvas_ignores_opaque_psd_flat_cache_for_first_paint_transparency},
      {"ui_top_menu_items_highlight_on_hover", ui_top_menu_items_highlight_on_hover},
      {"ui_save_as_dialog_lists_recent_files", ui_save_as_dialog_lists_recent_files},
      {"ui_open_recent_keeps_two_hundred_files_in_grouped_menu",
       ui_open_recent_keeps_two_hundred_files_in_grouped_menu},
      {"ui_open_recent_keeps_two_hundred_folders_in_grouped_menu",
       ui_open_recent_keeps_two_hundred_folders_in_grouped_menu},
      {"ui_recent_file_context_menu_copies_path", ui_recent_file_context_menu_copies_path},
      {"ui_recent_folder_context_menu_copies_path_and_offers_explorer",
       ui_recent_folder_context_menu_copies_path_and_offers_explorer},
      {"ui_save_as_remembers_last_save_directory_between_windows",
       ui_save_as_remembers_last_save_directory_between_windows},
      {"ui_open_remembers_last_directory_and_lists_recent_folders",
       ui_open_remembers_last_directory_and_lists_recent_folders},
      {"update_manifest_parser_handles_supported_cases", update_manifest_parser_handles_supported_cases},
      {"ui_update_available_dialog_warns_to_close_patchy_before_installing",
       ui_update_available_dialog_warns_to_close_patchy_before_installing},
      {"ui_update_preference_defaults_startup_check_setting_to_enabled",
       ui_update_preference_defaults_startup_check_setting_to_enabled},
      {"ui_update_preference_persists_startup_check_setting", ui_update_preference_persists_startup_check_setting},
      {"ui_gui_scale_preference_persists_setting", ui_gui_scale_preference_persists_setting},
      {"ui_main_window_persists_window_geometry", ui_main_window_persists_window_geometry},
      {"ui_psd_import_warning_preference_defaults_to_hidden",
       ui_psd_import_warning_preference_defaults_to_hidden},
      {"ui_psd_import_warning_preference_persists_enabled_setting",
       ui_psd_import_warning_preference_persists_enabled_setting},
      {"ui_language_switch_updates_existing_window", ui_language_switch_updates_existing_window},
      {"ui_language_preference_applies_at_startup", ui_language_preference_applies_at_startup},
      {"ui_language_missing_preference_uses_system_language", ui_language_missing_preference_uses_system_language},
      {"ui_language_saved_preference_overrides_system_language",
       ui_language_saved_preference_overrides_system_language},
      {"ui_language_invalid_preference_falls_back_to_english", ui_language_invalid_preference_falls_back_to_english},
      {"ui_language_catalog_covers_dialog_status_and_properties",
       ui_language_catalog_covers_dialog_status_and_properties},
      {"ui_filter_gallery_action_retranslates", ui_filter_gallery_action_retranslates},
      {"ui_about_dialog_shows_labeled_external_links", ui_about_dialog_shows_labeled_external_links},
      {"ui_frameless_window_edges_resize", ui_frameless_window_edges_resize},
      {"ui_right_edge_scrollbars_remain_draggable", ui_right_edge_scrollbars_remain_draggable},
      {"ui_svg_icon_resources_are_registered", ui_svg_icon_resources_are_registered},
  };
}
