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

void ui_image_adjustments_menu_applies_active_layer_filters() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(layer_list != nullptr);
  CHECK(tabs != nullptr);

  canvas->set_primary_color(QColor(10, 120, 240));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(24);
  canvas->set_primary_color(QColor(240, 20, 20));

  bool saw_live_filter_preview = false;
  bool canvas_zoomed_with_dialog_open = false;
  bool canvas_panned_with_dialog_open = false;
  bool saw_filter_edit_lock = false;
  const auto zoom_before_dialog = canvas->zoom();
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(window.isEnabled());
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!tabs->tabBar()->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      CHECK(!require_action(window, "layerFillForegroundAction")->isEnabled());
      CHECK(require_action(window, "viewZoomInAction")->isEnabled());
      auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
      CHECK(amount != nullptr);
      CHECK(amount->value() == 100);
      process_events_for(120);
      saw_live_filter_preview = color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8);
      const auto preview_pixel_before_edit = canvas_pixel(*canvas, QPoint(40, 40));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
           canvas->widget_position_for_document_point(QPoint(58, 58)));
      const auto preview_pixel_after_edit = canvas_pixel(*canvas, QPoint(40, 40));
      saw_filter_edit_lock = color_close(preview_pixel_after_edit, preview_pixel_before_edit, 2);
      send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
      canvas_zoomed_with_dialog_open = canvas->zoom() > zoom_before_dialog;
      const auto origin_before_pan = canvas->widget_position_for_document_point(QPoint(0, 0));
      send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(300, 240), Qt::MiddleButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseMove, QPoint(318, 252), Qt::NoButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(318, 252), Qt::MiddleButton, Qt::NoButton);
      canvas_panned_with_dialog_open = canvas->widget_position_for_document_point(QPoint(0, 0)) != origin_before_pan;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  CHECK(saw_live_filter_preview);
  CHECK(canvas_zoomed_with_dialog_open);
  CHECK(canvas_panned_with_dialog_open);
  CHECK(saw_filter_edit_lock);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  CHECK(tabs->tabBar()->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));

  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8));

  accept_filter_dialog();
  require_action(window, "imageAdjustDesaturateAction")->trigger();
  QApplication::processEvents();
  const auto gray = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(std::abs(gray.red() - gray.green()) <= 2);
  CHECK(std::abs(gray.green() - gray.blue()) <= 2);
  save_widget_artifact("ui_image_adjustments_invert_desaturate", *canvas);

  canvas->set_primary_color(QColor(120, 70, 210));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  accept_filter_dialog({{QStringLiteral("filterBrightnessSpin"), 10},
                        {QStringLiteral("filterContrastSpin"), 50}});
  require_action(window, "imageAdjustBrightnessContrastAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(126, 51, 255), 6));

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() > 40);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() < 190);

  accept_filter_dialog();
  require_action(window, "imageAdjustAutoContrastAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  save_widget_artifact("ui_image_adjustments_auto_contrast", *canvas);

  auto* edge_detect = require_action(window, "filterAction_patchy_filters_edge_detect");
  CHECK(edge_detect->toolTip().contains(QStringLiteral("Edge Detect")));
  accept_filter_dialog({{QStringLiteral("filterStrengthSpin"), 150}});
  edge_detect->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(140, 90)).red() < 80);
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Edge Detect")));
  save_widget_artifact("ui_filter_edge_detect", *canvas);

  auto* emboss = require_action(window, "filterAction_patchy_filters_emboss");
  accept_filter_dialog({{QStringLiteral("filterAngleSpin"), 90},
                        {QStringLiteral("filterHeightSpin"), 4},
                        {QStringLiteral("filterDepthSpin"), 140}});
  emboss->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QListWidget*>(QStringLiteral("historyList"))->item(0)->text().contains(QStringLiteral("Emboss")));

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  auto* clouds = require_action(window, "filterAction_patchy_filters_clouds");
  bool changed_live_swatches_while_clouds_open = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    const std::array values = {
        std::pair{QStringLiteral("filterScaleSpin"), 48}, std::pair{QStringLiteral("filterDetailSpin"), 4},
        std::pair{QStringLiteral("filterContrastSpin"), 60}, std::pair{QStringLiteral("filterSeedSpin"), 3}};
    for (const auto& [object_name, value] : values) {
      auto* spin = dialog->findChild<QSpinBox*>(object_name);
      CHECK(spin != nullptr);
      spin->setValue(value);
    }
    // The invocation captured red/blue when the command started. Changing the
    // toolbar state before OK must not recolor the pending render.
    canvas->set_primary_color(QColor(0, 255, 0));
    canvas->set_secondary_color(QColor(255, 255, 0));
    changed_live_swatches_while_clouds_open = true;
    dialog->accept();
  });
  clouds->trigger();
  QApplication::processEvents();
  CHECK(changed_live_swatches_while_clouds_open);
  const auto cloud_pixel = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(cloud_pixel.green() < 8);
  CHECK(cloud_pixel.red() > 0);
  CHECK(cloud_pixel.blue() > 0);
  CHECK(std::abs(cloud_pixel.red() - cloud_pixel.blue()) > 4);
  save_widget_artifact("ui_filter_clouds_foreground_background", *canvas);
}

void ui_image_adjustments_respect_active_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(20, 90, 220));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  QApplication::processEvents();
  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(235, 165, 35), 10));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(160, 70)), QColor(20, 90, 220), 10));
  save_widget_artifact("ui_image_adjustment_selection_scope", *canvas);
}

void ui_direct_pixel_previews_preserve_floating_layer_bounds() {
  patchy::Document document(180, 140, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 140, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer floating_layer(document.allocate_layer_id(), "Floating Console",
                               solid_pixels(44, 32, patchy::PixelFormat::rgba8(), QColor(96, 96, 96, 255)));
  const auto floating_id = floating_layer.id();
  const QRect floating_rect(70, 45, 44, 32);
  floating_layer.set_bounds(
      patchy::Rect{floating_rect.x(), floating_rect.y(), floating_rect.width(), floating_rect.height()});
  document.add_layer(std::move(floating_layer));
  document.set_active_layer(floating_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Floating Levels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto original_pixel = canvas_pixel(*canvas, QPoint(72, 47));
  CHECK(color_close(original_pixel, QColor(96, 96, 96), 5));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());

  bool saw_levels_preview_with_bounds = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
    CHECK(black != nullptr);
    CHECK(white != nullptr);
    black->setValue(40);
    white->setValue(140);
    process_events_for(160);
    const auto preview_rect = canvas->active_layer_document_rect();
    CHECK(preview_rect.has_value());
    saw_levels_preview_with_bounds = preview_rect->topLeft() == floating_rect.topLeft() &&
                                     color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5);
    dialog->reject();
  });
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_levels_preview_with_bounds);
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 47)), original_pixel, 6));

  accept_levels_dialog(40, 140, 100);
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(72, 47)), original_pixel, 8));

  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(canvas->active_layer_document_rect()->topLeft() == floating_rect.topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(4, 4)), QColor(Qt::white), 5));
}

void ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(140, 100, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto strokes = solid_pixels(140, 100, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(strokes, QRect(24, 24, 24, 16), QColor(0, 255, 0, 255));
  fill_pixel_rect(strokes, QRect(64, 24, 24, 16), QColor(0, 0, 0, 255));
  auto& paint = document.add_pixel_layer("Paint", std::move(strokes));
  document.set_active_layer(paint.id());

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Levels Channels"));
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(0, 255, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(0, 0, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();
  bool saw_channel_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
    auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
    CHECK(channel != nullptr);
    CHECK(black_output != nullptr);
    CHECK(channel->count() == 4);
    CHECK(channel->itemText(0) == QStringLiteral("RGB"));
    CHECK(channel->itemText(1) == QStringLiteral("Red"));
    CHECK(channel->itemText(2) == QStringLiteral("Green"));
    CHECK(channel->itemText(3) == QStringLiteral("Blue"));
    channel->setCurrentIndex(1);
    black_output->setValue(255);
    process_events_for(180);
    saw_channel_preview = color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 0), 8) &&
                          color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(255, 0, 0), 8) &&
                          color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5);
    save_widget_artifact("ui_levels_red_channel_transparent_layer", *canvas);
    dialog->accept();
  });
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();

  CHECK(saw_channel_preview);
  CHECK(layer_list->count() == layer_count_before);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 30)), QColor(255, 0, 0), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 70)), QColor(Qt::white), 5));
}

void ui_levels_dialog_preserves_independent_channel_records() {
  QTimer::singleShot(0, [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
    auto* black_input = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
    auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
    CHECK(channel != nullptr);
    CHECK(black_input != nullptr);
    CHECK(black_output != nullptr);
    CHECK(channel->currentText() == QStringLiteral("RGB"));
    black_input->setValue(24);
    channel->setCurrentIndex(1);
    CHECK(channel->currentText() == QStringLiteral("Red"));
    CHECK(black_input->value() == 0);
    black_output->setValue(255);
    channel->setCurrentIndex(0);
    CHECK(black_input->value() == 24);
    CHECK(black_output->value() == 0);
    channel->setCurrentIndex(1);
    CHECK(black_input->value() == 0);
    CHECK(black_output->value() == 255);
    dialog->accept();
  });

  const auto result = patchy::ui::request_levels_settings(nullptr);
  CHECK(result.has_value());
  CHECK(result->black_input == 24);
  CHECK(result->black_output == 0);
  CHECK(result->red.black_input == 0);
  CHECK(result->red.black_output == 255);
  CHECK(result->green.black_input == 0);
  CHECK(result->green.black_output == 0);
}

void ui_hue_saturation_dialog_adjusts_selected_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 8));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_hue_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      process_events_for(120);
      saw_hue_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_hue_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));

  accept_hue_saturation_dialog(120, 0, 0);
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_selection", *canvas);
}

