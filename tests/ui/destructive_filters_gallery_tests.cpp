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

const QStringList& expected_filter_gallery_ids() {
  static const QStringList ids = {
      QStringLiteral("patchy.filters.soft_glow"),
      QStringLiteral("patchy.filters.punchy_color"),
      QStringLiteral("patchy.filters.noir"),
      QStringLiteral("patchy.filters.cinematic_matte"),
      QStringLiteral("patchy.filters.vintage_fade"),
      QStringLiteral("patchy.filters.sepia"),
      QStringLiteral("patchy.filters.vignette"),
      QStringLiteral("patchy.filters.box_blur"),
      QStringLiteral("patchy.filters.gaussian_blur"),
      QStringLiteral("patchy.filters.motion_blur"),
      QStringLiteral("patchy.filters.radial_blur"),
      QStringLiteral("patchy.filters.surface_blur"),
      QStringLiteral("patchy.filters.lens_blur"),
      QStringLiteral("patchy.filters.iris_blur"),
      QStringLiteral("patchy.filters.tilt_shift_blur"),
      QStringLiteral("patchy.filters.sharpen"),
      QStringLiteral("patchy.filters.unsharp_mask"),
      QStringLiteral("patchy.filters.high_pass"),
      QStringLiteral("patchy.filters.twirl"),
      QStringLiteral("patchy.filters.wave"),
      QStringLiteral("patchy.filters.pinch_bloat"),
      QStringLiteral("patchy.filters.film_grain"),
      QStringLiteral("patchy.filters.median"),
      QStringLiteral("patchy.filters.dust_and_scratches"),
      QStringLiteral("patchy.filters.pixelate"),
      QStringLiteral("patchy.filters.color_halftone"),
      QStringLiteral("patchy.filters.edge_detect"),
      QStringLiteral("patchy.filters.emboss"),
      QStringLiteral("patchy.filters.glowing_edges"),
      QStringLiteral("patchy.filters.clouds"),
      QStringLiteral("patchy.filters.plastic_wrap"),
  };
  return ids;
}

QStringList visible_gallery_filter_ids(const QListWidget& looks) {
  QStringList result;
  for (int row = 0; row < looks.count(); ++row) {
    const auto* item = looks.item(row);
    if (item == nullptr || item->isHidden()) {
      continue;
    }
    const auto id = item->data(Qt::UserRole + 1).toString();
    if (!id.isEmpty()) {
      result.push_back(id);
    }
  }
  return result;
}

int require_combo_data_index(const QComboBox& combo, const QString& data) {
  const auto index = combo.findData(data);
  CHECK(index >= 0);
  return index;
}

bool filter_invocations_equal(const patchy::FilterInvocation& lhs,
                              const patchy::FilterInvocation& rhs) {
  return lhs.filter_id == rhs.filter_id &&
         lhs.schema_version == rhs.schema_version &&
         lhs.parameters == rhs.parameters &&
         filter_rgb_equal(lhs.foreground, rhs.foreground) &&
         filter_rgb_equal(lhs.background, rhs.background);
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

void ui_filter_gallery_photo_looks_layout_thumbnails_controls_zoom_and_before() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const auto source_copy = source;
  const patchy::Rect bounds{48, 38, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> canvas_previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    CHECK(!dialog->isModal());
    CHECK(dialog->windowModality() == Qt::NonModal);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview"));
    auto* parameters = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryParameters"));
    auto* before = dialog->findChild<QPushButton*>(QStringLiteral("filterGalleryBeforeButton"));
    auto* canvas_preview = dialog->findChild<QCheckBox*>(QStringLiteral("filterGalleryCanvasPreviewCheck"));
    auto* status = dialog->findChild<QLabel*>(QStringLiteral("filterGalleryStatusLabel"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("filterGalleryButtonBox"));
    auto* zoom_fit = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryZoomFit"));
    auto* zoom_100 = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryZoom100"));
    auto* zoom_out = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryZoomOut"));
    auto* zoom_in = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryZoomIn"));
    auto* zoom_label = dialog->findChild<QLabel*>(QStringLiteral("filterGalleryZoomLabel"));
    CHECK(looks != nullptr);
    CHECK(preview != nullptr);
    CHECK(parameters != nullptr);
    CHECK(before != nullptr);
    CHECK(canvas_preview != nullptr && canvas_preview->isChecked());
    CHECK(canvas_preview->text() == QStringLiteral("Live Canvas Preview"));
    CHECK(before->toolTip() == QStringLiteral("Hold to compare with the unadjusted image"));
    CHECK(status != nullptr);
    CHECK(buttons != nullptr);
    CHECK(zoom_fit != nullptr && zoom_100 != nullptr && zoom_out != nullptr && zoom_in != nullptr);
    CHECK(zoom_label != nullptr && zoom_label->text().contains(QStringLiteral("%")));
    CHECK(buttons->button(QDialogButtonBox::Ok) != nullptr);
    CHECK(buttons->button(QDialogButtonBox::Ok)->text() == QStringLiteral("Apply"));
    CHECK(buttons->button(QDialogButtonBox::Cancel) != nullptr);
    CHECK(buttons->button(QDialogButtonBox::Reset) != nullptr);

    QStringList expected_ids{QString()};
    expected_ids.append(expected_filter_gallery_ids());
    const QStringList expected_names{
        QStringLiteral("Original"),        QStringLiteral("Soft Glow"),
        QStringLiteral("Punchy Color"),    QStringLiteral("Noir"),
        QStringLiteral("Cinematic Matte"), QStringLiteral("Vintage Fade"),
        QStringLiteral("Vintage Sepia"),   QStringLiteral("Lens Vignette"),
        QStringLiteral("Box Blur"),        QStringLiteral("Gaussian Blur"),
        QStringLiteral("Motion Blur"),     QStringLiteral("Radial Blur"),
        QStringLiteral("Surface Blur"),    QStringLiteral("Lens Blur"),
        QStringLiteral("Iris Blur"),       QStringLiteral("Tilt-Shift Blur"),
        QStringLiteral("Sharpen"),         QStringLiteral("Unsharp Mask"),
        QStringLiteral("High Pass"),       QStringLiteral("Twirl"),
        QStringLiteral("Wave"),
        QStringLiteral("Pinch/Bloat"),     QStringLiteral("Analog Grain"),
        QStringLiteral("Median"),
        QStringLiteral("Dust & Scratches"),
        QStringLiteral("Pixel Mosaic"),    QStringLiteral("Color Halftone"),
        QStringLiteral("Edge Detect"),     QStringLiteral("Emboss"),
        QStringLiteral("Glowing Edges"),   QStringLiteral("Clouds"),
        QStringLiteral("Plastic Wrap"),
    };
    CHECK(looks->count() == expected_ids.size());
    CHECK(looks->currentRow() == 0);
    for (int row = 0; row < looks->count(); ++row) {
      auto* item = looks->item(row);
      CHECK(item != nullptr);
      CHECK(item->data(Qt::UserRole + 1).toString() == expected_ids[row]);
      CHECK(item->text() == expected_names[row]);
    }
    CHECK(preview->property("previewFitMode").toBool());
    // Placeholder icons exist from creation, so thumbnail readiness is
    // signaled by the ready role, never by icon nullity.
    CHECK(process_events_until(
        [&] {
          for (int row = 0; row < looks->count(); ++row) {
            if (!looks->item(row)->data(Qt::UserRole + 2).toBool()) {
              return false;
            }
          }
          return true;
        },
        20000));
    const auto original_thumbnail = looks->item(0)->icon().pixmap(QSize(144, 96)).toImage();
    const auto noir_thumbnail = looks->item(3)->icon().pixmap(QSize(144, 96)).toImage();
    const auto plastic_thumbnail =
        looks->item(looks->count() - 1)->icon().pixmap(QSize(144, 96)).toImage();
    CHECK(!original_thumbnail.isNull());
    CHECK(!noir_thumbnail.isNull());
    CHECK(!plastic_thumbnail.isNull());
    CHECK(original_thumbnail != noir_thumbnail);
    CHECK(original_thumbnail != plastic_thumbnail);
    CHECK(patchy::ui::pixel_buffers_equal(source, source_copy));

    const auto original_preview = preview->grab().toImage();
    looks->setCurrentRow(1);
    QApplication::processEvents();
    auto* amount = parameters->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    auto* amount_slider = parameters->findChild<QSlider*>(QStringLiteral("filterAmountSlider"));
    CHECK(amount != nullptr && amount_slider != nullptr);
    CHECK(amount->value() == 100 && amount_slider->value() == 100);
    amount->setValue(42);
    CHECK(amount_slider->value() == 42);
    buttons->button(QDialogButtonBox::Reset)->click();
    QApplication::processEvents();
    amount = parameters->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    amount_slider = parameters->findChild<QSlider*>(QStringLiteral("filterAmountSlider"));
    CHECK(amount != nullptr && amount_slider != nullptr);
    CHECK(amount->value() == 100 && amount_slider->value() == 100);

    QImage filtered_preview;
    CHECK(process_events_until(
        [&] {
          filtered_preview = preview->grab().toImage();
          return filtered_preview != original_preview;
        },
        6000));
    process_events_for(80);
    filtered_preview = preview->grab().toImage();
    CHECK(!canvas_previews.empty());
    CHECK(canvas_previews.back().canvas_enabled);
    CHECK(canvas_previews.back().recipe.has_value());
    CHECK(canvas_previews.back().recipe->entries.size() == 1);
    CHECK(canvas_previews.back().recipe->entries.front().invocation.filter_id ==
          "patchy.filters.soft_glow");

    const auto callback_count_before_compare = canvas_previews.size();
    const auto center = before->rect().center();
    send_mouse(*before, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
    CHECK(preview->grab().toImage() == original_preview);
    CHECK(canvas_previews.size() == callback_count_before_compare);
    send_mouse(*before, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    CHECK(process_events_until([&] { return preview->grab().toImage() == filtered_preview; }, 1000));
    CHECK(canvas_previews.size() == callback_count_before_compare);

    zoom_100->click();
    QApplication::processEvents();
    CHECK(!preview->property("previewFitMode").toBool());
    CHECK(preview->property("previewZoomPercent").toInt() == 100);
    for (int attempt = 0;
         attempt < 8 && preview->property("previewZoomPercent").toInt() < 300;
         ++attempt) {
      zoom_in->click();
      QApplication::processEvents();
    }
    CHECK(preview->property("previewZoomPercent").toInt() >= 300);
    const auto pan_before = preview->property("previewPanOffset").toPoint();
    drag(*preview, preview->rect().center(), preview->rect().center() + QPoint(24, 16));
    CHECK(preview->property("previewPanOffset").toPoint() != pan_before);
    for (int attempt = 0;
         attempt < 8 && preview->property("previewZoomPercent").toInt() < 1600;
         ++attempt) {
      zoom_in->click();
      QApplication::processEvents();
    }
    CHECK(preview->property("previewZoomPercent").toInt() == 1600);
    QElapsedTimer max_zoom_paint;
    max_zoom_paint.start();
    CHECK(!preview->grab().toImage().isNull());
    CHECK(max_zoom_paint.elapsed() < 1500);
    zoom_out->click();
    zoom_fit->click();
    QApplication::processEvents();
    CHECK(preview->property("previewFitMode").toBool());

    looks->setCurrentRow(7);
    QApplication::processEvents();
    auto* strength = parameters->findChild<QSpinBox*>(QStringLiteral("filterStrengthSpin"));
    auto* strength_slider = parameters->findChild<QSlider*>(QStringLiteral("filterStrengthSlider"));
    CHECK(strength != nullptr && strength_slider != nullptr);
    CHECK(strength->value() == 55 && strength_slider->value() == 55);
    CHECK(strength->buttonSymbols() == QAbstractSpinBox::PlusMinus);
    const auto click_spin = [strength](const QPoint& point) {
      send_mouse(*strength, QEvent::MouseButtonPress, point,
                 Qt::LeftButton, Qt::LeftButton);
      send_mouse(*strength, QEvent::MouseButtonRelease, point,
                 Qt::LeftButton, Qt::NoButton);
    };
    click_spin(QPoint(strength->width() - 12, strength->height() / 2));
    CHECK(strength->value() == 56);
    click_spin(QPoint(strength->width() - 39, strength->height() / 2));
    CHECK(strength->value() == 55);
    CHECK(process_events_until(
        [&] {
          return !canvas_previews.empty() && canvas_previews.back().recipe.has_value() &&
                 canvas_previews.back().recipe->entries.size() == 1 &&
                 canvas_previews.back().recipe->entries.front().invocation.filter_id ==
                     "patchy.filters.vignette";
        },
        1000));
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        3000));
    save_widget_artifact("ui_filter_gallery_photo_looks", *dialog);
    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry,
      patchy::RgbColor{220, 28, 24},
      patchy::RgbColor{255, 255, 255},
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) { canvas_previews.push_back(preview); });
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
  CHECK(!result.recipe.has_value());
  CHECK(patchy::ui::pixel_buffers_equal(source, source_copy));
}

