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

void ui_startup_defaults_to_round_brush() {
  SettingsValueRestorer saved_brush_preset(QStringLiteral("tools/brushPreset"));
  SettingsValueRestorer saved_brush_size(QStringLiteral("tools/brushSize"));
  SettingsValueRestorer saved_brush_opacity(QStringLiteral("tools/brushOpacity"));
  SettingsValueRestorer saved_brush_softness(QStringLiteral("tools/brushSoftness"));
  SettingsValueRestorer saved_brush_build_up(QStringLiteral("tools/brushBuildUp"));
  SettingsValueRestorer saved_eraser_size(QStringLiteral("tools/eraserSize"));
  SettingsValueRestorer saved_eraser_opacity(QStringLiteral("tools/eraserOpacity"));
  SettingsValueRestorer saved_eraser_softness(QStringLiteral("tools/eraserSoftness"));
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  {
    auto settings = patchy::ui::app_settings();
    // Stale brush state from an earlier session. A launch must reset all of it
    // (only the eraser size may survive a restart).
    settings.setValue(QStringLiteral("tools/brushPreset"), QStringLiteral("airbrush"));
    settings.setValue(QStringLiteral("tools/brushSize"), 56);
    settings.setValue(QStringLiteral("tools/brushOpacity"), 12);
    settings.setValue(QStringLiteral("tools/brushSoftness"), 100);
    settings.setValue(QStringLiteral("tools/brushBuildUp"), true);
    settings.setValue(QStringLiteral("tools/eraserSize"), 77);
    settings.setValue(QStringLiteral("tools/eraserOpacity"), 15);
    settings.setValue(QStringLiteral("tools/eraserSoftness"), 95);
    settings.setValue(QStringLiteral("tools/gradientMethod"), static_cast<int>(patchy::GradientMethod::Radial));
    settings.setValue(QStringLiteral("tools/gradientReverse"), true);
    settings.setValue(QStringLiteral("tools/gradientOpacity"), 66);
    settings.setValue(QStringLiteral("tools/gradientUseCustomStops"), true);
    settings.setValue(QStringLiteral("tools/gradientStops"),
                      QStringLiteral("[{\"location\":0,\"r\":10,\"g\":20,\"b\":30,\"a\":255},"
                                     "{\"location\":1,\"r\":40,\"g\":50,\"b\":60,\"a\":128}]"));
    settings.sync();
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  CHECK(brush_preset != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(brush_preset->currentData().toString() == QStringLiteral("round"));
  CHECK(brush_size->value() == 25);
  CHECK(brush_opacity->value() == 100);
  CHECK(brush_softness->value() == 0);
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_softness() == 0);
  CHECK(!canvas->brush_build_up());
  // The eraser restores only its size; opacity/softness reset with the brush.
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  CHECK(canvas->brush_size() == 77);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_softness() == 0);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_reverse());
  CHECK(canvas->gradient_opacity() == 66);
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 2);
  CHECK(gradient_method->currentData().toInt() == static_cast<int>(patchy::GradientMethod::Radial));
  CHECK(gradient_opacity->value() == 66);
  CHECK(gradient_reverse->isChecked());
}

void ui_canvas_wheel_matches_photoshop_navigation() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  // This test exercises the Photoshop-style pan navigation (wheel zoom off).
  canvas->set_wheel_zooms(false);

  const auto initial_zoom = canvas->zoom();
  const auto initial_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  send_wheel(*canvas, QPoint(300, 240), 120);
  const auto horizontal_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(horizontal_pan_origin.x() != initial_origin.x());
  CHECK(horizontal_pan_origin.y() == initial_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ControlModifier);
  const auto vertical_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(vertical_pan_origin.y() != horizontal_pan_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
  CHECK(canvas->zoom() > initial_zoom);
  save_widget_artifact("ui_canvas_wheel_navigation", *canvas);
}

void ui_canvas_wheel_zoom_mode_zooms_at_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();
  // The mode default is platform-dependent (macOS pans on a plain wheel; see
  // MainWindow::kWheelZoomsDefault). Pin the default, then switch the zoom mode on
  // explicitly -- this test drives the MODE's behavior, not the default.
  CHECK(canvas->wheel_zooms() == patchy::ui::MainWindow::kWheelZoomsDefault);
  canvas->set_wheel_zooms(true);

  const auto initial_zoom = canvas->zoom();
  send_wheel(*canvas, QPoint(300, 240), 120);
  CHECK(canvas->zoom() > initial_zoom);

  send_wheel(*canvas, QPoint(300, 240), -120);
  send_wheel(*canvas, QPoint(300, 240), -120);
  CHECK(canvas->zoom() < initial_zoom);

  const auto zoom_before_pan = canvas->zoom();
  const auto origin_before_pan = canvas->widget_position_for_document_point(QPoint(0, 0));
  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ShiftModifier);
  CHECK(canvas->zoom() == zoom_before_pan);
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)).x() != origin_before_pan.x());
}

void ui_status_bar_zoom_percent_box_edits_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  auto* zoom_edit = window.statusBar()->findChild<patchy::ui::ZoomPercentEdit*>(QStringLiteral("statusZoomEdit"));
  CHECK(zoom_edit != nullptr);
  CHECK(zoom_edit->isEnabled());
  CHECK(zoom_edit->text() == patchy::ui::ZoomPercentEdit::format_zoom_text(canvas->zoom()));

  // The box sits at the far left and must stay visible while a persistent status
  // message shows (QStatusBar hides normal left-side widgets whenever one does).
  window.statusBar()->showMessage(QStringLiteral("Something informative"));
  QApplication::processEvents();
  CHECK(zoom_edit->isVisible());
  CHECK(zoom_edit->x() < 20);

  // Typing a percentage and pressing Enter applies it.
  zoom_edit->setFocus();
  QApplication::processEvents();
  zoom_edit->setText(QStringLiteral("300"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 3.0) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("300%"));

  // Out-of-range values clamp to the canvas zoom limit (12800%).
  zoom_edit->setFocus();
  zoom_edit->setText(QStringLiteral("999999"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 128.0) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("12800%"));

  // Fractional percentages apply and display with two decimals.
  zoom_edit->setFocus();
  zoom_edit->setText(QStringLiteral("33.33"));
  send_key(*zoom_edit, Qt::Key_Return);
  CHECK(std::abs(canvas->zoom() - 0.3333) < 1e-6);
  CHECK(zoom_edit->text() == QStringLiteral("33.33%"));

  // Escape reverts a pending edit without changing the zoom.
  zoom_edit->setFocus();
  QApplication::processEvents();
  zoom_edit->setText(QStringLiteral("55"));
  send_key(*zoom_edit, Qt::Key_Escape);
  CHECK(zoom_edit->text() == QStringLiteral("33.33%"));
  CHECK(std::abs(canvas->zoom() - 0.3333) < 1e-6);

  // Zooming by any other means keeps the box in sync.
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  CHECK(zoom_edit->text() == QStringLiteral("100%"));

  save_widget_artifact("ui_status_zoom_percent_box", *window.statusBar());
}

void ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // Center the view on the middle of the default 1024x768 document, then zoom to 300%.
  canvas->fit_to_view();
  canvas->set_zoom_centered(3.0);
  CHECK(std::abs(canvas->zoom() - 3.0) < 1e-6);
  const QPoint document_center(512, 384);
  const QPoint widget_center(canvas->width() / 2, canvas->height() / 2);
  const auto before = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(before.x() - widget_center.x()) <= 2);
  CHECK(std::abs(before.y() - widget_center.y()) <= 2);

  // Double-clicking the Zoom tool resets to 100% and must keep the viewport anchored:
  // the document point that was at the viewport center stays there instead of the
  // canvas panning mostly off screen.
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(zoom_button != nullptr);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 1e-6);
  const auto after = canvas->widget_position_for_document_point(document_center);
  CHECK(std::abs(after.x() - widget_center.x()) <= 2);
  CHECK(std::abs(after.y() - widget_center.y()) <= 2);
}

void ui_canvas_focus_in_restores_tool_cursor() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(120, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(20);
  canvas.show();
  QApplication::processEvents();

  // Simulate the OS (Windows) resetting the cursor to an arrow on re-activation.
  canvas.setCursor(Qt::ArrowCursor);
  CHECK(canvas.cursor().shape() == Qt::ArrowCursor);

  // Regaining focus / re-entry must restore the brush (custom pixmap) cursor.
  canvas.refresh_tool_cursor();
  CHECK(canvas.cursor().shape() == Qt::BitmapCursor);
}

void ui_max_brush_uses_overlay_cursor() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(120, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_brush_size(patchy::ui::kMaxBrushSize);
  canvas.show();
  QApplication::processEvents();

  CHECK(canvas.cursor().shape() == Qt::CrossCursor);
  CHECK(canvas.cursor().pixmap().isNull());
}

void ui_canvas_pan_keeps_document_partly_visible() {
  patchy::Document document(100, 80, patchy::PixelFormat::rgba8());
  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 400);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Pan);
  canvas.show();
  QApplication::processEvents();

  const auto minimum_visible = [](int viewport_span, int document_span) {
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(std::min(viewport_span, document_span)) * 0.10)));
  };
  const auto visible_document_rect = [&] {
    const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
    return QRect(top_left, QSize(document.width(), document.height())).intersected(canvas.rect());
  };
  const auto check_minimum_visible = [&] {
    const auto visible = visible_document_rect();
    CHECK(visible.width() >= minimum_visible(canvas.width(), document.width()));
    CHECK(visible.height() >= minimum_visible(canvas.height(), document.height()));
  };

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(5000, 4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(5000, 4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();

  send_mouse(canvas, QEvent::MouseButtonPress, QPoint(250, 200), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, QPoint(-5000, -4000), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, QPoint(-5000, -4000), Qt::LeftButton, Qt::NoButton);
  check_minimum_visible();
}

void ui_canvas_fractional_zoom_paints_to_document_edge() {
  patchy::Document document(1024, 768, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(1024, 768, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 80;
      px[2] = 40;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Opaque", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(915, 706);
  canvas.set_document(&document);
  canvas.fit_to_view();
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QPoint right_edge_sample(bottom_right.x() - 1, (top_left.y() + bottom_right.y()) / 2);
  CHECK(preview.rect().contains(right_edge_sample));
  CHECK(!color_close(preview.pixelColor(right_edge_sample), QColor(36, 38, 41), 4));
}

void ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_zoom(5.5);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

void ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp() {
  patchy::Document document(2, 6, patchy::PixelFormat::rgb8());
  auto pixels = solid_pixels(2, 6, patchy::PixelFormat::rgb8(), Qt::white);
  fill_pixel_rect(pixels, QRect(0, 0, 1, 6), Qt::black);
  document.add_pixel_layer("Split", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_zoom(12.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const auto sample_y = (top_left.y() + bottom_right.y()) / 2;
  int black_columns = 0;
  int white_columns = 0;
  int interpolated_columns = 0;
  for (int x = top_left.x() + 1; x < bottom_right.x() - 1; ++x) {
    if (!preview.rect().contains(QPoint(x, sample_y))) {
      continue;
    }
    const auto color = preview.pixelColor(x, sample_y);
    if (color_close(color, QColor(Qt::black), 8)) {
      ++black_columns;
    } else if (color_close(color, QColor(Qt::white), 8)) {
      ++white_columns;
    } else {
      ++interpolated_columns;
    }
  }
  CHECK(black_columns > 0);
  CHECK(white_columns > 0);
  CHECK(interpolated_columns == 0);
}

void ui_zoomed_out_canvas_uses_downsampled_display_mip() {
  patchy::Document document(256, 256, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(256, 256, patchy::PixelFormat::rgb8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto value = ((x + y) % 2 == 0) ? 0 : 255;
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(value);
      px[1] = static_cast<std::uint8_t>(value);
      px[2] = static_cast<std::uint8_t>(value);
    }
  }
  document.add_pixel_layer("Checker", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 180);
  canvas.set_document(&document);
  canvas.set_zoom(0.25);
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QRect target_rect(top_left, QSize(bottom_right.x() - top_left.x(), bottom_right.y() - top_left.y()));
  const auto sample_rect = target_rect.adjusted(4, 4, -4, -4).intersected(preview.rect());
  CHECK(!sample_rect.isEmpty());

  int midtone_samples = 0;
  int source_tone_samples = 0;
  for (int y = sample_rect.top(); y <= sample_rect.bottom(); y += 3) {
    for (int x = sample_rect.left(); x <= sample_rect.right(); x += 3) {
      const auto color = preview.pixelColor(x, y);
      const auto value = (color.red() + color.green() + color.blue()) / 3;
      if (value >= 96 && value <= 160) {
        ++midtone_samples;
      }
      if (value <= 24 || value >= 231) {
        ++source_tone_samples;
      }
    }
  }

  CHECK(midtone_samples > 0);
  CHECK(midtone_samples > source_tone_samples * 4);
}

void ui_shape_flyout_and_zoom_tool_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(zoom_button != nullptr);

  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::EllipticalMarquee);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Elliptical Marquee")));

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Ellipse);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Ellipse")));

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Zoom);
  canvas->set_zoom(0.25);
  const auto before_zoom = canvas->zoom();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(420, 320)));
  CHECK(canvas->zoom() > before_zoom);

  canvas->set_zoom(2.0);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  save_widget_artifact("ui_shape_flyout_zoom_tool", window);
}

