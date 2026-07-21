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
#include "smart_filter_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_gaussian_blur_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::Layer artwork(
      built.allocate_layer_id(), "Ordinary Pixels",
      solid_pixels(18, 14, patchy::PixelFormat::rgba8(),
                   QColor(220, 40, 30, 255)));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Gaussian"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(3);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_gaussian_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(!filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_high_pass_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(18, 14, patchy::PixelFormat::rgba8(),
                             QColor(220, 40, 30, 255));
  pixels.pixel(9, 7)[0] = 15;
  pixels.pixel(9, 7)[1] = 240;
  pixels.pixel(9, 7)[2] = 90;
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive High Pass"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->minimum() - 0.1) < 0.000001);
    CHECK(std::abs(radius->maximum() - 1000.0) < 0.000001);
    CHECK(slider->maximum() == 119);
    CHECK(std::abs(radius->value() - 10.0) < 0.000001);
    radius->setValue(4.25);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_high_pass")->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  for (std::int32_t y = 0; y < original_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < original_pixels.width(); ++x) {
      CHECK(filtered->pixels().pixel(x, y)[3] ==
            original_pixels.pixel(x, y)[3]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_median_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 29 + y * 31 + 64) % 256);
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Median"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 500.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(slider->maximum() == 2400);
    CHECK(std::abs(radius->value() - 1.0) < 0.000001);
    radius->setValue(2.75);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_median")->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_dust_and_scratches_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = static_cast<std::uint8_t>((x * 29 + y * 31 + 64) % 256);
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Dust and Scratches"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool callback_ran = false;
  bool dialog_found = false;
  bool controls_found = false;
  int radius_minimum = -1;
  int radius_maximum = -1;
  int radius_value = -1;
  int radius_slider_minimum = -1;
  int radius_slider_maximum = -1;
  int radius_value_after_typed_maximum = -1;
  int slider_value_after_typed_maximum = -1;
  int threshold_minimum = -1;
  int threshold_maximum = -1;
  int threshold_value = -1;
  QTimer::singleShot(0, [&] {
    callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* radius_slider =
        dialog->findChild<QSlider*>(QStringLiteral("filterRadiusSlider"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    controls_found =
        radius != nullptr && radius_slider != nullptr && threshold != nullptr;
    if (controls_found) {
      radius_minimum = radius->minimum();
      radius_maximum = radius->maximum();
      radius_value = radius->value();
      radius_slider_minimum = radius_slider->minimum();
      radius_slider_maximum = radius_slider->maximum();
      threshold_minimum = threshold->minimum();
      threshold_maximum = threshold->maximum();
      threshold_value = threshold->value();
      radius->setValue(100);
      radius_value_after_typed_maximum = radius->value();
      slider_value_after_typed_maximum = radius_slider->value();
      radius->setValue(2);
      threshold->setValue(17);
    }
    dialog->accept();
  });
  require_action(window,
                 "filterAction_patchy_filters_dust_and_scratches")
      ->trigger();
  QApplication::processEvents();
  CHECK(callback_ran);
  CHECK(dialog_found);
  CHECK(controls_found);
  CHECK(radius_minimum == 1 && radius_maximum == 100);
  CHECK(radius_slider_minimum == 1 && radius_slider_maximum == 25);
  CHECK(radius_value == 1);
  CHECK(radius_value_after_typed_maximum == 100);
  CHECK(slider_value_after_typed_maximum == 25);
  CHECK(threshold_minimum == 0 && threshold_maximum == 255);
  CHECK(threshold_value == 0);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  for (std::int32_t y = 0; y < original_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < original_pixels.width(); ++x) {
      CHECK(filtered->pixels().pixel(x, y)[3] ==
            original_pixels.pixel(x, y)[3]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_surface_blur_normal_pixel_layer_stays_destructive() {
  patchy::Document built(56, 44, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(18, 14, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 43 + y * 11) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 17 + y * 71) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 89 + y * 7) % 256);
      pixel[3] = 255U;
    }
  }
  patchy::Layer artwork(built.allocate_layer_id(), "Ordinary Pixels",
                        std::move(pixels));
  const auto layer_id = artwork.id();
  const patchy::Rect original_bounds{17, 15, 18, 14};
  artwork.set_bounds(original_bounds);
  const auto original_pixels = artwork.pixels();
  built.add_layer(std::move(artwork));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Destructive Surface Blur"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* radius_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && radius_slider != nullptr &&
          threshold != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 100.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(std::abs(radius->value() - 5.0) < 0.000001);
    CHECK(radius_slider->maximum() == 2400);
    CHECK(threshold->minimum() == 2 && threshold->maximum() == 255);
    CHECK(threshold->value() == 15);
    radius->setValue(100.0);
    CHECK(radius_slider->value() == radius_slider->maximum());
    radius->setValue(2.0);
    threshold->setValue(255);
    accepted = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_surface_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(!patchy::layer_is_smart_object(*filtered));
  CHECK(filtered->smart_filter_stack() == nullptr);
  CHECK(smart_filter_effect_record_count(document) == 0U);
  CHECK(filtered->bounds().x == original_bounds.x - 2);
  CHECK(filtered->bounds().y == original_bounds.y - 2);
  CHECK(filtered->bounds().width == original_bounds.width + 4);
  CHECK(filtered->bounds().height == original_bounds.height + 4);
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(document).find_layer(layer_id);
  CHECK(restored != nullptr && !patchy::layer_is_smart_object(*restored));
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_smart_filter_high_pass_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->value() - 10.0) < 0.000001);
    CHECK(std::abs(radius->minimum() - 0.1) < 0.000001);
    CHECK(std::abs(radius->maximum() - 1000.0) < 0.000001);
    CHECK(slider->maximum() == 1190);
    radius->setValue(4.25);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_high_pass")->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto radius_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind == patchy::SmartFilterKind::HighPass);
    const auto* high_pass = std::get_if<patchy::HighPassSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(high_pass != nullptr);
    return high_pass->radius_pixels;
  };
  CHECK(std::abs(radius_at() - 4.25) < 0.000001);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("High Pass"));
  CHECK(label->toolTip().contains(QStringLiteral("4.25 px")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    CHECK(std::abs(radius->value() - 4.25) < 0.000001);
    radius->setValue(9.75);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  CHECK(std::abs(radius_at() - 9.75) < 0.000001);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_high_pass.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::HighPass);
  const auto* reopened_high_pass = std::get_if<patchy::HighPassSmartFilter>(
      &reopened_entry.parameters);
  CHECK(reopened_high_pass != nullptr);
  CHECK(std::abs(reopened_high_pass->radius_pixels - 9.75) < 0.000001);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_high_pass_row", *active_row());
}