void ui_filter_gallery_live_canvas_latest_off_on_and_cancel_restore_exact() {
  GallerySettingsRestorer gallery_settings;
  patchy::LayerId layer_id{};
  patchy::Rect bounds;
  patchy::PixelBuffer original_pixels;
  auto document = make_filter_gallery_document(layer_id, bounds, original_pixels);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto vignette = registry.default_invocation("patchy.filters.vignette");
  patchy::Rect expected_bounds = bounds;
  const auto expected_pixels = patchy::ui::build_filter_preview_pixels(
      original_pixels, QRegion(), bounds, registry, patchy::ui::FilterPreviewSettings{true, vignette}, nullptr,
      &expected_bounds);
  CHECK(filter_rect_equal(expected_bounds, bounds));
  CHECK(!patchy::ui::pixel_buffers_equal(expected_pixels, original_pixels));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Gallery Live Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(layer_list != nullptr);
  CHECK(tabs != nullptr);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  bool drove_dialog = false;

  const auto layer_matches = [&](const patchy::PixelBuffer& wanted, const patchy::Rect& wanted_bounds) {
    const auto& read_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    return layer != nullptr && filter_rect_equal(layer->bounds(), wanted_bounds) &&
           patchy::ui::pixel_buffers_equal(layer->pixels(), wanted);
  };

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* canvas_preview = dialog->findChild<QCheckBox*>(QStringLiteral("filterGalleryCanvasPreviewCheck"));
    auto* before = dialog->findChild<QPushButton*>(QStringLiteral("filterGalleryBeforeButton"));
    CHECK(looks != nullptr);
    CHECK(canvas_preview != nullptr && canvas_preview->isChecked());
    CHECK(before != nullptr);
    CHECK(window.isEnabled());
    CHECK(canvas->edit_locked());
    CHECK(!layer_list->isEnabled());
    CHECK(!tabs->tabBar()->isEnabled());
    CHECK(require_action(window, "viewZoomInAction")->isEnabled());

    // The first request is deliberately superseded several times without
    // yielding. Only the final Vignette generation may reach the canvas.
    looks->setCurrentRow(1);
    looks->setCurrentRow(2);
    looks->setCurrentRow(3);
    looks->setCurrentRow(5);
    looks->setCurrentRow(7);
    CHECK(process_events_until([&] { return layer_matches(expected_pixels, expected_bounds); }, 7000));
    process_events_for(120);
    CHECK(layer_matches(expected_pixels, expected_bounds));

    canvas_preview->setChecked(false);
    QApplication::processEvents();
    CHECK(layer_matches(original_pixels, bounds));
    canvas_preview->setChecked(true);
    CHECK(process_events_until([&] { return layer_matches(expected_pixels, expected_bounds); }, 7000));

    // Before compares only the dialog preview. It must not cancel or replace
    // the full-resolution canvas generation.
    const auto compare_center = before->rect().center();
    send_mouse(*before, QEvent::MouseButtonPress, compare_center, Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
    CHECK(layer_matches(expected_pixels, expected_bounds));
    send_mouse(*before, QEvent::MouseButtonRelease, compare_center, Qt::LeftButton, Qt::NoButton);
    CHECK(layer_matches(expected_pixels, expected_bounds));

    // Close with a newer worker in flight. Its queued result must be invalidated
    // and must never repaint the restored original after reject returns.
    looks->setCurrentRow(1);
    looks->setCurrentRow(3);
    drove_dialog = true;
    dialog->reject();
  });

  require_action(window, "filterGalleryAction")->trigger();
  CHECK(drove_dialog);
  process_events_for(350);
  CHECK(layer_matches(original_pixels, bounds));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(!canvas->edit_locked());
  CHECK(layer_list->isEnabled());
  CHECK(tabs->tabBar()->isEnabled());
}