void ui_tool_palette_icons_render_sheet() {
  patchy::ui::MainWindow window;
  show_window(window);

  struct ToolIconEntry {
    const char* action_name;
    const char* label;
  };
  const std::vector<ToolIconEntry> tools = {
      {"toolMoveAction", "Move"},
      {"toolMarqueeAction", "Marquee"},
      {"toolEllipticalMarqueeAction", "Elliptical Marquee"},
      {"toolLassoAction", "Lasso"},
      {"toolMagneticLassoAction", "Magnetic Lasso"},
      {"toolMagicWandAction", "Magic Wand"},
      {"toolQuickSelectAction", "Quick Select"},
      {"toolBrushAction", "Brush"},
      {"toolCloneAction", "Clone"},
      {"toolSmudgeAction", "Smudge"},
      {"toolEraserAction", "Eraser"},
      {"toolGradientAction", "Gradient"},
      {"toolFillAction", "Fill"},
      {"toolLineAction", "Line"},
      {"toolRectAction", "Rect"},
      {"toolEllipseAction", "Ellipse"},
      {"toolPickAction", "Pick"},
      {"toolTypeAction", "Type"},
      {"toolHandAction", "Hand"},
      {"toolZoomAction", "Zoom"},
  };

  // The real button backgrounds from the app stylesheet: palette, hover, checked.
  const std::array<QColor, 3> state_backgrounds = {QColor(0x53, 0x53, 0x53), QColor(0x4a, 0x4a, 0x4a),
                                                   QColor(0x2f, 0x75, 0xbd)};

  constexpr int kLabelWidth = 118;
  constexpr int kSmallCell = 34;
  constexpr int kLargeCell = 56;
  constexpr int kRowHeight = 56;
  const int sheet_width = kLabelWidth + kSmallCell * 4 + kLargeCell + 12;
  const int sheet_height = kRowHeight * static_cast<int>(tools.size()) + 8;
  QImage sheet(sheet_width, sheet_height, QImage::Format_RGB32);
  sheet.fill(QColor(0x2b, 0x2b, 0x2b));
  QPainter painter(&sheet);
  painter.setFont(visual_test_font());

  std::vector<QImage> normal_renders;
  QImage gradient_render;
  QStringList coverage_problems;
  int y = 4;
  for (const auto& tool : tools) {
    auto* action = window.findChild<QAction*>(QString::fromLatin1(tool.action_name));
    CHECK(action != nullptr);
    const auto icon = action->icon();
    CHECK(!icon.isNull());

    // Tool icons come from SVG resources; a missing file or typo'd qrc alias renders EMPTY
    // silently, so assert real pixel coverage of the 20px render the palette uses. The
    // sparsest legitimate icon is the single-stroke Line tool at ~30 covered pixels.
    const auto normal20 = icon.pixmap(QSize(20, 20)).toImage().convertToFormat(QImage::Format_ARGB32);
    CHECK(normal20.width() == 20 && normal20.height() == 20);
    int covered = 0;
    int bright = 0;
    for (int py = 0; py < normal20.height(); ++py) {
      for (int px = 0; px < normal20.width(); ++px) {
        const auto pixel = normal20.pixel(px, py);
        if (qAlpha(pixel) > 60) {
          ++covered;
          if (qGray(pixel) > 140) {
            ++bright;
          }
        }
      }
    }
    if (covered <= 25 || bright <= 15) {
      coverage_problems << QStringLiteral("%1: covered=%2 bright=%3")
                               .arg(QString::fromLatin1(tool.label))
                               .arg(covered)
                               .arg(bright);
    }
    normal_renders.push_back(normal20);
    if (QString::fromLatin1(tool.label) == QStringLiteral("Gradient")) {
      gradient_render = normal20;
    }

    painter.setPen(QColor(0xdc, 0xe2, 0xeb));
    painter.drawText(QRect(6, y, kLabelWidth - 10, kRowHeight - 8), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromLatin1(tool.label));
    int x = kLabelWidth;
    const auto draw_cell = [&painter](int cell_x, int cell_y, int cell_size, const QColor& background,
                                      const QPixmap& pixmap) {
      const QRect cell(cell_x, cell_y, cell_size, cell_size);
      painter.fillRect(cell, background);
      painter.drawPixmap(cell.x() + (cell.width() - pixmap.width()) / 2,
                         cell.y() + (cell.height() - pixmap.height()) / 2, pixmap);
    };
    const auto small20 = icon.pixmap(QSize(20, 20));
    for (const auto& background : state_backgrounds) {
      draw_cell(x + 3, y + (kRowHeight - kSmallCell) / 2 + 3, kSmallCell - 6, background, small20);
      x += kSmallCell;
    }
    draw_cell(x + 3, y + (kRowHeight - kSmallCell) / 2 + 3, kSmallCell - 6, state_backgrounds[0],
              icon.pixmap(QSize(20, 20), QIcon::Disabled));
    x += kSmallCell;
    draw_cell(x + 5, y + (kRowHeight - kLargeCell) / 2 + 5, kLargeCell - 10, state_backgrounds[0],
              icon.pixmap(QSize(40, 40)));
    y += kRowHeight;
  }
  painter.end();

  for (const auto& problem : coverage_problems) {
    std::fprintf(stderr, "tool icon coverage problem: %s\n", qPrintable(problem));
  }
  CHECK(coverage_problems.isEmpty());

  // Every tool must render distinctly (catches copy-paste mistakes in the qrc aliases).
  for (std::size_t i = 0; i < normal_renders.size(); ++i) {
    for (std::size_t j = i + 1; j < normal_renders.size(); ++j) {
      CHECK(normal_renders[i] != normal_renders[j]);
    }
  }

  // The Gradient icon is the one SVG relying on linearGradient support: its swatch must
  // interpolate from the neutral left edge to a clearly blue right edge.
  CHECK(!gradient_render.isNull());
  const auto gradient_left = gradient_render.pixel(5, 10);
  const auto gradient_right = gradient_render.pixel(15, 10);
  CHECK(qAlpha(gradient_left) > 200);
  CHECK(qAlpha(gradient_right) > 200);
  CHECK(qBlue(gradient_right) - qRed(gradient_right) > 60);
  CHECK(qBlue(gradient_left) - qRed(gradient_left) < 40);

  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_tool_palette_icons_sheet.png")));

  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  save_widget_artifact("ui_tool_palette", *tool_palette);
}

