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
#include "destructive_filters_gallery_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_brightness_contrast_filter_applies_settings() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto* filter = registry.find("patchy.filters.brightness_contrast");
  CHECK(filter != nullptr);
  const auto spec = patchy::ui::filter_dialog_spec_for(*filter);
  CHECK(spec.display_name == QStringLiteral("Brightness/Contrast"));
  CHECK(spec.controls.size() == 2);
  CHECK(spec.controls[0].object_name == QStringLiteral("filterBrightness"));
  CHECK(spec.controls[0].value == 0);
  CHECK(spec.controls[1].object_name == QStringLiteral("filterContrast"));
  CHECK(spec.controls[1].value == 0);

  const auto source = solid_pixels(1, 1, patchy::PixelFormat::rgb8(), QColor(120, 70, 210));
  const auto apply = [&](int brightness, int contrast) {
    auto invocation = filter_invocation(registry, "patchy.filters.brightness_contrast");
    set_filter_integer(invocation, "brightness", brightness);
    set_filter_integer(invocation, "contrast", contrast);
    return patchy::ui::build_filter_preview_pixels(
        source, QRegion(), patchy::Rect{0, 0, 1, 1}, registry,
        patchy::ui::FilterPreviewSettings{true, std::move(invocation)});
  };
  const auto check_pixel = [](const patchy::PixelBuffer& pixels, int red, int green, int blue) {
    const auto* px = pixels.pixel(0, 0);
    CHECK(px[0] == red);
    CHECK(px[1] == green);
    CHECK(px[2] == blue);
  };

  check_pixel(apply(0, 0), 120, 70, 210);
  check_pixel(apply(24, 0), 144, 94, 234);
  check_pixel(apply(0, 25), 118, 56, 231);
  check_pixel(apply(10, 50), 126, 51, 255);
}

void ui_filter_preview_restores_unselected_region_runs() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto pixels = solid_pixels(4, 3, patchy::PixelFormat::rgb8(), QColor(20, 30, 40));
  QRegion selection(QRect(11, 21, 2, 1));
  selection = selection.united(QRegion(QRect(13, 22, 1, 1)));

  auto invocation = filter_invocation(registry, "patchy.filters.brightness_contrast");
  set_filter_integer(invocation, "brightness", 10);
  set_filter_integer(invocation, "contrast", 0);

  const auto result = patchy::ui::build_filter_preview_pixels(
      pixels, selection, patchy::Rect{10, 20, 4, 3}, registry,
      patchy::ui::FilterPreviewSettings{true, std::move(invocation)});

  for (std::int32_t y = 0; y < result.height(); ++y) {
    for (std::int32_t x = 0; x < result.width(); ++x) {
      const auto selected = selection.contains(QPoint(10 + x, 20 + y));
      const auto* px = result.pixel(x, y);
      CHECK(px[0] == static_cast<std::uint8_t>(selected ? 30 : 20));
      CHECK(px[1] == static_cast<std::uint8_t>(selected ? 40 : 30));
      CHECK(px[2] == static_cast<std::uint8_t>(selected ? 50 : 40));
    }
  }
}

void ui_median_selection_uses_full_layer_transparent_color_extension() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::PixelBuffer source(6, 1, patchy::PixelFormat::rgba8());
  source.clear(0U);
  for (std::int32_t x = 0; x < source.width(); ++x) {
    source.pixel(x, 0)[3] = 255U;
  }
  for (const auto x : {2, 5}) {
    auto* pixel = source.pixel(x, 0);
    pixel[0] = 255U;
    pixel[1] = 255U;
    pixel[2] = 255U;
  }
  source.pixel(4, 0)[3] = 0U;

  auto invocation =
      registry.default_invocation("patchy.filters.median");
  invocation.parameters["radius"] = 1.0;
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  const QRegion selection(QRect(3, 0, 1, 1));

  auto expected = registry.render(invocation, source, bounds, false).pixels;
  for (std::int32_t x = 0; x < source.width(); ++x) {
    if (!selection.contains(QPoint(x, 0))) {
      std::copy_n(source.pixel(x, 0), 4U, expected.pixel(x, 0));
    }
  }
  const auto result = patchy::ui::build_filter_preview_pixels(
      source, selection, bounds, registry,
      patchy::ui::FilterPreviewSettings{true, invocation});
  CHECK(patchy::ui::pixel_buffers_equal(result, expected));
  const auto* selected = result.pixel(3, 0);
  CHECK(selected[0] == 255U && selected[1] == 255U &&
        selected[2] == 255U && selected[3] == 255U);
}

void ui_dust_and_scratches_selection_uses_full_layer_transparent_color_extension() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto source = solid_pixels(6, 1, patchy::PixelFormat::rgba8(),
                             QColor(9, 19, 29, 0));
  auto* left = source.pixel(0, 0);
  left[0] = 240U;
  left[1] = 20U;
  left[2] = 30U;
  left[3] = 255U;
  auto* right = source.pixel(5, 0);
  right[0] = 20U;
  right[1] = 230U;
  right[2] = 70U;
  right[3] = 255U;

  auto invocation =
      registry.default_invocation("patchy.filters.dust_and_scratches");
  invocation.parameters["radius"] = std::int64_t{1};
  invocation.parameters["threshold"] = std::int64_t{255};
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  const QRegion selection(QRect(3, 0, 1, 1));

  auto expected = registry.render(invocation, source, bounds, false).pixels;
  for (std::int32_t x = 0; x < source.width(); ++x) {
    if (!selection.contains(QPoint(x, 0))) {
      std::copy_n(source.pixel(x, 0), 4U, expected.pixel(x, 0));
    }
  }
  const auto result = patchy::ui::build_filter_preview_pixels(
      source, selection, bounds, registry,
      patchy::ui::FilterPreviewSettings{true, invocation});
  CHECK(patchy::ui::pixel_buffers_equal(result, expected));
  const auto* selected = result.pixel(3, 0);
  CHECK(selected[0] == 20U && selected[1] == 230U &&
        selected[2] == 70U && selected[3] == 0U);
}

void ui_surface_blur_selection_uses_full_layer_transparent_color_extension() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto source = solid_pixels(6, 1, patchy::PixelFormat::rgba8(),
                             QColor(9, 19, 29, 0));
  auto* left = source.pixel(0, 0);
  left[0] = 240U;
  left[1] = 20U;
  left[2] = 30U;
  left[3] = 255U;
  auto* right = source.pixel(5, 0);
  right[0] = 20U;
  right[1] = 230U;
  right[2] = 70U;
  right[3] = 255U;

  auto invocation =
      registry.default_invocation("patchy.filters.surface_blur");
  invocation.parameters["radius"] = 1.0;
  invocation.parameters["threshold"] = std::int64_t{255};
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  const QRegion selection(QRect(3, 0, 1, 1));

  auto expected = source;
  registry.apply(invocation, expected);
  for (std::int32_t x = 0; x < source.width(); ++x) {
    if (!selection.contains(QPoint(x, 0))) {
      std::copy_n(source.pixel(x, 0), 4U, expected.pixel(x, 0));
    }
  }
  patchy::Rect result_bounds;
  const auto result = patchy::ui::build_filter_preview_pixels(
      source, selection, bounds, registry,
      patchy::ui::FilterPreviewSettings{true, invocation}, nullptr,
      &result_bounds);
  CHECK(filter_rect_equal(result_bounds, bounds));
  CHECK(patchy::ui::pixel_buffers_equal(result, expected));
  const auto* selected = result.pixel(3, 0);
  CHECK(selected[0] == 74U && selected[1] == 177U &&
        selected[2] == 57U && selected[3] == 0U);
}