void ui_filter_gallery_original_noop_and_selected_apply_undo_redo() {
  GallerySettingsRestorer gallery_settings;
  patchy::LayerId layer_id{};
  patchy::Rect bounds;
  patchy::PixelBuffer original_pixels;
  auto document = make_filter_gallery_document(layer_id, bounds, original_pixels);
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Gallery Apply"));
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto undo_before_original = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto& before_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* before_layer = before_document.find_layer(layer_id);
  CHECK(before_layer != nullptr);
  const auto render_revision_before = before_layer->render_revision();
  const auto content_revision_before = before_layer->content_revision();
  const auto pixel_revision_before = before_layer->pixel_revision();

  bool accepted_original = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("filterGalleryButtonBox"));
    CHECK(looks != nullptr && looks->currentRow() == 0);
    CHECK(looks->currentItem()->data(Qt::UserRole + 1).toString().isEmpty());
    CHECK(buttons != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr);
    accepted_original = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(accepted_original);
  process_events_for(120);
  {
    const auto& read_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), original_pixels));
    CHECK(layer->render_revision() == render_revision_before);
    CHECK(layer->content_revision() == content_revision_before);
    CHECK(layer->pixel_revision() == pixel_revision_before);
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_original);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("No visual filter applied"));

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_selection_mode(patchy::ui::CanvasWidget::SelectionMode::Replace);
  canvas->set_selection_feather_radius(0);
  canvas->set_selection_antialias(false);
  const QPoint selection_start(bounds.x + 6, bounds.y + 5);
  const QPoint selection_end(bounds.x + bounds.width / 2, bounds.y + bounds.height - 6);
  drag(*canvas, canvas->widget_position_for_document_point(selection_start),
       canvas->widget_position_for_document_point(selection_end));
  QApplication::processEvents();
  const auto selection = canvas->selected_document_region();
  CHECK(!selection.isEmpty());
  CHECK(selection.contains(selection_start + QPoint(3, 3)));
  CHECK(!selection.contains(QPoint(bounds.x + bounds.width - 8, bounds.y + bounds.height / 2)));
  const auto undo_before_apply = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto sepia = registry.default_invocation("patchy.filters.sepia");
  set_filter_integer(sepia, "amount", 64);
  patchy::Rect expected_bounds = bounds;
  const auto expected_pixels = patchy::ui::build_filter_preview_pixels(
      original_pixels, selection, bounds, registry, patchy::ui::FilterPreviewSettings{true, sepia}, nullptr,
      &expected_bounds);
  CHECK(filter_rect_equal(expected_bounds, bounds));

  bool accepted_filter = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* parameters = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryParameters"));
    auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("filterGalleryButtonBox"));
    CHECK(looks != nullptr && parameters != nullptr && buttons != nullptr);
    looks->setCurrentRow(6);
    QApplication::processEvents();
    auto* amount = parameters->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
    CHECK(amount != nullptr);
    amount->setValue(64);
    CHECK(buttons->button(QDialogButtonBox::Ok) != nullptr);
    accepted_filter = true;
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  require_action(window, "filterGalleryAction")->trigger();
  CHECK(accepted_filter);

  patchy::PixelBuffer applied_pixels;
  {
    const auto& read_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), expected_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), expected_pixels));
    applied_pixels = layer->pixels();
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before_apply + 1U);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  bool changed_inside = false;
  for (int y = 0; y < original_pixels.height(); ++y) {
    for (int x = 0; x < original_pixels.width(); ++x) {
      const auto* original = original_pixels.pixel(x, y);
      const auto* applied = applied_pixels.pixel(x, y);
      const auto equal = std::equal(original, original + 4, applied);
      const QPoint document_point(bounds.x + x, bounds.y + y);
      if (selection.contains(document_point)) {
        changed_inside = changed_inside || !equal;
      } else {
        CHECK(equal);
      }
    }
  }
  CHECK(changed_inside);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  {
    const auto& read_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), original_pixels));
  }
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  {
    const auto& read_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = read_document.find_layer(layer_id);
    CHECK(layer != nullptr);
    CHECK(filter_rect_equal(layer->bounds(), expected_bounds));
    CHECK(patchy::ui::pixel_buffers_equal(layer->pixels(), expected_pixels));
  }
}