void ui_filled_shape_preview_clears_after_commit() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Ellipse);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(96);
  canvas->set_fill_shapes(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 170)),
       canvas->widget_position_for_document_point(QPoint(370, 310)));
  QApplication::processEvents();

  const auto outside_final_shape = canvas_pixel(*canvas, QPoint(170, 125));
  CHECK(color_close(outside_final_shape, Qt::white, 10));
  save_widget_artifact("ui_filled_shape_preview_cleanup", *canvas);

  const auto immediate = canvas->grab().toImage();
  canvas->document_changed();
  QApplication::processEvents();
  const auto repainted = canvas->grab().toImage();
  CHECK(immediate.size() == repainted.size());
  CHECK(immediate.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))) ==
        repainted.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))));
}

void ui_options_bar_tracks_active_tool() {
  SettingsValueRestorer saved_gradient_method(QStringLiteral("tools/gradientMethod"));
  SettingsValueRestorer saved_gradient_reverse(QStringLiteral("tools/gradientReverse"));
  SettingsValueRestorer saved_gradient_opacity(QStringLiteral("tools/gradientOpacity"));
  SettingsValueRestorer saved_gradient_use_custom(QStringLiteral("tools/gradientUseCustomStops"));
  SettingsValueRestorer saved_gradient_stops(QStringLiteral("tools/gradientStops"));
  SettingsValueRestorer saved_text_smoothing(QStringLiteral("tools/textSmoothing"));
  SettingsValueRestorer saved_show_transform_controls(QStringLiteral("tools/showTransformControls"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/showTransformControls"));
  settings.sync();
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* move_auto_select = window.findChild<QCheckBox*>(QStringLiteral("moveAutoSelectCheck"));
  auto* move_show_transform_controls = window.findChild<QCheckBox*>(QStringLiteral("moveShowTransformControlsCheck"));
  auto* text_font = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* text_smoothing = window.findChild<QComboBox*>(QStringLiteral("textSmoothingCombo"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* gradient_method = window.findChild<QComboBox*>(QStringLiteral("gradientMethodCombo"));
  auto* gradient_opacity = window.findChild<QSpinBox*>(QStringLiteral("gradientOpacitySpin"));
  auto* gradient_opacity_slider = window.findChild<QSlider*>(QStringLiteral("gradientOpacitySlider"));
  auto* gradient_reverse = window.findChild<QCheckBox*>(QStringLiteral("gradientReverseCheck"));
  auto* gradient_preview = window.findChild<QPushButton*>(QStringLiteral("gradientPreviewButton"));
  auto* gradient_edit_stops = window.findChild<QPushButton*>(QStringLiteral("gradientEditStopsButton"));
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* wand_tolerance = window.findChild<QSpinBox*>(QStringLiteral("wandToleranceSpin"));
  auto* wand_contiguous = window.findChild<QCheckBox*>(QStringLiteral("wandContiguousCheck"));
  auto* wand_sample_all_layers = window.findChild<QCheckBox*>(QStringLiteral("wandSampleAllLayersCheck"));
  auto* feather_group = window.findChild<QWidget*>(QStringLiteral("selectionFeatherGroup"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(move_auto_select != nullptr);
  CHECK(move_show_transform_controls != nullptr);
  CHECK(text_font != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(text_size->minimum() <= 0.01);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(text_smoothing != nullptr);
  CHECK(text_color != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(gradient_method != nullptr);
  CHECK(gradient_opacity != nullptr);
  CHECK(gradient_opacity_slider != nullptr);
  CHECK(gradient_reverse != nullptr);
  CHECK(gradient_preview != nullptr);
  CHECK(gradient_edit_stops != nullptr);
  CHECK(clone_aligned != nullptr);
  CHECK(wand_tolerance != nullptr);
  CHECK(wand_contiguous != nullptr);
  CHECK(wand_sample_all_layers != nullptr);
  CHECK(feather_group != nullptr);
  CHECK(anti_alias != nullptr);
  CHECK(anti_alias->isChecked());
  CHECK(wand_contiguous->isChecked());
  CHECK(!wand_sample_all_layers->isChecked());

  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!gradient_method->isVisible());
  CHECK(!gradient_opacity->isVisible());
  CHECK(!gradient_reverse->isVisible());
  CHECK(!gradient_edit_stops->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!move_show_transform_controls->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(move_auto_select->isVisible());
  CHECK(move_show_transform_controls->isVisible());
  CHECK(move_show_transform_controls->isChecked());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_size_slider->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_opacity_slider->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!text_font->isVisible());
  move_auto_select->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->auto_select_layer());
  move_auto_select->setChecked(true);
  QApplication::processEvents();
  CHECK(move_auto_select->isChecked());
  CHECK(canvas->auto_select_layer());
  move_show_transform_controls->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->show_transform_controls());
  move_show_transform_controls->setChecked(true);
  QApplication::processEvents();
  CHECK(move_show_transform_controls->isChecked());
  CHECK(canvas->show_transform_controls());
  save_widget_artifact("ui_tool_options_move", window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(text_font->isVisible());
  CHECK(text_size->isVisible());
  CHECK(text_bold->isVisible());
  CHECK(text_italic->isVisible());
  CHECK(text_color->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  text_size->setValue(0.01);
  CHECK(std::abs(text_size->value() - 0.01) < 0.001);
  text_size->setValue(text_points_for_pixels(36));
  text_bold->setChecked(true);
  save_widget_artifact("ui_tool_options_text", window);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(wand_tolerance->isVisible());
  CHECK(wand_contiguous->isVisible());
  CHECK(wand_sample_all_layers->isVisible());
  CHECK(feather_group->isVisible());
  CHECK(anti_alias->isVisible());
  CHECK(!text_font->isVisible());
  CHECK(!text_color->isVisible());
  wand_contiguous->setChecked(false);
  wand_sample_all_layers->setChecked(true);
  QApplication::processEvents();
  CHECK(!canvas->wand_contiguous());
  CHECK(canvas->wand_sample_all_layers());
  wand_contiguous->setChecked(true);
  wand_sample_all_layers->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->wand_contiguous());
  CHECK(!canvas->wand_sample_all_layers());

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(clone_aligned->isVisible());
  CHECK(clone_aligned->isChecked());
  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  clone_aligned->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->clone_aligned());
  CHECK(!wand_tolerance->isVisible());
  CHECK(!wand_contiguous->isVisible());
  CHECK(!wand_sample_all_layers->isVisible());

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());

  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  QApplication::processEvents();
  CHECK(gradient_method->isVisible());
  CHECK(gradient_opacity->isVisible());
  CHECK(gradient_opacity_slider->isVisible());
  CHECK(gradient_reverse->isVisible());
  CHECK(gradient_preview->isVisible());
  CHECK(gradient_edit_stops->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  const auto radial_index = gradient_method->findText(QStringLiteral("Radial"));
  CHECK(radial_index >= 0);
  gradient_method->setCurrentIndex(radial_index);
  gradient_opacity_slider->setValue(55);
  gradient_reverse->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->gradient_method() == patchy::GradientMethod::Radial);
  CHECK(canvas->gradient_opacity() == 55);
  CHECK(canvas->gradient_reverse());

  QTimer::singleShot(0, [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("gradientStopsDialog"));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QWidget*>(QStringLiteral("gradientStopsPreview"));
    auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gradientStopsTable"));
    auto* add_stop = dialog->findChild<QPushButton*>(QStringLiteral("gradientAddStopButton"));
    auto* choose_color = dialog->findChild<QPushButton*>(QStringLiteral("gradientChooseStopColorButton"));
    CHECK(preview != nullptr);
    CHECK(table != nullptr);
    CHECK(add_stop != nullptr);
    CHECK(choose_color != nullptr);
    CHECK(table->rowCount() == 2);

    constexpr int gradient_gutter = 10;
    const auto handle_x = [preview](double location) {
      constexpr int gutter = 10;
      const int track_max = std::max(1, preview->width() - gutter * 2 - 2);
      return gutter + static_cast<int>(std::lround(std::clamp(location, 0.0, 1.0) * track_max));
    };
    const int bar_right_x = handle_x(1.0);
    const auto preview_image = preview->grab().toImage();
    CHECK(preview_image.rect().contains(QPoint(bar_right_x, 16)));
    CHECK(preview_image.pixelColor(bar_right_x, 16).value() > 180);
    int visible_left_handle_pixels = 0;
    int visible_right_handle_pixels = 0;
    for (int y = 44; y < preview_image.height(); ++y) {
      for (int x = 0; x <= gradient_gutter + 12 && x < preview_image.width(); ++x) {
        const auto color = preview_image.pixelColor(x, y);
        if (color.blue() > 120 || color.value() < 16) {
          ++visible_left_handle_pixels;
        }
      }
      for (int x = std::max(0, preview_image.width() - gradient_gutter - 13); x < preview_image.width(); ++x) {
        if (preview_image.pixelColor(x, y).value() > 180) {
          ++visible_right_handle_pixels;
        }
      }
    }
    CHECK(visible_left_handle_pixels > 12);
    CHECK(visible_right_handle_pixels > 12);

    const int handle_y = 48;
    const int x10 = handle_x(0.10);
    const int x50 = handle_x(0.50);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x10, handle_y), Qt::NoButton, Qt::NoButton);
    CHECK(preview->cursor().shape() == Qt::CrossCursor);
    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x10, handle_y), Qt::LeftButton, Qt::LeftButton);
    CHECK(table->rowCount() == 3);
    CHECK(table->currentRow() == 2);
    CHECK(table->item(2, 0)->text() == QStringLiteral("10"));
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, handle_y), Qt::NoButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, handle_y), Qt::LeftButton, Qt::NoButton);
    CHECK(table->item(2, 0)->text() == QStringLiteral("50"));

    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(bar_right_x, 16), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(bar_right_x, 16), Qt::LeftButton, Qt::NoButton);
    const QColor sampled_color(table->item(2, 1)->text());
    CHECK(sampled_color.isValid());
    CHECK(sampled_color.value() > 180);

    add_stop->click();
    CHECK(table->rowCount() == 4);
    const int x60 = handle_x(0.60);
    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x60, handle_y), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x60, preview->height() + 4), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x60, preview->height() + 16), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, handle_y), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    CHECK(table->item(3, 0)->text() == QStringLiteral("50"));
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, handle_y), Qt::LeftButton, Qt::NoButton);
    CHECK(table->rowCount() == 4);

    send_mouse(*preview, QEvent::MouseButtonPress, QPoint(x50, handle_y), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*preview, QEvent::MouseMove, QPoint(x50, 8), Qt::NoButton, Qt::LeftButton);
    CHECK(table->rowCount() == 4);
    send_mouse(*preview, QEvent::MouseButtonRelease, QPoint(x50, 8), Qt::LeftButton, Qt::NoButton);
    CHECK(table->rowCount() == 3);

    table->item(2, 0)->setText(QStringLiteral("50"));
    table->item(2, 1)->setText(QStringLiteral("#00FF00"));
    table->item(2, 2)->setText(QStringLiteral("25"));
    table->setCurrentCell(2, 1);

    const QColor original_stop_color(table->item(2, 1)->text());
    bool saw_live_stop_picker = false;
    QTimer::singleShot(0, [&] {
      auto* color_dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyColorDialog")));
      CHECK(color_dialog != nullptr);
      auto* picker = color_dialog->findChild<patchy::ui::PatchyColorPicker*>(
          QStringLiteral("patchyAdvancedColorPicker"));
      CHECK(picker != nullptr);
      picker->setCurrentColor(QColor(12, 34, 56));
      QApplication::processEvents();
      CHECK(table->item(2, 1)->text() == QStringLiteral("#0C2238"));
      saw_live_stop_picker = true;
      color_dialog->reject();
    });
    choose_color->click();
    CHECK(saw_live_stop_picker);
    CHECK(table->item(2, 1)->text() == original_stop_color.name(QColor::HexRgb).toUpper());

    dialog->accept();
  });
  gradient_edit_stops->click();
  QApplication::processEvents();
  CHECK(canvas->gradient_stops().has_value());
  CHECK(canvas->gradient_stops()->size() == 3);
  CHECK(canvas->gradient_stops()->at(1).color.g == 255);
  CHECK(canvas->gradient_stops()->at(1).color.a >= 63);
  CHECK(canvas->gradient_stops()->at(1).color.a <= 64);
}