void ui_smart_filter_median_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    CHECK(radius != nullptr && slider != nullptr);
    CHECK(std::abs(radius->value() - 1.0) < 0.000001);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 500.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(slider->maximum() == 2400);
    radius->setValue(2.75);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_median")->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto radius_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind == patchy::SmartFilterKind::Median);
    const auto* median = std::get_if<patchy::MedianSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(median != nullptr);
    return median->radius_pixels;
  };
  CHECK(std::abs(radius_at() - 2.75) < 0.000001);
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Median"));
  CHECK(label->toolTip().contains(QStringLiteral("2.75 px")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    CHECK(std::abs(radius->value() - 2.75) < 0.000001);
    radius->setValue(7.5);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  CHECK(std::abs(radius_at() - 7.5) < 0.000001);
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr &&
        filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_median.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::Median);
  const auto* reopened_median = std::get_if<patchy::MedianSmartFilter>(
      &reopened_entry.parameters);
  CHECK(reopened_median != nullptr);
  CHECK(std::abs(reopened_median->radius_pixels - 7.5) < 0.000001);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_median_row", *active_row());
}

void ui_smart_filter_dust_and_scratches_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool apply_callback_ran = false;
  bool apply_dialog_found = false;
  bool apply_controls_found = false;
  int apply_radius_minimum = -1;
  int apply_radius_maximum = -1;
  int apply_radius_value = -1;
  int apply_slider_minimum = -1;
  int apply_slider_maximum = -1;
  int apply_threshold_minimum = -1;
  int apply_threshold_maximum = -1;
  int apply_threshold_value = -1;
  QTimer::singleShot(0, [&] {
    apply_callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    apply_dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* radius_slider =
        dialog->findChild<QSlider*>(QStringLiteral("filterRadiusSlider"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    apply_controls_found =
        radius != nullptr && radius_slider != nullptr && threshold != nullptr;
    if (apply_controls_found) {
      apply_radius_minimum = radius->minimum();
      apply_radius_maximum = radius->maximum();
      apply_radius_value = radius->value();
      apply_slider_minimum = radius_slider->minimum();
      apply_slider_maximum = radius_slider->maximum();
      apply_threshold_minimum = threshold->minimum();
      apply_threshold_maximum = threshold->maximum();
      apply_threshold_value = threshold->value();
      radius->setValue(7);
      threshold->setValue(23);
    }
    dialog->accept();
  });
  require_action(window,
                 "filterAction_patchy_filters_dust_and_scratches")
      ->trigger();
  QApplication::processEvents();
  CHECK(apply_callback_ran);
  CHECK(apply_dialog_found);
  CHECK(apply_controls_found);
  CHECK(apply_radius_value == 1);
  CHECK(apply_radius_minimum == 1 && apply_radius_maximum == 100);
  CHECK(apply_slider_minimum == 1 && apply_slider_maximum == 25);
  CHECK(apply_threshold_value == 0);
  CHECK(apply_threshold_minimum == 0 && apply_threshold_maximum == 255);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto parameters_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    CHECK(stack.entries.front().kind ==
          patchy::SmartFilterKind::DustAndScratches);
    CHECK(stack.entries.front().native_name == "Dust && Scratches...");
    CHECK(stack.entries.front().native_class_id == "DstS");
    CHECK(stack.entries.front().native_filter_id == 0x44737453U);
    const auto* dust = std::get_if<patchy::DustAndScratchesSmartFilter>(
        &stack.entries.front().parameters);
    CHECK(dust != nullptr);
    return std::pair{dust->radius_pixels, dust->threshold};
  };
  CHECK((parameters_at() ==
         std::pair{std::int32_t{7}, std::int32_t{23}}));
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  // The native Smart Object fixture is a constant green rectangle, so Dust &
  // Scratches is correctly an identity while still creating an editable stack.
  CHECK(patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr &&
        label->text() == QStringLiteral("Dust & Scratches"));
  CHECK(label->toolTip().contains(QStringLiteral("7 px")));
  CHECK(label->toolTip().contains(QStringLiteral("23")));

  bool edit_callback_ran = false;
  bool edit_dialog_found = false;
  bool edit_controls_found = false;
  int edit_radius_value = -1;
  int edit_threshold_value = -1;
  QTimer::singleShot(20, [&] {
    edit_callback_ran = true;
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    edit_dialog_found = dialog != nullptr;
    if (dialog == nullptr) {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* visible_dialog = qobject_cast<QDialog*>(widget);
        if (visible_dialog != nullptr && visible_dialog->isVisible()) {
          visible_dialog->reject();
          break;
        }
      }
      return;
    }
    auto* radius =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    auto* threshold =
        dialog->findChild<QSpinBox*>(QStringLiteral("filterThresholdSpin"));
    edit_controls_found = radius != nullptr && threshold != nullptr;
    if (edit_controls_found) {
      edit_radius_value = radius->value();
      edit_threshold_value = threshold->value();
      radius->setValue(9);
      threshold->setValue(31);
    }
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edit_callback_ran; }));
  CHECK(edit_dialog_found);
  CHECK(edit_controls_found);
  CHECK(edit_radius_value == 7);
  CHECK(edit_threshold_value == 23);
  CHECK((parameters_at() ==
         std::pair{std::int32_t{9}, std::int32_t{31}}));
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr &&
        filter_rect_equal(filtered->bounds(), original_bounds));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_dust_and_scratches.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind ==
        patchy::SmartFilterKind::DustAndScratches);
  CHECK(reopened_entry.native_name == "Dust && Scratches...");
  const auto* reopened_dust =
      std::get_if<patchy::DustAndScratchesSmartFilter>(
          &reopened_entry.parameters);
  CHECK(reopened_dust != nullptr);
  CHECK(reopened_dust->radius_pixels == 9);
  CHECK(reopened_dust->threshold == 31);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_dust_and_scratches_row",
                       *active_row());
}