void ui_filter_gallery_categories_have_stable_tokens_and_exact_members() {
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
    auto* category = dialog->findChild<QComboBox*>(QStringLiteral("filterGalleryCategoryCombo"));
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("filterGallerySearchEdit"));
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* favorite = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryFavoriteButton"));
    auto* empty = dialog->findChild<QLabel*>(QStringLiteral("filterGalleryEmptyLabel"));
    auto* parameter_editor =
        dialog->findChild<QWidget*>(QStringLiteral("filterGalleryParameterEditor"));
    CHECK(category != nullptr && search != nullptr && looks != nullptr);
    CHECK(favorite != nullptr && empty != nullptr && parameter_editor != nullptr);

    const QStringList expected_tokens{
        QStringLiteral("all"),         QStringLiteral("favorites"),
        QStringLiteral("photo_looks"), QStringLiteral("blur"),
        QStringLiteral("sharpen"),     QStringLiteral("distort"),
        QStringLiteral("noise"),       QStringLiteral("pixelate"),
        QStringLiteral("stylize"),     QStringLiteral("render"),
        QStringLiteral("artistic"),
    };
    CHECK(category->count() == expected_tokens.size());
    for (int index = 0; index < category->count(); ++index) {
      CHECK(category->itemData(index).toString() == expected_tokens[index]);
    }
    CHECK(category->currentData().toString() == QStringLiteral("all"));
    CHECK(visible_gallery_filter_ids(*looks) == expected_filter_gallery_ids());
    CHECK(looks->count() == expected_filter_gallery_ids().size() + 1);

    const std::array<std::pair<QString, QStringList>, 9> categories{{
        {QStringLiteral("photo_looks"), expected_filter_gallery_ids().mid(0, 7)},
        {QStringLiteral("blur"), expected_filter_gallery_ids().mid(7, 8)},
        {QStringLiteral("sharpen"), expected_filter_gallery_ids().mid(15, 3)},
        {QStringLiteral("distort"), expected_filter_gallery_ids().mid(18, 3)},
        {QStringLiteral("noise"), expected_filter_gallery_ids().mid(21, 3)},
        {QStringLiteral("pixelate"), expected_filter_gallery_ids().mid(24, 2)},
        {QStringLiteral("stylize"), expected_filter_gallery_ids().mid(26, 3)},
        {QStringLiteral("render"), expected_filter_gallery_ids().mid(29, 1)},
        {QStringLiteral("artistic"), expected_filter_gallery_ids().mid(30, 1)},
    }};
    for (const auto& [token, ids] : categories) {
      category->setCurrentIndex(require_combo_data_index(*category, token));
      QApplication::processEvents();
      CHECK(visible_gallery_filter_ids(*looks) == ids);
      CHECK(!looks->item(0)->isHidden());
    }

    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("favorites")));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks).isEmpty());
    CHECK(empty->isVisible());
    CHECK(!favorite->isEnabled());

    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_search_matches_localized_and_canonical_names() {
  GallerySettingsRestorer gallery_settings;
  LanguageRestorer language;
  CHECK(patchy::ui::LocalizationManager::instance().set_language(
      QStringLiteral("ja"), false));
  QApplication::processEvents();

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* category = dialog->findChild<QComboBox*>(QStringLiteral("filterGalleryCategoryCombo"));
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("filterGallerySearchEdit"));
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* empty = dialog->findChild<QLabel*>(QStringLiteral("filterGalleryEmptyLabel"));
    CHECK(category != nullptr && search != nullptr && looks != nullptr && empty != nullptr);
    CHECK(search->placeholderText() == QStringLiteral("フィルターを検索"));
    CHECK(category->itemText(require_combo_data_index(*category, QStringLiteral("all"))) ==
          QStringLiteral("すべて"));
    CHECK(category->itemText(require_combo_data_index(*category, QStringLiteral("favorites"))) ==
          QStringLiteral("お気に入り"));
    CHECK(category->itemText(require_combo_data_index(*category, QStringLiteral("blur"))) ==
          QStringLiteral("ぼかし"));

    search->setText(QStringLiteral("ガウス"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.gaussian_blur")});
    search->setText(QStringLiteral("Gaussian"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.gaussian_blur")});
    search->setText(QStringLiteral("ダスト"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.dust_and_scratches")});
    search->setText(QStringLiteral("Dust"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.dust_and_scratches")});
    search->setText(QStringLiteral("ぼかし（表面）"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.surface_blur")});
    search->setText(QStringLiteral("Surface"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.surface_blur")});
    search->setText(QStringLiteral("チルトシフト"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.tilt_shift_blur")});
    search->setText(QStringLiteral("Tilt-Shift"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.tilt_shift_blur")});

    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("photo_looks")));
    search->setText(QStringLiteral("Vintage"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          (QStringList{QStringLiteral("patchy.filters.vintage_fade"),
                       QStringLiteral("patchy.filters.sepia")}));

    search->setText(QStringLiteral("一致しない検索"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks).isEmpty());
    CHECK(empty->isVisible());
    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
  CHECK(patchy::ui::LocalizationManager::instance().set_language(
      QStringLiteral("en"), false));
  QApplication::processEvents();
}

void ui_filter_gallery_favorites_and_dialog_state_persist_across_reopen() {
  GallerySettingsRestorer gallery_settings;
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(
        QStringLiteral("filters/gallery/favorites"),
        QStringList{QStringLiteral("patchy.filters.gaussian_blur"),
                    QStringLiteral("patchy.filters.missing"),
                    QStringLiteral("patchy.filters.invert"),
                    QStringLiteral("patchy.filters.gaussian_blur")});
    settings.setValue(QStringLiteral("filters/gallery/category"),
                      QStringLiteral("missing_category"));
    settings.setValue(QStringLiteral("filters/gallery/lastFilterId"),
                      QStringLiteral("patchy.filters.missing"));
    settings.setValue(QStringLiteral("filters/gallery/liveCanvasPreview"), false);
    settings.setValue(QStringLiteral("filters/gallery/size"), QSize(1040, 640));
    settings.sync();
  }

  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_first = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* category = dialog->findChild<QComboBox*>(QStringLiteral("filterGalleryCategoryCombo"));
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* favorite = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryFavoriteButton"));
    auto* empty = dialog->findChild<QLabel*>(QStringLiteral("filterGalleryEmptyLabel"));
    auto* live = dialog->findChild<QCheckBox*>(QStringLiteral("filterGalleryCanvasPreviewCheck"));
    CHECK(category != nullptr && looks != nullptr && favorite != nullptr && empty != nullptr && live != nullptr);
    CHECK(dialog->size() == QSize(1040, 640));
    CHECK(!live->isChecked());
    CHECK(category->currentData().toString() == QStringLiteral("all"));
    CHECK(looks->currentItem()->data(Qt::UserRole + 1).toString().isEmpty());

    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("filters/gallery/favorites")).toStringList() ==
          QStringList{QStringLiteral("patchy.filters.gaussian_blur")});
    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("favorites")));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.gaussian_blur")});
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    QApplication::processEvents();
    CHECK(favorite->isChecked());
    favorite->click();
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks).isEmpty());
    CHECK(empty->isVisible());

    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("all")));
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.vignette")));
    QApplication::processEvents();
    favorite->click();
    CHECK(favorite->isChecked());
    live->setChecked(true);
    dialog->resize(1010, 650);
    QApplication::processEvents();
    drove_first = true;
    dialog->reject();
  });
  const auto first = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_first);
  CHECK(first.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("filters/gallery/favorites")).toStringList() ==
          QStringList{QStringLiteral("patchy.filters.vignette")});
    CHECK(settings.value(QStringLiteral("filters/gallery/category")).toString() ==
          QStringLiteral("all"));
    CHECK(settings.value(QStringLiteral("filters/gallery/lastFilterId")).toString() ==
          QStringLiteral("patchy.filters.vignette"));
    CHECK(settings.value(QStringLiteral("filters/gallery/liveCanvasPreview")).toBool());
    CHECK(settings.value(QStringLiteral("filters/gallery/size")).toSize() == QSize(1010, 650));
  }

  bool drove_second = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* category = dialog->findChild<QComboBox*>(QStringLiteral("filterGalleryCategoryCombo"));
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* favorite = dialog->findChild<QToolButton*>(QStringLiteral("filterGalleryFavoriteButton"));
    auto* live = dialog->findChild<QCheckBox*>(QStringLiteral("filterGalleryCanvasPreviewCheck"));
    CHECK(category != nullptr && looks != nullptr && favorite != nullptr && live != nullptr);
    CHECK(dialog->size() == QSize(1010, 650));
    CHECK(live->isChecked());
    CHECK(category->currentData().toString() == QStringLiteral("all"));
    CHECK(looks->currentItem()->data(Qt::UserRole + 1).toString() ==
          QStringLiteral("patchy.filters.vignette"));
    CHECK(favorite->isChecked());
    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("favorites")));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.vignette")});
    drove_second = true;
    dialog->reject();
  });
  const auto second = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_second);
  CHECK(second.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_generated_controls_match_catalog_and_direct_defaults() {
  GallerySettingsRestorer gallery_settings;
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  const patchy::RgbColor foreground{220, 28, 24};
  const patchy::RgbColor background{255, 255, 255};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_gallery = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* editor = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryParameterEditor"));
    CHECK(looks != nullptr && editor != nullptr);
    for (const auto& id : expected_filter_gallery_ids()) {
      const auto* definition = registry.find(id.toStdString());
      CHECK(definition != nullptr);
      const auto spec = patchy::ui::filter_dialog_spec_for(*definition);
      looks->setCurrentItem(require_gallery_filter_item(*looks, id));
      QApplication::processEvents();
      CHECK(!previews.empty() && previews.back().recipe.has_value());
      CHECK(previews.back().recipe->entries.size() == 1);
      CHECK(filter_invocations_equal(
          previews.back().recipe->entries.front().invocation,
          registry.default_invocation(definition->identifier, foreground,
                                      background)));
      for (const auto& control : spec.controls) {
        if (control.kind == patchy::FilterParameterKind::Integer) {
          auto* spin = editor->findChild<QSpinBox*>(
              control.object_name + QStringLiteral("Spin"));
          auto* slider = editor->findChild<QSlider*>(
              control.object_name + QStringLiteral("Slider"));
          CHECK(spin != nullptr && slider != nullptr);
          const auto* value = std::get_if<std::int64_t>(&control.default_value);
          CHECK(value != nullptr);
          CHECK(spin->value() == *value && slider->value() == *value);
        } else if (control.kind == patchy::FilterParameterKind::Double) {
          auto* spin = editor->findChild<QDoubleSpinBox*>(
              control.object_name + QStringLiteral("Spin"));
          auto* slider = editor->findChild<QSlider*>(
              control.object_name + QStringLiteral("Slider"));
          CHECK(spin != nullptr && slider != nullptr);
          const auto* value = std::get_if<double>(&control.default_value);
          CHECK(value != nullptr);
          CHECK(std::abs(spin->value() - *value) < 0.000001);
          CHECK(spin->singleStep() == control.step.value_or(1.0));
        }
      }
    }
    drove_gallery = true;
    dialog->reject();
  });
  const auto gallery_result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, foreground, background,
      [&](const patchy::ui::VisualFilterGalleryPreview& preview) {
        previews.push_back(preview);
      });
  CHECK(drove_gallery);
  CHECK(gallery_result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);

  for (const auto& id : expected_filter_gallery_ids()) {
    const auto* definition = registry.find(id.toStdString());
    CHECK(definition != nullptr);
    const auto spec = patchy::ui::filter_dialog_spec_for(*definition);
    const auto expected = registry.default_invocation(definition->identifier,
                                                      foreground, background);
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFilterDialog"));
      CHECK(dialog != nullptr);
      for (const auto& control : spec.controls) {
        if (control.kind == patchy::FilterParameterKind::Integer) {
          auto* spin = dialog->findChild<QSpinBox*>(
              control.object_name + QStringLiteral("Spin"));
          CHECK(spin != nullptr);
          const auto* value = std::get_if<std::int64_t>(&control.default_value);
          CHECK(value != nullptr && spin->value() == *value);
        } else if (control.kind == patchy::FilterParameterKind::Double) {
          auto* spin = dialog->findChild<QDoubleSpinBox*>(
              control.object_name + QStringLiteral("Spin"));
          CHECK(spin != nullptr);
          const auto* value = std::get_if<double>(&control.default_value);
          CHECK(value != nullptr && std::abs(spin->value() - *value) < 0.000001);
        }
      }
      inspected = true;
      dialog->accept();
    });
    const auto direct = patchy::ui::request_filter_settings(
        nullptr, spec, [](patchy::ui::FilterPreviewSettings) {}, expected);
    CHECK(inspected && direct.has_value());
    CHECK(filter_invocations_equal(*direct, expected));
  }
}