void ui_hue_saturation_creates_masked_adjustment_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(255, 0, 0));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_adjustment_layer_preview = false;
  bool saw_adjustment_layer_edit_lock = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      process_events_for(120);
      saw_adjustment_layer_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      const auto preview_pixel_before_edit = canvas_pixel(*canvas, QPoint(70, 70));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
           canvas->widget_position_for_document_point(QPoint(220, 110)));
      const auto preview_pixel_after_edit = canvas_pixel(*canvas, QPoint(70, 70));
      saw_adjustment_layer_edit_lock = color_close(preview_pixel_after_edit, preview_pixel_before_edit, 2);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerNewHueSaturationAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_adjustment_layer_preview);
  CHECK(saw_adjustment_layer_edit_lock);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());

  CHECK(layer_list->item(0) != nullptr);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Hue/Saturation"));
  auto* adjustment_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(adjustment_row != nullptr);
  CHECK(adjustment_row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) != nullptr);
  auto* adjustment_thumbnail = adjustment_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(adjustment_thumbnail != nullptr);
  const auto thumbnail_image = adjustment_thumbnail->pixmap(Qt::ReturnByValue).toImage();
  CHECK(thumbnail_image.pixelColor(2, 2).lightness() < 220);
  auto* details = adjustment_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("Normal")));
  CHECK(details->text().contains(QStringLiteral("100%")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));

  bool saw_initial_adjustment_settings = false;
  bool saw_adjustment_edit_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      saw_initial_adjustment_settings = hue->value() == 120;
      hue->setValue(-120);
      process_events_for(120);
      saw_adjustment_edit_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_initial_adjustment_settings);
  CHECK(saw_adjustment_edit_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_adjustment_layer", window);
}

void ui_hue_saturation_colorize_toggle_switches_ranges_and_creates_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(128, 128, 128));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  bool saw_master_ranges = false;
  bool saw_colorize_ranges = false;
  bool saw_colorize_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      auto* saturation = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationSaturationSpin"));
      auto* colorize = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationColorizeCheck"));
      CHECK(hue != nullptr);
      CHECK(saturation != nullptr);
      CHECK(colorize != nullptr);
      saw_master_ranges = hue->minimum() == -180 && hue->maximum() == 180 && !colorize->isChecked();
      colorize->setChecked(true);
      saw_colorize_ranges = hue->minimum() == 0 && hue->maximum() == 360 && saturation->minimum() == 0 &&
                            saturation->maximum() == 100 && saturation->value() == 25;
      hue->setValue(203);
      saturation->setValue(52);
      process_events_for(150);
      // Gray 128 colorized with (203, 52, 0): PS-calibrated (62, 141, 194).
      saw_colorize_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(62, 141, 194), 4);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerNewHueSaturationAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_master_ranges);
  CHECK(saw_colorize_ranges);
  CHECK(saw_colorize_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(62, 141, 194), 4));

  // Re-editing restores the colorize state and values; unchecking returns the
  // master ranges; cancelling leaves the layer untouched.
  bool saw_restored = false;
  bool saw_master_after_toggle_off = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      auto* colorize = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationColorizeCheck"));
      CHECK(hue != nullptr);
      CHECK(colorize != nullptr);
      saw_restored = colorize->isChecked() && hue->value() == 203 && hue->maximum() == 360;
      colorize->setChecked(false);
      saw_master_after_toggle_off = hue->minimum() == -180 && hue->maximum() == 180 && hue->value() == 0;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_restored);
  CHECK(saw_master_after_toggle_off);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(62, 141, 194), 4));
  save_widget_artifact("ui_hue_saturation_colorize", window);
}

void ui_layer_clipping_menu_toggles_renders_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_primary_color(QColor(255, 255, 255));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  patchy::Layer base(doc.allocate_layer_id(), "Base",
                     solid_pixels(40, 30, patchy::PixelFormat::rgba8(), QColor(200, 40, 40, 255)));
  base.set_bounds(patchy::Rect{20, 20, 40, 30});
  doc.add_layer(std::move(base));
  patchy::Layer member(doc.allocate_layer_id(), "Member",
                       solid_pixels(doc.width(), doc.height(), patchy::PixelFormat::rgba8(),
                                    QColor(20, 180, 60, 255)));
  const auto member_id = member.id();
  doc.add_layer(std::move(member));
  doc.set_active_layer(member_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed(QRect(0, 0, doc.width(), doc.height()));
  QApplication::processEvents();

  auto* action = require_action(window, "layerClippingMaskAction");
  CHECK(action->isEnabled());
  CHECK(action->text() == QStringLiteral("Create Clipping Mask"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(20, 180, 60), 6));

  action->trigger();
  QApplication::processEvents();
  const auto* member_layer = doc.find_layer(member_id);
  CHECK(member_layer != nullptr);
  CHECK(member_layer->clipped());
  CHECK(action->text() == QStringLiteral("Release Clipping Mask"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(255, 255, 255), 6));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 180, 60), 6));

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* member_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(member_row != nullptr);
  CHECK(member_row->findChild<QToolButton*>(QStringLiteral("layerClippingBadgeButton")) != nullptr);
  auto* details = member_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("clipped")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  member_layer = doc.find_layer(member_id);
  CHECK(member_layer != nullptr);
  CHECK(!member_layer->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(20, 180, 60), 6));
  member_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(member_row != nullptr);
  CHECK(member_row->findChild<QToolButton*>(QStringLiteral("layerClippingBadgeButton")) == nullptr);

  // Clicking the row badge releases the clip too.
  action->trigger();
  QApplication::processEvents();
  CHECK(doc.find_layer(member_id)->clipped());
  member_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(member_row != nullptr);
  auto* clip_badge = member_row->findChild<QToolButton*>(QStringLiteral("layerClippingBadgeButton"));
  CHECK(clip_badge != nullptr);
  clip_badge->click();
  QApplication::processEvents();
  CHECK(!doc.find_layer(member_id)->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(20, 180, 60), 6));

  // Group children repaint immediately too: a full-canvas magenta member inside
  // a folder clips to the folder's small base, revealing the green below.
  patchy::Layer folder(doc.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  patchy::Layer group_base(doc.allocate_layer_id(), "Group Base",
                           solid_pixels(30, 20, patchy::PixelFormat::rgba8(), QColor(60, 60, 200, 255)));
  group_base.set_bounds(patchy::Rect{90, 60, 30, 20});
  folder.add_child(std::move(group_base));
  patchy::Layer group_member(doc.allocate_layer_id(), "Group Member",
                             solid_pixels(doc.width(), doc.height(), patchy::PixelFormat::rgba8(),
                                          QColor(220, 40, 200, 255)));
  const auto group_member_id = group_member.id();
  folder.add_child(std::move(group_member));
  doc.add_layer(std::move(folder));
  doc.set_active_layer(group_member_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed(QRect(0, 0, doc.width(), doc.height()));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(220, 40, 200), 6));
  CHECK(action->isEnabled());
  action->trigger();
  QApplication::processEvents();
  CHECK(doc.find_layer(group_member_id)->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(20, 180, 60), 6));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 70)), QColor(220, 40, 200), 6));
  action->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(220, 40, 200), 6));
  save_widget_artifact("ui_layer_clipping_toggle", window);
}

void ui_generic_bg_clip_toggle_repaints_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("generic_bg.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdString(path.string()));
  QApplication::processEvents();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  std::function<const patchy::Layer*(const std::vector<patchy::Layer>&, const std::string&)> find_named =
      [&](const std::vector<patchy::Layer>& layers, const std::string& name) -> const patchy::Layer* {
    for (const auto& layer : layers) {
      if (layer.name() == name) {
        return &layer;
      }
      if (const auto* found = find_named(layer.children(), name); found != nullptr) {
        return found;
      }
    }
    return nullptr;
  };
  const auto* hue_layer = find_named(doc.layers(), "Hue/Saturation 1");
  CHECK(hue_layer != nullptr);
  doc.set_active_layer(hue_layer->id());
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  QApplication::processEvents();

  // Colorized panels before clipping (PS-calibrated blue).
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 260)), QColor(61, 140, 193), 6));

  // Clipping the adjustment onto Layer 10 must repaint immediately: the panels
  // lose the colorize everywhere except over the base's footprint.
  auto* action = require_action(window, "layerClippingMaskAction");
  CHECK(action->isEnabled());
  action->trigger();
  QApplication::processEvents();
  CHECK(doc.find_layer(hue_layer->id())->clipped());
  const auto clipped_panel = canvas_pixel(*canvas, QPoint(150, 260));
  CHECK(!color_close(clipped_panel, QColor(61, 140, 193), 6));

  action->trigger();
  QApplication::processEvents();
  CHECK(!doc.find_layer(hue_layer->id())->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 260)), QColor(61, 140, 193), 6));
}

void ui_view_layer_mask_menu_action_shows_and_selects_mask() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.active_layer_id().has_value());
  auto* layer = doc.find_layer(*doc.active_layer_id());
  CHECK(layer != nullptr);

  patchy::LayerMask mask;
  mask.bounds = patchy::Rect::from_size(doc.width(), doc.height());
  mask.pixels = patchy::PixelBuffer(doc.width(), doc.height(), patchy::PixelFormat::gray8());
  mask.pixels.clear(255);
  layer->set_mask(std::move(mask));
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);

  auto* action = require_action(window, "layerViewMaskAction");
  CHECK(action->isEnabled());
  CHECK(!action->isChecked());

  action->trigger();
  QApplication::processEvents();
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::Grayscale);
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  CHECK(action->isChecked());

  action->trigger();
  QApplication::processEvents();
  CHECK(canvas->mask_display_mode() == patchy::ui::CanvasWidget::MaskDisplayMode::None);
  // The mask stays selected for editing after leaving the view, like Alt-click.
  CHECK(canvas->layer_edit_target() == patchy::ui::CanvasWidget::LayerEditTarget::Mask);
  CHECK(!action->isChecked());
}

