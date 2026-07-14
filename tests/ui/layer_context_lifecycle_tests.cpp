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

void ui_layer_style_color_overlay_patch_double_click_opens_picker() {
  // The Color Overlay page's color patch is a passive QLabel next to the Choose
  // Color... button; double-clicking the patch must open the same picker.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      saw_dialog = true;
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* patch = dialog->findChild<QLabel*>(QStringLiteral("layerStyleColorOverlayColorPreview"));
      auto* red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayRedSpin"));
      auto* green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayGreenSpin"));
      auto* blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleColorOverlayBlueSpin"));
      CHECK(patch != nullptr);
      CHECK(red != nullptr && green != nullptr && blue != nullptr);

      bool saw_picker = false;
      QTimer::singleShot(0, [&saw_picker] {
        for (auto* picker_widget : QApplication::topLevelWidgets()) {
          if (picker_widget->objectName() != QStringLiteral("patchyColorDialog") || !picker_widget->isVisible()) {
            continue;
          }
          auto* picker =
              picker_widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(12, 200, 99));
          saw_picker = true;
          qobject_cast<QDialog*>(picker_widget)->accept();
          return;
        }
        CHECK(false);
      });
      const auto patch_center = patch->rect().center();
      QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(patch_center),
                               patch->mapToGlobal(patch_center), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(patch, &double_click);
      QApplication::processEvents();
      CHECK(saw_picker);
      CHECK(red->value() == 12);
      CHECK(green->value() == 200);
      CHECK(blue->value() == 99);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
}