void ui_filter_gallery_specialized_controls_sync_and_drag_in_expected_directions() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  // Parent the artifact-producing dialog to the real application window so
  // the visual canary exercises the production dark stylesheet as well as the
  // native Windows widget metrics.
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  std::vector<patchy::ui::VisualFilterGalleryPreview> previews;
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* editor = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryParameterEditor"));
    auto* preview_widget = dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(preview_widget);
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(looks != nullptr && editor != nullptr && preview != nullptr &&
          status != nullptr);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.motion_blur")));
    QApplication::processEvents();
    auto* angle_dial = editor->findChild<QWidget*>(QStringLiteral("filterAngleDial"));
    auto* angle_spin = editor->findChild<QSpinBox*>(QStringLiteral("filterAngleSpin"));
    CHECK(angle_dial != nullptr && angle_spin != nullptr);
    CHECK(angle_dial->property("filterAngleDegrees").toInt() == 0);
    angle_spin->setValue(-180);
    const QPoint dial_top(angle_dial->width() / 2, 10);
    send_mouse(*angle_dial, QEvent::MouseButtonPress, dial_top,
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*angle_dial, QEvent::MouseButtonRelease, dial_top,
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(angle_spin->value() >= 88 && angle_spin->value() <= 92);
    CHECK(angle_dial->property("filterAngleDegrees").toInt() == angle_spin->value());

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.radial_blur")));
    QApplication::processEvents();
    auto* center_x = editor->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterXSpin"));
    auto* center_y = editor->findChild<QDoubleSpinBox*>(QStringLiteral("filterCenterYSpin"));
    CHECK(center_x != nullptr && center_y != nullptr);
    CHECK(center_x->value() == 50.0 && center_y->value() == 50.0);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterSpatialOverlayVisible").toBool();
        },
        3000));
    CHECK(preview->property("filterSpatialOverlayVisible").toBool());
    CHECK(!preview->property("filterSpatialRadiusVisible").toBool());
    center_x->setValue(20.0);
    QApplication::processEvents();
    const auto pending_size = preview->image().size();
    const auto pending_display_size =
        QSizeF(preview->image().width() * preview->zoom(),
               preview->image().height() * preview->zoom());
    const QRectF pending_displayed(
        QPointF((preview->width() - pending_display_size.width()) / 2.0,
                (preview->height() - pending_display_size.height()) / 2.0),
        pending_display_size);
    const auto pending_handle =
        QPointF(pending_displayed.left() +
                    preview->property("filterCenterXNormalized").toDouble() *
                        pending_displayed.width(),
                pending_displayed.top() +
                    preview->property("filterCenterYNormalized").toDouble() *
                        pending_displayed.height())
            .toPoint();
    send_mouse(*preview, QEvent::MouseButtonPress, pending_handle,
               Qt::LeftButton, Qt::LeftButton);
    process_events_for(300);
    CHECK(preview->image().size() == pending_size);
    CHECK(status->text() ==
          QCoreApplication::translate("QObject", "Rendering preview..."));
    send_mouse(*preview, QEvent::MouseButtonRelease, pending_handle,
               Qt::LeftButton, Qt::NoButton);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        3000));
    center_x->setValue(50.0);
    center_y->setValue(50.0);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        3000));
    const auto displayed_size = QSizeF(preview->image().width() * preview->zoom(),
                                       preview->image().height() * preview->zoom());
    const QRectF displayed(
        QPointF((preview->width() - displayed_size.width()) / 2.0,
                (preview->height() - displayed_size.height()) / 2.0),
        displayed_size);
    const QPointF overlay_center(
        displayed.left() +
            preview->property("filterCenterXNormalized").toDouble() *
                displayed.width(),
        displayed.top() +
            preview->property("filterCenterYNormalized").toDouble() *
                displayed.height());
    const auto moved_center =
        QPointF(displayed.left() + displayed.width() * 0.70,
                displayed.top() + displayed.height() * 0.30)
            .toPoint();
    const auto centered_proxy_size = preview->image().size();
    send_mouse(*preview, QEvent::MouseButtonPress, overlay_center.toPoint(),
               Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, moved_center, Qt::NoButton,
               Qt::LeftButton);
    process_events_for(80);
    CHECK(preview->image().size() == centered_proxy_size);
    send_mouse(*preview, QEvent::MouseButtonRelease, moved_center,
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(center_x->value() > 50.0 && center_x->value() <= 100.0);
    CHECK(center_y->value() < 50.0 && center_y->value() >= 0.0);
    CHECK(std::abs(preview->property("filterCenterXNormalized").toDouble() - 0.70) <= 0.002);
    CHECK(std::abs(preview->property("filterCenterYNormalized").toDouble() - 0.30) <= 0.002);
    CHECK(!previews.empty() && previews.back().recipe.has_value());
    CHECK(previews.back().recipe->entries.size() == 1);
    CHECK(std::abs(std::get<double>(
                       previews.back().recipe->entries.front().invocation.parameters.at("center_x")) -
                   center_x->value()) < 0.000001);
    CHECK(std::abs(std::get<double>(
                       previews.back().recipe->entries.front().invocation.parameters.at("center_y")) -
                   center_y->value()) < 0.000001);
    process_events_for(120);
    auto* before = dialog->findChild<QPushButton*>(
        QStringLiteral("filterGalleryBeforeButton"));
    CHECK(before != nullptr);
    preview->zoom_to(2.0);
    const auto comparison_zoom =
        preview->property("previewZoomPercent").toInt();
    send_mouse(*before, QEvent::MouseButtonPress, before->rect().center(),
               Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();
    CHECK(preview->property("previewZoomPercent").toInt() ==
          comparison_zoom);
    send_mouse(*before, QEvent::MouseButtonRelease,
               before->rect().center(), Qt::LeftButton, Qt::NoButton);
    process_events_for(80);
    CHECK(preview->property("previewZoomPercent").toInt() ==
          comparison_zoom);
    preview->zoom_to_fit();

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.twirl")));
    QApplication::processEvents();
    auto* radius = editor->findChild<QSpinBox*>(QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr && radius->value() == 100);
    CHECK(process_events_until(
        [&] {
          return preview->property("filterSpatialRadiusVisible").toBool();
        },
        3000));
    CHECK(preview->property("filterSpatialRadiusVisible").toBool());
    const auto twirl_size = QSizeF(preview->image().width() * preview->zoom(),
                                   preview->image().height() * preview->zoom());
    const QRectF twirl_displayed(
        QPointF((preview->width() - twirl_size.width()) / 2.0,
                (preview->height() - twirl_size.height()) / 2.0),
        twirl_size);
    const auto twirl_center = twirl_displayed.center();
    const auto full_radius = std::min(twirl_displayed.width(),
                                      twirl_displayed.height()) /
                             2.0;
    drag(*preview, (twirl_center + QPointF(full_radius, 0.0)).toPoint(),
         (twirl_center + QPointF(full_radius * 0.5, 0.0)).toPoint());
    QApplication::processEvents();
    CHECK(std::abs(radius->value() - 50) <= 1);
    CHECK(std::abs(preview->property("filterRadiusNormalized").toDouble() - 0.5) <= 0.02);
    radius->setValue(1);
    process_events_for(80);
    const auto small_radius_center = twirl_displayed.center();
    const auto small_radius_handle =
        small_radius_center + QPointF(full_radius * 0.01, 0.0);
    drag(*preview, small_radius_handle.toPoint(),
         (small_radius_center + QPointF(full_radius * 0.30, 0.0)).toPoint());
    QApplication::processEvents();
    CHECK(radius->value() >= 25);
    auto* twirl_center_x = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    auto* twirl_center_y = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    CHECK(twirl_center_x != nullptr && twirl_center_y != nullptr);
    CHECK(twirl_center_x->value() == 50.0 &&
          twirl_center_y->value() == 50.0);
    radius->setValue(50);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                 QCoreApplication::translate("QObject", "Ready");
        },
        3000));
    dialog->repaint();
    process_events_for(40);
    save_widget_artifact("ui_filter_gallery_all_filters", *dialog);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.wave")));
    QApplication::processEvents();
    auto* waveform = editor->findChild<QWidget*>(QStringLiteral("filterWaveformControl"));
    auto* amplitude = editor->findChild<QSpinBox*>(QStringLiteral("filterAmplitudeSpin"));
    auto* wavelength = editor->findChild<QSpinBox*>(QStringLiteral("filterWavelengthSpin"));
    auto* phase = editor->findChild<QSpinBox*>(QStringLiteral("filterPhaseSpin"));
    CHECK(waveform != nullptr && amplitude != nullptr && wavelength != nullptr && phase != nullptr);
    CHECK(waveform->property("filterWaveAmplitude").toInt() == 12);
    CHECK(waveform->property("filterWaveWavelength").toInt() == 48);
    CHECK(waveform->property("filterWavePhase").toInt() == 0);
    amplitude->setValue(20);
    QApplication::processEvents();
    CHECK(waveform->property("filterWaveAmplitude").toInt() == 20);
    const auto wave_center = waveform->rect().center();
    drag(*waveform, wave_center,
         wave_center + QPoint(waveform->width() / 4, -waveform->height() / 4));
    QApplication::processEvents();
    CHECK(amplitude->value() > 20);
    CHECK(phase->value() > 0);
    CHECK(waveform->property("filterWaveAmplitude").toInt() == amplitude->value());
    CHECK(waveform->property("filterWavePhase").toInt() == phase->value());
    const auto wavelength_before = wavelength->value();
    send_wheel(*waveform, waveform->rect().center(), 120);
    QApplication::processEvents();
    CHECK(wavelength->value() == wavelength_before + 1);
    CHECK(waveform->property("filterWaveWavelength").toInt() == wavelength->value());

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