void ui_alt_click_layer_boundary_toggles_clipping() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_primary_color(QColor(255, 255, 255));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  patchy::Layer base(doc.allocate_layer_id(), "Base",
                     solid_pixels(40, 30, patchy::PixelFormat::rgba8(), QColor(200, 40, 40, 255)));
  base.set_bounds(patchy::Rect{20, 20, 40, 30});
  doc.add_layer(std::move(base));
  patchy::Layer member(doc.allocate_layer_id(), "Member",
                       solid_pixels(doc.width(), doc.height(), patchy::PixelFormat::rgba8(),
                                    QColor(20, 180, 60, 255)));
  const auto member_id = member.id();
  doc.add_layer(std::move(member));
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  canvas->document_changed(QRect(0, 0, doc.width(), doc.height()));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  // Row 0 = Member (top), row 1 = Base: Alt-click their shared boundary.
  const auto member_rect = layer_list->visualItemRect(layer_list->item(0));
  const QPoint boundary(member_rect.center().x(), member_rect.bottom() - 1);
  const auto send_alt_click = [layer_list, boundary] {
    const QPointF pos(boundary);
    QMouseEvent press(QEvent::MouseButtonPress, pos, layer_list->viewport()->mapToGlobal(boundary), Qt::LeftButton,
                      Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(layer_list->viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, layer_list->viewport()->mapToGlobal(boundary),
                        Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::sendEvent(layer_list->viewport(), &release);
    QApplication::processEvents();
  };

  send_alt_click();
  const auto* member_layer = doc.find_layer(member_id);
  CHECK(member_layer != nullptr);
  CHECK(member_layer->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(255, 255, 255), 6));

  send_alt_click();
  member_layer = doc.find_layer(member_id);
  CHECK(member_layer != nullptr);
  CHECK(!member_layer->clipped());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 5)), QColor(20, 180, 60), 6));

  // Alt-clicking the boundary above the top row's base does nothing when the
  // layer below is the document bottom: pick the boundary between Base (row 1)
  // and the background (row 2) with Base selected as the would-be member.
  const auto base_rect = layer_list->visualItemRect(layer_list->item(1));
  const QPoint base_boundary(base_rect.center().x(), base_rect.bottom() - 1);
  const QPointF base_pos(base_boundary);
  QMouseEvent press(QEvent::MouseButtonPress, base_pos, layer_list->viewport()->mapToGlobal(base_boundary),
                    Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  QApplication::sendEvent(layer_list->viewport(), &press);
  QMouseEvent release(QEvent::MouseButtonRelease, base_pos, layer_list->viewport()->mapToGlobal(base_boundary),
                      Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  QApplication::sendEvent(layer_list->viewport(), &release);
  QApplication::processEvents();
  const auto* base_layer = [&]() -> const patchy::Layer* {
    for (const auto& doc_layer : doc.layers()) {
      if (doc_layer.name() == "Base") {
        return &doc_layer;
      }
    }
    return nullptr;
  }();
  CHECK(base_layer != nullptr);
  CHECK(base_layer->clipped());  // Base clips onto the background pixel layer
}

void ui_layer_clipping_action_enablement() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  auto* action = require_action(window, "layerClippingMaskAction");

  // The bottom layer has nothing below to clip to.
  CHECK(!doc.layers().empty());
  doc.set_active_layer(doc.layers().front().id());
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(!action->isEnabled());

  // A pixel layer above the background can clip.
  patchy::Layer top(doc.allocate_layer_id(), "Top",
                    solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(10, 20, 30, 255)));
  const auto top_id = top.id();
  doc.add_layer(std::move(top));
  doc.set_active_layer(top_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(action->isEnabled());
  CHECK(action->text() == QStringLiteral("Create Clipping Mask"));

  // A folder cannot clip, and a layer directly above a folder has no base.
  patchy::Layer folder(doc.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(doc.allocate_layer_id(), "Inside",
                                 solid_pixels(4, 4, patchy::PixelFormat::rgba8(), QColor(1, 2, 3, 255))));
  const auto folder_id = folder.id();
  doc.add_layer(std::move(folder));
  patchy::Layer above_folder(doc.allocate_layer_id(), "Above",
                             solid_pixels(4, 4, patchy::PixelFormat::rgba8(), QColor(9, 9, 9, 255)));
  const auto above_id = above_folder.id();
  doc.add_layer(std::move(above_folder));

  doc.set_active_layer(folder_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(!action->isEnabled());
  doc.set_active_layer(above_id);
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(!action->isEnabled());

  // An already-clipped layer always offers Release.
  doc.set_active_layer(top_id);
  if (auto* top_layer = doc.find_layer(top_id); top_layer != nullptr) {
    top_layer->set_clipped(true);
  }
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  CHECK(action->isEnabled());
  CHECK(action->text() == QStringLiteral("Release Clipping Mask"));
}

void ui_adjustment_layer_thumbnails_show_type_symbols() {
  patchy::Document document(120, 96, patchy::PixelFormat::rgba8());
  auto add_adjustment_layer = [&document](const QString& name, const patchy::AdjustmentSettings& settings) {
    patchy::Layer layer(document.allocate_layer_id(), name.toStdString(), patchy::LayerKind::Adjustment);
    patchy::configure_adjustment_layer(layer, settings);
    document.add_layer(std::move(layer));
  };

  patchy::AdjustmentSettings levels;
  levels.kind = patchy::AdjustmentKind::Levels;
  levels.levels = patchy::LevelsAdjustment{18, 232, 85};
  add_adjustment_layer(QStringLiteral("Levels"), levels);

  patchy::AdjustmentSettings curves;
  curves.kind = patchy::AdjustmentKind::Curves;
  curves.curves = patchy::curves_adjustment_from_legacy_outputs(36, 176, 245);
  add_adjustment_layer(QStringLiteral("Curves"), curves);

  patchy::AdjustmentSettings hue_saturation;
  hue_saturation.kind = patchy::AdjustmentKind::HueSaturation;
  hue_saturation.hue_saturation = patchy::HueSaturationAdjustment{35, 35, 10};
  add_adjustment_layer(QStringLiteral("Hue/Saturation"), hue_saturation);

  patchy::AdjustmentSettings color_balance;
  color_balance.kind = patchy::AdjustmentKind::ColorBalance;
  color_balance.color_balance = patchy::ColorBalanceAdjustment{45, -25, 35};
  add_adjustment_layer(QStringLiteral("Color Balance"), color_balance);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Adjustment Thumbnails"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto thumbnail_image = [layer_list](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    CHECK(item != nullptr);
    auto* row = layer_list->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    CHECK(thumbnail->toolTip() == QStringLiteral("Adjustment Layer"));
    return thumbnail->pixmap(Qt::ReturnByValue).toImage();
  };
  const auto levels_image = thumbnail_image(QStringLiteral("Levels"));
  const auto curves_image = thumbnail_image(QStringLiteral("Curves"));
  const auto hue_saturation_image = thumbnail_image(QStringLiteral("Hue/Saturation"));
  const auto color_balance_image = thumbnail_image(QStringLiteral("Color Balance"));

  auto vivid_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        const auto maximum = std::max({color.red(), color.green(), color.blue()});
        const auto minimum = std::min({color.red(), color.green(), color.blue()});
        if (maximum > 120 && maximum - minimum > 55) {
          ++count;
        }
      }
    }
    return count;
  };
  auto non_black_pixels = [](const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.red() + color.green() + color.blue() > 150) {
          ++count;
        }
      }
    }
    return count;
  };
  auto differing_pixels = [](const QImage& left, const QImage& right) {
    int count = 0;
    for (int y = 0; y < std::min(left.height(), right.height()); ++y) {
      for (int x = 0; x < std::min(left.width(), right.width()); ++x) {
        const auto a = left.pixelColor(x, y);
        const auto b = right.pixelColor(x, y);
        if (std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()) > 80) {
          ++count;
        }
      }
    }
    return count;
  };

  const auto levels_vivid = vivid_pixels(levels_image);
  const auto curves_vivid = vivid_pixels(curves_image);
  const auto hue_saturation_vivid = vivid_pixels(hue_saturation_image);
  const auto color_balance_vivid = vivid_pixels(color_balance_image);
  const auto levels_non_black = non_black_pixels(levels_image);
  const auto curves_non_black = non_black_pixels(curves_image);
  const auto levels_curves_difference = differing_pixels(levels_image, curves_image);
  const auto hue_color_balance_difference = differing_pixels(hue_saturation_image, color_balance_image);
  CHECK(levels_vivid > 80);
  CHECK(curves_vivid > 80);
  CHECK(hue_saturation_vivid > 120);
  CHECK(color_balance_vivid > 80);
  CHECK(levels_non_black > 300);
  CHECK(curves_non_black > 300);
  CHECK(levels_curves_difference > 100);
  CHECK(hue_color_balance_difference > 100);
}

void ui_levels_dialog_remaps_selected_tonal_range() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  canvas->set_brush_opacity(100);
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(outside_before.red() >= 170);
  CHECK(outside_before.red() <= 190);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();

  accept_levels_dialog(50, 180, 100);
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layer_count_before);
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));
  save_widget_artifact("ui_levels_selection", *canvas);
}