void ui_right_docks_collapse_layers_show_metadata_and_info_updates() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  auto* document_info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  auto* active_layer_info = window.findChild<QLabel*>(QStringLiteral("activeLayerInfoLabel"));
  auto* active_layer_geometry = window.findChild<QLabel*>(QStringLiteral("activeLayerGeometryLabel"));
  auto* active_layer_mask = window.findChild<QLabel*>(QStringLiteral("activeLayerMaskLabel"));
  auto* active_layer_adjustment = window.findChild<QLabel*>(QStringLiteral("activeLayerAdjustmentLabel"));
  auto* active_layer_text = window.findChild<QLabel*>(QStringLiteral("activeLayerTextLabel"));
  auto* active_tool_info = window.findChild<QLabel*>(QStringLiteral("activeToolInfoLabel"));
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(layer_list != nullptr);
  CHECK(info != nullptr);
  CHECK(document_info != nullptr);
  CHECK(active_layer_info != nullptr);
  CHECK(active_layer_geometry != nullptr);
  CHECK(active_layer_mask != nullptr);
  CHECK(active_layer_adjustment != nullptr);
  CHECK(active_layer_text != nullptr);
  CHECK(active_tool_info != nullptr);
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(document_info->text().contains(QStringLiteral("Document")));
  CHECK(document_info->text().contains(QStringLiteral("1024 x 768 px")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Paint Layer")));
  CHECK(active_layer_info->text().contains(QStringLiteral("Pixel Layer")));
  CHECK(active_layer_geometry->text().contains(QStringLiteral("Bounds:")));
  CHECK(!active_layer_mask->isVisible());
  CHECK(!active_layer_adjustment->isVisible());
  CHECK(!active_layer_text->isVisible());
  CHECK(active_tool_info->text().contains(QStringLiteral("Brush")));
  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  auto* history_dock = window.findChild<QDockWidget*>(QStringLiteral("historyDock"));
  auto* properties_dock = window.findChild<QDockWidget*>(QStringLiteral("propertiesDock"));
  auto* history_list = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layers_dock != nullptr);
  CHECK(history_dock != nullptr);
  CHECK(properties_dock != nullptr);
  CHECK(history_list != nullptr);
  CHECK(layers_dock->minimumWidth() >= 280);
  CHECK(layers_dock->minimumHeight() >= 300);
  CHECK(layer_list->minimumHeight() >= 120);
  CHECK(properties_dock->maximumHeight() <= 240);
  CHECK(properties_dock->height() <= 240);
  CHECK(window.minimumSizeHint().height() <= 780);
  CHECK(layer_list->contextMenuPolicy() == Qt::CustomContextMenu);
  const auto layer_action_buttons = window.findChildren<QPushButton*>();
  int visible_layer_action_buttons = 0;
  for (const auto* button : layer_action_buttons) {
    if (button->property("layerActionButton").toBool()) {
      ++visible_layer_action_buttons;
      CHECK(button->minimumWidth() >= 40);
      CHECK(button->minimumHeight() >= 34);
      CHECK(button->iconSize().width() >= 24);
      CHECK(button->iconSize().height() >= 24);
    }
  }
  CHECK(visible_layer_action_buttons == 5);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(window.findChild<QDockWidget*>(QStringLiteral("swatchesDock")) == nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));
  CHECK(history_toggle->icon().isNull());
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(history_list->count() > 0);
  history_toggle->setChecked(true);
  QApplication::processEvents();
  const auto history_row_height = history_list->sizeHintForRow(0);
  CHECK(history_row_height > 0);
  CHECK(history_toggle->text() == QStringLiteral("v"));
  CHECK(history_dock->minimumHeight() >= 190);
  CHECK(history_dock->height() >= 190);
  CHECK(history_list->viewport()->height() >= history_row_height * 3);
  save_widget_artifact("ui_history_expanded_default", *history_dock);
  history_toggle->setChecked(false);
  QApplication::processEvents();
  CHECK(layers_dock->width() >= 260);
  const auto dock_width_before_resize = layers_dock->width();
  auto* dock_resize_handle = window.findChild<QWidget*>(QStringLiteral("rightDockResizeHandle"));
  CHECK(dock_resize_handle != nullptr);
  const auto dock_resize_point = dock_resize_handle->rect().center();
  send_mouse(*dock_resize_handle, QEvent::MouseButtonPress, dock_resize_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseMove, dock_resize_point + QPoint(-90, 0), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(*dock_resize_handle, QEvent::MouseButtonRelease, dock_resize_point + QPoint(-90, 0), Qt::LeftButton,
             Qt::NoButton);
  CHECK(layers_dock->width() > dock_width_before_resize + 40);

  auto* row_widget = layer_list->itemWidget(layer_list->item(0));
  CHECK(row_widget != nullptr);
  CHECK(row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")) != nullptr);

  const auto point = canvas->widget_position_for_document_point(QPoint(64, 48));
  send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("X: 64")));
  CHECK(info->text().contains(QStringLiteral("Y: 48")));
  CHECK(info->text().contains(QStringLiteral("RGB:")));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  const auto marquee_start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto marquee_end = canvas->widget_position_for_document_point(QPoint(140, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, marquee_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, marquee_end, Qt::NoButton, Qt::LeftButton);
  CHECK(info->text().contains(QStringLiteral("Selection:")));
  CHECK(info->text().contains(QStringLiteral(" at 40, 40")));
  send_mouse(*canvas, QEvent::MouseButtonRelease, marquee_end, Qt::LeftButton, Qt::NoButton);
  save_widget_artifact("ui_info_panel_layers_docks", window);
}