void ui_filter_gallery_tilt_shift_overlay_syncs_and_freezes_during_drag() {
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
    auto* editor = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryParameterEditor"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview")));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(looks != nullptr && editor != nullptr && preview != nullptr &&
          status != nullptr);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.tilt_shift_blur")));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterTiltShiftOverlayVisible").toBool() &&
                 status->text() ==
                     QCoreApplication::translate("QObject", "Ready");
        },
        7000));

    auto* blur = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterBlurSpin"));
    auto* blur_slider = editor->findChild<QSlider*>(
        QStringLiteral("filterBlurSlider"));
    auto* center_x = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterXSpin"));
    auto* center_y = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterCenterYSpin"));
    auto* angle = editor->findChild<QSpinBox*>(
        QStringLiteral("filterAngleSpin"));
    auto* angle_dial = editor->findChild<QWidget*>(
        QStringLiteral("filterAngleDial"));
    auto* focus = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterFocusHalfWidthSpin"));
    auto* transition = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterTransitionWidthSpin"));
    CHECK(blur != nullptr && blur_slider != nullptr && center_x != nullptr &&
          center_y != nullptr && angle != nullptr && angle_dial != nullptr &&
          focus != nullptr && transition != nullptr);
    CHECK(blur->minimum() == 0.0 && blur->maximum() == 500.0 &&
          blur->value() == 15.0 && blur->singleStep() == 0.1);
    CHECK(blur_slider->minimum() == 0 && blur_slider->maximum() == 500);
    CHECK(center_x->value() == 50.0 && center_y->value() == 50.0);
    CHECK(angle->minimum() == -180 && angle->maximum() == 180 &&
          angle->value() == 0);
    CHECK(focus->value() == 10.0 && transition->value() == 20.0);
    CHECK(std::abs(
              preview->property("filterTiltShiftCenterXNormalized").toDouble() -
              0.5) < 0.02);
    CHECK(std::abs(
              preview->property("filterTiltShiftCenterYNormalized").toDouble() -
              0.5) < 0.02);
    CHECK(preview->property("filterTiltShiftAngleDegrees").toDouble() == 0.0);
    const auto default_mapped_focus =
        preview->property("filterTiltShiftFocusHalfWidthPercent").toDouble();
    const auto default_mapped_transition =
        preview->property("filterTiltShiftTransitionWidthPercent").toDouble();
    const auto source_shorter =
        std::max(1, std::min(source.width(), source.height()));
    const auto default_proxy_shorter = std::max(
        1, std::min(preview->image().width(), preview->image().height()));
    CHECK(std::abs(default_mapped_focus -
                   std::min(100.0, 10.0 * source_shorter /
                                       default_proxy_shorter)) < 0.000001);
    CHECK(std::abs(default_mapped_transition -
                   std::min(100.0, 20.0 * source_shorter /
                                       default_proxy_shorter)) < 0.000001);

    blur->setValue(3.0);
    center_x->setValue(43.0);
    center_y->setValue(57.0);
    angle->setValue(25);
    focus->setValue(15.0);
    transition->setValue(18.0);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                     QCoreApplication::translate("QObject", "Ready") &&
                 std::abs(preview
                              ->property("filterTiltShiftAngleDegrees")
                              .toDouble() -
                          25.0) < 0.000001;
        },
        7000));
    CHECK(angle_dial->property("filterAngleDegrees").toInt() == 25);
    const auto configured_proxy_shorter = std::max(
        1, std::min(preview->image().width(), preview->image().height()));
    CHECK(std::abs(
              preview->property("filterTiltShiftFocusHalfWidthPercent")
                      .toDouble() -
              std::min(100.0, 15.0 * source_shorter /
                                  configured_proxy_shorter)) < 0.000001);
    CHECK(std::abs(
              preview->property("filterTiltShiftTransitionWidthPercent")
                      .toDouble() -
              std::min(100.0, 18.0 * source_shorter /
                                  configured_proxy_shorter)) < 0.000001);

    const auto center_point =
        preview->property("filterTiltShiftCenterPoint").toPointF();
    const auto angle_point =
        preview->property("filterTiltShiftAngleHandlePoint").toPointF();
    const auto focus_point =
        preview->property("filterTiltShiftFocusHandlePoint").toPointF();
    const auto transition_point =
        preview->property("filterTiltShiftTransitionHandlePoint").toPointF();
    CHECK(center_point.x() >= 0.0 && center_point.x() <= preview->width());
    CHECK(center_point.y() >= 0.0 && center_point.y() <= preview->height());
    CHECK(angle_point != center_point);
    CHECK(angle_point.x() > center_point.x());
    CHECK(angle_point.y() < center_point.y());
    CHECK(focus_point != center_point);
    CHECK(transition_point != focus_point);

    dialog->repaint();
    process_events_for(80);
    save_widget_artifact("ui_filter_gallery_tilt_shift_overlay", *dialog);

    const auto frozen_image = preview->image();
    const auto preview_count_before_drag = previews.size();
    const auto moved_center =
        (center_point + QPointF(42.0, -31.0)).toPoint();
    send_mouse(*preview, QEvent::MouseButtonPress, center_point.toPoint(),
               Qt::LeftButton, Qt::LeftButton);
    CHECK(preview->property("filterTiltShiftDragging").toBool());
    CHECK(preview->property("filterTiltShiftDragHandle").toString() ==
          QStringLiteral("tiltCenter"));
    send_mouse(*preview, QEvent::MouseMove, moved_center, Qt::NoButton,
               Qt::LeftButton);
    process_events_for(100);
    CHECK(preview->property("filterTiltShiftDragging").toBool());
    CHECK(center_x->value() > 43.0);
    CHECK(center_y->value() < 57.0);
    CHECK(preview->image() == frozen_image);
    CHECK(previews.size() == preview_count_before_drag);
    send_mouse(*preview, QEvent::MouseButtonRelease, moved_center,
               Qt::LeftButton, Qt::NoButton);
    CHECK(!preview->property("filterTiltShiftDragging").toBool());
    CHECK(process_events_until(
        [&] {
          return previews.size() > preview_count_before_drag &&
                 status->text() ==
                     QCoreApplication::translate("QObject", "Ready");
        },
        7000));
    CHECK(!previews.empty() && previews.back().recipe.has_value());
    CHECK(previews.back().recipe->entries.size() == 1U);
    const auto& invocation =
        previews.back().recipe->entries.front().invocation;
    CHECK(invocation.filter_id == "patchy.filters.tilt_shift_blur");
    CHECK(std::abs(std::get<double>(invocation.parameters.at("center_x")) -
                   center_x->value()) < 0.000001);
    CHECK(std::abs(std::get<double>(invocation.parameters.at("center_y")) -
                   center_y->value()) < 0.000001);
    CHECK(std::get<std::int64_t>(invocation.parameters.at("angle")) == 25);
    CHECK(std::abs(
              std::get<double>(invocation.parameters.at("focus_half_width")) -
              focus->value()) < 0.000001);
    CHECK(std::abs(
              std::get<double>(invocation.parameters.at("transition_width")) -
              transition->value()) < 0.000001);

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