void ui_curves_dialog_remaps_midtones_in_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_secondary_color(QColor(255, 255, 255));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto selected_before = canvas_pixel(*canvas, QPoint(140, 90));
  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(selected_before.red() > 110);
  CHECK(selected_before.red() < 150);
  CHECK(outside_before.red() > 245);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();

  accept_curves_dialog(0, 220, 255);
  require_action(window, "imageAdjustCurvesAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layer_count_before);
  const auto selected_after = canvas_pixel(*canvas, QPoint(140, 90));
  CHECK(selected_after.red() > selected_before.red() + 60);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 90)), selected_before, 3));
  CHECK(layer_list->count() == layer_count_before);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_curves_selection", *canvas);
}

void ui_curves_editor_points_channels_keyboard_auto_and_reset() {
  patchy::PixelBuffer rgb_source(2, 1, patchy::PixelFormat::rgb8());
  auto* transparent_pixel = rgb_source.pixel(0, 0);
  transparent_pixel[0] = 10;
  transparent_pixel[1] = 20;
  transparent_pixel[2] = 30;
  auto* opaque_pixel = rgb_source.pixel(1, 0);
  opaque_pixel[0] = 40;
  opaque_pixel[1] = 80;
  opaque_pixel[2] = 120;
  const std::array<std::uint8_t, 2> external_alpha{0, 255};
  const auto external_alpha_histograms =
      patchy::ui::curves_histograms_from_pixels(&rgb_source, std::span<const std::uint8_t>(external_alpha));
  CHECK(external_alpha_histograms.red[10] == 0U);
  CHECK(external_alpha_histograms.green[20] == 0U);
  CHECK(external_alpha_histograms.blue[30] == 0U);
  CHECK(external_alpha_histograms.red[40] == 1U);
  CHECK(external_alpha_histograms.green[80] == 1U);
  CHECK(external_alpha_histograms.blue[120] == 1U);
  const std::array<std::uint8_t, 1> mismatched_alpha{255};
  const auto mismatched_histograms =
      patchy::ui::curves_histograms_from_pixels(&rgb_source, std::span<const std::uint8_t>(mismatched_alpha));
  CHECK(std::accumulate(mismatched_histograms.rgb.begin(), mismatched_histograms.rgb.end(), std::uint64_t{0}) ==
        0U);
  CHECK(std::accumulate(mismatched_histograms.red.begin(), mismatched_histograms.red.end(), std::uint64_t{0}) ==
        0U);

  patchy::ui::CurvesEditorWidget editor;
  editor.resize(460, 520);

  patchy::ui::CurvesHistograms histograms;
  histograms.rgb[20] = 10;
  histograms.rgb[220] = 10;
  histograms.red[16] = 8;
  histograms.red[236] = 8;
  editor.set_histograms(histograms);

  patchy::CurvesAdjustment accepted;
  int change_count = 0;
  int finished_count = 0;
  editor.adjustment_changed = [&](const patchy::CurvesAdjustment& adjustment, bool finished) {
    accepted = adjustment;
    ++change_count;
    if (finished) {
      ++finished_count;
    }
    editor.set_adjustment(adjustment);
  };

  editor.show();
  QApplication::processEvents();
  auto* graph = editor.findChild<QWidget*>(QStringLiteral("curvesGraph"));
  auto* tabs = editor.findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
  auto* input = editor.findChild<QSpinBox*>(QStringLiteral("curvesInputSpin"));
  auto* output = editor.findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
  auto* auto_button = editor.findChild<QPushButton*>(QStringLiteral("curvesAutoButton"));
  auto* reset_button = editor.findChild<QPushButton*>(QStringLiteral("curvesResetButton"));
  CHECK(graph != nullptr);
  CHECK(tabs != nullptr);
  CHECK(input != nullptr);
  CHECK(output != nullptr);
  CHECK(auto_button != nullptr);
  CHECK(reset_button != nullptr);
  CHECK(auto_button->isEnabled());

  click_curves_graph(*graph, 128, 200);
  CHECK(accepted.rgb.size() == 3U);
  CHECK(std::abs(input->value() - 128) <= 1);
  CHECK(std::abs(output->value() - 200) <= 1);
  send_key(*graph, Qt::Key_Up, Qt::ShiftModifier);
  CHECK(output->value() >= 209);
  input->setValue(120);
  output->setValue(205);
  const patchy::CurveControlPoint edited_rgb_point{120, 205};
  CHECK(accepted.rgb[1] == edited_rgb_point);
  send_key(*graph, Qt::Key_PageDown);
  CHECK(input->value() == 255);
  send_key(*graph, Qt::Key_PageUp);
  CHECK(input->value() == 120);

  graph->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto selected_before_tab = editor.selected_point();
  send_key(*graph, Qt::Key_Tab);
  CHECK(editor.selected_point() == selected_before_tab);
  CHECK(!graph->hasFocus());

  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  click_curves_graph(*graph, 80, 170);
  CHECK(accepted.red.size() == 3U);
  CHECK(accepted.rgb.size() == 3U);
  send_key(*graph, Qt::Key_Delete);
  CHECK(accepted.red.size() == 2U);

  patchy::CurvesAdjustment display_curves;
  display_curves.rgb = {{0, 12}, {64, 35}, {128, 205}, {255, 248}};
  display_curves.red = {{0, 0}, {80, 210}, {255, 255}};
  display_curves.green = {{0, 25}, {150, 60}, {255, 250}};
  display_curves.blue = {{0, 255}, {120, 170}, {255, 0}};
  editor.set_adjustment(display_curves);
  accepted = editor.adjustment();
  tabs->setCurrentIndex(0);
  QApplication::processEvents();

  drag(*graph, curves_graph_position(*graph, 0, 12), curves_graph_position(*graph, 22, 42));
  CHECK(std::abs(accepted.rgb.front().input - 22) <= 1);
  CHECK(std::abs(accepted.rgb.front().output - 42) <= 1);

  reset_button->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto graph_image = graph->grab().toImage();
  int red_curve_pixels = 0;
  int green_curve_pixels = 0;
  int blue_curve_pixels = 0;
  for (int y = 0; y < graph_image.height(); ++y) {
    for (int x = 0; x < graph_image.width(); ++x) {
      const auto color = graph_image.pixelColor(x, y);
      if (color.red() > 85 && color.red() > color.green() + 25 && color.red() > color.blue() + 25) {
        ++red_curve_pixels;
      }
      if (color.green() > 85 && color.green() > color.red() + 20 && color.green() > color.blue() + 15) {
        ++green_curve_pixels;
      }
      if (color.blue() > 85 && color.blue() > color.red() + 20 && color.blue() > color.green() + 15) {
        ++blue_curve_pixels;
      }
    }
  }
  CHECK(red_curve_pixels > 20);
  CHECK(green_curve_pixels > 20);
  CHECK(blue_curve_pixels > 20);
  editor.grab().save(QStringLiteral("test-artifacts/ui_curves_editor.png"));

  patchy::CurvesAdjustment capacity_curves;
  capacity_curves.rgb.clear();
  for (int index = 0; index < 17; ++index) {
    const auto value = index * 15;
    capacity_curves.rgb.push_back({value, value});
  }
  capacity_curves.rgb.push_back({255, 255});
  editor.set_adjustment(capacity_curves);
  accepted = editor.adjustment();
  CHECK(accepted.rgb.size() == 18U);
  click_curves_graph(*graph, 248, 40);
  CHECK(accepted.rgb.size() == 19U);
  const auto at_capacity = accepted.rgb;
  click_curves_graph(*graph, 246, 140);
  CHECK(accepted.rgb == at_capacity);

  auto_button->click();
  CHECK(accepted.rgb == patchy::CurveControlPoints({{20, 0}, {220, 255}}));
  CHECK(finished_count > 0);

  reset_button->click();
  const patchy::CurveControlPoints identity{{0, 0}, {255, 255}};
  CHECK(accepted.rgb == identity);
  CHECK(accepted.red == identity);
  CHECK(accepted.green == identity);
  CHECK(accepted.blue == identity);
  CHECK(change_count >= 9);
  editor.hide();
}

void ui_curves_transient_canvas_read_is_non_mutating() {
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Sample", solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(90, 120, 150)));
  const auto layer_id = document.layers().front().id();
  const auto* original_layer = std::as_const(document).find_layer(layer_id);
  CHECK(original_layer != nullptr);
  const auto original_render_revision = original_layer->render_revision();
  const auto original_content_revision = original_layer->content_revision();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 240);
  canvas.set_document(&document);
  canvas.fit_to_view();
  canvas.set_edit_locked(true);
  canvas.set_primary_color(QColor(12, 34, 56));
  canvas.show();
  QApplication::processEvents();

  std::vector<patchy::ui::CanvasReadPhase> phases;
  canvas.set_transient_read_interaction(
      [&](const patchy::ui::CanvasReadGesture& gesture) { phases.push_back(gesture.phase); }, Qt::CrossCursor);
  const auto from = canvas.widget_position_for_document_point(QPoint(8, 8));
  const auto to = canvas.widget_position_for_document_point(QPoint(12, 5));
  drag(canvas, from, to);
  CHECK(phases.size() == 3U);
  CHECK(phases[0] == patchy::ui::CanvasReadPhase::Press);
  CHECK(phases[1] == patchy::ui::CanvasReadPhase::Drag);
  CHECK(phases[2] == patchy::ui::CanvasReadPhase::Release);
  send_key(canvas, Qt::Key_Escape);
  CHECK(phases.size() == 4U);
  CHECK(phases[3] == patchy::ui::CanvasReadPhase::Dismiss);
  CHECK(canvas.primary_color() == QColor(12, 34, 56));
  CHECK(canvas.has_transient_read_interaction());
  const auto* unchanged_layer = std::as_const(document).find_layer(layer_id);
  CHECK(unchanged_layer != nullptr);
  CHECK(unchanged_layer->render_revision() == original_render_revision);
  CHECK(unchanged_layer->content_revision() == original_content_revision);

  canvas.set_curves_clipping_preview(patchy::ui::CurvesClippingMode::Both,
                                     patchy::CurvesChannel::Rgb);
  CHECK(canvas.curves_clipping_preview_mode() == patchy::ui::CurvesClippingMode::Both);
  canvas.set_curves_clipping_preview(std::nullopt);
  CHECK(!canvas.curves_clipping_preview_mode().has_value());
  unchanged_layer = std::as_const(document).find_layer(layer_id);
  CHECK(unchanged_layer != nullptr);
  CHECK(unchanged_layer->render_revision() == original_render_revision);
  CHECK(unchanged_layer->content_revision() == original_content_revision);

  phases.clear();
  send_mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  canvas.clear_transient_read_interaction();
  CHECK(phases.size() == 2U);
  CHECK(phases[0] == patchy::ui::CanvasReadPhase::Press);
  CHECK(phases[1] == patchy::ui::CanvasReadPhase::Cancel);
  CHECK(!canvas.has_transient_read_interaction());
}