void ui_layer_opacity_slider_defers_slow_rendering_and_undoes_once() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Opacity Target",
                      solid_pixels(90, 70, patchy::PixelFormat::rgba8(), QColor(30, 150, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{35, 25, 90, 70});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Opacity Deferred"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  canvas->force_refresh();
  QApplication::processEvents();

  auto* opacity_slider = window.findChild<QSlider*>(QStringLiteral("layerOpacitySlider"));
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(opacity_slider != nullptr);
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_slider->value() == 100);
  CHECK(opacity_spin->value() == 100);

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_render_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("240"));

  QElapsedTimer slider_updates;
  slider_updates.start();
  for (int value = 99; value >= 25; --value) {
    opacity_slider->setValue(value);
    CHECK(opacity_spin->value() == value);
    QApplication::processEvents(QEventLoop::AllEvents, 1);
  }
  CHECK(slider_updates.elapsed() < 300);
  CHECK(opacity_slider->value() == 25);
  CHECK(opacity_spin->value() == 25);

  auto& edited_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* edited_layer = edited_document.find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  QElapsedTimer wait_for_apply;
  wait_for_apply.start();
  while (std::abs(edited_layer->opacity() - 0.25F) > 0.001F && wait_for_apply.elapsed() < 900) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  CHECK(std::abs(edited_layer->opacity() - 0.25F) <= 0.001F);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 1.0F) <= 0.001F);
}