void ui_layer_context_menu_exposes_blending_options_dialog() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  CHECK(layer_list != nullptr);
  CHECK(blending_options->text().remove('&') == QStringLiteral("Edit Layer Styles..."));

  bool saw_context_action = false;
  bool saw_rasterize_action = false;
  bool saw_rasterize_style_action = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      for (auto* action : menu->actions()) {
        auto text = action->text();
        text.remove('&');
        if (text == QStringLiteral("Edit Layer Styles...")) {
          saw_context_action = true;
        } else if (text == QStringLiteral("Rasterize")) {
          saw_rasterize_action = true;
        } else if (text == QStringLiteral("Rasterize (including layer style)")) {
          saw_rasterize_style_action = true;
        }
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(layer_list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_action);
  CHECK(saw_rasterize_action);
  CHECK(saw_rasterize_style_action);

  canvas->set_primary_color(QColor(230, 40, 40));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas_pixel(*canvas, QPoint(80, 80));
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(24);
  canvas->set_primary_color(QColor(20, 220, 60));

  bool saw_live_style_preview = false;
  bool saw_non_modal_dialog = false;
  bool saw_layer_style_edit_lock = false;
  bool saw_shared_color_picker = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_editor =
          dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
      auto* gradient_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientBlendModeCombo"));
      auto* gradient_reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientReverseCheck"));
      auto* gradient_style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientStyleCombo"));
      auto* gradient_stop_location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
      auto* gradient_stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
      auto* gradient_stop_swatch = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientStopSwatchButton"));
      auto* stroke_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleStrokeColorPreview"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGreenSpin"));
      auto* stroke_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeBlueSpin"));
      auto* outer_glow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleOuterGlowColorPreview"));
      auto* outer_glow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowRedSpin"));
      auto* outer_glow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowGreenSpin"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
      auto* shadow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleDropShadowColorPreview"));
      auto* shadow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowRedSpin"));
      auto* shadow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowGreenSpin"));
      auto* shadow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      CHECK(categories != nullptr);
      CHECK(gradient_check != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_editor != nullptr);
      CHECK(gradient_blend != nullptr);
      CHECK(gradient_reverse != nullptr);
      CHECK(gradient_style_combo != nullptr);
      CHECK(gradient_stop_location != nullptr);
      CHECK(gradient_stop_hex != nullptr);
      CHECK(gradient_stop_swatch != nullptr);
      CHECK(stroke_color != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_green != nullptr);
      CHECK(stroke_blue != nullptr);
      CHECK(outer_glow_color != nullptr);
      CHECK(outer_glow_red != nullptr);
      CHECK(outer_glow_green != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(shadow_blend != nullptr);
      CHECK(shadow_color != nullptr);
      CHECK(shadow_red != nullptr);
      CHECK(shadow_green != nullptr);
      CHECK(shadow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(shadow_blend->findText(QStringLiteral("Normal")) >= 0);
      CHECK(stroke_red->value() == 255);
      CHECK(stroke_green->value() == 0);
      CHECK(stroke_blue->value() == 0);
      saw_non_modal_dialog = !dialog->isModal() && dialog->windowModality() == Qt::NonModal &&
                             dialog->windowFlags().testFlag(Qt::FramelessWindowHint) &&
                             dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr &&
                             dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr;
      CHECK(canvas->edit_locked());
      CHECK(!layer_list->isEnabled());
      CHECK(!require_action(window, "layerNewAction")->isEnabled());
      CHECK(!require_action(window, "layerFillForegroundAction")->isEnabled());
      CHECK(require_action(window, "viewZoomInAction")->isEnabled());
      const auto locked_pixel_before_edit = canvas_pixel(*canvas, QPoint(80, 80));
      drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
           canvas->widget_position_for_document_point(QPoint(98, 98)));
      const auto locked_pixel_after_edit = canvas_pixel(*canvas, QPoint(80, 80));
      saw_layer_style_edit_lock = color_close(locked_pixel_after_edit, locked_pixel_before_edit, 2);
      gradient_check->setChecked(true);
      gradient_angle_slider->setValue(0);
      QApplication::processEvents();
      QElapsedTimer live_preview_wait;
      live_preview_wait.start();
      while (live_preview_wait.elapsed() < 2000) {
        QApplication::processEvents(QEventLoop::AllEvents, 16);
        const auto layer_id = patchy::ui::MainWindowTestAccess::document(window).active_layer_id();
        const auto* live_layer = layer_id.has_value()
                                     ? patchy::ui::MainWindowTestAccess::document(window).find_layer(*layer_id)
                                     : nullptr;
        saw_live_style_preview =
            live_layer != nullptr && !live_layer->layer_style().gradient_fills.empty() &&
            live_layer->layer_style().gradient_fills.front().enabled;
        if (saw_live_style_preview) {
          break;
        }
      }
      const auto gradient_items = categories->findItems(QStringLiteral("Gradient Overlay"), Qt::MatchExactly);
      CHECK(!gradient_items.empty());
      categories->setCurrentItem(gradient_items.front());
      QApplication::processEvents();
      CHECK(gradient_editor->isVisible());
      CHECK(gradient_stop_hex->isVisible());
      CHECK(gradient_stop_location->value() == 0);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#FFFFFF"));
      QTimer::singleShot(0, [&saw_shared_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* color_dialog = qobject_cast<QDialog*>(widget);
          CHECK(color_dialog != nullptr);
          auto* picker =
              color_dialog->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(64, 128, 192));
          saw_shared_color_picker = true;
          color_dialog->accept();
          return;
        }
        CHECK(false);
      });
      gradient_stop_swatch->click();
      CHECK(gradient_stop_hex->text() == QStringLiteral("#4080C0"));
      // Click the 100% color tag under the bar to select the second stop.
      const auto second_stop_point = QPoint(gradient_editor->width() - 11, gradient_editor->height() - 18);
      send_mouse(*gradient_editor, QEvent::MouseButtonPress, second_stop_point, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*gradient_editor, QEvent::MouseButtonRelease, second_stop_point, Qt::LeftButton, Qt::NoButton);
      QApplication::processEvents();
      CHECK(gradient_stop_location->value() == 100);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#202020"));
      CHECK(gradient_stop_swatch->styleSheet().contains(QStringLiteral("rgb(32, 32, 32)")));
      bool saw_gradient_swatch_picker = false;
      QTimer::singleShot(0, [&saw_gradient_swatch_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(220, 40, 96));
          saw_gradient_swatch_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      gradient_stop_swatch->click();
      CHECK(saw_gradient_swatch_picker);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#DC2860"));
      bool saw_stroke_color_picker = false;
      QTimer::singleShot(0, [&saw_stroke_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(24, 48, 72));
          saw_stroke_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      stroke_color->click();
      CHECK(saw_stroke_color_picker);
      CHECK(stroke_red->value() == 24);
      CHECK(stroke_green->value() == 48);
      CHECK(stroke_blue->value() == 72);
      bool saw_outer_glow_color_picker = false;
      QTimer::singleShot(0, [&saw_outer_glow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(190, 170, 64));
          saw_outer_glow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      outer_glow_color->click();
      CHECK(saw_outer_glow_color_picker);
      CHECK(outer_glow_red->value() == 190);
      CHECK(outer_glow_green->value() == 170);
      CHECK(outer_glow_blue->value() == 64);
      bool saw_shadow_color_picker = false;
      QTimer::singleShot(0, [&saw_shadow_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
          CHECK(picker != nullptr);
          picker->setCurrentColor(QColor(250, 250, 250));
          saw_shadow_color_picker = true;
          qobject_cast<QDialog*>(widget)->accept();
          return;
        }
        CHECK(false);
      });
      shadow_color->click();
      CHECK(saw_shadow_color_picker);
      CHECK(shadow_red->value() == 250);
      CHECK(shadow_green->value() == 250);
      CHECK(shadow_blue->value() == 250);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_non_modal_dialog);
  CHECK(saw_live_style_preview);
  CHECK(saw_layer_style_edit_lock);
  CHECK(saw_shared_color_picker);
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 8));

  accept_layer_style_dialog(false, true, false);
  blending_options->trigger();
  QApplication::processEvents();
  const auto after = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(!color_close(before, after, 20));

  auto* styled_item = layer_list->item(0);
  CHECK(styled_item != nullptr);
  styled_item->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  styled_item->setCheckState(Qt::Checked);
  QApplication::processEvents();
  const auto after_visibility_toggle = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(color_close(after, after_visibility_toggle, 8));

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  const auto before_move_click = canvas->grab().toImage();
  const auto move_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, move_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto during_move_click = canvas->grab().toImage();
  CHECK(color_close(during_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));
  send_mouse(*canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move_click = canvas->grab().toImage();
  CHECK(color_close(after_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));

  auto* fx_badge =
      layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  CHECK(fx_badge != nullptr);
  save_widget_artifact("ui_layer_style_result", window);
}