void ui_curves_canvas_tools_before_and_clipping_hooks() {
  patchy::ui::CurvesDialogHooks hooks;
  patchy::ui::CurvesCanvasMode active_mode = patchy::ui::CurvesCanvasMode::None;
  std::function<void(const patchy::ui::CurvesCanvasSample&)> sample_changed;
  int clear_count = 0;
  std::vector<std::pair<std::optional<patchy::ui::CurvesClippingMode>, patchy::CurvesChannel>> clipping_calls;
  hooks.set_canvas_mode = [&](patchy::ui::CurvesCanvasMode mode, auto callback) {
    active_mode = mode;
    sample_changed = std::move(callback);
  };
  hooks.clear_canvas_mode = [&] {
    active_mode = patchy::ui::CurvesCanvasMode::None;
    sample_changed = {};
    ++clear_count;
  };
  hooks.clipping_changed = [&](std::optional<patchy::ui::CurvesClippingMode> mode,
                               patchy::CurvesChannel channel) {
    clipping_calls.emplace_back(mode, channel);
  };

  std::vector<bool> preview_states;
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* editor = dialog->findChild<patchy::ui::CurvesEditorWidget*>(QStringLiteral("curvesEditor"));
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
    auto* targeted = dialog->findChild<QPushButton*>(QStringLiteral("curvesTargetedAdjustmentButton"));
    auto* black = dialog->findChild<QPushButton*>(QStringLiteral("curvesBlackPointButton"));
    auto* gray = dialog->findChild<QPushButton*>(QStringLiteral("curvesGrayPointButton"));
    auto* white = dialog->findChild<QPushButton*>(QStringLiteral("curvesWhitePointButton"));
    auto* reset = dialog->findChild<QPushButton*>(QStringLiteral("curvesResetButton"));
    auto* before = dialog->findChild<QPushButton*>(QStringLiteral("curvesBeforeButton"));
    auto* clipping = dialog->findChild<QPushButton*>(QStringLiteral("curvesClippingPreviewButton"));
    CHECK(editor != nullptr);
    CHECK(tabs != nullptr);
    CHECK(targeted != nullptr && targeted->isEnabled());
    CHECK(black != nullptr && black->isEnabled());
    CHECK(gray != nullptr && gray->isEnabled());
    CHECK(white != nullptr && white->isEnabled());
    CHECK(reset != nullptr);
    CHECK(before != nullptr && before->isEnabled());
    CHECK(clipping != nullptr && clipping->isEnabled());

    const auto send_sample = [&](QColor color, patchy::ui::CanvasReadPhase phase, int global_y) {
      CHECK(static_cast<bool>(sample_changed));
      const auto callback = sample_changed;
      callback(patchy::ui::CurvesCanvasSample{
          color, patchy::ui::CanvasReadGesture{QPoint(10, 10), QPoint(200, global_y), Qt::NoModifier, phase}});
    };

    targeted->click();
    CHECK(active_mode == patchy::ui::CurvesCanvasMode::Targeted);
    send_sample(QColor(100, 120, 140), patchy::ui::CanvasReadPhase::Press, 100);
    send_sample({}, patchy::ui::CanvasReadPhase::Drag, 70);
    send_sample({}, patchy::ui::CanvasReadPhase::Release, 70);
    const auto targeted_points = editor->adjustment().rgb;
    const auto targeted_point = std::find_if(targeted_points.begin(), targeted_points.end(),
                                             [](const patchy::CurveControlPoint& point) {
                                               return point.input == 117;
                                             });
    CHECK(targeted_point != targeted_points.end());
    CHECK(targeted_point->output == 147);

    black->click();
    CHECK(active_mode == patchy::ui::CurvesCanvasMode::BlackPoint);
    send_sample(QColor(20, 30, 40), patchy::ui::CanvasReadPhase::Press, 100);
    white->click();
    send_sample(QColor(220, 230, 240), patchy::ui::CanvasReadPhase::Press, 100);
    gray->click();
    send_sample(QColor(100, 110, 120), patchy::ui::CanvasReadPhase::Press, 100);
    CHECK(editor->adjustment().red == patchy::CurveControlPoints({{20, 0}, {100, 128}, {220, 255}}));
    CHECK(editor->adjustment().green == patchy::CurveControlPoints({{30, 0}, {110, 128}, {230, 255}}));
    CHECK(editor->adjustment().blue == patchy::CurveControlPoints({{40, 0}, {120, 128}, {240, 255}}));

    black->click();
    send_sample(QColor(20, 30, 40), patchy::ui::CanvasReadPhase::Press, 100);
    reset->click();
    white->click();
    send_sample(QColor(220, 230, 240), patchy::ui::CanvasReadPhase::Press, 100);
    CHECK(editor->adjustment().red == patchy::CurveControlPoints({{0, 0}, {220, 255}}));
    CHECK(editor->adjustment().green == patchy::CurveControlPoints({{0, 0}, {230, 255}}));
    CHECK(editor->adjustment().blue == patchy::CurveControlPoints({{0, 0}, {240, 255}}));

    preview_states.clear();
    const auto center = before->rect().center();
    send_mouse(*before, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*before, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    CHECK(preview_states.size() >= 2U);
    CHECK(std::find(preview_states.begin(), preview_states.end(), false) != preview_states.end());
    CHECK(preview_states.back());

    clipping->click();
    CHECK(!clipping_calls.empty());
    CHECK(clipping_calls.back().first == patchy::ui::CurvesClippingMode::Both);
    CHECK(clipping_calls.back().second == patchy::CurvesChannel::Rgb);
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    CHECK(clipping_calls.back().first == patchy::ui::CurvesClippingMode::Both);
    CHECK(clipping_calls.back().second == patchy::CurvesChannel::Red);
    clipping->click();
    CHECK(!clipping_calls.back().first.has_value());

    inspected = true;
    send_sample({}, patchy::ui::CanvasReadPhase::Dismiss, 100);
  });

  const auto result = patchy::ui::request_curves_settings(
      nullptr,
      [&](bool enabled, const patchy::ui::CurvesSettings&) { preview_states.push_back(enabled); },
      {}, {}, hooks);
  CHECK(!result.has_value());
  CHECK(inspected);
  CHECK(clear_count >= 1);
  CHECK(active_mode == patchy::ui::CurvesCanvasMode::None);
  CHECK(!clipping_calls.empty());
  CHECK(!clipping_calls.back().first.has_value());
}