void ui_collapsed_right_docks_keep_deep_layer_rows_readable() {
  patchy::Document document(128, 128, patchy::PixelFormat::rgba8());
  patchy::Layer root(document.allocate_layer_id(), "Root Folder", patchy::LayerKind::Group);
  auto* current = &root;
  for (int depth = 1; depth <= 8; ++depth) {
    current->add_child(
        patchy::Layer(document.allocate_layer_id(), "Nested Folder " + std::to_string(depth), patchy::LayerKind::Group));
    current = &current->children().back();
  }
  auto deep_pixels = solid_pixels(128, 128, patchy::PixelFormat::rgba8(), QColor(20, 120, 220, 255));
  patchy::Layer deep_layer(document.allocate_layer_id(), "Deep Paint Layer With Long Name", std::move(deep_pixels));
  const auto deep_layer_id = deep_layer.id();
  current->add_child(std::move(deep_layer));
  for (int index = 1; index <= 24; ++index) {
    current->add_child(patchy::Layer(document.allocate_layer_id(), "Deep Scroll Filler " + std::to_string(index),
                                     solid_pixels(128, 128, patchy::PixelFormat::rgba8(),
                                                  QColor(35, 70 + (index * 7) % 120, 160, 255))));
  }
  document.add_layer(std::move(root));
  document.set_active_layer(deep_layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Deep Layers"));
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* properties_toggle = window.findChild<QToolButton*>(QStringLiteral("propertiesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layer_list != nullptr);
  CHECK(history_toggle != nullptr);
  CHECK(properties_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(history_toggle->text() == QStringLiteral(">"));
  CHECK(properties_toggle->text() == QStringLiteral(">"));
  CHECK(info_toggle->text() == QStringLiteral(">"));

  auto* deep_item = require_layer_item(*layer_list, QStringLiteral("Deep Paint Layer With Long Name"));
  layer_list->scrollToItem(deep_item, QAbstractItemView::PositionAtCenter);
  QApplication::processEvents();

  auto* row_widget = layer_list->itemWidget(deep_item);
  CHECK(row_widget != nullptr);
  auto* visibility = row_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  auto* thumbnail = row_widget->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  auto* horizontal_scroll = layer_list->horizontalScrollBar();
  CHECK(visibility != nullptr);
  CHECK(thumbnail != nullptr);
  CHECK(horizontal_scroll != nullptr);
  CHECK(horizontal_scroll->maximum() > horizontal_scroll->minimum());
  horizontal_scroll->setValue(horizontal_scroll->minimum());
  QApplication::processEvents();
  const auto initial_visibility_left = visibility->mapTo(layer_list->viewport(), QPoint()).x();
  const auto initial_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(initial_visibility_left >= 0);
  CHECK(initial_visibility_left < layer_list->viewport()->width() / 2);
  horizontal_scroll->setValue(std::clamp(initial_thumbnail_right - (layer_list->viewport()->width() - 16),
                                        horizontal_scroll->minimum(), horizontal_scroll->maximum()));
  QApplication::processEvents();
  const auto scrolled_thumbnail_right = thumbnail->mapTo(layer_list->viewport(), QPoint(thumbnail->width(), 0)).x();
  CHECK(scrolled_thumbnail_right <= layer_list->viewport()->width() - 16);

  auto scrollbar_ancestor = [](QWidget* widget) -> QScrollBar* {
    for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
      if (auto* scroll = qobject_cast<QScrollBar*>(current); scroll != nullptr) {
        return scroll;
      }
    }
    return nullptr;
  };
  auto scrollbar_slider_rect = [](QScrollBar* scroll) {
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
    return scroll->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, scroll);
  };
  auto check_scrollbar_hit_target = [&](QScrollBar* scroll) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = scroll->orientation() == Qt::Vertical
                           ? QPoint(std::clamp(scroll->width() - 2, slider.left(), slider.right()),
                                    slider.center().y())
                           : QPoint(slider.center().x(),
                                    std::clamp(scroll->height() - 2, slider.top(), slider.bottom()));
    auto* hit = layer_list->childAt(scroll->mapTo(layer_list, start));
    CHECK(scrollbar_ancestor(hit) == scroll);
  };
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());

  auto clear_layer_row_masks = [&] {
    for (int row_index = 0; row_index < layer_list->count(); ++row_index) {
      if (auto* row = layer_list->itemWidget(layer_list->item(row_index)); row != nullptr) {
        row->clearMask();
      }
    }
  };
  auto send_mouse_at_global = [](QWidget& widget, QEvent::Type type, QPoint global_position,
                                 Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, widget.mapFromGlobal(global_position), global_position, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
  };
  auto drag_scrollbar_through_current_hit = [&](QScrollBar* scroll, int pixels) {
    CHECK(scroll != nullptr);
    CHECK(scroll->maximum() > scroll->minimum());
    scroll->setValue((scroll->minimum() + scroll->maximum()) / 2);
    QApplication::processEvents();
    const auto slider = scrollbar_slider_rect(scroll);
    CHECK(slider.isValid());
    const auto start = slider.center();
    const auto start_global = scroll->mapToGlobal(start);
    auto* hit = layer_list->childAt(layer_list->mapFromGlobal(start_global));
    CHECK(hit != nullptr);
    const auto before = scroll->value();
    const auto end_global =
        start_global + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0));
    if (hit == scroll) {
      send_mouse(*scroll, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseMove, start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels)
                                                                                             : QPoint(pixels, 0)),
                 Qt::NoButton, Qt::LeftButton);
      send_mouse(*scroll, QEvent::MouseButtonRelease,
                 start + (scroll->orientation() == Qt::Vertical ? QPoint(0, pixels) : QPoint(pixels, 0)),
                 Qt::LeftButton, Qt::NoButton);
    } else {
      send_mouse_at_global(*hit, QEvent::MouseButtonPress, start_global, Qt::LeftButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseMove, end_global, Qt::NoButton, Qt::LeftButton);
      send_mouse_at_global(*hit, QEvent::MouseButtonRelease, end_global, Qt::LeftButton, Qt::NoButton);
    }
    return scroll->value() > before;
  };
  QMessageBox warning(QMessageBox::Warning, QStringLiteral("Warning"), QStringLiteral("Warning"), QMessageBox::Ok,
                      &window);
  QTimer::singleShot(0, &warning, [&] { warning.accept(); });
  warning.exec();
  clear_layer_row_masks();
  CHECK(drag_scrollbar_through_current_hit(layer_list->verticalScrollBar(), 48));
  CHECK(drag_scrollbar_through_current_hit(layer_list->horizontalScrollBar(), 48));
  QEvent activate_event(QEvent::WindowActivate);
  QApplication::sendEvent(layer_list, &activate_event);
  QApplication::processEvents();
  check_scrollbar_hit_target(layer_list->verticalScrollBar());
  check_scrollbar_hit_target(layer_list->horizontalScrollBar());
  save_widget_artifact("ui_collapsed_right_docks_deep_layer_rows", window);
}