void ui_layer_row_double_click_opens_blending_options_dialog() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = layer_list->item(0);
  CHECK(item != nullptr);
  auto* row_widget = layer_list->itemWidget(item);
  CHECK(row_widget != nullptr);
  auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(row_name != nullptr);

  bool saw_blending_options = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
    CHECK(categories != nullptr);
    CHECK(blend != nullptr);
    CHECK(categories->currentItem() != nullptr);
    CHECK(categories->currentItem()->text() == QStringLiteral("Blending Options"));
    saw_blending_options = true;
    dialog->reject();
  });

  send_double_click(*row_name, row_name->rect().center());
  QApplication::processEvents();
  CHECK(saw_blending_options);
}

// The fx badge is a clickable icon button on the row's details line (it replaced
// the old "fx" text): clicking it activates the badge's own layer and opens the
// layer styles dialog. Styleless rows show no badge.
void ui_layer_fx_badge_button_opens_layer_style_dialog() {
  patchy::Document document(48, 48, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, patchy::PixelFormat::rgba8(), QColor(60, 60, 70, 255)));
  document.add_pixel_layer("Styled", solid_pixels(48, 48, patchy::PixelFormat::rgba8(), QColor(40, 110, 230, 255)));
  auto& styled = document.layers().back();
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.75F;
  shadow.distance = 4.0F;
  shadow.size = 3.0F;
  styled.layer_style().drop_shadows.push_back(shadow);
  // The click must activate the styled layer itself, so start elsewhere.
  document.set_active_layer(document.layers().front().id());

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Fx Badge"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* background_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Background")));
  CHECK(background_row != nullptr);
  CHECK(background_row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton")) == nullptr);
  auto* styled_row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Styled")));
  CHECK(styled_row != nullptr);
  auto* fx_badge = styled_row->findChild<QToolButton*>(QStringLiteral("layerFxBadgeButton"));
  CHECK(fx_badge != nullptr);
  CHECK(!fx_badge->icon().isNull());
  // The details line dropped the old "fx" text in favor of the button.
  auto* details = styled_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(!details->text().contains(QStringLiteral("fx")));

  // A plain mouse click must reach the badge itself instead of the list's row
  // select/drag handling. send_mouse processes events internally, so the badge's
  // deferred action (and its modal style dialog) runs INSIDE the release call:
  // arm a re-arming closer beforehand that rejects the dialog once it appears.
  bool saw_dialog = false;
  int close_attempts = 0;
  std::function<void()> close_style_dialog = [&] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")); dialog != nullptr) {
      saw_dialog = true;
      dialog->reject();
      return;
    }
    if (++close_attempts < 300) {
      QTimer::singleShot(0, &window, close_style_dialog);
    }
  };
  QTimer::singleShot(0, &window, close_style_dialog);
  send_mouse(*fx_badge, QEvent::MouseButtonPress, fx_badge->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*fx_badge, QEvent::MouseButtonRelease, fx_badge->rect().center(), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(saw_dialog);
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(doc.active_layer_id().has_value());
  const auto* active_layer = doc.find_layer(*doc.active_layer_id());
  CHECK(active_layer != nullptr);
  CHECK(active_layer->name() == "Styled");
}

void ui_layer_style_stroke_blend_mode_round_trips() {
  // The Stroke page's Blend Mode combo (added July 2026 for Photoshop parity) must
  // save into LayerStroke::blend_mode and load back when the dialog reopens.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");

  bool edited = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* stroke_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeBlendModeCombo"));
    auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
    CHECK(categories != nullptr);
    CHECK(stroke_blend != nullptr);
    CHECK(stroke_size != nullptr);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    stroke_items.front()->setCheckState(Qt::Checked);
    categories->setCurrentItem(stroke_items.front());
    stroke_size->setValue(6);
    stroke_blend->setCurrentIndex(
        std::max(0, stroke_blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    CHECK(stroke_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Multiply));
    edited = true;
    qobject_cast<QDialog*>(dialog)->accept();
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(edited);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  CHECK(!layer->layer_style().strokes.empty());
  CHECK(layer->layer_style().strokes.front().enabled);
  CHECK(layer->layer_style().strokes.front().blend_mode == patchy::BlendMode::Multiply);
  CHECK(layer->layer_style().strokes.front().size == 6.0F);

  bool reloaded = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    CHECK(dialog != nullptr);
    auto* stroke_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeBlendModeCombo"));
    CHECK(stroke_blend != nullptr);
    CHECK(stroke_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Multiply));
    reloaded = true;
    qobject_cast<QDialog*>(dialog)->reject();
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(reloaded);
}