void ui_curves_canvas_sampler_reads_below_preview_and_escape_closes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(20, 30, 40));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto base_id = document.active_layer_id();
  CHECK(base_id.has_value());
  const auto* base = std::as_const(document).find_layer(*base_id);
  CHECK(base != nullptr);
  const auto base_render_revision = base->render_revision();
  const auto base_content_revision = base->content_revision();
  const auto layer_count_before = document.layers().size();
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto primary_before = canvas->primary_color();

  bool sampled_original_input = false;
  bool escape_closed_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* editor = dialog->findChild<patchy::ui::CurvesEditorWidget*>(QStringLiteral("curvesEditor"));
    auto* black = dialog->findChild<QPushButton*>(QStringLiteral("curvesBlackPointButton"));
    CHECK(graph != nullptr);
    CHECK(editor != nullptr);
    CHECK(black != nullptr && black->isEnabled());

    // Make the live adjustment visibly remap the sampled tone. The canvas
    // sampler must still read the unadjusted layer below the preview layer.
    click_curves_graph(*graph, 20, 220);
    process_events_for(200);
    black->click();
    CHECK(canvas->has_transient_read_interaction());
    const auto canvas_point = canvas->widget_position_for_document_point(QPoint(80, 80));
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas_point, Qt::LeftButton, Qt::NoButton);
    const auto sampled = editor->adjustment();
    sampled_original_input = sampled.red.front().input == 20 && sampled.green.front().input == 30 &&
                             sampled.blue.front().input == 40;

    send_key(*canvas, Qt::Key_Escape);
    escape_closed_dialog = !dialog->isVisible();
  });
  require_action(window, "layerNewCurvesAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(sampled_original_input);
  CHECK(escape_closed_dialog);
  CHECK(!canvas->has_transient_read_interaction());
  CHECK(canvas->primary_color() == primary_before);
  CHECK(document.layers().size() == layer_count_before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  base = std::as_const(document).find_layer(*base_id);
  CHECK(base != nullptr);
  CHECK(base->render_revision() == base_render_revision);
  CHECK(base->content_revision() == base_content_revision);
}

void ui_curves_preset_thumbnails_apply_all_channels_and_track_custom() {
  const auto presets = patchy::ui::builtin_curves_presets();
  CHECK(presets.size() == 8U);
  const std::array<QString, 8> expected_ids{
      QStringLiteral("curves.default"),
      QStringLiteral("curves.medium_contrast"),
      QStringLiteral("curves.strong_contrast"),
      QStringLiteral("curves.lift_shadows"),
      QStringLiteral("curves.recover_highlights"),
      QStringLiteral("curves.matte"),
      QStringLiteral("curves.warm"),
      QStringLiteral("curves.cool"),
  };
  for (std::size_t index = 0; index < presets.size(); ++index) {
    CHECK(presets[index].id == expected_ids[index]);
    CHECK(patchy::ui::find_curves_preset(presets[index].id) == &presets[index]);
    CHECK(patchy::ui::find_curves_preset(presets[index].adjustment) == &presets[index]);
    for (const auto channel : {patchy::CurvesChannel::Rgb, patchy::CurvesChannel::Red,
                               patchy::CurvesChannel::Green, patchy::CurvesChannel::Blue}) {
      const auto& channel_points = patchy::curve_points_for_channel(presets[index].adjustment, channel);
      CHECK(channel_points.size() >= 2U);
      CHECK(channel_points.size() <= 19U);
      CHECK(patchy::normalized_curve_control_points(channel_points) == channel_points);
    }
    const auto thumbnail = patchy::ui::curves_adjustment_thumbnail(presets[index].adjustment);
    CHECK(!thumbnail.isNull());
    CHECK(thumbnail.size() == QSize(72, 48));
  }
  const auto default_thumbnail = patchy::ui::curves_adjustment_thumbnail(presets[0].adjustment);
  const auto strong_thumbnail = patchy::ui::curves_adjustment_thumbnail(presets[2].adjustment);
  const auto warm_thumbnail = patchy::ui::curves_adjustment_thumbnail(presets[6].adjustment);
  const auto cool_thumbnail = patchy::ui::curves_adjustment_thumbnail(presets[7].adjustment);
  CHECK(default_thumbnail != strong_thumbnail);
  CHECK(warm_thumbnail != cool_thumbnail);

  std::optional<patchy::ui::CurvesSettings> result;
  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* preset_list = dialog->findChild<QListWidget*>(QStringLiteral("curvesPresetList"));
    auto* editor = dialog->findChild<patchy::ui::CurvesEditorWidget*>(QStringLiteral("curvesEditor"));
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    CHECK(preset_list != nullptr);
    CHECK(editor != nullptr);
    CHECK(graph != nullptr);
    CHECK(preset_list->count() == static_cast<int>(presets.size()) + 1);
    CHECK(preset_list->currentItem() != nullptr);
    CHECK(preset_list->currentItem()->data(Qt::UserRole + 1).toString() == QStringLiteral("curves.default"));

    QListWidgetItem* warm_item = nullptr;
    QListWidgetItem* strong_item = nullptr;
    for (int row = 0; row < preset_list->count(); ++row) {
      auto* item = preset_list->item(row);
      CHECK(item != nullptr);
      if (row > 0) {
        CHECK(!item->icon().isNull());
      }
      if (item->data(Qt::UserRole + 1).toString() == QStringLiteral("curves.warm")) {
        warm_item = item;
      }
      if (item->data(Qt::UserRole + 1).toString() == QStringLiteral("curves.strong_contrast")) {
        strong_item = item;
      }
    }
    CHECK(warm_item != nullptr);
    CHECK(strong_item != nullptr);

    preset_list->setCurrentItem(warm_item);
    QApplication::processEvents();
    CHECK(editor->adjustment() == presets[6].adjustment);
    CHECK(editor->adjustment().red != editor->adjustment().blue);

    click_curves_graph(*graph, 108, 183);
    QApplication::processEvents();
    CHECK(preset_list->currentItem() == preset_list->item(0));
    CHECK(preset_list->currentItem()->data(Qt::UserRole + 1).toString().isEmpty());
    CHECK(preset_list->currentItem()->text() == QStringLiteral("Custom"));
    CHECK(!preset_list->currentItem()->icon().isNull());

    preset_list->setCurrentItem(strong_item);
    QApplication::processEvents();
    CHECK(editor->adjustment() == presets[2].adjustment);
    dialog->grab().save(QStringLiteral("test-artifacts/ui_curves_presets.png"));
    inspected = true;
    dialog->accept();
  });
  result = patchy::ui::request_curves_settings(nullptr);
  CHECK(inspected);
  CHECK(result.has_value());
  CHECK(*result == presets[2].adjustment);
}

void ui_curves_destructive_and_adjustment_luts_match() {
  patchy::PixelBuffer original(256, 1, patchy::PixelFormat::rgba8());
  for (int value = 0; value < 256; ++value) {
    auto* pixel = original.pixel(value, 0);
    pixel[0] = static_cast<std::uint8_t>(value);
    pixel[1] = static_cast<std::uint8_t>(255 - value);
    pixel[2] = static_cast<std::uint8_t>((value * 37) & 0xff);
    pixel[3] = static_cast<std::uint8_t>((value * 53) & 0xff);
  }

  patchy::CurvesAdjustment curves;
  curves.rgb = {{0, 0}, {48, 22}, {128, 204}, {220, 214}, {255, 255}};
  curves.red = {{8, 0}, {92, 148}, {248, 255}};
  curves.green = {{0, 6}, {180, 158}, {255, 249}};
  curves.blue = {{0, 255}, {116, 170}, {255, 0}};

  auto destructive = original;
  patchy::ui::apply_curves_to_pixels(destructive, patchy::Rect::from_size(256, 1), QRegion(), curves);

  auto adjustment_pixels = original;
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Curves;
  settings.curves = curves;
  patchy::apply_adjustment_to_pixels(adjustment_pixels, settings);
  CHECK(destructive.data().size() == adjustment_pixels.data().size());
  CHECK(std::equal(destructive.data().begin(), destructive.data().end(), adjustment_pixels.data().begin()));
  for (int value = 0; value < 256; ++value) {
    CHECK(destructive.pixel(value, 0)[3] == original.pixel(value, 0)[3]);
  }
}

void ui_curves_acv_load_save_updates_editor_and_preview() {
  ensure_artifact_dir();
  const auto input_path =
      QFileInfo(QStringLiteral("test-artifacts/ui-curves-load.acv")).absoluteFilePath();
  const auto output_without_suffix =
      QFileInfo(QStringLiteral("test-artifacts/ui-curves-saved")).absoluteFilePath();
  const auto output_path = output_without_suffix + QStringLiteral(".acv");
  QFile::remove(input_path);
  QFile::remove(output_path);

  patchy::CurvesAdjustment expected;
  expected.rgb = {{0, 8}, {64, 35}, {128, 190}, {255, 245}};
  expected.red = {{0, 20}, {80, 45}, {160, 225}, {255, 230}};
  expected.green = {{0, 4}, {96, 120}, {220, 238}, {255, 250}};
  expected.blue = {{0, 250}, {112, 160}, {255, 5}};
  patchy::acv::write_file(std::filesystem::path(input_path.toStdU16String()), expected);

  QWidget parent;
  parent.show();
  QApplication::processEvents();
  int preview_count = 0;
  bool preview_enabled = false;
  patchy::CurvesAdjustment preview_curves;
  bool saw_load_dialog = false;
  bool saw_save_dialog = false;
  bool loaded_editor = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyCurvesDialog")));
    CHECK(dialog != nullptr);
    auto* load = dialog->findChild<QPushButton*>(QStringLiteral("curvesLoadPresetButton"));
    auto* save = dialog->findChild<QPushButton*>(QStringLiteral("curvesSavePresetButton"));
    auto* editor = dialog->findChild<patchy::ui::CurvesEditorWidget*>(QStringLiteral("curvesEditor"));
    CHECK(load != nullptr);
    CHECK(save != nullptr);
    CHECK(editor != nullptr);
    CHECK(load->text() == QStringLiteral("Load..."));
    CHECK(save->text() == QStringLiteral("Save..."));

    QTimer::singleShot(0, [&] {
      auto* file_dialog = qobject_cast<QFileDialog*>(
          find_top_level_dialog(QStringLiteral("curvesPresetOpenFileDialog")));
      CHECK(file_dialog != nullptr);
      CHECK(file_dialog->nameFilters().contains(QStringLiteral("Photoshop Curves Preset (*.acv)")));
      saw_load_dialog = true;
      file_dialog->selectFile(input_path);
      static_cast<QDialog*>(file_dialog)->accept();
    });
    load->click();
    loaded_editor = editor->adjustment() == expected;

    QTimer::singleShot(0, [&] {
      auto* file_dialog = qobject_cast<QFileDialog*>(
          find_top_level_dialog(QStringLiteral("curvesPresetSaveFileDialog")));
      CHECK(file_dialog != nullptr);
      saw_save_dialog = true;
      file_dialog->selectFile(output_without_suffix);
      static_cast<QDialog*>(file_dialog)->accept();
    });
    save->click();
    dialog->accept();
  });

  const auto accepted = patchy::ui::request_curves_settings(
      &parent,
      [&](bool enabled, const patchy::ui::CurvesSettings& settings) {
        ++preview_count;
        preview_enabled = enabled;
        preview_curves = settings;
      });
  CHECK(accepted.has_value());
  CHECK(*accepted == expected);
  CHECK(saw_load_dialog);
  CHECK(saw_save_dialog);
  CHECK(loaded_editor);
  CHECK(preview_count > 0);
  CHECK(preview_enabled);
  CHECK(preview_curves == expected);
  CHECK(QFileInfo::exists(output_path));
  CHECK(patchy::acv::read_file(std::filesystem::path(output_path.toStdU16String())) == expected);
}