void ui_smart_filter_surface_blur_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_pixels = original->pixels();
  const auto original_bounds = original->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool applied = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* radius_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterRadiusSlider"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && radius_slider != nullptr &&
          threshold != nullptr);
    CHECK(std::abs(radius->minimum() - 1.0) < 0.000001);
    CHECK(std::abs(radius->maximum() - 100.0) < 0.000001);
    CHECK(std::abs(radius->singleStep() - 0.01) < 0.000001);
    CHECK(std::abs(radius->value() - 5.0) < 0.000001);
    CHECK(radius_slider->maximum() == 2400);
    CHECK(threshold->minimum() == 2 && threshold->maximum() == 255);
    CHECK(threshold->value() == 15);
    radius->setValue(2.0);
    threshold->setValue(255);
    applied = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_surface_blur")
      ->trigger();
  QApplication::processEvents();
  CHECK(applied);

  const auto require_stack = [&]() -> const patchy::SmartFilterStack& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto parameters_at = [&]() {
    const auto& stack = require_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    const auto& entry = stack.entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::SurfaceBlur);
    CHECK(entry.native_name == "Surface Blur...");
    CHECK(entry.native_class_id == "surfaceBlur");
    CHECK(entry.native_filter_id == 854U);
    const auto* surface =
        std::get_if<patchy::SurfaceBlurSmartFilter>(&entry.parameters);
    CHECK(surface != nullptr);
    return std::pair{surface->radius_pixels, surface->threshold};
  };
  {
    const auto [radius, threshold] = parameters_at();
    CHECK(std::abs(radius - 2.0) < 0.000001);
    CHECK(threshold == 255);
  }
  const auto* filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filtered->bounds().x == original_bounds.x - 2);
  CHECK(filtered->bounds().y == original_bounds.y - 2);
  CHECK(filtered->bounds().width == original_bounds.width + 4);
  CHECK(filtered->bounds().height == original_bounds.height + 4);
  CHECK(!patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto* record = std::as_const(document)
                           .metadata()
                           .smart_filter_effects.find_unique(placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  const auto active_row = [&]() -> QWidget* {
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto* row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto* label = active_row()->findChild<QLabel*>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Surface Blur"));
  CHECK(label->toolTip().contains(QStringLiteral("Radius 2 px")));
  CHECK(label->toolTip().contains(QStringLiteral("Threshold 255")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    auto* threshold = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterThresholdSpin"));
    CHECK(radius != nullptr && threshold != nullptr);
    CHECK(std::abs(radius->value() - 2.0) < 0.000001);
    CHECK(threshold->value() == 255);
    radius->setValue(9.25);
    threshold->setValue(31);
    edited = true;
    dialog->accept();
  });
  auto* edit = active_row()->findChild<QToolButton*>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  {
    const auto [radius, threshold] = parameters_at();
    CHECK(std::abs(radius - 9.25) < 0.000001);
    CHECK(threshold == 31);
  }
  filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  CHECK(filter_rect_equal(filtered->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(filtered->pixels(), original_pixels));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_surface_blur.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer& layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto& reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::SurfaceBlur);
  CHECK(reopened_entry.native_name == "Surface Blur...");
  CHECK(reopened_entry.native_class_id == "surfaceBlur");
  CHECK(reopened_entry.native_filter_id == 854U);
  const auto* reopened_surface =
      std::get_if<patchy::SurfaceBlurSmartFilter>(
          &reopened_entry.parameters);
  CHECK(reopened_surface != nullptr);
  CHECK(std::abs(reopened_surface->radius_pixels - 9.25) < 0.000001);
  CHECK(reopened_surface->threshold == 31);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_surface_blur_row", *active_row());
}

void ui_smart_filter_plastic_wrap_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto &document = patchy::ui::MainWindowTestAccess::document(window);
  const auto *original = std::as_const(document).find_layer(layer_id);
  CHECK(original != nullptr && original->smart_filter_stack() == nullptr);
  const auto original_bounds = original->bounds();
  const auto original_record_count = smart_filter_effect_record_count(document);

  bool added = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *highlight = dialog->findChild<QSpinBox *>(
        QStringLiteral("filterHighlightStrengthSpin"));
    auto *detail =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterDetailSpin"));
    auto *smoothness = dialog->findChild<QSpinBox *>(
        QStringLiteral("filterSmoothnessSpin"));
    CHECK(highlight != nullptr && detail != nullptr && smoothness != nullptr);
    CHECK(highlight->minimum() == 0 && highlight->maximum() == 20 &&
          highlight->value() == 9);
    CHECK(detail->minimum() == 1 && detail->maximum() == 15 &&
          detail->value() == 7);
    CHECK(smoothness->minimum() == 1 && smoothness->maximum() == 15 &&
          smoothness->value() == 5);
    highlight->setValue(13);
    detail->setValue(9);
    smoothness->setValue(7);
    added = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_plastic_wrap")
      ->trigger();
  QApplication::processEvents();
  CHECK(added);

  const auto parameters_at = [&]() {
    const auto *layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    const auto &stack = *layer->smart_filter_stack();
    CHECK(stack.support == patchy::SmartFilterStackSupport::Supported);
    CHECK(stack.entries.size() == 1U);
    const auto &entry = stack.entries.front();
    CHECK(entry.kind == patchy::SmartFilterKind::PlasticWrap);
    CHECK(entry.native_name == "Plastic Wrap...");
    CHECK(entry.native_class_id == "PlsW");
    CHECK(entry.native_filter_id == 0x506C7357U);
    const auto *plastic =
        std::get_if<patchy::PlasticWrapSmartFilter>(&entry.parameters);
    CHECK(plastic != nullptr);
    return std::array<std::int32_t, 3>{plastic->highlight_strength,
                                       plastic->detail,
                                       plastic->smoothness};
  };
  CHECK((parameters_at() == std::array<std::int32_t, 3>{13, 9, 7}));
  const auto *filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr && filter_rect_equal(filtered->bounds(),
                                                  original_bounds));
  CHECK(smart_filter_effect_record_count(document) ==
        original_record_count + 1U);

  const auto active_row = [&]() -> QWidget * {
    auto *list = window.findChild<QListWidget *>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto *row = list->itemWidget(
        require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  auto *label = active_row()->findChild<QLabel *>(
      QStringLiteral("layerSmartFilterEntryLabel"));
  CHECK(label != nullptr && label->text() == QStringLiteral("Plastic Wrap"));
  CHECK(label->toolTip().contains(QStringLiteral("Highlight 13")));
  CHECK(label->toolTip().contains(QStringLiteral("Detail 9")));
  CHECK(label->toolTip().contains(QStringLiteral("Smoothness 7")));

  bool edited = false;
  QTimer::singleShot(20, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *highlight = dialog->findChild<QSpinBox *>(
        QStringLiteral("filterHighlightStrengthSpin"));
    auto *detail =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterDetailSpin"));
    auto *smoothness = dialog->findChild<QSpinBox *>(
        QStringLiteral("filterSmoothnessSpin"));
    CHECK(highlight != nullptr && detail != nullptr && smoothness != nullptr);
    CHECK(highlight->value() == 13 && detail->value() == 9 &&
          smoothness->value() == 7);
    highlight->setValue(20);
    detail->setValue(15);
    smoothness->setValue(1);
    edited = true;
    dialog->accept();
  });
  auto *edit = active_row()->findChild<QToolButton *>(
      QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] { return edited; }));
  CHECK((parameters_at() == std::array<std::int32_t, 3>{20, 15, 1}));

  ensure_artifact_dir();
  const auto artifact_path = std::filesystem::absolute(
      std::filesystem::path("test-artifacts") /
      "ui_smart_filter_plastic_wrap.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer &layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  CHECK(reopened_it->smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_it->smart_filter_stack()->entries.size() == 1U);
  const auto &reopened_entry =
      reopened_it->smart_filter_stack()->entries.front();
  CHECK(reopened_entry.kind == patchy::SmartFilterKind::PlasticWrap);
  CHECK(reopened_entry.native_name == "Plastic Wrap...");
  CHECK(reopened_entry.native_class_id == "PlsW");
  CHECK(reopened_entry.native_filter_id == 0x506C7357U);
  const auto *reopened_plastic =
      std::get_if<patchy::PlasticWrapSmartFilter>(
          &reopened_entry.parameters);
  CHECK(reopened_plastic != nullptr);
  CHECK(reopened_plastic->highlight_strength == 20);
  CHECK(reopened_plastic->detail == 15);
  CHECK(reopened_plastic->smoothness == 1);
  CHECK(filter_rect_equal(reopened_it->bounds(), original_bounds));
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_plastic_wrap_row", *active_row());
}