void ui_layer_row_double_click_skips_folders_and_edits_adjustments() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(245, 245, 245)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder 1", patchy::LayerKind::Group);
  folder.add_child(patchy::Layer(document.allocate_layer_id(), "Paint Layer",
                                 solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(40, 90, 220))));
  document.add_layer(std::move(folder));
  patchy::Layer levels(document.allocate_layer_id(), "Levels", patchy::LayerKind::Adjustment);
  patchy::AdjustmentSettings settings;
  settings.kind = patchy::AdjustmentKind::Levels;
  patchy::configure_adjustment_layer(levels, settings);
  document.add_layer(std::move(levels));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Double Click Layer Kinds"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto row_name_for = [layer_list](const QString& layer_name) {
    auto* item = require_layer_item(*layer_list, layer_name);
    auto* row_widget = layer_list->itemWidget(item);
    CHECK(row_widget != nullptr);
    auto* row_name = row_widget->findChild<QLabel*>(QStringLiteral("layerRowName"));
    CHECK(row_name != nullptr);
    return row_name;
  };

  // Double-clicking a folder opens nothing: layer styles cannot apply to
  // groups, so the blending dialog must not appear.
  bool folder_opened_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* style_dialog = find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog"));
    auto* levels_dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    if (style_dialog != nullptr || levels_dialog != nullptr) {
      folder_opened_dialog = true;
      (style_dialog != nullptr ? style_dialog : levels_dialog)->reject();
    }
  });
  auto* folder_name = row_name_for(QStringLiteral("Folder 1"));
  send_double_click(*folder_name, folder_name->rect().center());
  QApplication::processEvents();
  CHECK(!folder_opened_dialog);

  // Double-clicking an adjustment layer opens its settings dialog (Levels
  // here) instead of the blending dialog.
  bool saw_levels_dialog = false;
  QTimer::singleShot(0, [&] {
    CHECK(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")) == nullptr);
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyLevelsDialog"));
    CHECK(dialog != nullptr);
    saw_levels_dialog = true;
    dialog->reject();
  });
  auto* levels_name = row_name_for(QStringLiteral("Levels"));
  send_double_click(*levels_name, levels_name->rect().center());
  QApplication::processEvents();
  CHECK(saw_levels_dialog);
}