void ui_tilt_shift_blur_dialog_cancel_selection_apply_and_undo() {
  patchy::Document built(64, 52, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(29, 23, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>((x * 41 + y * 17 + 3) % 256);
      pixel[1] = static_cast<std::uint8_t>((x * 13 + y * 59 + 11) % 256);
      pixel[2] = static_cast<std::uint8_t>((x * 73 + y * 7 + 29) % 256);
      pixel[3] = static_cast<std::uint8_t>(96 + (x * 19 + y * 23) % 160);
    }
  }
  patchy::Layer layer(built.allocate_layer_id(), "Tilt Pixels",
                      std::move(pixels));
  const auto layer_id = layer.id();
  const patchy::Rect original_bounds{16, 13, 29, 23};
  layer.set_bounds(original_bounds);
  const auto original_pixels = layer.pixels();
  built.add_layer(std::move(layer));
  built.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built),
                              QStringLiteral("Tilt-Shift Blur Dialog"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* action = require_action(
      window, "filterAction_patchy_filters_tilt_shift_blur");
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  bool inspected_controls = false;
  bool saw_cancel_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* blur = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterBlurSpin"));
    auto* blur_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterBlurSlider"));
    auto* center_x = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    auto* center_y = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    auto* angle = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterAngleSpin"));
    auto* focus = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterFocusHalfWidthSpin"));
    auto* transition = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterTransitionWidthSpin"));
    CHECK(blur != nullptr && blur_slider != nullptr && center_x != nullptr &&
          center_y != nullptr && angle != nullptr && focus != nullptr &&
          transition != nullptr);
    CHECK(blur->minimum() == 0.0 && blur->maximum() == 500.0);
    CHECK(blur->singleStep() == 0.1 && blur->value() == 15.0);
    CHECK(blur_slider->minimum() == 0 && blur_slider->maximum() == 500);
    CHECK(center_x->minimum() == 0.0 && center_x->maximum() == 100.0 &&
          center_x->value() == 50.0);
    CHECK(center_y->minimum() == 0.0 && center_y->maximum() == 100.0 &&
          center_y->value() == 50.0);
    CHECK(angle->minimum() == -180 && angle->maximum() == 180 &&
          angle->value() == 0);
    CHECK(focus->minimum() == 0.0 && focus->maximum() == 100.0 &&
          focus->value() == 10.0);
    CHECK(transition->minimum() == 0.0 && transition->maximum() == 100.0 &&
          transition->value() == 20.0);
    inspected_controls = true;

    blur->setValue(4.0);
    center_y->setValue(0.0);
    focus->setValue(0.0);
    transition->setValue(0.0);
    CHECK(process_events_until(
        [&] {
          const auto& document = std::as_const(
              patchy::ui::MainWindowTestAccess::document(window));
          const auto* preview = document.find_layer(layer_id);
          if (preview == nullptr) {
            return false;
          }
          saw_cancel_preview =
              !filter_rect_equal(preview->bounds(), original_bounds) ||
              !patchy::ui::pixel_buffers_equal(preview->pixels(),
                                               original_pixels);
          return saw_cancel_preview;
        },
        5000));
    dialog->reject();
  });
  action->trigger();
  process_events_for(100);
  CHECK(inspected_controls && saw_cancel_preview);
  {
    const auto& document = std::as_const(
        patchy::ui::MainWindowTestAccess::document(window));
    const auto* cancelled = document.find_layer(layer_id);
    CHECK(cancelled != nullptr);
    CHECK(filter_rect_equal(cancelled->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(cancelled->pixels(),
                                          original_pixels));
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  patchy::PixelBuffer selection(64, 52, patchy::PixelFormat::gray8());
  selection.clear(0U);
  const QRect selected_rect(original_bounds.x + 5, original_bounds.y + 6,
                            original_bounds.width - 10,
                            original_bounds.height - 11);
  for (int y = selected_rect.top(); y <= selected_rect.bottom(); ++y) {
    for (int x = selected_rect.left(); x <= selected_rect.right(); ++x) {
      selection.pixel(x, y)[0] = 255U;
    }
  }
  canvas->replace_selection_from_grayscale(
      selection, QStringLiteral("Tilt-Shift selection"));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  const auto undo_before_apply =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  bool accepted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(
        find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* blur = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterBlurSpin"));
    auto* center_y = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    auto* focus = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterFocusHalfWidthSpin"));
    auto* transition = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterTransitionWidthSpin"));
    CHECK(blur != nullptr && center_y != nullptr && focus != nullptr &&
          transition != nullptr);
    blur->setValue(3.5);
    center_y->setValue(0.0);
    focus->setValue(0.0);
    transition->setValue(0.0);
    accepted = true;
    dialog->accept();
  });
  action->trigger();
  QApplication::processEvents();
  CHECK(accepted);

  const auto& applied_document = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window));
  const auto* applied = applied_document.find_layer(layer_id);
  CHECK(applied != nullptr);
  CHECK(filter_rect_equal(applied->bounds(), original_bounds));
  bool changed_inside = false;
  for (std::int32_t y = 0; y < original_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < original_pixels.width(); ++x) {
      const auto equal = std::equal(original_pixels.pixel(x, y),
                                    original_pixels.pixel(x, y) + 4,
                                    applied->pixels().pixel(x, y));
      if (selected_rect.contains(original_bounds.x + x,
                                 original_bounds.y + y)) {
        changed_inside = changed_inside || !equal;
      } else {
        CHECK(equal);
      }
    }
  }
  CHECK(changed_inside);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_apply + 1U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto* restored = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window)).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(filter_rect_equal(restored->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(restored->pixels(), original_pixels));
}

void ui_blur_grows_layer_into_transparency() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);

  const auto source = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(220, 28, 24));
  const patchy::Rect bounds{10, 20, 24, 24};

  // With no selection a box blur grows the layer so it can fade into the
  // surrounding transparency instead of stopping at a hard rectangular edge.
  auto invocation = filter_invocation(registry, "patchy.filters.box_blur");
  set_filter_integer(invocation, "radius", 4);
  patchy::Rect grown_bounds = bounds;
  const auto grown = patchy::ui::build_filter_preview_pixels(
      source, QRegion(), bounds, registry, patchy::ui::FilterPreviewSettings{true, invocation}, nullptr,
      &grown_bounds);

  CHECK(grown.width() > source.width());
  CHECK(grown.height() > source.height());
  CHECK(grown_bounds.x < bounds.x);
  CHECK(grown_bounds.y < bounds.y);
  CHECK(grown_bounds.width == grown.width());
  CHECK(grown_bounds.height == grown.height());

  // The original content stays opaque and red at the centre of the grown buffer.
  const auto* center = grown.pixel(grown.width() / 2, grown.height() / 2);
  CHECK(center[3] == 255);
  CHECK(center[0] > 170 && center[1] < 90 && center[2] < 90);

  // The outermost column was outside the original layer; it now carries a soft,
  // partially transparent red halo, proving the blur bled past the old box.
  bool found_soft_halo = false;
  for (std::int32_t y = 0; y < grown.height(); ++y) {
    const auto* px = grown.pixel(0, y);
    if (px[3] > 0 && px[3] < 255) {
      found_soft_halo = true;
      CHECK(px[0] > 150 && px[1] < 110 && px[2] < 110);
      break;
    }
  }
  CHECK(found_soft_halo);

  // With an active selection the filter stays confined to it and the layer keeps
  // its original bounds (matching Photoshop, which never grows for a selection).
  const QRegion selection(QRect(bounds.x, bounds.y, bounds.width, bounds.height));
  patchy::Rect selected_bounds = bounds;
  const auto selected = patchy::ui::build_filter_preview_pixels(
      source, selection, bounds, registry, patchy::ui::FilterPreviewSettings{true, invocation}, nullptr,
      &selected_bounds);
  CHECK(selected.width() == source.width());
  CHECK(selected.height() == source.height());
  CHECK(selected_bounds.x == bounds.x);
  CHECK(selected_bounds.y == bounds.y);
  CHECK(selected_bounds.width == bounds.width);
  CHECK(selected_bounds.height == bounds.height);
}