// Patent design constraint (Apple US 8971623; docs/smart-objects.md "Patents
// and trademarks"): the tilt-shift boundary marks must stay short grip bars
// near the center axis. This test fails if anyone reintroduces boundary
// lines that span the image and divide it around the center.
void ui_filter_gallery_tilt_shift_overlay_uses_grip_bars() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  QImage flat(220, 160, QImage::Format_ARGB32);
  flat.fill(QColor(128, 128, 128));
  const auto source = patchy::ui::pixels_from_image_rgba(flat);
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* editor = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryParameterEditor"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(
        dialog->findChild<QWidget*>(QStringLiteral("filterGalleryPreview")));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(looks != nullptr && editor != nullptr && preview != nullptr &&
          status != nullptr);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.tilt_shift_blur")));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterTiltShiftOverlayVisible").toBool() &&
                 status->text() ==
                     QCoreApplication::translate("QObject", "Ready");
        },
        7000));
    CHECK(preview->property("filterTiltShiftAngleDegrees").toDouble() == 0.0);

    // A zero blur renders the flat source without bounds growth, so the
    // preview stays an opaque uniform gray and every non-background pixel is
    // overlay ink. The default blur would grow the layer and feather its
    // edges over the checkerboard, which would break the flat-row probes.
    auto* blur = editor->findChild<QDoubleSpinBox*>(
        QStringLiteral("filterBlurSpin"));
    CHECK(blur != nullptr);
    blur->setValue(0.0);
    CHECK(process_events_until(
        [&] {
          return status->text() ==
                     QCoreApplication::translate("QObject", "Ready") &&
                 !preview->image().isNull() &&
                 preview->image().pixelColor(1, 1).alpha() == 255;
        },
        7000));
    dialog->repaint();
    process_events_for(80);

    const auto center =
        preview->property("filterTiltShiftCenterPoint").toPointF();
    const auto focus_handle =
        preview->property("filterTiltShiftFocusHandlePoint").toPointF();
    const auto transition_handle =
        preview->property("filterTiltShiftTransitionHandlePoint").toPointF();
    const auto grabbed = preview->grab().toImage();
    CHECK(!grabbed.isNull());
    save_widget_artifact("ui_tilt_shift_overlay_grip_bars", *preview);
    const auto ratio = grabbed.devicePixelRatio();
    const auto sample = [&](double x, double y) {
      const auto px = qRound(x * ratio);
      const auto py = qRound(y * ratio);
      CHECK(px >= 0 && px < grabbed.width() && py >= 0 &&
            py < grabbed.height());
      return grabbed.pixel(px, py);
    };

    // Blurring a flat gray source leaves a flat preview, so any pixel that
    // differs from the row 18 px closer to the center axis clean band is
    // overlay ink. Away from the short grips there must be none on either
    // boundary row.
    const auto boundary_rows =
        std::array<double, 2>{focus_handle.y(), transition_handle.y()};
    for (const auto row : boundary_rows) {
      const auto clean_row = row - 18.0;
      for (auto x = 2; x + 2 < preview->width(); ++x) {
        if (std::abs(static_cast<double>(x) - center.x()) <= 40.0) {
          continue;
        }
        for (auto dy = -3; dy <= 3; ++dy) {
          CHECK(sample(x, row + dy) == sample(x, clean_row + dy));
        }
      }
    }

    // The grips themselves must remain visible: solid focus bar ink near the
    // axis, and at least one dash of the transition bar within its span.
    bool focus_ink = false;
    for (auto dy = -3; dy <= 3 && !focus_ink; ++dy) {
      focus_ink = sample(center.x() - 20.0, focus_handle.y() + dy) !=
                  sample(center.x() - 20.0, focus_handle.y() - 18.0 + dy);
    }
    CHECK(focus_ink);
    bool transition_ink = false;
    for (auto dx = -24; dx <= 24 && !transition_ink; ++dx) {
      for (auto dy = -3; dy <= 3 && !transition_ink; ++dy) {
        transition_ink =
            sample(center.x() + dx, transition_handle.y() + dy) !=
            sample(center.x() + dx, transition_handle.y() - 18.0 + dy);
      }
    }
    CHECK(transition_ink);

    drove_dialog = true;
    dialog->reject();
  });

  const auto result = patchy::ui::request_visual_filter_gallery(
      &theme_host, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255},
      [](const patchy::ui::VisualFilterGalleryPreview&) {});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_heavy_thumbnail_queue_yields_to_event_loop() {
  GallerySettingsRestorer gallery_settings;
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  auto slow_started = std::make_shared<std::atomic_bool>(false);
  auto release_slow = std::make_shared<std::atomic_bool>(false);
  patchy::FilterCatalogMetadata slow_catalog;
  slow_catalog.category = patchy::FilterCategory::Render;
  slow_catalog.execute =
      [slow_started, release_slow](
          const patchy::FilterRegistry&, const patchy::FilterInvocation&,
          patchy::PixelBuffer& pixels, const patchy::FilterProgress*) {
        if (pixels.width() > 200) {
          slow_started->store(true, std::memory_order_release);
          for (int wait = 0;
               wait < 500 &&
               !release_slow->load(std::memory_order_acquire);
               ++wait) {
            QThread::msleep(1);
          }
        }
      };
  registry.register_filter({"test.filters.slow_proxy", "Slow Proxy Test",
                            [](patchy::PixelBuffer&) {},
                            std::move(slow_catalog)});
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* category = dialog->findChild<QComboBox*>(QStringLiteral("filterGalleryCategoryCombo"));
    auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("filterGallerySearchEdit"));
    auto* looks = dialog->findChild<QListWidget*>(QStringLiteral("filterGalleryLooksList"));
    auto* preview = dialog->findChild<QWidget*>(
        QStringLiteral("filterGalleryPreview"));
    auto* status = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryStatusLabel"));
    CHECK(category != nullptr && search != nullptr && looks != nullptr &&
          preview != nullptr && status != nullptr);
    // Placeholder icons exist from creation, so thumbnail readiness is
    // signaled by the ready role, never by icon nullity.
    int ready_at_first_tick = -1;
    bool first_tick = false;
    QTimer::singleShot(0, dialog, [&] {
      first_tick = true;
      ready_at_first_tick = 0;
      for (int row = 0; row < looks->count(); ++row) {
        ready_at_first_tick +=
            looks->item(row)->data(Qt::UserRole + 2).toBool() ? 1 : 0;
      }
    });
    CHECK(process_events_until([&] { return first_tick; }, 500));
    CHECK(ready_at_first_tick >= 1);
    CHECK(ready_at_first_tick < looks->count());

    category->setCurrentIndex(
        require_combo_data_index(*category, QStringLiteral("render")));
    search->setText(QStringLiteral("Clouds"));
    QApplication::processEvents();
    CHECK(visible_gallery_filter_ids(*looks) ==
          QStringList{QStringLiteral("patchy.filters.clouds")});
    auto* clouds = require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.clouds"));
    bool ui_marker = false;
    QTimer::singleShot(0, dialog, [&] { ui_marker = true; });
    CHECK(process_events_until([&] { return ui_marker; }, 500));
    CHECK(dialog->isVisible());
    CHECK(process_events_until(
        [&] { return clouds->data(Qt::UserRole + 2).toBool(); }, 5000));

    search->clear();
    auto* slow = require_gallery_filter_item(
        *looks, QStringLiteral("test.filters.slow_proxy"));
    QElapsedTimer responsiveness;
    responsiveness.start();
    bool central_marker = false;
    QTimer::singleShot(50, dialog, [&] { central_marker = true; });
    looks->setCurrentItem(slow);
    CHECK(process_events_until([&] { return central_marker; }, 500));
    CHECK(responsiveness.elapsed() < 180);
    CHECK(slow_started->load(std::memory_order_acquire));
    auto* clouds_after_slow = require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.clouds"));
    looks->setCurrentItem(clouds_after_slow);
    release_slow->store(true, std::memory_order_release);
    process_events_for(20);
    CHECK(preview->property("filterGalleryRenderedFilterId").toString() !=
          QStringLiteral("test.filters.slow_proxy"));
    CHECK(process_events_until(
        [&] {
          return preview->property("filterGalleryRenderedFilterId").toString() ==
                     QStringLiteral("patchy.filters.clouds") &&
                 status->text() ==
                     QCoreApplication::translate("QObject", "Ready");
        },
        1500));
    drove_dialog = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

const QStringList& expected_smart_capable_gallery_ids() {
  static const QStringList ids = {
      QStringLiteral("patchy.filters.box_blur"),
      QStringLiteral("patchy.filters.gaussian_blur"),
      QStringLiteral("patchy.filters.motion_blur"),
      QStringLiteral("patchy.filters.surface_blur"),
      QStringLiteral("patchy.filters.unsharp_mask"),
      QStringLiteral("patchy.filters.high_pass"),
      QStringLiteral("patchy.filters.median"),
      QStringLiteral("patchy.filters.dust_and_scratches"),
      QStringLiteral("patchy.filters.pixelate"),
      QStringLiteral("patchy.filters.emboss"),
      QStringLiteral("patchy.filters.plastic_wrap"),
  };
  return ids;
}

// Counts pixels of the badge chip's fill color in the icon's bottom-right
// corner. The check deliberately targets the chip background, not the "SF"
// glyph, because offscreen font metrics vary.
int smart_filter_badge_pixel_count(const QIcon& icon) {
  const auto image = icon.pixmap(QSize(128, 78)).toImage();
  int count = 0;
  for (int y = 60; y < 75; ++y) {
    for (int x = 101; x < 125; ++x) {
      if (image.pixelColor(x, y) == QColor(0x14, 0x73, 0xe6)) {
        ++count;
      }
    }
  }
  return count;
}