void ui_layer_context_menu_rasterizes_text_and_layer_styles() {
  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    patchy::Layer text_layer(document.allocate_layer_id(), "Text: Raster", patchy::LayerKind::Text);
    text_layer.set_bounds(patchy::Rect{24, 24, 96, 36});
    text_layer.metadata()[patchy::kLayerMetadataText] = "Raster";
    text_layer.metadata()[patchy::kLayerMetadataTextSize] = "28";
    text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#000000";
    patchy::LayerDropShadow text_shadow;
    text_shadow.enabled = true;
    text_shadow.opacity = 1.0F;
    text_shadow.distance = 6.0F;
    text_shadow.size = 6.0F;
    text_layer.layer_style().drop_shadows.push_back(text_shadow);
    document.add_layer(std::move(text_layer));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QStringLiteral("Rasterize Text"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    auto* text_info = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
    CHECK(layer_list != nullptr);
    CHECK(layer_info != nullptr);
    CHECK(geometry != nullptr);
    CHECK(text_info != nullptr);
    auto* thumbnail = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    CHECK(thumbnail->toolTip() == QStringLiteral("Text layer"));
    const auto thumbnail_image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    int bright_thumbnail_pixels = 0;
    for (int y = 0; y < thumbnail_image.height(); ++y) {
      for (int x = 0; x < thumbnail_image.width(); ++x) {
        const auto color = thumbnail_image.pixelColor(x, y);
        if (color.red() + color.green() + color.blue() > 600) {
          ++bright_thumbnail_pixels;
        }
      }
    }
    CHECK(bright_thumbnail_pixels > 20);
    CHECK(layer_info->text().contains(QStringLiteral("Text Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!text_info->isHidden());

    require_action(window, "layerRasterizeAction")->trigger();
    QApplication::processEvents();

    CHECK(layer_info->text().contains(QStringLiteral("Pixel Layer")));
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(text_info->isHidden());
    auto* name = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerRowName"));
    CHECK(name != nullptr);
    CHECK(name->text() == QStringLiteral("Raster"));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
  }

  {
    patchy::Document document(140, 96, patchy::PixelFormat::rgba8());
    auto pixels = solid_pixels(36, 28, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255));
    patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Pixel", std::move(pixels));
    styled_layer.set_bounds(patchy::Rect{34, 28, 36, 28});
    patchy::LayerDropShadow shadow;
    shadow.enabled = true;
    shadow.opacity = 1.0F;
    shadow.distance = 8.0F;
    shadow.size = 8.0F;
    shadow.color = patchy::RgbColor{0, 0, 0};
    styled_layer.layer_style().drop_shadows.push_back(shadow);
    document.add_layer(std::move(styled_layer));

    // Reimport once so this exercises the Photoshop raw-style preservation
    // path as well as the modeled style. Rasterizing must remove both or the
    // next PSD save would resurrect the original effect over baked pixels.
    auto imported_document =
        patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
    CHECK(std::any_of(imported_document.layers().front().unknown_psd_blocks().begin(),
                      imported_document.layers().front().unknown_psd_blocks().end(),
                      [](const patchy::UnknownPsdBlock& block) {
                        return block.key == "lfx2" || block.key == "lrFX";
                      }));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(imported_document), QStringLiteral("Rasterize Style"));
    show_window(window);
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
    CHECK(layer_list != nullptr);
    CHECK(geometry != nullptr);
    CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
    CHECK(!require_action(window, "layerRasterizeAction")->isEnabled());
    CHECK(require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());

    require_action(window, "layerRasterizeLayerStyleAction")->trigger();
    QApplication::processEvents();

    CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
    CHECK(!require_action(window, "layerRasterizeLayerStyleAction")->isEnabled());
    const auto& rasterized_document = patchy::ui::MainWindowTestAccess::document(window);
    const auto& rasterized_layer = rasterized_document.layers().front();
    CHECK(std::none_of(rasterized_layer.unknown_psd_blocks().begin(),
                       rasterized_layer.unknown_psd_blocks().end(),
                       [](const patchy::UnknownPsdBlock& block) {
                         return block.key == "lfx2" || block.key == "lrFX";
                       }));
    const auto resaved = patchy::psd::DocumentIo::read(
        patchy::psd::DocumentIo::write_layered_rgb8(rasterized_document));
    CHECK(resaved.layers().front().layer_style().empty());
    CHECK(std::none_of(resaved.layers().front().unknown_psd_blocks().begin(),
                       resaved.layers().front().unknown_psd_blocks().end(),
                       [](const patchy::UnknownPsdBlock& block) {
                         return block.key == "lfx2" || block.key == "lrFX";
                       }));
  }
}

void ui_layer_context_menu_layer_style_actions_follow_selection_state() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer styled_layer(document.allocate_layer_id(), "Styled Source",
                             solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  styled_layer.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 8.0F;
  shadow.size = 8.0F;
  styled_layer.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(styled_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Plain Target A",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{56, 16, 24, 24});
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Plain Target B",
                               solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{96, 16, 24, 24});
  document.add_layer(std::move(target_b_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Menu State"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* styled_item = require_layer_item(*layer_list, QStringLiteral("Styled Source"));
  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Plain Target A"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Plain Target B"));

  layer_list->clearSelection();
  layer_list->setCurrentItem(styled_item);
  styled_item->setSelected(true);
  QApplication::processEvents();
  auto state = layer_style_context_menu_state(*layer_list, *styled_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);

  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(state.saw_copy);
  CHECK(state.saw_paste);
  CHECK(state.saw_delete);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(!state.delete_enabled);

  layer_list->clearSelection();
  styled_item->setSelected(true);
  target_a_item->setSelected(true);
  QApplication::processEvents();
  state = layer_style_context_menu_state(*layer_list, *target_a_item);
  CHECK(!state.copy_enabled);
  CHECK(!state.paste_enabled);
  CHECK(state.delete_enabled);
}

void ui_layer_style_copy_paste_delete_applies_to_selected_layers() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  patchy::Layer source_layer(document.allocate_layer_id(), "Style Source",
                             solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(230, 60, 40, 255)));
  source_layer.set_bounds(patchy::Rect{16, 22, 26, 26});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.9F;
  shadow.distance = 7.0F;
  shadow.size = 7.0F;
  source_layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerSatin custom_satin;
  custom_satin.enabled = false;
  custom_satin.unsupported_contour_options = true;
  source_layer.layer_style().satins.push_back(custom_satin);
  document.add_layer(std::move(source_layer));

  patchy::Layer target_a_layer(document.allocate_layer_id(), "Target Multiply",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 180, 80, 255)));
  target_a_layer.set_bounds(patchy::Rect{66, 22, 26, 26});
  target_a_layer.set_blend_mode(patchy::BlendMode::Multiply);
  target_a_layer.set_opacity(0.42F);
  document.add_layer(std::move(target_a_layer));

  patchy::Layer target_b_layer(document.allocate_layer_id(), "Target Screen",
                               solid_pixels(26, 26, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  target_b_layer.set_bounds(patchy::Rect{116, 22, 26, 26});
  target_b_layer.set_blend_mode(patchy::BlendMode::Screen);
  target_b_layer.set_opacity(0.77F);
  document.add_layer(std::move(target_b_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Style Copy Paste Delete"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  CHECK(layer_list != nullptr);
  CHECK(layer_info != nullptr);
  CHECK(geometry != nullptr);

  auto select_single_layer = [&](const QString& name) {
    auto* item = require_layer_item(*layer_list, name);
    layer_list->clearSelection();
    layer_list->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
    return item;
  };

  select_single_layer(QStringLiteral("Style Source"));
  auto* copy_style = require_action(window, "layerCopyStyleAction");
  auto* paste_style = require_action(window, "layerPasteStyleAction");
  auto* delete_style = require_action(window, "layerDeleteStyleAction");
  CHECK(copy_style->isEnabled());
  CHECK(!paste_style->isEnabled());
  copy_style->trigger();
  QApplication::processEvents();
  CHECK(paste_style->isEnabled());

  auto* target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  auto* target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(!copy_style->isEnabled());
  CHECK(paste_style->isEnabled());
  paste_style->trigger();
  QApplication::processEvents();

  const auto& pasted_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto find_named_layer = [&](std::string_view name) -> const patchy::Layer* {
    const auto found = std::find_if(pasted_document.layers().begin(), pasted_document.layers().end(),
                                    [name](const patchy::Layer& layer) { return layer.name() == name; });
    return found == pasted_document.layers().end() ? nullptr : &*found;
  };
  const auto* source_after_copy = find_named_layer("Style Source");
  const auto* target_a_after_paste = find_named_layer("Target Multiply");
  const auto* target_b_after_paste = find_named_layer("Target Screen");
  CHECK(source_after_copy != nullptr);
  CHECK(target_a_after_paste != nullptr);
  CHECK(target_b_after_paste != nullptr);
  CHECK(source_after_copy->layer_style().satins.front().unsupported_contour_options);
  CHECK(!target_a_after_paste->layer_style().satins.front().unsupported_contour_options);
  CHECK(!target_b_after_paste->layer_style().satins.front().unsupported_contour_options);

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  target_a_item = require_layer_item(*layer_list, QStringLiteral("Target Multiply"));
  target_b_item = require_layer_item(*layer_list, QStringLiteral("Target Screen"));
  layer_list->clearSelection();
  target_a_item->setSelected(true);
  target_b_item->setSelected(true);
  QApplication::processEvents();
  CHECK(delete_style->isEnabled());
  delete_style->trigger();
  QApplication::processEvents();

  select_single_layer(QStringLiteral("Target Multiply"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Multiply")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 42%")));

  select_single_layer(QStringLiteral("Target Screen"));
  CHECK(geometry->text().contains(QStringLiteral("Effects: none")));
  CHECK(layer_info->text().contains(QStringLiteral("Mode: Screen")));
  CHECK(layer_info->text().contains(QStringLiteral("Opacity: 77%")));

  select_single_layer(QStringLiteral("Style Source"));
  CHECK(geometry->text().contains(QStringLiteral("Drop Shadow")));
}

void inspect_new_document_dialog_without_clipboard() {
  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentPresetCombo"));
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
      auto* unit = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentUnitCombo"));
      CHECK(dialog != nullptr);
      CHECK(preset != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(resolution != nullptr);
      CHECK(unit != nullptr);
      const QStringList labels = {
          QStringLiteral("Clipboard"),   QStringLiteral("1024 x 768"), QStringLiteral("A4 300 ppi"),
          QStringLiteral("A3 300 ppi"),  QStringLiteral("US Letter 300 ppi"),
          QStringLiteral("1080p"),       QStringLiteral("4K")};
      CHECK(preset->count() == static_cast<int>(labels.size()));
      for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        CHECK(preset->itemText(index) == labels[index]);
      }
      CHECK((preset->model()->flags(preset->model()->index(0, 0)) & Qt::ItemIsEnabled) == 0);
      CHECK(preset->currentText() == QStringLiteral("1024 x 768"));
      CHECK(width->value() == 1024);
      CHECK(height->value() == 768);
      CHECK(std::abs(resolution->value() - 300.0) < 0.01);
      CHECK(unit->currentText() == QStringLiteral("Pixels"));
      // A physical unit converts the display through the resolution: 1024 px at
      // 300 ppi is 3.413 in.
      unit->setCurrentIndex(unit->findText(QStringLiteral("Inches")));
      QApplication::processEvents();
      CHECK(std::abs(width->value() - 1024.0 / 300.0) < 0.01);
      unit->setCurrentIndex(unit->findText(QStringLiteral("Pixels")));
      QApplication::processEvents();
      CHECK(width->value() == 1024);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
}

void ui_closing_last_document_leaves_empty_workspace() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(foreground != nullptr);
  CHECK(tabs->count() == 1);

  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();

  CHECK(tabs->count() == 0);
  CHECK(info->text() == QStringLiteral("No document"));
  CHECK(layer_list->count() == 0);
  CHECK(!layer_list->isEnabled());
  CHECK(!foreground->isEnabled());
  CHECK(!require_action(window, "fileSaveAction")->isEnabled());
  CHECK(!require_action(window, "layerNewAction")->isEnabled());
  CHECK(!require_action(window, "brushLargerAction")->isEnabled());
  CHECK(require_action(window, "fileNewAction")->isEnabled());
  CHECK(require_action(window, "fileOpenAction")->isEnabled());

  accept_new_document_dialog(320, 180);
  require_action(window, "fileNewAction")->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 1);
  CHECK(info->text().contains(QStringLiteral("320 x 180 px")));
  CHECK(layer_list->isEnabled());
  CHECK(foreground->isEnabled());
  CHECK(require_action(window, "fileSaveAction")->isEnabled());
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("round"));
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_softness() == 0);
}