void ui_expanding_filter_cancel_and_undo_redo_restore_pixels_and_bounds() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Floating Blur",
                      solid_pixels(24, 20, patchy::PixelFormat::rgba8(), QColor(220, 28, 24, 255)));
  const auto layer_id = layer.id();
  const patchy::Rect original_bounds{42, 31, 24, 20};
  layer.set_bounds(original_bounds);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Expanding Blur"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* box_blur = require_action(window, "filterAction_patchy_filters_box_blur");
  const auto& original_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* original_layer = original_document.find_layer(layer_id);
  CHECK(original_layer != nullptr);
  const auto original_pixels = original_layer->pixels();
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  bool saw_expanded_preview = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(6);
    for (int attempt = 0; attempt < 80 && !saw_expanded_preview; ++attempt) {
      process_events_for(20);
      const auto& preview_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
      const auto* preview_layer = preview_document.find_layer(layer_id);
      CHECK(preview_layer != nullptr);
      saw_expanded_preview = !filter_rect_equal(preview_layer->bounds(), original_bounds) &&
                             preview_layer->pixels().width() > original_pixels.width() &&
                             preview_layer->pixels().height() > original_pixels.height();
    }
    dialog->reject();
  });
  box_blur->trigger();
  QApplication::processEvents();
  CHECK(saw_expanded_preview);
  {
    const auto& cancelled_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* cancelled_layer = cancelled_document.find_layer(layer_id);
    CHECK(cancelled_layer != nullptr);
    CHECK(filter_rect_equal(cancelled_layer->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(cancelled_layer->pixels(), original_pixels));
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* radius = dialog->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(6);
    dialog->accept();
  });
  box_blur->trigger();
  QApplication::processEvents();
  patchy::Rect applied_bounds;
  patchy::PixelBuffer applied_pixels;
  {
    const auto& applied_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* applied_layer = applied_document.find_layer(layer_id);
    CHECK(applied_layer != nullptr);
    applied_bounds = applied_layer->bounds();
    applied_pixels = applied_layer->pixels();
    CHECK(!filter_rect_equal(applied_bounds, original_bounds));
    CHECK(applied_pixels.width() > original_pixels.width());
    CHECK(applied_pixels.height() > original_pixels.height());
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  {
    const auto& undone_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* undone_layer = undone_document.find_layer(layer_id);
    CHECK(undone_layer != nullptr);
    CHECK(filter_rect_equal(undone_layer->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(undone_layer->pixels(), original_pixels));
  }

  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  {
    const auto& redone_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* redone_layer = redone_document.find_layer(layer_id);
    CHECK(redone_layer != nullptr);
    CHECK(filter_rect_equal(redone_layer->bounds(), applied_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(redone_layer->pixels(), applied_pixels));
  }
  CHECK(canvas->active_layer_document_rect().has_value());
  CHECK(*canvas->active_layer_document_rect() == QRect(applied_bounds.x, applied_bounds.y, applied_bounds.width,
                                                       applied_bounds.height));
}

bool filter_recipe_entries_equal(const patchy::FilterRecipeEntry& lhs,
                                 const patchy::FilterRecipeEntry& rhs) {
  return filter_invocations_equal(lhs.invocation, rhs.invocation) &&
         lhs.enabled == rhs.enabled && lhs.opacity == rhs.opacity &&
         lhs.blend_mode == rhs.blend_mode;
}

bool filter_recipes_equal(const patchy::FilterRecipe& lhs,
                          const patchy::FilterRecipe& rhs) {
  return lhs.entries.size() == rhs.entries.size() &&
         std::equal(lhs.entries.begin(), lhs.entries.end(),
                    rhs.entries.begin(), filter_recipe_entries_equal);
}

void ui_filter_look_library_round_trips_and_isolates_bad_records() {
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto storage = directory.filePath(QStringLiteral("looks"));

  patchy::FilterInvocation first;
  first.filter_id = "patchy.filters.clouds";
  first.schema_version = 7;
  first.parameters = {
      {"integer", std::int64_t{-42}},
      {"double", 12.375},
      {"boolean", true},
      {"string", std::string{"stable-option"}},
  };
  first.foreground = patchy::RgbColor{17, 34, 51};
  first.background = patchy::RgbColor{221, 187, 153};
  patchy::FilterInvocation second;
  second.filter_id = "future.filters.still_preserved";
  second.schema_version = 29;
  second.parameters = {{"amount", std::int64_t{73}}};
  second.foreground = patchy::RgbColor{1, 2, 3};
  second.background = patchy::RgbColor{4, 5, 6};
  const patchy::FilterRecipe recipe{{
      patchy::FilterRecipeEntry{first, false, 0.375,
                                patchy::BlendMode::Multiply},
      patchy::FilterRecipeEntry{second, true, 0.8,
                                patchy::BlendMode::Screen},
  }};

  patchy::ui::FilterLookLibraryError error =
      patchy::ui::FilterLookLibraryError::WriteFailed;
  patchy::ui::FilterLookLibrary library(storage);
  const auto id = library.add_look(QStringLiteral("  Okinawa Look  "),
                                   recipe, &error);
  CHECK(error == patchy::ui::FilterLookLibraryError::None);
  CHECK(id.size() == 36);
  CHECK(library.entries().size() == 1);
  CHECK(library.entries().front().id == id);
  CHECK(library.entries().front().name == QStringLiteral("Okinawa Look"));
  CHECK(filter_recipes_equal(library.entries().front().recipe, recipe));
  const auto json_path = QDir(storage).filePath(id + QStringLiteral(".json"));
  CHECK(QFileInfo::exists(json_path));

  // Unknown filters remain structurally valid and load unchanged. One broken
  // neighboring record must not hide the valid record.
  const auto malformed_id = QStringLiteral("11111111-1111-4111-8111-111111111111");
  QFile malformed(QDir(storage).filePath(malformed_id + QStringLiteral(".json")));
  CHECK(malformed.open(QIODevice::WriteOnly));
  CHECK(malformed.write("{ definitely not JSON") > 0);
  malformed.close();
  patchy::ui::FilterLookLibrary reloaded(storage);
  CHECK(reloaded.entries().size() == 1);
  const auto* loaded = reloaded.find_entry(id);
  CHECK(loaded != nullptr);
  CHECK(loaded->name == QStringLiteral("Okinawa Look"));
  CHECK(filter_recipes_equal(loaded->recipe, recipe));
  CHECK(loaded->recipe.entries[1].invocation.filter_id ==
        "future.filters.still_preserved");

  CHECK(reloaded.rename_look(id, QStringLiteral("  Evening Ride  "),
                             &error));
  CHECK(error == patchy::ui::FilterLookLibraryError::None);
  CHECK(reloaded.find_entry(id) != nullptr);
  CHECK(reloaded.find_entry(id)->id == id);
  CHECK(reloaded.find_entry(id)->name == QStringLiteral("Evening Ride"));
  CHECK(QFileInfo::exists(json_path));
  patchy::ui::FilterLookLibrary renamed(storage);
  CHECK(renamed.entries().size() == 1);
  CHECK(renamed.entries().front().id == id);
  CHECK(renamed.entries().front().name == QStringLiteral("Evening Ride"));
  CHECK(filter_recipes_equal(renamed.entries().front().recipe, recipe));

  patchy::FilterRecipe invalid = recipe;
  invalid.entries.front().opacity =
      std::numeric_limits<double>::quiet_NaN();
  CHECK(renamed.add_look(QStringLiteral("Invalid"), invalid, &error).isEmpty());
  CHECK(error == patchy::ui::FilterLookLibraryError::InvalidRecipe);
  CHECK(renamed.entries().size() == 1);

  CHECK(renamed.remove_look(id, &error));
  CHECK(error == patchy::ui::FilterLookLibraryError::None);
  CHECK(renamed.entries().empty());
  CHECK(!QFileInfo::exists(json_path));
  CHECK(!renamed.remove_look(id, &error));
  CHECK(error == patchy::ui::FilterLookLibraryError::NotFound);

  // A path occupied by a regular file exercises the failure path without
  // platform-specific permission assumptions. The failed atomic save must not
  // publish an in-memory entry.
  const auto blocked_path = directory.filePath(QStringLiteral("not-a-directory"));
  QFile blocked(blocked_path);
  CHECK(blocked.open(QIODevice::WriteOnly));
  CHECK(blocked.write("occupied") == 8);
  blocked.close();
  patchy::ui::FilterLookLibrary unavailable(blocked_path);
  CHECK(unavailable.add_look(QStringLiteral("Cannot Save"), recipe, &error)
            .isEmpty());
  CHECK(error == patchy::ui::FilterLookLibraryError::StorageUnavailable);
  CHECK(unavailable.entries().empty());
}

void ui_filter_recipe_selection_is_restored_once_after_the_full_stack() {
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::PixelBuffer source(13, 9, patchy::PixelFormat::rgba8());
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      auto* pixel = source.pixel(x, y);
      const bool stripe = ((x / 2) + (y / 2)) % 2 == 0;
      pixel[0] = stripe ? 238 : 12;
      pixel[1] = stripe ? 24 : 206;
      pixel[2] = stripe ? 44 : 232;
      pixel[3] = static_cast<std::uint8_t>(170 + ((x * 11 + y * 7) % 86));
    }
  }
  const patchy::Rect bounds{31, 47, source.width(), source.height()};
  const QRegion selection(QRect(bounds.x + 3, bounds.y + 2, 7, 5));
  auto blur = registry.default_invocation("patchy.filters.box_blur");
  blur.parameters["radius"] = std::int64_t{2};
  const patchy::FilterRecipe recipe{{patchy::FilterRecipeEntry{blur},
                                     patchy::FilterRecipeEntry{blur}}};

  auto expected = registry.render(recipe, source, bounds, false).pixels;
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      if (!selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        std::copy_n(source.pixel(x, y), 4, expected.pixel(x, y));
      }
    }
  }
  patchy::Rect result_bounds = bounds;
  const auto result = patchy::ui::build_filter_preview_pixels(
      source, selection, bounds, registry, recipe, nullptr, &result_bounds);
  CHECK(filter_rect_equal(result_bounds, bounds));
  CHECK(patchy::ui::pixel_buffers_equal(result, expected));

  auto restored_after_each_entry = source;
  for (int entry = 0; entry < 2; ++entry) {
    restored_after_each_entry = patchy::ui::build_filter_preview_pixels(
        restored_after_each_entry, selection, bounds, registry,
        patchy::ui::FilterPreviewSettings{true, blur});
  }
  CHECK(!patchy::ui::pixel_buffers_equal(result, restored_after_each_entry));
}