void ui_curves_dialog_preview_toggle_and_cancel_restore_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(128, 128, 128));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto original = canvas_pixel(*canvas, QPoint(80, 80));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active_layer_id = document.active_layer_id();
  CHECK(active_layer_id.has_value());
  const auto* original_layer = std::as_const(document).find_layer(*active_layer_id);
  CHECK(original_layer != nullptr);
  const auto original_render_revision = original_layer->render_revision();
  const auto original_content_revision = original_layer->content_revision();
  const auto original_bounds = original_layer->bounds();
  const auto original_metadata = original_layer->metadata();
  const auto original_pixels = original_layer->pixels();
  const auto layer_is_exactly_original = [&] {
    const auto* current = std::as_const(document).find_layer(*active_layer_id);
    const auto current_bounds = current == nullptr ? patchy::Rect{} : current->bounds();
    if (current == nullptr || current->render_revision() != original_render_revision ||
        current->content_revision() != original_content_revision || current_bounds.x != original_bounds.x ||
        current_bounds.y != original_bounds.y || current_bounds.width != original_bounds.width ||
        current_bounds.height != original_bounds.height ||
        current->metadata() != original_metadata || current->pixels().data().size() != original_pixels.data().size()) {
      return false;
    }
    return std::equal(current->pixels().data().begin(), current->pixels().data().end(),
                      original_pixels.data().begin());
  };

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layer_count_before = layer_list->count();
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool preview_changed_pixels = false;
  bool preview_off_restored = false;
  bool preview_off_restored_exact_layer = false;
  bool preview_reenabled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("curvesPreviewCheck"));
    CHECK(graph != nullptr);
    CHECK(preview != nullptr);
    click_curves_graph(*graph, 128, 235);
    process_events_for(300);
    preview_changed_pixels = canvas_pixel(*canvas, QPoint(80, 80)).red() > original.red() + 70;

    preview->setChecked(false);
    process_events_for(200);
    preview_off_restored = color_close(canvas_pixel(*canvas, QPoint(80, 80)), original, 2);
    preview_off_restored_exact_layer = layer_is_exactly_original();

    preview->setChecked(true);
    process_events_for(300);
    preview_reenabled = canvas_pixel(*canvas, QPoint(80, 80)).red() > original.red() + 70;
    dialog->reject();
  });
  require_action(window, "imageAdjustCurvesAction")->trigger();
  QApplication::processEvents();

  CHECK(preview_changed_pixels);
  CHECK(preview_off_restored);
  CHECK(preview_off_restored_exact_layer);
  CHECK(preview_reenabled);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), original, 2));
  CHECK(layer_is_exactly_original());
  CHECK(layer_list->count() == layer_count_before);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
}

void ui_curves_destructive_apply_cancel_restores_exact_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(112, 136, 172));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active_layer_id = document.active_layer_id();
  CHECK(active_layer_id.has_value());
  const auto* original_layer = std::as_const(document).find_layer(*active_layer_id);
  CHECK(original_layer != nullptr);
  const auto original_render_revision = original_layer->render_revision();
  const auto original_content_revision = original_layer->content_revision();
  const auto original_bounds = original_layer->bounds();
  const auto original_metadata = original_layer->metadata();
  const auto original_pixels = original_layer->pixels();
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool saw_progress_dialog = false;
  QTimer progress_canceller;
  progress_canceller.setInterval(0);
  QObject::connect(&progress_canceller, &QTimer::timeout, [&] {
    auto* progress =
        qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("adjustmentProgressDialog")));
    if (progress == nullptr) {
      return;
    }
    saw_progress_dialog = true;
    progress->cancel();
    progress_canceller.stop();
  });

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    CHECK(graph != nullptr);
    click_curves_graph(*graph, 128, 230);
    progress_canceller.start();
    dialog->accept();
  });
  require_action(window, "imageAdjustCurvesAction")->trigger();
  progress_canceller.stop();
  QApplication::processEvents();

  CHECK(saw_progress_dialog);
  const auto* restored_layer = std::as_const(document).find_layer(*active_layer_id);
  CHECK(restored_layer != nullptr);
  CHECK(restored_layer->render_revision() == original_render_revision);
  CHECK(restored_layer->content_revision() == original_content_revision);
  const auto restored_bounds = restored_layer->bounds();
  CHECK(restored_bounds.x == original_bounds.x);
  CHECK(restored_bounds.y == original_bounds.y);
  CHECK(restored_bounds.width == original_bounds.width);
  CHECK(restored_bounds.height == original_bounds.height);
  CHECK(restored_layer->metadata() == original_metadata);
  CHECK(restored_layer->pixels().data().size() == original_pixels.data().size());
  CHECK(std::equal(restored_layer->pixels().data().begin(), restored_layer->pixels().data().end(),
                   original_pixels.data().begin()));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
}

void ui_curves_adjustment_layer_preserves_rich_channels_and_undoes_one_edit() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);

  canvas->set_primary_color(QColor(96, 128, 168));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const auto undo_before_create = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool create_auto_enabled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
    auto* auto_button = dialog->findChild<QPushButton*>(QStringLiteral("curvesAutoButton"));
    CHECK(graph != nullptr);
    CHECK(tabs != nullptr);
    CHECK(auto_button != nullptr);
    create_auto_enabled = auto_button->isEnabled();

    click_curves_graph(*graph, 128, 190);
    tabs->setCurrentIndex(1);
    click_curves_graph(*graph, 96, 210);
    tabs->setCurrentIndex(2);
    click_curves_graph(*graph, 144, 72);
    tabs->setCurrentIndex(3);
    click_curves_graph(*graph, 200, 230);
    dialog->accept();
  });
  require_action(window, "layerNewCurvesAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(create_auto_enabled);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_create + 1U);
  const auto adjustment_id = document.active_layer_id();
  CHECK(adjustment_id.has_value());
  const auto read_curves = [&document, adjustment_id] {
    const auto* layer = std::as_const(document).find_layer(*adjustment_id);
    CHECK(layer != nullptr);
    CHECK(layer->kind() == patchy::LayerKind::Adjustment);
    const auto settings = patchy::adjustment_settings_from_layer(*layer);
    CHECK(settings.has_value());
    CHECK(settings->kind == patchy::AdjustmentKind::Curves);
    return settings->curves;
  };

  const auto created_curves = read_curves();
  CHECK(created_curves.rgb.size() == 3U);
  CHECK(created_curves.red.size() == 3U);
  CHECK(created_curves.green.size() == 3U);
  CHECK(created_curves.blue.size() == 3U);
  const auto created_pixel = canvas_pixel(*canvas, QPoint(80, 80));
  const auto undo_before_cancel = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool edit_auto_enabled = false;
  bool cancel_loaded_rich_point = false;
  bool cancel_preview_changed = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
    auto* input = dialog->findChild<QSpinBox*>(QStringLiteral("curvesInputSpin"));
    auto* output = dialog->findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
    auto* auto_button = dialog->findChild<QPushButton*>(QStringLiteral("curvesAutoButton"));
    CHECK(graph != nullptr);
    CHECK(tabs != nullptr);
    CHECK(input != nullptr);
    CHECK(output != nullptr);
    CHECK(auto_button != nullptr);
    edit_auto_enabled = auto_button->isEnabled();

    tabs->setCurrentIndex(1);
    const auto red_point = created_curves.red[1];
    click_curves_graph(*graph, red_point.input, red_point.output);
    cancel_loaded_rich_point = input->value() == red_point.input && output->value() == red_point.output;
    output->setValue(32);
    process_events_for(120);
    cancel_preview_changed = !color_close(canvas_pixel(*canvas, QPoint(80, 80)), created_pixel, 2);
    dialog->reject();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(edit_auto_enabled);
  CHECK(cancel_loaded_rich_point);
  CHECK(cancel_preview_changed);
  CHECK(read_curves() == created_curves);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), created_pixel, 2));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_cancel);

  const auto undo_before_edit = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accept_loaded_rich_point = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
    auto* input = dialog->findChild<QSpinBox*>(QStringLiteral("curvesInputSpin"));
    auto* output = dialog->findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
    CHECK(graph != nullptr);
    CHECK(tabs != nullptr);
    CHECK(input != nullptr);
    CHECK(output != nullptr);

    tabs->setCurrentIndex(3);
    const auto blue_point = created_curves.blue[1];
    click_curves_graph(*graph, blue_point.input, blue_point.output);
    accept_loaded_rich_point = input->value() == blue_point.input && output->value() == blue_point.output;
    output->setValue(40);
    dialog->accept();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(accept_loaded_rich_point);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_edit + 1U);
  const auto edited_curves = read_curves();
  CHECK(edited_curves.rgb == created_curves.rgb);
  CHECK(edited_curves.red == created_curves.red);
  CHECK(edited_curves.green == created_curves.green);
  CHECK(edited_curves.blue.size() == 3U);
  CHECK(edited_curves.blue[1].input == created_curves.blue[1].input);
  CHECK(edited_curves.blue[1].output == 40);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(read_curves() == created_curves);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), created_pixel, 2));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_edit);
}