// Regression: closing the last tab while an inline text edit is open must
// commit the edit before the save prompt.  An editor that survived into
// removeTab() auto-committed on the focus change mid teardown, after
// activate_document_tab() had already cleared the active canvas pointer, and
// crashed dereferencing it (the June 10 2026 WER dumps for patchy.exe).
void ui_close_last_tab_with_active_text_edit_commits_editor_first() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto* canvas = require_canvas(window);

  // Dirty the document so confirm_close_session shows the save prompt.
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  const auto center = canvas->rect().center();
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Patchy"));
  QApplication::processEvents();

  // The prompt opens a nested event loop, so dismiss it from a polling timer.
  // The editor must already be finished by prompt time: that is the fix.
  bool prompt_seen = false;
  bool editor_gone_at_prompt = false;
  auto* dismiss_timer = new QTimer(&window);
  dismiss_timer->setInterval(10);
  QObject::connect(dismiss_timer, &QTimer::timeout, &window, [&] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    prompt_seen = true;
    editor_gone_at_prompt = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr;
    dismiss_timer->stop();
    dialog->button(QMessageBox::No)->click();
  });
  dismiss_timer->start();

  CHECK(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)));
  QApplication::processEvents();
  dismiss_timer->stop();

  CHECK(prompt_seen);
  CHECK(editor_gone_at_prompt);
  CHECK(tabs->count() == 0);
}