void ui_filter_gallery_stack_edits_reverse_order_and_stay_independent() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* remove = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryRemoveEffectButton"));
    auto* search = dialog->findChild<QLineEdit*>(
        QStringLiteral("filterGallerySearchEdit"));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(
        QStringLiteral("filterGalleryButtonBox"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          remove != nullptr && search != nullptr && status != nullptr &&
          buttons != nullptr);
    CHECK(applied->count() == 0);
    CHECK(!duplicate->isEnabled() && !remove->isEnabled());

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    CHECK(applied->count() == 1);
    CHECK(applied->item(0)->text() == QStringLiteral("Soft Glow"));
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.noir")));
    QApplication::processEvents();
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.sepia")));
    QApplication::processEvents();

    CHECK(applied->count() == 3);
    CHECK(applied->item(0)->text() == QStringLiteral("Vintage Sepia"));
    CHECK(applied->item(1)->text() == QStringLiteral("Noir"));
    CHECK(applied->item(2)->text() == QStringLiteral("Soft Glow"));
    CHECK(!previews.empty() && previews.back().recipe.has_value());
    CHECK(previews.back().recipe->entries.size() == 3);
    CHECK(previews.back().recipe->entries[0].invocation.filter_id ==
          "patchy.filters.soft_glow");
    CHECK(previews.back().recipe->entries[1].invocation.filter_id ==
          "patchy.filters.noir");
    CHECK(previews.back().recipe->entries[2].invocation.filter_id ==
          "patchy.filters.sepia");

    // A duplicate receives its own transient identity and parameter map.
    duplicate->click();
    QApplication::processEvents();
    CHECK(applied->count() == 4);
    auto* amount = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterAmountSpin"));
    CHECK(amount != nullptr && amount->value() == 100);
    amount->setValue(37);
    QApplication::processEvents();
    CHECK(previews.back().recipe->entries.size() == 4);
    CHECK(std::get<std::int64_t>(previews.back()
                                    .recipe->entries[2]
                                    .invocation.parameters.at("amount")) ==
          100);
    CHECK(std::get<std::int64_t>(previews.back()
                                    .recipe->entries[3]
                                    .invocation.parameters.at("amount")) ==
          37);
    remove->click();
    QApplication::processEvents();
    CHECK(applied->count() == 3);
    CHECK(previews.back().recipe->entries.size() == 3);

    const auto recipe_before_filter = *previews.back().recipe;
    search->setText(QStringLiteral("Clouds"));
    QApplication::processEvents();
    CHECK(applied->count() == 3);
    CHECK(applied->item(0)->text() == QStringLiteral("Vintage Sepia"));
    CHECK(applied->item(1)->text() == QStringLiteral("Noir"));
    CHECK(applied->item(2)->text() == QStringLiteral("Soft Glow"));
    CHECK(filter_recipes_equal(*previews.back().recipe,
                               recipe_before_filter));
    search->clear();
    QApplication::processEvents();

    // Visual rows are the reverse of execution order. Disable Noir, then move
    // the visually top Sepia row to the bottom and verify the persisted recipe
    // is rebuilt bottom-to-top.
    applied->item(1)->setCheckState(Qt::Unchecked);
    QApplication::processEvents();
    CHECK(!previews.back().recipe->entries[1].enabled);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        5000));
    save_widget_artifact("ui_filter_gallery_stack", *dialog);

    for (int row = 0; row < applied->count(); ++row) {
      CHECK(!applied->item(row)->flags().testFlag(Qt::ItemIsDropEnabled));
    }
    CHECK(applied->model()
              ->flags(QModelIndex())
              .testFlag(Qt::ItemIsDropEnabled));
    // Offscreen Qt cannot complete the nested native QDrag loop. This call
    // covers the model move and rowsMoved recipe synchronization; the
    // MainWindow integration test separately pins drag-event delivery through
    // the preview edit lock.
    CHECK(applied->model()->moveRow(QModelIndex(), 0, QModelIndex(), 3));
    CHECK(process_events_until(
        [&] {
          return previews.back().recipe.has_value() &&
                 previews.back().recipe->entries[0].invocation.filter_id ==
                     "patchy.filters.sepia";
        },
        1000));
    CHECK(applied->item(0)->text() == QStringLiteral("Noir"));
    CHECK(applied->item(1)->text() == QStringLiteral("Soft Glow"));
    CHECK(applied->item(2)->text() == QStringLiteral("Vintage Sepia"));
    CHECK(previews.back().recipe->entries[0].invocation.filter_id ==
          "patchy.filters.sepia");
    CHECK(previews.back().recipe->entries[1].invocation.filter_id ==
          "patchy.filters.soft_glow");
    CHECK(previews.back().recipe->entries[2].invocation.filter_id ==
          "patchy.filters.noir");
    CHECK(!previews.back().recipe->entries[2].enabled);
    drove_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry,
      patchy::RgbColor{20, 40, 60}, patchy::RgbColor{240, 220, 200},
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) {
        previews.push_back(preview);
      });
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Filter);
  CHECK(result.recipe.has_value());
  CHECK(previews.back().recipe.has_value());
  CHECK(filter_recipes_equal(*result.recipe, *previews.back().recipe));
}