void ui_menu_disabled_items_render_grayed() {
  // The app stylesheet styles QMenu::item text, so without an explicit :disabled rule
  // disabled entries rendered in the same bright color as enabled ones and were only
  // discoverable by their refusal to highlight.
  patchy::ui::MainWindow window;
  show_window(window);

  QMenu menu(&window);
  auto* enabled_action = menu.addAction(QStringLiteral("Enabled entry"));
  auto* disabled_action = menu.addAction(QStringLiteral("Disabled entry"));
  disabled_action->setEnabled(false);
  menu.popup(window.mapToGlobal(QPoint(60, 60)));
  QApplication::processEvents();

  const auto image = menu.grab().toImage();
  const auto enabled_rect = menu.actionGeometry(enabled_action);
  const auto disabled_rect = menu.actionGeometry(disabled_action);
  menu.close();
  QApplication::processEvents();

  const QColor enabled_text(0xe6, 0xe6, 0xe6);
  const QColor disabled_text(0x73, 0x73, 0x73);
  CHECK(count_pixels_close(image, enabled_rect, enabled_text, 24) > 10);   // bright enabled label
  CHECK(count_pixels_close(image, disabled_rect, enabled_text, 24) == 0);  // no bright pixels on the disabled row
  CHECK(count_pixels_close(image, disabled_rect, disabled_text, 24) > 10);  // grayed label
}

}  // namespace

std::vector<patchy::test::TestCase> canvas_view_tools_tests() {
  return {
      {"ui_startup_defaults_to_round_brush", ui_startup_defaults_to_round_brush},
      {"ui_canvas_wheel_matches_photoshop_navigation", ui_canvas_wheel_matches_photoshop_navigation},
      {"ui_canvas_wheel_zoom_mode_zooms_at_cursor", ui_canvas_wheel_zoom_mode_zooms_at_cursor},
      {"ui_status_bar_zoom_percent_box_edits_zoom", ui_status_bar_zoom_percent_box_edits_zoom},
      {"ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels",
       ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels},
      {"ui_canvas_focus_in_restores_tool_cursor", ui_canvas_focus_in_restores_tool_cursor},
      {"ui_max_brush_uses_overlay_cursor", ui_max_brush_uses_overlay_cursor},
      {"ui_canvas_pan_keeps_document_partly_visible", ui_canvas_pan_keeps_document_partly_visible},
      {"ui_canvas_fractional_zoom_paints_to_document_edge", ui_canvas_fractional_zoom_paints_to_document_edge},
      {"ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp",
       ui_canvas_fractional_zoom_keeps_zoomed_in_pixels_sharp},
      {"ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp",
       ui_canvas_deep_zoom_without_grid_keeps_pixels_sharp},
      {"ui_zoomed_out_canvas_uses_downsampled_display_mip",
       ui_zoomed_out_canvas_uses_downsampled_display_mip},
      {"ui_shape_flyout_and_zoom_tool_work", ui_shape_flyout_and_zoom_tool_work},
      {"ui_tool_palette_icons_render_sheet", ui_tool_palette_icons_render_sheet},
      {"ui_filled_shape_preview_clears_after_commit", ui_filled_shape_preview_clears_after_commit},
      {"ui_options_bar_tracks_active_tool", ui_options_bar_tracks_active_tool},
      {"ui_right_docks_collapse_layers_show_metadata_and_info_updates",
       ui_right_docks_collapse_layers_show_metadata_and_info_updates},
      {"ui_layer_opacity_slider_defers_slow_rendering_and_undoes_once",
       ui_layer_opacity_slider_defers_slow_rendering_and_undoes_once},
      {"ui_collapsed_right_docks_keep_deep_layer_rows_readable",
       ui_collapsed_right_docks_keep_deep_layer_rows_readable},
      {"ui_menu_disabled_items_render_grayed", ui_menu_disabled_items_render_grayed},
  };
}