void ui_filter_gallery_smart_filter_badges_and_tooltips() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_dialog = false;

  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("filterGalleryDialog"));
    CHECK(dialog != nullptr);
    auto* looks = dialog->findChild<QListWidget*>(
        QStringLiteral("filterGalleryLooksList"));
    auto* outcome = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryOutcomeLabel"));
    CHECK(looks != nullptr && outcome != nullptr);

    const auto& smart_ids = expected_smart_capable_gallery_ids();
    CHECK(looks->count() == expected_filter_gallery_ids().size() + 1);
    auto* original = looks->item(0);
    CHECK(!original->data(Qt::UserRole + 6).isValid());
    CHECK(original->toolTip().isEmpty());
    int badged_rows = 0;
    for (int row = 1; row < looks->count(); ++row) {
      auto* item = looks->item(row);
      const auto id = item->data(Qt::UserRole + 1).toString();
      const auto capable = smart_ids.contains(id);
      CHECK(item->data(Qt::UserRole + 6).toBool() == capable);
      CHECK(item->toolTip() ==
            (capable
                 ? QStringLiteral(
                       "This filter can run as an editable Smart Filter. "
                       "Use Filter > Convert for Smart Filters on this "
                       "layer to keep it editable.")
                 : QStringLiteral("Applies permanently to the layer "
                                  "pixels.")));
      badged_rows += capable ? 1 : 0;
    }
    CHECK(badged_rows == smart_ids.size());

    // Placeholder icons already carry the chip before any thumbnail renders.
    auto* gaussian = require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur"));
    auto* sepia = require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.sepia"));
    CHECK(smart_filter_badge_pixel_count(gaussian->icon()) > 50);
    CHECK(smart_filter_badge_pixel_count(sepia->icon()) == 0);

    // The plain-layer outcome line is static across selection and edits.
    const auto plain_outcome = QStringLiteral(
        "Applies permanently to this layer. To keep effects editable, use "
        "Filter > Convert for Smart Filters first.");
    CHECK(outcome->text() == plain_outcome);
    looks->setCurrentItem(gaussian);
    QApplication::processEvents();
    auto* radius = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterRadiusSpin"));
    CHECK(radius != nullptr);
    radius->setValue(5);
    QApplication::processEvents();
    CHECK(outcome->text() == plain_outcome);

    // The rendered thumbnail keeps the chip when it replaces the placeholder.
    CHECK(process_events_until(
        [&] { return gaussian->data(Qt::UserRole + 2).toBool(); }, 20000));
    CHECK(smart_filter_badge_pixel_count(gaussian->icon()) > 50);
    save_widget_artifact("ui_filter_gallery_smart_filter_badges", *dialog);

    drove_dialog = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_filter_gallery_smart_object_outcome_line_and_warning_marks() {
  GallerySettingsRestorer gallery_settings;
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  patchy::ui::MainWindow theme_host;
  const auto source = make_filter_stroke_source();
  const patchy::Rect bounds{0, 0, source.width(), source.height()};
  bool drove_dialog = false;
  const auto editable_outcome =
      QStringLiteral("Applies as editable Smart Filters.");
  const auto rasterize_outcome = QStringLiteral(
      "Applying will rasterize the Smart Object (some effects have no "
      "Smart Filter mapping).");
  const auto warning_tooltip = QStringLiteral(
      "This effect cannot be applied as a Smart Filter. Applying the stack "
      "will rasterize the Smart Object.");

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
    auto* outcome = dialog->findChild<QLabel*>(
        QStringLiteral("filterGalleryOutcomeLabel"));
    CHECK(looks != nullptr && applied != nullptr && duplicate != nullptr &&
          remove != nullptr && outcome != nullptr);

    // Smart-object tooltip variants.
    CHECK(require_gallery_filter_item(
              *looks, QStringLiteral("patchy.filters.gaussian_blur"))
              ->toolTip() ==
          QStringLiteral("Applies to this Smart Object as an editable "
                         "Smart Filter."));
    CHECK(require_gallery_filter_item(
              *looks, QStringLiteral("patchy.filters.sepia"))
              ->toolTip() ==
          QStringLiteral("This filter has no Smart Filter mapping. "
                         "Applying it will rasterize the Smart Object."));

    // The empty recipe is Original: nothing rasterizes.
    CHECK(outcome->text() == editable_outcome);

    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.gaussian_blur")));
    QApplication::processEvents();
    CHECK(applied->count() == 1);
    CHECK(outcome->text() == editable_outcome);
    CHECK(applied->item(0)->icon().isNull());
    CHECK(applied->item(0)->toolTip().isEmpty());

    // A Patchy-only entry flips the outcome and marks exactly that row.
    duplicate->click();
    QApplication::processEvents();
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.sepia")));
    QApplication::processEvents();
    CHECK(applied->count() == 2);
    CHECK(applied->item(0)->text() == QStringLiteral("Vintage Sepia"));
    CHECK(outcome->text() == rasterize_outcome);
    CHECK(!applied->item(0)->icon().isNull());
    CHECK(applied->item(0)->toolTip() == warning_tooltip);
    CHECK(applied->item(1)->icon().isNull());
    CHECK(applied->item(1)->toolTip().isEmpty());
    save_widget_artifact("ui_filter_gallery_smart_object_hints", *dialog);

    // The mapping is all-or-nothing over disabled entries too.
    applied->item(0)->setCheckState(Qt::Unchecked);
    QApplication::processEvents();
    CHECK(outcome->text() == rasterize_outcome);
    CHECK(!applied->item(0)->icon().isNull());

    remove->click();
    QApplication::processEvents();
    CHECK(applied->count() == 1);
    CHECK(outcome->text() == editable_outcome);
    CHECK(applied->item(0)->icon().isNull());

    // Parameter gates come from the real mapper, not the static ID list:
    // Photoshop's Emboss Amount minimum is 1, so amount 0 stays destructive.
    looks->setCurrentItem(require_gallery_filter_item(
        *looks, QStringLiteral("patchy.filters.emboss")));
    QApplication::processEvents();
    CHECK(applied->count() == 1);
    CHECK(applied->item(0)->text() == QStringLiteral("Emboss"));
    CHECK(outcome->text() == editable_outcome);
    auto* amount = dialog->findChild<QSpinBox*>(
        QStringLiteral("filterDepthSpin"));
    CHECK(amount != nullptr);
    amount->setValue(0);
    QApplication::processEvents();
    CHECK(outcome->text() == rasterize_outcome);
    CHECK(!applied->item(0)->icon().isNull());
    CHECK(applied->item(0)->toolTip() == warning_tooltip);
    amount->setValue(100);
    QApplication::processEvents();
    CHECK(outcome->text() == editable_outcome);
    CHECK(applied->item(0)->icon().isNull());
    CHECK(applied->item(0)->toolTip().isEmpty());

    drove_dialog = true;
    dialog->reject();
  });
  const auto result = patchy::ui::request_visual_filter_gallery(
      nullptr, source, bounds, QRegion(), registry, patchy::RgbColor{},
      patchy::RgbColor{255, 255, 255}, {}, nullptr, {}, {},
      patchy::ui::GalleryTargetContext{
          patchy::ui::GalleryTargetKind::SmartObject, {}});
  CHECK(drove_dialog);
  CHECK(result.outcome == patchy::ui::VisualFilterGalleryOutcome::Cancelled);
}

void ui_all_builtin_filters_render_stroke_contact_sheet() {
  ensure_artifact_dir();
  patchy::FilterRegistry registry;
  patchy::register_builtin_filters(registry);
  const auto source = make_filter_stroke_source();
  const auto bounds = patchy::Rect{0, 0, source.width(), source.height()};

  std::vector<std::pair<QString, QImage>> cells;
  cells.push_back({QStringLiteral("Original"), flattened_on_white(source)});
  for (const auto& filter : registry.filters()) {
    const auto spec = patchy::ui::filter_dialog_spec_for(filter);
    auto invocation = registry.default_invocation(filter.identifier, patchy::RgbColor{220, 28, 24},
                                                  patchy::RgbColor{255, 255, 255});
    patchy::Rect result_bounds = bounds;
    const auto result = patchy::ui::build_filter_preview_pixels(
        source, QRegion(), bounds, registry, patchy::ui::FilterPreviewSettings{true, std::move(invocation)}, nullptr,
        &result_bounds);
    if (spec.identifier == QStringLiteral("patchy.filters.box_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.gaussian_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.motion_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.radial_blur") ||
        spec.identifier == QStringLiteral("patchy.filters.pixelate")) {
      CHECK(spatial_filter_spreads_clean_red_alpha(source, result, result_bounds.x - bounds.x,
                                                   result_bounds.y - bounds.y));
    }
    if (spec.identifier == QStringLiteral("patchy.filters.clouds")) {
      CHECK(result.pixel(0, 0)[3] == 255);
      CHECK(result.pixel(result.width() - 1, result.height() - 1)[3] == 255);
    }
    cells.push_back({spec.display_name, flattened_on_white(result)});
  }

  constexpr int kColumns = 5;
  constexpr int kCellWidth = 250;
  constexpr int kCellHeight = 220;
  constexpr int kPadding = 10;
  const auto rows = static_cast<int>((cells.size() + kColumns - 1) / kColumns);
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));
  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (std::size_t index = 0; index < cells.size(); ++index) {
    const auto column = static_cast<int>(index % kColumns);
    const auto row = static_cast<int>(index / kColumns);
    const QRect cell(column * kCellWidth, row * kCellHeight, kCellWidth, kCellHeight);
    painter.fillRect(cell.adjusted(4, 4, -4, -4), QColor(42, 45, 51));
    painter.drawText(cell.adjusted(kPadding, 8, -kPadding, -kPadding), Qt::AlignTop | Qt::AlignLeft,
                     cells[index].first);
    const QRect image_rect = cell.adjusted(kPadding, 34, -kPadding, -kPadding);
    const auto scaled = cells[index].second.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint image_pos(image_rect.x() + (image_rect.width() - scaled.width()) / 2,
                           image_rect.y() + (image_rect.height() - scaled.height()) / 2);
    painter.drawImage(image_pos, scaled);
  }
  painter.end();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_all_builtin_filters_stroke_contact_sheet.png")));
}

}  // namespace

std::vector<patchy::test::TestCase> destructive_filters_gallery_tests() {
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
      {"ui_filter_gallery_photo_looks_layout_thumbnails_controls_zoom_and_before",
       ui_filter_gallery_photo_looks_layout_thumbnails_controls_zoom_and_before},
      {"ui_filter_gallery_live_canvas_latest_off_on_and_cancel_restore_exact",
       ui_filter_gallery_live_canvas_latest_off_on_and_cancel_restore_exact},
      {"ui_filter_gallery_original_noop_and_selected_apply_undo_redo",
       ui_filter_gallery_original_noop_and_selected_apply_undo_redo},
      {"ui_filter_gallery_categories_have_stable_tokens_and_exact_members",
       ui_filter_gallery_categories_have_stable_tokens_and_exact_members},
      {"ui_filter_gallery_search_matches_localized_and_canonical_names",
       ui_filter_gallery_search_matches_localized_and_canonical_names},
      {"ui_filter_gallery_favorites_and_dialog_state_persist_across_reopen",
       ui_filter_gallery_favorites_and_dialog_state_persist_across_reopen},
      {"ui_filter_gallery_generated_controls_match_catalog_and_direct_defaults",
       ui_filter_gallery_generated_controls_match_catalog_and_direct_defaults},
      {"ui_filter_gallery_specialized_controls_sync_and_drag_in_expected_directions",
       ui_filter_gallery_specialized_controls_sync_and_drag_in_expected_directions},
      {"ui_filter_gallery_tilt_shift_overlay_syncs_and_freezes_during_drag",
       ui_filter_gallery_tilt_shift_overlay_syncs_and_freezes_during_drag},
      {"ui_filter_gallery_tilt_shift_overlay_uses_grip_bars",
       ui_filter_gallery_tilt_shift_overlay_uses_grip_bars},
      {"ui_filter_gallery_heavy_thumbnail_queue_yields_to_event_loop",
       ui_filter_gallery_heavy_thumbnail_queue_yields_to_event_loop},
      {"ui_filter_gallery_smart_filter_badges_and_tooltips",
       ui_filter_gallery_smart_filter_badges_and_tooltips},
      {"ui_filter_gallery_smart_object_outcome_line_and_warning_marks",
       ui_filter_gallery_smart_object_outcome_line_and_warning_marks},
      {"ui_all_builtin_filters_render_stroke_contact_sheet",
       ui_all_builtin_filters_render_stroke_contact_sheet},
  };
}