// The applied-effects panel's Mode/Opacity controls edit the selected recipe
// entry's blending (the recipe model always carried these; Saved Looks
// persist them and the Smart Filter mapping copies them into native entries).
void ui_filter_gallery_blending_controls_edit_recipe_entries() {
  GallerySettingsRestorer gallery_settings;
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* mode = dialog->findChild<QComboBox*>(
        QStringLiteral("filterGalleryBlendModeCombo"));
    auto* opacity_spin = dialog->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterGalleryOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(
        QStringLiteral("filterGalleryOpacitySlider"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(
        QStringLiteral("filterGalleryButtonBox"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          mode != nullptr && opacity_spin != nullptr &&
          opacity_slider != nullptr && buttons != nullptr);
    // Empty recipe: the blending controls are disabled.
    CHECK(!mode->isEnabled() && !opacity_spin->isEnabled());

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    CHECK(mode->isEnabled() && opacity_spin->isEnabled());
    CHECK(static_cast<patchy::BlendMode>(mode->currentData().toInt()) ==
          patchy::BlendMode::Normal);
    CHECK(std::abs(opacity_spin->value() - 100.0) < 0.5);

    const auto multiply =
        mode->findData(static_cast<int>(patchy::BlendMode::Multiply));
    CHECK(multiply >= 0);
    mode->setCurrentIndex(multiply);
    opacity_spin->setValue(40.0);
    QApplication::processEvents();
    CHECK(opacity_slider->value() == 40);
    CHECK(!previews.empty() && previews.back().recipe.has_value());
    CHECK(previews.back().recipe->entries.size() == 1);
    CHECK(previews.back().recipe->entries[0].blend_mode ==
          patchy::BlendMode::Multiply);
    CHECK(std::abs(previews.back().recipe->entries[0].opacity - 0.4) <
          0.0000001);

    // Duplicate copies the blending; replacing the duplicate's filter keeps
    // it, like the enabled state.
    duplicate->click();
    QApplication::processEvents();
    CHECK(static_cast<patchy::BlendMode>(mode->currentData().toInt()) ==
          patchy::BlendMode::Multiply);
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.noir")));
    QApplication::processEvents();
    CHECK(previews.back().recipe->entries[1].invocation.filter_id ==
          "patchy.filters.noir");
    CHECK(previews.back().recipe->entries[1].blend_mode ==
          patchy::BlendMode::Multiply);
    const auto normal =
        mode->findData(static_cast<int>(patchy::BlendMode::Normal));
    CHECK(normal >= 0);
    mode->setCurrentIndex(normal);
    opacity_spin->setValue(75.0);
    QApplication::processEvents();
    CHECK(previews.back().recipe->entries[1].blend_mode ==
          patchy::BlendMode::Normal);
    CHECK(std::abs(previews.back().recipe->entries[1].opacity - 0.75) <
          0.0000001);

    // Switching applied rows resyncs the controls to that entry.
    applied->setCurrentItem(applied->item(1));
    QApplication::processEvents();
    CHECK(static_cast<patchy::BlendMode>(mode->currentData().toInt()) ==
          patchy::BlendMode::Multiply);
    CHECK(std::abs(opacity_spin->value() - 40.0) < 0.5);
    applied->setCurrentItem(applied->item(0));
    QApplication::processEvents();
    CHECK(static_cast<patchy::BlendMode>(mode->currentData().toInt()) ==
          patchy::BlendMode::Normal);
    CHECK(std::abs(opacity_spin->value() - 75.0) < 0.5);

    // Reset returns the active entry's blending to Normal/100% along with
    // its parameters.
    buttons->button(QDialogButtonBox::Reset)->click();
    QApplication::processEvents();
    CHECK(static_cast<patchy::BlendMode>(mode->currentData().toInt()) ==
          patchy::BlendMode::Normal);
    CHECK(std::abs(opacity_spin->value() - 100.0) < 0.5);
    CHECK(previews.back().recipe->entries[1].blend_mode ==
          patchy::BlendMode::Normal);
    CHECK(std::abs(previews.back().recipe->entries[1].opacity - 1.0) <
          0.0000001);

    drove_dialog = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry,
      patchy::RgbColor{20, 40, 60}, patchy::RgbColor{240, 220, 200},
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) {
        previews.push_back(preview);
      });
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Filter);
  CHECK(result.recipe.has_value());
  CHECK(result.recipe->entries.size() == 2);
  CHECK(result.recipe->entries[0].invocation.filter_id ==
        "patchy.filters.soft_glow");
  CHECK(result.recipe->entries[0].blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(result.recipe->entries[0].opacity - 0.4) < 0.0000001);
  CHECK(result.recipe->entries[1].invocation.filter_id ==
        "patchy.filters.noir");
  CHECK(result.recipe->entries[1].blend_mode == patchy::BlendMode::Normal);
  CHECK(std::abs(result.recipe->entries[1].opacity - 1.0) < 0.0000001);
}