// The close-document save prompt asks Yes/No/Cancel, and bare Y/N key presses
// (no Alt) activate Yes/No like native Windows message boxes. Qt itself only
// wires the Alt+mnemonic; show_warning_message adds the plain letters, so both
// the button set and the accelerators are pinned here.
void ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys() {
  std::filesystem::create_directories("test-artifacts");
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts"))
                                  .filePath(QStringLiteral("ui_save_prompt_yes_no.tga")))
                        .absoluteFilePath();
  const QColor left_color(200, 40, 40);
  const QColor right_color(40, 80, 200);
  {
    patchy::Document source(8, 6, patchy::PixelFormat::rgb8());
    patchy::PixelBuffer pixels(8, 6, patchy::PixelFormat::rgb8());
    for (std::int32_t y = 0; y < 6; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        const auto color = x < 4 ? left_color : right_color;
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(color.red());
        px[1] = static_cast<std::uint8_t>(color.green());
        px[2] = static_cast<std::uint8_t>(color.blue());
      }
    }
    source.add_pixel_layer("Background", std::move(pixels));
    patchy::tga::DocumentIo::write_file(source, std::filesystem::path(path.toStdWString()));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  const auto corner_color = [&window] {
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto* px = document.layers().front().pixels().pixel(0, 0);
    return QColor(px[0], px[1], px[2]);
  };

  // Dismisses the save prompt with a bare letter key once it appears, recording
  // the button layout. The prompt runs a nested event loop, hence the timer.
  bool prompt_seen = false;
  bool buttons_are_yes_no_cancel = false;
  const auto dismiss_prompt_with_key = [&](int key) {
    prompt_seen = false;
    buttons_are_yes_no_cancel = false;
    auto* dismiss_timer = new QTimer(&window);
    dismiss_timer->setInterval(10);
    QObject::connect(dismiss_timer, &QTimer::timeout, &window, [&, key, dismiss_timer] {
      auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("saveChangesMessageBox")));
      if (dialog == nullptr) {
        return;
      }
      prompt_seen = true;
      buttons_are_yes_no_cancel =
          dialog->button(QMessageBox::Yes) != nullptr && dialog->button(QMessageBox::No) != nullptr &&
          dialog->button(QMessageBox::Cancel) != nullptr && dialog->button(QMessageBox::Save) == nullptr &&
          dialog->button(QMessageBox::Discard) == nullptr;
      dismiss_timer->stop();
      dismiss_timer->deleteLater();
      // Send to the focused button when there is one: the bare letter must reach
      // the box by propagating up from the child, the interactive path.
      auto* target = dialog->focusWidget() != nullptr ? dialog->focusWidget() : dialog;
      send_key(*target, key);
    });
    dismiss_timer->start();
  };

  // N answers No: the document closes without saving.
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Flip Layer Horizontal"))->trigger();
  QApplication::processEvents();
  CHECK(corner_color() == right_color);
  int tabs_before_close = tabs->count();
  dismiss_prompt_with_key(Qt::Key_N);
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, tabs->currentIndex()));
  QApplication::processEvents();
  CHECK(prompt_seen);
  CHECK(buttons_are_yes_no_cancel);
  CHECK(tabs->count() == tabs_before_close - 1);

  // The file kept its original pixels.
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  CHECK(corner_color() == left_color);

  // Y answers Yes: the document saves to its path, then closes.
  require_action_by_text(window, QStringLiteral("Flip Layer Horizontal"))->trigger();
  QApplication::processEvents();
  tabs_before_close = tabs->count();
  dismiss_prompt_with_key(Qt::Key_Y);
  CHECK(patchy::ui::MainWindowTestAccess::close_document_tab(window, tabs->currentIndex()));
  QApplication::processEvents();
  CHECK(prompt_seen);
  CHECK(buttons_are_yes_no_cancel);
  CHECK(tabs->count() == tabs_before_close - 1);

  // The flipped pixels reached disk.
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  CHECK(corner_color() == right_color);
}

void ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all() {
  const auto make_document = [](QColor color) {
    patchy::Document document(32, 24, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(32, 24, patchy::PixelFormat::rgb8(), color));
    return document;
  };

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  CHECK(tab_bar->contextMenuPolicy() == Qt::CustomContextMenu);

  window.add_document_session(make_document(QColor(50, 80, 140)), QStringLiteral("Zulu"));
  window.add_document_session(make_document(QColor(140, 80, 50)), QStringLiteral("Alpha"));
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  CHECK(window.findChild<QAction*>(QStringLiteral("windowArrangeWindowsAction")) == nullptr);
  CHECK(window.findChild<QAction*>(QStringLiteral("windowCloseAllWindowsAction")) == nullptr);

  const auto target_index = tabs->indexOf(tabs->widget(1));
  CHECK(target_index == 1);
  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
        continue;
      }
      auto* close_action = find_menu_action_by_text(*menu, QStringLiteral("Close"));
      auto* close_others_action = find_menu_action_by_text(*menu, QStringLiteral("Close Others"));
      auto* close_all_action = find_menu_action_by_text(*menu, QStringLiteral("Close All"));
      CHECK(close_action != nullptr);
      CHECK(close_others_action != nullptr);
      CHECK(close_all_action != nullptr);
      CHECK(close_action->objectName() == QStringLiteral("documentTabCloseAction"));
      CHECK(close_others_action->objectName() == QStringLiteral("documentTabCloseOthersAction"));
      CHECK(close_all_action->objectName() == QStringLiteral("documentTabCloseAllAction"));
      CHECK(close_others_action->isEnabled());
      close_others_action->trigger();
      menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });

  const auto context_point = tab_bar->tabRect(target_index).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point, tab_bar->mapToGlobal(context_point));
  QApplication::sendEvent(tab_bar, &context_event);
  QApplication::processEvents();

  CHECK(saw_context_menu);
  CHECK(tabs->count() == 1);
  CHECK(tabs->tabText(0) == QStringLiteral("Zulu"));
  CHECK(require_action(window, "fileCloseAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_W));
  auto* file_close_all_action = require_action(window, "fileCloseAllAction");
  CHECK(file_close_all_action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  file_close_all_action->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 0);
  CHECK(!file_close_all_action->isEnabled());
}

void ui_new_document_and_canvas_size_dialogs_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);

  accept_new_document_dialog(640, 360);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_new_document_result", window);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("1024 x 768 px")));
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_multiple_documents", window);

  accept_image_size_dialog(800, 450);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("300 ppi")));
  save_widget_artifact("ui_image_size_result", window);

  accept_image_size_resolution_dialog(144);
  require_action(window, "imageSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("800 x 450 px")));
  CHECK(info->text().contains(QStringLiteral("144 ppi")));

  accept_canvas_size_dialog(720, 405);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("720 x 405 px")));
  save_widget_artifact("ui_canvas_size_result", window);
}