void ui_curves_legacy_metadata_cancel_and_undo_restore_exact_map() {
  patchy::Document legacy_document(96, 72, patchy::PixelFormat::rgba8());
  legacy_document.add_pixel_layer(
      "Base", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(86, 126, 168)));
  patchy::Layer legacy_curves(legacy_document.allocate_layer_id(), "Legacy Curves",
                              patchy::LayerKind::Adjustment);
  legacy_curves.set_bounds(patchy::Rect::from_size(96, 72));
  legacy_curves.metadata()[patchy::kLayerMetadataAdjustmentType] = "curves";
  legacy_curves.metadata()[patchy::kLayerMetadataAdjustmentCurvesShadowOutput] = "14";
  legacy_curves.metadata()[patchy::kLayerMetadataAdjustmentCurvesMidtoneOutput] = "171";
  legacy_curves.metadata()[patchy::kLayerMetadataAdjustmentCurvesHighlightOutput] = "241";
  legacy_curves.metadata()["patchy.test.legacy_curves_sentinel"] = "leave this metadata untouched";
  const auto adjustment_id = legacy_curves.id();
  legacy_document.add_layer(std::move(legacy_curves));
  legacy_document.set_active_layer(adjustment_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(legacy_document), QStringLiteral("Legacy Curves Metadata"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto metadata_for_adjustment = [&document, adjustment_id] {
    const auto* layer = std::as_const(document).find_layer(adjustment_id);
    CHECK(layer != nullptr);
    return layer->metadata();
  };
  const auto original_metadata = metadata_for_adjustment();
  CHECK(original_metadata.size() == 5U);
  CHECK(!original_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRgbPoints));
  CHECK(!original_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRedPoints));
  CHECK(!original_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesGreenPoints));
  CHECK(!original_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesBluePoints));

  const auto undo_before_cancel = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool loaded_legacy_midpoint = false;
  bool preview_added_rich_metadata = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* input = dialog->findChild<QSpinBox*>(QStringLiteral("curvesInputSpin"));
    auto* output = dialog->findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
    CHECK(graph != nullptr);
    CHECK(input != nullptr);
    CHECK(output != nullptr);

    click_curves_graph(*graph, 128, 171);
    loaded_legacy_midpoint = input->value() == 128 && output->value() == 171;
    output->setValue(218);
    process_events_for(120);
    const auto preview_metadata = metadata_for_adjustment();
    preview_added_rich_metadata =
        preview_metadata != original_metadata &&
        preview_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRgbPoints) &&
        preview_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRedPoints) &&
        preview_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesGreenPoints) &&
        preview_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesBluePoints);
    dialog->reject();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(loaded_legacy_midpoint);
  CHECK(preview_added_rich_metadata);
  CHECK(metadata_for_adjustment() == original_metadata);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_cancel);

  const auto undo_before_accept = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
    auto* tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
    auto* output = dialog->findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
    CHECK(graph != nullptr);
    CHECK(tabs != nullptr);
    CHECK(output != nullptr);

    click_curves_graph(*graph, 128, 171);
    output->setValue(205);
    tabs->setCurrentIndex(1);
    click_curves_graph(*graph, 96, 214);
    dialog->accept();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_accept + 1U);
  const auto accepted_metadata = metadata_for_adjustment();
  CHECK(accepted_metadata != original_metadata);
  CHECK(accepted_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRgbPoints));
  CHECK(accepted_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesRedPoints));
  CHECK(accepted_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesGreenPoints));
  CHECK(accepted_metadata.contains(patchy::kLayerMetadataAdjustmentCurvesBluePoints));
  const auto* accepted_layer = std::as_const(document).find_layer(adjustment_id);
  CHECK(accepted_layer != nullptr);
  const auto accepted_settings = patchy::adjustment_settings_from_layer(*accepted_layer);
  CHECK(accepted_settings.has_value());
  CHECK(accepted_settings->curves.rgb.size() == 3U);
  CHECK(accepted_settings->curves.red.size() == 3U);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(metadata_for_adjustment() == original_metadata);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_accept);
}

void ui_curves_clipped_adjustment_reedit_disables_auto() {
  patchy::Document clipped_document(96, 72, patchy::PixelFormat::rgba8());
  clipped_document.add_pixel_layer(
      "Clip Base", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(96, 132, 174)));

  patchy::AdjustmentSettings curves;
  curves.kind = patchy::AdjustmentKind::Curves;
  curves.curves.rgb = {{0, 0}, {128, 188}, {255, 255}};
  patchy::Layer adjustment(clipped_document.allocate_layer_id(), "Clipped Curves",
                           patchy::LayerKind::Adjustment);
  adjustment.set_bounds(patchy::Rect::from_size(96, 72));
  patchy::configure_adjustment_layer(adjustment, curves);
  adjustment.set_clipped(true);
  const auto adjustment_id = adjustment.id();
  clipped_document.add_layer(std::move(adjustment));
  clipped_document.set_active_layer(adjustment_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(clipped_document), QStringLiteral("Clipped Curves Auto"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original_layer = std::as_const(document).find_layer(adjustment_id);
  CHECK(original_layer != nullptr);
  CHECK(original_layer->clipped());
  const auto original_metadata = original_layer->metadata();
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool dialog_opened = false;
  bool auto_disabled = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyCurvesDialog"));
    CHECK(dialog != nullptr);
    auto* auto_button = dialog->findChild<QPushButton*>(QStringLiteral("curvesAutoButton"));
    CHECK(auto_button != nullptr);
    dialog_opened = true;
    auto_disabled = !auto_button->isEnabled();
    dialog->reject();
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();

  CHECK(dialog_opened);
  CHECK(auto_disabled);
  const auto* restored_layer = std::as_const(document).find_layer(adjustment_id);
  CHECK(restored_layer != nullptr);
  CHECK(restored_layer->clipped());
  CHECK(restored_layer->metadata() == original_metadata);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
}

void ui_color_balance_dialog_adjusts_selected_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(128, 128, 128));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  accept_color_balance_dialog(50, -40, 30);
  require_action(window, "imageAdjustColorBalanceAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto adjusted = canvas_pixel(*canvas, QPoint(70, 70));
  CHECK(adjusted.red() > 245);
  CHECK(adjusted.green() < 40);
  CHECK(adjusted.blue() > 195);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(128, 128, 128), 8));
  save_widget_artifact("ui_color_balance_selection", *canvas);
}

}  // namespace

std::vector<patchy::test::TestCase> image_adjustments_curves_tests() {
  return {
      {"ui_image_adjustments_menu_applies_active_layer_filters",
       ui_image_adjustments_menu_applies_active_layer_filters},
      {"ui_image_adjustments_respect_active_selection", ui_image_adjustments_respect_active_selection},
      {"ui_direct_pixel_previews_preserve_floating_layer_bounds",
       ui_direct_pixel_previews_preserve_floating_layer_bounds},
      {"ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer",
       ui_levels_dialog_adjusts_selected_color_channel_on_transparent_layer},
      {"ui_levels_dialog_preserves_independent_channel_records",
       ui_levels_dialog_preserves_independent_channel_records},
      {"ui_hue_saturation_dialog_adjusts_selected_pixels", ui_hue_saturation_dialog_adjusts_selected_pixels},
      {"ui_hue_saturation_creates_masked_adjustment_layer", ui_hue_saturation_creates_masked_adjustment_layer},
      {"ui_hue_saturation_colorize_toggle_switches_ranges_and_creates_layer",
       ui_hue_saturation_colorize_toggle_switches_ranges_and_creates_layer},
      {"ui_layer_clipping_menu_toggles_renders_and_undoes", ui_layer_clipping_menu_toggles_renders_and_undoes},
      {"ui_layer_clipping_action_enablement", ui_layer_clipping_action_enablement},
      {"ui_generic_bg_clip_toggle_repaints_if_available", ui_generic_bg_clip_toggle_repaints_if_available},
      {"ui_view_layer_mask_menu_action_shows_and_selects_mask",
       ui_view_layer_mask_menu_action_shows_and_selects_mask},
      {"ui_alt_click_layer_boundary_toggles_clipping", ui_alt_click_layer_boundary_toggles_clipping},
      {"ui_adjustment_layer_thumbnails_show_type_symbols",
       ui_adjustment_layer_thumbnails_show_type_symbols},
      {"ui_levels_dialog_remaps_selected_tonal_range", ui_levels_dialog_remaps_selected_tonal_range},
      {"ui_curves_dialog_remaps_midtones_in_selection", ui_curves_dialog_remaps_midtones_in_selection},
      {"ui_curves_editor_points_channels_keyboard_auto_and_reset",
       ui_curves_editor_points_channels_keyboard_auto_and_reset},
      {"ui_curves_transient_canvas_read_is_non_mutating",
       ui_curves_transient_canvas_read_is_non_mutating},
      {"ui_curves_canvas_tools_before_and_clipping_hooks",
       ui_curves_canvas_tools_before_and_clipping_hooks},
      {"ui_curves_canvas_sampler_reads_below_preview_and_escape_closes",
       ui_curves_canvas_sampler_reads_below_preview_and_escape_closes},
      {"ui_curves_preset_thumbnails_apply_all_channels_and_track_custom",
       ui_curves_preset_thumbnails_apply_all_channels_and_track_custom},
      {"ui_curves_destructive_and_adjustment_luts_match",
       ui_curves_destructive_and_adjustment_luts_match},
      {"ui_curves_acv_load_save_updates_editor_and_preview",
       ui_curves_acv_load_save_updates_editor_and_preview},
      {"ui_curves_dialog_preview_toggle_and_cancel_restore_pixels",
       ui_curves_dialog_preview_toggle_and_cancel_restore_pixels},
      {"ui_curves_destructive_apply_cancel_restores_exact_layer",
       ui_curves_destructive_apply_cancel_restores_exact_layer},
      {"ui_curves_adjustment_layer_preserves_rich_channels_and_undoes_one_edit",
       ui_curves_adjustment_layer_preserves_rich_channels_and_undoes_one_edit},
      {"ui_curves_legacy_metadata_cancel_and_undo_restore_exact_map",
       ui_curves_legacy_metadata_cancel_and_undo_restore_exact_map},
      {"ui_curves_clipped_adjustment_reedit_disables_auto",
       ui_curves_clipped_adjustment_reedit_disables_auto},
      {"ui_color_balance_dialog_adjusts_selected_pixels", ui_color_balance_dialog_adjusts_selected_pixels},
  };
}