void ui_filter_gallery_drag_events_reach_stack_during_preview_lock() {
  GallerySettingsRestorer gallery_settings;
  patchy::LayerId layer_id{};
  patchy::Rect original_bounds;
  patchy::PixelBuffer original_pixels;
  auto document = make_filter_gallery_document(
      layer_id, original_bounds, original_pixels);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document),
                              QStringLiteral("Gallery Drag Routing"));
  show_window(window);
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* layer_duplicate = window.findChild<QPushButton*>(
        QStringLiteral("layerDuplicateButton"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          layer_duplicate != nullptr);
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    duplicate->click();
    QApplication::processEvents();
    CHECK(applied->count() == 2);
    DragEventRecorder stack_events(true);
    DragEventRecorder layer_button_events(true);
    applied->viewport()->installEventFilter(&stack_events);
    layer_duplicate->installEventFilter(&layer_button_events);

    QMimeData stack_mime_data;
    stack_mime_data.setData(
        QStringLiteral("application/x-qabstractitemmodeldatalist"),
        QByteArrayLiteral("filter-recipe-entry"));
    QMimeData layer_mime_data;
    layer_mime_data.setData(
        QString::fromLatin1(patchy::ui::kLayerDragMimeType),
        patchy::ui::layer_ids_to_mime_data({layer_id}));
    const auto send_drag_sequence = [&](QWidget& target,
                                        QMimeData& mime_data) {
      const auto position = target.rect().center();
      QDragEnterEvent enter(position, Qt::MoveAction, &mime_data,
                            Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&target, &enter);
      QDragMoveEvent move(position, Qt::MoveAction, &mime_data,
                          Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&target, &move);
      QDropEvent drop(QPointF(position), Qt::MoveAction, &mime_data,
                      Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(&target, &drop);
      QDragLeaveEvent leave;
      QApplication::sendEvent(&target, &leave);
    };

    // The application-wide MainWindow filter must not consume drags owned by
    // the preview dialog. Qt's list view can then classify the real native
    // InternalMove and paint its insertion marker.
    send_drag_sequence(*applied->viewport(), stack_mime_data);
    CHECK(stack_events.enters == 1);
    CHECK(stack_events.moves == 1);
    CHECK(stack_events.drops == 1);
    CHECK(stack_events.leaves == 1);

    // The same preview lock still blocks document-mutating layer action drops.
    send_drag_sequence(*layer_duplicate, layer_mime_data);
    CHECK(layer_button_events.enters == 0);
    CHECK(layer_button_events.moves == 0);
    CHECK(layer_button_events.drops == 0);
    CHECK(layer_button_events.leaves == 0);

    applied->viewport()->removeEventFilter(&stack_events);
    layer_duplicate->removeEventFilter(&layer_button_events);
    drove_dialog = true;
    dialog->reject();
  });

  require_action(window, "filterGalleryAction")->trigger();
  CHECK(drove_dialog);
}