void ui_new_document_presets_and_clipboard_work() {
  QApplication::clipboard()->clear();

  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(info != nullptr);
  CHECK(layer_list != nullptr);
  CHECK(tabs->count() == 1);

  inspect_new_document_dialog_without_clipboard();
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 1);

  QImage clipboard_image(123, 45, QImage::Format_RGBA8888);
  clipboard_image.fill(QColor(30, 160, 220, 180));
  QApplication::clipboard()->setImage(clipboard_image);
  QApplication::processEvents();

  accept_clipboard_new_document_dialog(clipboard_image.size());
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();

  CHECK(tabs->count() == 2);
  CHECK(info->text().contains(QStringLiteral("123 x 45 px")));
  CHECK(layer_list->count() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Clipboard Image")) != nullptr);
  auto* canvas = require_canvas(window);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == QPoint(0, 0));
  CHECK(pasted_rect->size() == clipboard_image.size());
  QApplication::clipboard()->clear();
}

void ui_new_document_background_starts_locked() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  const auto require_locked_background = [layer_list] {
    auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
    auto* background_row = layer_list->itemWidget(background_item);
    CHECK(background_row != nullptr);
    const auto badges = background_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
    CHECK(badges.size() == 1);
    CHECK(badges.front()->toolTip().contains(QStringLiteral("Position locked")));
    CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());
  };

  require_locked_background();

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  require_locked_background();
}

void ui_merge_down_into_position_locked_background_works() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->isSelected());

  canvas->set_primary_color(QColor(220, 30, 40));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));

  require_action(window, "layerMergeDownAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 1);
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  CHECK(background->isSelected());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));
  auto* background_row = layer_list->itemWidget(background);
  CHECK(background_row != nullptr);
  const auto badges = background_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(badges.size() == 1);
  CHECK(badges.front()->toolTip().contains(QStringLiteral("Position locked")));
  save_widget_artifact("ui_merge_down_position_locked_background", window);
}

void ui_first_tab_still_draws_after_second_tab_created() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(30, 190, 255));
  drag(*canvas, QPoint(80, 80), QPoint(200, 120));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(140, 100)), QColor(30, 190, 255), 80));
  save_widget_artifact("ui_first_tab_draw_after_second_tab", *canvas);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_context_lifecycle_tests() {
  return {
      {"ui_layer_style_color_overlay_patch_double_click_opens_picker",
       ui_layer_style_color_overlay_patch_double_click_opens_picker},
      {"ui_layer_context_menu_exposes_blending_options_dialog",
       ui_layer_context_menu_exposes_blending_options_dialog},
      {"ui_layer_row_double_click_opens_blending_options_dialog",
       ui_layer_row_double_click_opens_blending_options_dialog},
      {"ui_layer_fx_badge_button_opens_layer_style_dialog", ui_layer_fx_badge_button_opens_layer_style_dialog},
      {"ui_layer_style_stroke_blend_mode_round_trips", ui_layer_style_stroke_blend_mode_round_trips},
      {"ui_layer_row_double_click_skips_folders_and_edits_adjustments",
       ui_layer_row_double_click_skips_folders_and_edits_adjustments},
      {"ui_layer_context_menu_rasterizes_text_and_layer_styles",
       ui_layer_context_menu_rasterizes_text_and_layer_styles},
      {"ui_layer_context_menu_layer_style_actions_follow_selection_state",
       ui_layer_context_menu_layer_style_actions_follow_selection_state},
      {"ui_layer_style_copy_paste_delete_applies_to_selected_layers",
       ui_layer_style_copy_paste_delete_applies_to_selected_layers},
      {"ui_closing_last_document_leaves_empty_workspace", ui_closing_last_document_leaves_empty_workspace},
      {"ui_close_last_tab_with_active_text_edit_commits_editor_first",
       ui_close_last_tab_with_active_text_edit_commits_editor_first},
      {"ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys",
       ui_save_prompt_uses_yes_no_cancel_with_letter_hotkeys},
      {"ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all",
       ui_document_tab_context_menu_closes_tabs_and_file_menu_closes_all},
      {"ui_new_document_and_canvas_size_dialogs_work", ui_new_document_and_canvas_size_dialogs_work},
      {"ui_new_document_presets_and_clipboard_work", ui_new_document_presets_and_clipboard_work},
      {"ui_new_document_background_starts_locked", ui_new_document_background_starts_locked},
      {"ui_merge_down_into_position_locked_background_works",
       ui_merge_down_into_position_locked_background_works},
      {"ui_first_tab_still_draws_after_second_tab_created", ui_first_tab_still_draws_after_second_tab_created},
  };
}