void ui_smart_filter_unsharp_motion_add_edit_and_reopen() {
  SettingsValueRestorer notes_setting(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(
      QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto &document = patchy::ui::MainWindowTestAccess::document(window);

  bool unsharp_added = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *amount =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterAmountSpin"));
    auto *radius =
        dialog->findChild<QDoubleSpinBox *>(QStringLiteral("filterRadiusSpin"));
    auto *threshold =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterThresholdSpin"));
    CHECK(amount != nullptr && radius != nullptr && threshold != nullptr);
    CHECK(amount->minimum() == 1 && amount->maximum() == 500);
    CHECK(std::abs(radius->minimum() - 0.1) < 0.000001);
    CHECK(std::abs(radius->maximum() - 1000.0) < 0.000001);
    CHECK(threshold->minimum() == 0 && threshold->maximum() == 255);
    amount->setValue(175);
    radius->setValue(2.5);
    threshold->setValue(7);
    unsharp_added = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_unsharp_mask")->trigger();
  QApplication::processEvents();
  CHECK(unsharp_added);

  bool motion_added = false;
  QTimer::singleShot(0, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *angle =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterAngleSpin"));
    auto *distance =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterDistanceSpin"));
    CHECK(angle != nullptr && distance != nullptr);
    CHECK(angle->minimum() == -360 && angle->maximum() == 360);
    CHECK(distance->minimum() == 1 && distance->maximum() == 999);
    angle->setValue(37);
    distance->setValue(12);
    motion_added = true;
    dialog->accept();
  });
  require_action(window, "filterAction_patchy_filters_motion_blur")->trigger();
  QApplication::processEvents();
  CHECK(motion_added);

  const auto stack = [&]() -> const patchy::SmartFilterStack & {
    const auto *layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer->smart_filter_stack();
  };
  const auto unsharp = [&]() -> const patchy::UnsharpMaskSmartFilter & {
    CHECK(stack().entries.size() == 2U);
    const auto &entry = stack().entries[0];
    CHECK(entry.kind == patchy::SmartFilterKind::UnsharpMask);
    CHECK(entry.native_name == "Unsharp Mask...");
    CHECK(entry.native_class_id == "UnsM");
    CHECK(entry.native_filter_id == 0x556e734dU);
    const auto *settings =
        std::get_if<patchy::UnsharpMaskSmartFilter>(&entry.parameters);
    CHECK(settings != nullptr);
    return *settings;
  };
  const auto motion = [&]() -> const patchy::MotionBlurSmartFilter & {
    CHECK(stack().entries.size() == 2U);
    const auto &entry = stack().entries[1];
    CHECK(entry.kind == patchy::SmartFilterKind::MotionBlur);
    CHECK(entry.native_name == "Motion Blur...");
    CHECK(entry.native_class_id == "MtnB");
    CHECK(entry.native_filter_id == 0x4d746e42U);
    const auto *settings =
        std::get_if<patchy::MotionBlurSmartFilter>(&entry.parameters);
    CHECK(settings != nullptr);
    return *settings;
  };
  CHECK(std::abs(unsharp().amount_percent - 175.0) < 0.000001);
  CHECK(std::abs(unsharp().radius_pixels - 2.5) < 0.000001);
  CHECK(unsharp().threshold == 7);
  CHECK(motion().angle_degrees == 37 && motion().distance_pixels == 12);

  const auto active_row = [&]() -> QWidget * {
    auto *list = window.findChild<QListWidget *>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    auto *row =
        list->itemWidget(require_layer_item(*list, QStringLiteral("small")));
    CHECK(row != nullptr);
    return row;
  };
  const auto label_for = [&](std::size_t execution_index) {
    const auto labels = active_row()->findChildren<QLabel *>(
        QStringLiteral("layerSmartFilterEntryLabel"));
    const auto found = std::find_if(
        labels.begin(), labels.end(), [execution_index](QLabel *label) {
          return label->property("smartFilterExecutionIndex").toULongLong() ==
                 static_cast<qulonglong>(execution_index);
        });
    CHECK(found != labels.end());
    return *found;
  };
  const auto edit_for = [&](std::size_t execution_index) {
    const auto buttons = active_row()->findChildren<QToolButton *>(
        QStringLiteral("layerSmartFilterEditButton"));
    const auto found = std::find_if(
        buttons.begin(), buttons.end(), [execution_index](QToolButton *button) {
          return button->property("smartFilterExecutionIndex").toULongLong() ==
                 static_cast<qulonglong>(execution_index);
        });
    CHECK(found != buttons.end());
    return *found;
  };
  CHECK(label_for(0)->text() == QStringLiteral("Unsharp Mask"));
  CHECK(label_for(0)->toolTip().contains(QStringLiteral("Amount 175%")));
  CHECK(label_for(0)->toolTip().contains(QStringLiteral("Radius 2.5 px")));
  CHECK(label_for(1)->text() == QStringLiteral("Motion Blur"));
  CHECK(label_for(1)->toolTip().contains(QStringLiteral("Angle 37 degrees")));
  CHECK(label_for(1)->toolTip().contains(QStringLiteral("Distance 12 px")));

  bool unsharp_edited = false;
  QTimer::singleShot(20, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *amount =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterAmountSpin"));
    auto *radius =
        dialog->findChild<QDoubleSpinBox *>(QStringLiteral("filterRadiusSpin"));
    auto *threshold =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterThresholdSpin"));
    CHECK(amount != nullptr && radius != nullptr && threshold != nullptr);
    CHECK(amount->value() == 175);
    CHECK(std::abs(radius->value() - 2.5) < 0.000001);
    CHECK(threshold->value() == 7);
    amount->setValue(225);
    radius->setValue(4.75);
    threshold->setValue(11);
    unsharp_edited = true;
    dialog->accept();
  });
  edit_for(0)->click();
  CHECK(process_events_until([&] { return unsharp_edited; }));
  CHECK(std::abs(unsharp().amount_percent - 225.0) < 0.000001);
  CHECK(std::abs(unsharp().radius_pixels - 4.75) < 0.000001);
  CHECK(unsharp().threshold == 11);

  bool motion_edited = false;
  QTimer::singleShot(20, [&] {
    auto *dialog = qobject_cast<QDialog *>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto *angle =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterAngleSpin"));
    auto *distance =
        dialog->findChild<QSpinBox *>(QStringLiteral("filterDistanceSpin"));
    CHECK(angle != nullptr && distance != nullptr);
    CHECK(angle->value() == 37 && distance->value() == 12);
    angle->setValue(-61);
    distance->setValue(27);
    motion_edited = true;
    dialog->accept();
  });
  edit_for(1)->click();
  CHECK(process_events_until([&] { return motion_edited; }));
  CHECK(motion().angle_degrees == -61 && motion().distance_pixels == 27);

  const auto *filtered = std::as_const(document).find_layer(layer_id);
  CHECK(filtered != nullptr);
  const auto placed_uuid = patchy::smart_object_placed_uuid(*filtered);
  const auto *record =
      std::as_const(document).metadata().smart_filter_effects.find_unique(
          placed_uuid);
  CHECK(record != nullptr && record->semantic_supported());

  ensure_artifact_dir();
  const auto artifact_path =
      std::filesystem::absolute(std::filesystem::path("test-artifacts") /
                                "ui_smart_filter_unsharp_motion.psd");
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, artifact_path);
  const auto reopened = patchy::psd::DocumentIo::read_file(artifact_path);
  const auto reopened_it = std::find_if(
      reopened.layers().begin(), reopened.layers().end(),
      [](const patchy::Layer &layer) { return layer.name() == "small"; });
  CHECK(reopened_it != reopened.layers().end());
  CHECK(reopened_it->smart_filter_stack() != nullptr);
  const auto &reopened_stack = *reopened_it->smart_filter_stack();
  CHECK(reopened_stack.support == patchy::SmartFilterStackSupport::Supported);
  CHECK(reopened_stack.entries.size() == 2U);
  const auto *reopened_unsharp = std::get_if<patchy::UnsharpMaskSmartFilter>(
      &reopened_stack.entries[0].parameters);
  const auto *reopened_motion = std::get_if<patchy::MotionBlurSmartFilter>(
      &reopened_stack.entries[1].parameters);
  CHECK(reopened_unsharp != nullptr && reopened_motion != nullptr);
  CHECK(std::abs(reopened_unsharp->amount_percent - 225.0) < 0.000001);
  CHECK(std::abs(reopened_unsharp->radius_pixels - 4.75) < 0.000001);
  CHECK(reopened_unsharp->threshold == 11);
  CHECK(reopened_motion->angle_degrees == -61);
  CHECK(reopened_motion->distance_pixels == 27);
  CHECK(reopened.metadata().smart_filter_effects.find_unique(
            patchy::smart_object_placed_uuid(*reopened_it)) != nullptr);
  save_widget_artifact("ui_smart_filter_unsharp_motion_row", *active_row());
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part3() {
  return {
      {"ui_gaussian_blur_normal_pixel_layer_stays_destructive",
       ui_gaussian_blur_normal_pixel_layer_stays_destructive},
      {"ui_high_pass_normal_pixel_layer_stays_destructive",
       ui_high_pass_normal_pixel_layer_stays_destructive},
      {"ui_median_normal_pixel_layer_stays_destructive",
       ui_median_normal_pixel_layer_stays_destructive},
      {"ui_dust_and_scratches_normal_pixel_layer_stays_destructive",
       ui_dust_and_scratches_normal_pixel_layer_stays_destructive},
      {"ui_surface_blur_normal_pixel_layer_stays_destructive",
       ui_surface_blur_normal_pixel_layer_stays_destructive},
      {"ui_smart_filter_high_pass_add_edit_and_reopen",
       ui_smart_filter_high_pass_add_edit_and_reopen},
      {"ui_smart_filter_median_add_edit_and_reopen",
       ui_smart_filter_median_add_edit_and_reopen},
      {"ui_smart_filter_dust_and_scratches_add_edit_and_reopen",
       ui_smart_filter_dust_and_scratches_add_edit_and_reopen},
      {"ui_smart_filter_surface_blur_add_edit_and_reopen",
       ui_smart_filter_surface_blur_add_edit_and_reopen},
      {"ui_smart_filter_plastic_wrap_add_edit_and_reopen",
       ui_smart_filter_plastic_wrap_add_edit_and_reopen},
      {"ui_smart_filter_unsharp_motion_add_edit_and_reopen",
       ui_smart_filter_unsharp_motion_add_edit_and_reopen},
  };
}