void ui_filter_gallery_stack_spatial_overlay_tracks_active_input_bounds() {
  GallerySettingsRestorer gallery_settings;
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview")));
    auto* editor = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryParameterEditor"));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          preview != nullptr && editor != nullptr && status != nullptr);

    // Seth's reported sequence: the active spatial effect must retain its
    // center/radius handles after it is created through Duplicate.
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.noir")));
    QApplication::processEvents();
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.twirl")));
    CHECK(process_events_until(
        [&] {
          return applied->count() == 3 &&
                 applied->item(0)->text() == QStringLiteral("Twirl") &&
                 preview->property("filterSpatialOverlayVisible").toBool() &&
                 preview->property("filterSpatialRadiusVisible").toBool();
        },
        5000));

    // Exercise nontrivial geometry: an expanding filter before and after the
    // active Twirl changes both its input bounds and the final displayed bounds.
    looks->setCurrentItem(looks->item(0));
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    QApplication::processEvents();
    auto* radius = editor->findChild<QSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(4);
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.twirl")));
    QApplication::processEvents();
    auto* center_x = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    auto* center_y = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    radius = editor->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(center_x != nullptr && center_y != nullptr && radius != nullptr);
    center_x->setValue(30.0);
    center_y->setValue(65.0);
    radius->setValue(70);
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    QApplication::processEvents();
    radius = editor->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(3);
    CHECK(process_events_until(
        [&] {
          return !previews.empty() && previews.back().recipe.has_value() &&
                 previews.back().recipe->entries.size() == 3 &&
                 status->text() ==
                     QCoreApplication::translate("QObject", "Ready");
        },
        5000));

    applied->setCurrentRow(1);
    QApplication::processEvents();
    CHECK(applied->currentItem()->text() == QStringLiteral("Twirl"));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterSpatialOverlayVisible").toBool() &&
                 preview->property("filterSpatialRadiusVisible").toBool();
        },
        1000));

    const auto recipe = *previews.back().recipe;
    patchy::FilterRecipe prefix;
    prefix.entries.push_back(recipe.entries.front());
    const auto active_input = registry.render(prefix, source, bounds);
    const auto final_render = registry.render(recipe, source, bounds);
    const auto mapped = [](double percent, int source_origin,
                           int source_extent, int result_origin,
                           int result_extent) {
      return (static_cast<double>(source_origin) +
              static_cast<double>(source_extent - 1) * percent / 100.0 -
              static_cast<double>(result_origin)) /
             static_cast<double>(result_extent - 1);
    };
    const auto expected_x =
        mapped(30.0, active_input.bounds.x, active_input.bounds.width,
               final_render.bounds.x, final_render.bounds.width);
    const auto expected_y =
        mapped(65.0, active_input.bounds.y, active_input.bounds.height,
               final_render.bounds.y, final_render.bounds.height);
    const auto expected_radius =
        0.70 *
        static_cast<double>(std::min(active_input.bounds.width,
                                     active_input.bounds.height)) /
        static_cast<double>(std::min(final_render.bounds.width,
                                     final_render.bounds.height));
    CHECK(std::abs(preview->property("filterCenterXNormalized").toDouble() -
                   expected_x) < 0.000001);
    CHECK(std::abs(preview->property("filterCenterYNormalized").toDouble() -
                   expected_y) < 0.000001);
    CHECK(std::abs(preview->property("filterRadiusNormalized").toDouble() -
                   expected_radius) < 0.000001);

    // Dragging the controls uses the inverse mapping: final-preview
    // coordinates must become percentages in the active entry's input bounds.
    center_x = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    center_y = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    radius = editor->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(center_x != nullptr && center_y != nullptr && radius != nullptr);
    const auto display_size = QSizeF(preview->image().width() * preview->zoom(),
                                     preview->image().height() * preview->zoom());
    const QRectF displayed(
        QPointF((preview->width() - display_size.width()) / 2.0,
                (preview->height() - display_size.height()) / 2.0),
        display_size);
    const auto center_point = [&](double x, double y) {
      return QPointF(displayed.left() + displayed.width() * x,
                     displayed.top() + displayed.height() * y)
          .toPoint();
    };
    const auto start_center = center_point(expected_x, expected_y);
    constexpr double target_x = 0.60;
    constexpr double target_y = 0.40;
    drag(*preview, start_center, center_point(target_x, target_y));
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        5000));
    const auto source_percent = [](double normalized, int source_origin,
                                   int source_extent, int result_origin,
                                   int result_extent) {
      return std::clamp(
          (static_cast<double>(result_origin) +
               normalized * static_cast<double>(result_extent - 1) -
           static_cast<double>(source_origin)) /
              static_cast<double>(source_extent - 1) *
              100.0,
          0.0, 100.0);
    };
    const auto actual_target_x =
        preview->property("filterCenterXNormalized").toDouble();
    const auto actual_target_y =
        preview->property("filterCenterYNormalized").toDouble();
    CHECK(std::abs(actual_target_x - target_x) <= 0.002);
    CHECK(std::abs(actual_target_y - target_y) <= 0.002);
    const auto dragged_x = source_percent(
        actual_target_x, active_input.bounds.x, active_input.bounds.width,
        final_render.bounds.x, final_render.bounds.width);
    const auto dragged_y = source_percent(
        actual_target_y, active_input.bounds.y, active_input.bounds.height,
        final_render.bounds.y, final_render.bounds.height);
    CHECK(std::abs(center_x->value() - dragged_x) <= 0.11);
    CHECK(std::abs(center_y->value() - dragged_y) <= 0.11);

    const auto displayed_center = center_point(target_x, target_y);
    const auto shorter_display_extent =
        std::min(displayed.width(), displayed.height());
    const auto current_radius =
        preview->property("filterRadiusNormalized").toDouble();
    constexpr double target_radius = 0.25;
    const auto radius_direction = target_x <= 0.5 ? 1.0 : -1.0;
    drag(*preview,
         displayed_center +
             QPoint(static_cast<int>(std::lround(
                        radius_direction * shorter_display_extent *
                        current_radius / 2.0)),
                    0),
         displayed_center +
             QPoint(static_cast<int>(std::lround(
                        radius_direction * shorter_display_extent *
                        target_radius / 2.0)),
                    0));
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        5000));
    const auto actual_target_radius =
        preview->property("filterRadiusNormalized").toDouble();
    CHECK(std::abs(actual_target_radius - target_radius) <= 0.01);
    const auto expected_dragged_radius =
        actual_target_radius *
        static_cast<double>(std::min(final_render.bounds.width,
                                     final_render.bounds.height)) /
        static_cast<double>(std::min(active_input.bounds.width,
                                     active_input.bounds.height)) *
        100.0;
    CHECK(std::abs(radius->value() - expected_dragged_radius) <= 1.0);
    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255},
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) {
        previews.push_back(preview);
      });
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_explicit_original_click_clears_filtered_stack() {
  GallerySettingsRestorer gallery_settings;
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* search = dialog->findChild<QLineEdit*>(
        QStringLiteral("filterGallerySearchEdit"));
    CHECK(looks != nullptr && applied != nullptr && search != nullptr);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    CHECK(applied->count() == 1);
    CHECK(!previews.empty() && previews.back().recipe.has_value());

    search->setText(QStringLiteral("no filter can match this query"));
    QApplication::processEvents();
    CHECK(looks->currentItem() == looks->item(0));
    CHECK(looks->currentItem()->data(Qt::UserRole + 1).toString().isEmpty());
    CHECK(applied->count() == 1);
    CHECK(previews.back().recipe.has_value());

    const auto original_rect = looks->visualItemRect(looks->item(0));
    CHECK(original_rect.isValid());
    QTest::mouseClick(looks->viewport(), Qt::LeftButton, Qt::NoModifier,
                      original_rect.center());
    QApplication::processEvents();
    CHECK(applied->count() == 0);
    CHECK(!previews.empty() && !previews.back().recipe.has_value());
    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255},
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) {
        previews.push_back(preview);
      });
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_stack_cancel_and_apply_are_one_transaction() {
  GallerySettingsRestorer gallery_settings;
  patchy::LayerId layer_id{};
  patchy::Rect original_bounds;
  patchy::PixelBuffer original_pixels;
  auto document = make_filter_gallery_document(
      layer_id, original_bounds, original_pixels);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document),
                              QStringLiteral("Gallery Stack Transaction"));
  show_window(window);
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto box = registry.default_invocation("patchy.filters.box_blur");
  box.parameters["radius"] = std::int64_t{3};
  auto gaussian =
      registry.default_invocation("patchy.filters.gaussian_blur");
  gaussian.parameters["radius"] = std::int64_t{2};
  const patchy::FilterRecipe expected_recipe{{
      patchy::FilterRecipeEntry{box}, patchy::FilterRecipeEntry{gaussian}}};
  patchy::Rect expected_bounds = original_bounds;
  const auto expected_pixels = patchy::ui::build_filter_preview_pixels(
      original_pixels, QRegion(), original_bounds, registry, expected_recipe,
      nullptr, &expected_bounds);

  const auto configure_stack = [&](QDialog& dialog) {
    auto* looks = dialog.findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog.findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog.findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr);
    looks->setCurrentRow(0);
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.box_blur")));
    QApplication::processEvents();
    auto* radius = dialog.findChild<QSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(3);
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    QApplication::processEvents();
    radius = dialog.findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(2);
    CHECK(applied->count() == 2);
    CHECK(applied->item(0)->text() == QStringLiteral("Gaussian Blur"));
    CHECK(applied->item(1)->text() == QStringLiteral("Box Blur"));
  };

  bool cancelled_stack = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    configure_stack(*dialog);
    CHECK(process_events_until(
        [&] {
          const auto& read_document = std::as_const(
              patchy::ui::MainWindowTestAccess::document(window));
          const auto* layer = read_document.find_layer(layer_id);
          return layer != nullptr &&
                 filter_rect_equal(layer->bounds(), expected_bounds) &&
                 patchy::ui::pixel_buffers_equal(layer->pixels(),
                                                 expected_pixels);
        },
        7000));
    cancelled_stack = true;
    dialog->reject();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(cancelled_stack);
  process_events_for(120);
  {
    const auto& read_document = std::as_const(
        patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), original_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), original_pixels));
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  bool applied_stack = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    configure_stack(*dialog);
    auto* buttons = dialog->findChild<QDialogButtonBox*>(
        QStringLiteral("filterGalleryButtonBox"));
    CHECK(buttons != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr);
    applied_stack = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(applied_stack);
  {
    const auto& read_document = std::as_const(
        patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), expected_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), expected_pixels));
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  const auto& undone_document = std::as_const(
      patchy::ui::MainWindowTestAccess::document(window));
  const auto* undone_layer = undone_document.find_layer(layer_id);
  CHECK(undone_layer != nullptr);
  CHECK(filter_rect_equal(undone_layer->bounds(), original_bounds));
  CHECK(patchy::ui::pixel_buffers_equal(undone_layer->pixels(),
                                        original_pixels));
}

void ui_filter_gallery_saved_looks_persist_after_cancel_and_support_crud() {
  GallerySettingsRestorer gallery_settings;
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto storage = directory.filePath(QStringLiteral("looks"));
  patchy::ui::FilterLookLibrary library(storage);
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  QString saved_id;
  patchy::FilterRecipe saved_recipe;
  bool saved_and_renamed = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* duplicate = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDuplicateEffectButton"));
    auto* combo = dialog->findChild<QComboBox*>(
        QStringLiteral("filterGallerySavedLooksCombo"));
    auto* save = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGallerySaveLookButton"));
    auto* rename = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryRenameLookButton"));
    auto* remove = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDeleteLookButton"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          combo != nullptr && save != nullptr && rename != nullptr &&
          remove != nullptr);
    CHECK(combo->count() == 1);
    CHECK(combo->itemText(0) == QStringLiteral("Custom"));
    CHECK(!save->isEnabled() && !rename->isEnabled() && !remove->isEnabled());

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.soft_glow")));
    QApplication::processEvents();
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.noir")));
    QApplication::processEvents();
    CHECK(applied->count() == 2);
    CHECK(save->isEnabled());

    QTimer::singleShot(0, dialog, [] {
      auto* input = qobject_cast<QInputDialog*>(find_top_level_dialog(
          QStringLiteral("filterGalleryLookNameDialog")));
      CHECK(input != nullptr);
      input->setTextValue(QStringLiteral("Road Trip"));
      input->accept();
    });
    save->click();
    CHECK(library.entries().size() == 1);
    saved_id = library.entries().front().id;
    saved_recipe = library.entries().front().recipe;
    CHECK(combo->currentData().toString() == saved_id);
    CHECK(rename->isEnabled() && remove->isEnabled());

    QTimer::singleShot(0, dialog, [] {
      auto* input = qobject_cast<QInputDialog*>(find_top_level_dialog(
          QStringLiteral("filterGalleryLookNameDialog")));
      CHECK(input != nullptr);
      CHECK(input->textValue() == QStringLiteral("Road Trip"));
      input->setTextValue(QStringLiteral("Island Evening"));
      input->accept();
    });
    rename->click();
    CHECK(library.find_entry(saved_id) != nullptr);
    CHECK(library.find_entry(saved_id)->name ==
          QStringLiteral("Island Evening"));
    CHECK(combo->currentData().toString() == saved_id);
    saved_and_renamed = true;
    dialog->reject();
  });
  const auto first_result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255}, {}, &library);
  CHECK(saved_and_renamed);
  CHECK(first_result.outcome ==
        patchy::ui::VisualFilterGalleryOutcome::Cancelled);

  // Save and rename are library operations, so cancelling the filter dialog
  // does not roll them back.
  patchy::ui::FilterLookLibrary reloaded(storage);
  CHECK(reloaded.entries().size() == 1);
  CHECK(reloaded.entries().front().id == saved_id);
  CHECK(reloaded.entries().front().name == QStringLiteral("Island Evening"));
  CHECK(filter_recipes_equal(reloaded.entries().front().recipe, saved_recipe));
  patchy::FilterInvocation future;
  future.filter_id = "future.filters.not_installed";
  const patchy::FilterRecipe unsupported{
      {patchy::FilterRecipeEntry{future}}};
  patchy::ui::FilterLookLibraryError error =
      patchy::ui::FilterLookLibraryError::None;
  const auto future_id = reloaded.add_look(
      QStringLiteral("Future Look"), unsupported, &error);
  CHECK(!future_id.isEmpty());
  CHECK(error == patchy::ui::FilterLookLibraryError::None);

  bool loaded_and_deleted = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* combo = dialog->findChild<QComboBox*>(
        QStringLiteral("filterGallerySavedLooksCombo"));
    auto* applied = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryAppliedEffectsList"));
    auto* remove = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryDeleteLookButton"));
    CHECK(combo != nullptr && applied != nullptr && remove != nullptr);
    CHECK(combo->count() == 3);
    const auto future_index = combo->findData(future_id);
    const auto saved_index = combo->findData(saved_id);
    CHECK(future_index > 0 && saved_index > 0);
    auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    CHECK(model != nullptr && model->item(future_index) != nullptr);
    CHECK(!model->item(future_index)->isEnabled());
    CHECK(combo->itemData(future_index, Qt::ToolTipRole)
              .toString()
              .contains(QStringLiteral("cannot apply")));

    combo->setCurrentIndex(saved_index);
    QApplication::processEvents();
    CHECK(applied->count() == 2);
    CHECK(applied->item(0)->text() == QStringLiteral("Noir"));
    CHECK(applied->item(1)->text() == QStringLiteral("Soft Glow"));
    CHECK(remove->isEnabled());
    QTimer::singleShot(0, dialog, [] {
      auto* message = qobject_cast<QMessageBox*>(find_top_level_dialog(
          QStringLiteral("filterGalleryDeleteLookMessageBox")));
      CHECK(message != nullptr);
      CHECK(message->text().contains(QStringLiteral("Island Evening")));
      message->done(QMessageBox::Yes);
    });
    remove->click();
    CHECK(reloaded.find_entry(saved_id) == nullptr);
    CHECK(combo->findData(saved_id) < 0);
    CHECK(combo->findData(future_id) > 0);
    loaded_and_deleted = true;
    dialog->reject();
  });
  const auto second_result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255}, {}, &reloaded);
  CHECK(loaded_and_deleted);
  CHECK(second_result.outcome ==
        patchy::ui::VisualFilterGalleryOutcome::Cancelled);
  patchy::ui::FilterLookLibrary final_reload(storage);
  CHECK(final_reload.find_entry(saved_id) == nullptr);
  CHECK(final_reload.find_entry(future_id) != nullptr);
}

}  // namespace

std::vector<patchy::test::TestCase> destructive_filters_gallery_tests_part1() {
  return {
      {"ui_brightness_contrast_filter_applies_settings",
       ui_brightness_contrast_filter_applies_settings},
      {"ui_filter_preview_restores_unselected_region_runs",
       ui_filter_preview_restores_unselected_region_runs},
      {"ui_median_selection_uses_full_layer_transparent_color_extension",
       ui_median_selection_uses_full_layer_transparent_color_extension},
      {"ui_dust_and_scratches_selection_uses_full_layer_transparent_color_extension",
       ui_dust_and_scratches_selection_uses_full_layer_transparent_color_extension},
      {"ui_surface_blur_selection_uses_full_layer_transparent_color_extension",
       ui_surface_blur_selection_uses_full_layer_transparent_color_extension},
      {"ui_tilt_shift_blur_dialog_cancel_selection_apply_and_undo",
       ui_tilt_shift_blur_dialog_cancel_selection_apply_and_undo},
      {"ui_blur_grows_layer_into_transparency", ui_blur_grows_layer_into_transparency},
      {"ui_expanding_filter_cancel_and_undo_redo_restore_pixels_and_bounds",
       ui_expanding_filter_cancel_and_undo_redo_restore_pixels_and_bounds},
      {"ui_filter_look_library_round_trips_and_isolates_bad_records",
       ui_filter_look_library_round_trips_and_isolates_bad_records},
      {"ui_filter_recipe_selection_is_restored_once_after_the_full_stack",
       ui_filter_recipe_selection_is_restored_once_after_the_full_stack},
      {"ui_filter_gallery_stack_edits_reverse_order_and_stay_independent",
       ui_filter_gallery_stack_edits_reverse_order_and_stay_independent},
      {"ui_filter_gallery_blending_controls_edit_recipe_entries",
       ui_filter_gallery_blending_controls_edit_recipe_entries},
      {"ui_filter_gallery_drag_events_reach_stack_during_preview_lock",
       ui_filter_gallery_drag_events_reach_stack_during_preview_lock},
      {"ui_filter_gallery_stack_spatial_overlay_tracks_active_input_bounds",
       ui_filter_gallery_stack_spatial_overlay_tracks_active_input_bounds},
      {"ui_filter_gallery_explicit_original_click_clears_filtered_stack",
       ui_filter_gallery_explicit_original_click_clears_filtered_stack},
      {"ui_filter_gallery_stack_cancel_and_apply_are_one_transaction",
       ui_filter_gallery_stack_cancel_and_apply_are_one_transaction},
      {"ui_filter_gallery_saved_looks_persist_after_cancel_and_support_crud",
       ui_filter_gallery_saved_looks_persist_after_cancel_and_support_crud},
  };
}
