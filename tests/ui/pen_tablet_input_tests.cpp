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

void ui_pen_pressure_controls_brush_size_and_opacity() {
  patchy::Document document(96, 48, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(96, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.set_pen_input_settings(patchy::ui::CanvasWidget::PenInputSettings{});
  canvas.show();
  QApplication::processEvents();

  const auto low = canvas.widget_position_for_document_point(QPoint(24, 24));
  send_tablet(canvas, QEvent::TabletPress, low, 0.10);
  send_tablet(canvas, QEvent::TabletRelease, low, 0.0, Qt::LeftButton, Qt::NoButton);
  const auto high = canvas.widget_position_for_document_point(QPoint(66, 24));
  send_tablet(canvas, QEvent::TabletPress, high, 1.0);
  send_tablet(canvas, QEvent::TabletRelease, high, 0.0, Qt::LeftButton, Qt::NoButton);

  const auto* painted = document.find_layer(layer_id);
  CHECK(painted != nullptr);
  const auto& pixels = painted->pixels();
  const auto row_alpha_count = [&pixels](int y, int left, int right) {
    int count = 0;
    for (int x = left; x <= right; ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++count;
      }
    }
    return count;
  };
  const auto* low_center = pixels.pixel(24, 24);
  const auto* high_center = pixels.pixel(66, 24);
  CHECK(low_center[3] > 0U);
  CHECK(low_center[3] < 80U);
  CHECK(high_center[3] >= 250U);
  CHECK(row_alpha_count(24, 56, 76) > row_alpha_count(24, 14, 34) * 2);

  const auto sample = canvas.last_pen_input_sample();
  CHECK(sample.has_value());
  CHECK(sample->pressure_available);
  CHECK(sample->pointer_type == patchy::ui::CanvasWidget::PenInputSample::PointerType::Pen);
}

void ui_pen_pressure_respects_brush_control_override() {
  // A brush whose dynamics set a size/opacity control (including Off) owns that aspect and the
  // global pressure preferences leave it alone — per aspect, Brush tool only.
  patchy::Document document(144, 48, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(144, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(288, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.set_pen_input_settings(patchy::ui::CanvasWidget::PenInputSettings{});  // globals all on
  canvas.show();
  QApplication::processEvents();

  const auto& pixels = document.find_layer(layer_id)->pixels();
  const auto row_alpha_count = [&pixels](int y, int left, int right, int threshold) {
    int count = 0;
    for (int x = left; x <= right; ++x) {
      if (pixels.pixel(x, y)[3] > threshold) {
        ++count;
      }
    }
    return count;
  };
  const auto tap = [&canvas](QPoint document_point, qreal pressure,
                             QPointingDevice::PointerType pointer =
                                 QPointingDevice::PointerType::Pen) {
    const auto position = canvas.widget_position_for_document_point(document_point);
    send_tablet(canvas, QEvent::TabletPress, position, pressure, Qt::LeftButton, Qt::LeftButton,
                Qt::NoModifier, pointer);
    send_tablet(canvas, QEvent::TabletRelease, position, 0.0, Qt::LeftButton, Qt::NoButton,
                Qt::NoModifier, pointer);
  };

  // Size control Off: a light tap paints at the full 20px width, while opacity (still on the
  // global preference) stays pressure-faded — the override is per aspect.
  patchy::BrushDynamics size_off;
  size_off.size_control = patchy::BrushDynamicControl::Off;
  CHECK(!size_off.active());  // suppression-only overrides stay on the procedural path
  canvas.set_brush_dynamics(size_off);
  tap(QPoint(24, 24), 0.10);
  CHECK(row_alpha_count(24, 14, 34, 0) >= 17);
  CHECK(pixels.pixel(24, 24)[3] > 0U);
  CHECK(pixels.pixel(24, 24)[3] < 90U);

  // Opacity control Off as well: the same light tap now paints full-strength and full-width.
  auto both_off = size_off;
  both_off.opacity_control = patchy::BrushDynamicControl::Off;
  canvas.set_brush_dynamics(both_off);
  tap(QPoint(72, 24), 0.10);
  CHECK(row_alpha_count(24, 62, 82, 0) >= 17);
  CHECK(pixels.pixel(72, 24)[3] >= 250U);

  // Back to the defaults (follow the global preferences): the light tap shrinks like today.
  canvas.set_brush_dynamics(patchy::BrushDynamics{});
  tap(QPoint(120, 24), 0.10);
  const auto global_width = row_alpha_count(24, 110, 130, 0);
  CHECK(global_width >= 3);
  CHECK(global_width <= 10);

  // The pen's eraser end never runs dynamics, so the brush override must not leak to it: a
  // medium-pressure erase over the solid dab still cuts a pressure-sized (narrow) hole.
  canvas.set_brush_dynamics(both_off);
  tap(QPoint(72, 24), 0.30, QPointingDevice::PointerType::Eraser);
  const auto erased_width = row_alpha_count(24, 62, 82, 0) - row_alpha_count(24, 62, 82, 200);
  CHECK(erased_width >= 3);
  CHECK(erased_width <= 14);
}

void ui_pen_missing_pressure_uses_full_pressure_and_hover_does_not_paint() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto no_pressure_caps = QInputDevice::Capabilities(QInputDevice::Capability::Position);
  send_tablet(canvas, QEvent::TabletMove, canvas.widget_position_for_document_point(QPoint(8, 8)), 0.0,
              Qt::NoButton, Qt::NoButton, Qt::NoModifier, QPointingDevice::PointerType::Pen, no_pressure_caps);
  CHECK(document.find_layer(layer_id)->pixels().pixel(8, 8)[3] == 0U);

  const auto center = canvas.widget_position_for_document_point(QPoint(32, 24));
  send_tablet(canvas, QEvent::TabletPress, center, 0.0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
              QPointingDevice::PointerType::Pen, no_pressure_caps);
  send_tablet(canvas, QEvent::TabletRelease, center, 0.0, Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
              QPointingDevice::PointerType::Pen, no_pressure_caps);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  CHECK(pixels.pixel(32, 24)[3] >= 250U);
  int width = 0;
  for (int x = 20; x <= 44; ++x) {
    if (pixels.pixel(x, 24)[3] > 0U) {
      ++width;
    }
  }
  CHECK(width >= 17);
  const auto sample = canvas.last_pen_input_sample();
  CHECK(sample.has_value());
  CHECK(!sample->pressure_available);
  CHECK(sample->pressure == 1.0F);
}

void ui_pen_eraser_tip_temporarily_erases_without_switching_tool() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto center = canvas.widget_position_for_document_point(QPoint(32, 24));
  send_tablet(canvas, QEvent::TabletPress, center, 1.0);
  send_tablet(canvas, QEvent::TabletRelease, center, 0.0, Qt::LeftButton, Qt::NoButton);
  CHECK(document.find_layer(layer_id)->pixels().pixel(32, 24)[3] >= 250U);

  send_tablet(canvas, QEvent::TabletPress, center, 1.0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
              QPointingDevice::PointerType::Eraser);
  send_tablet(canvas, QEvent::TabletRelease, center, 0.0, Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
              QPointingDevice::PointerType::Eraser);
  CHECK(document.find_layer(layer_id)->pixels().pixel(32, 24)[3] == 0U);
  CHECK(canvas.tool() == patchy::ui::CanvasTool::Brush);
  const auto sample = canvas.last_pen_input_sample();
  CHECK(sample.has_value());
  CHECK(sample->pointer_type == patchy::ui::CanvasWidget::PenInputSample::PointerType::Eraser);
}

void ui_pen_barrel_button_pans_instead_of_painting() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);
  canvas.show();
  QApplication::processEvents();

  const auto before_origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto start = QPoint(70, 60);
  send_tablet(canvas, QEvent::TabletPress, start, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletMove, start + QPoint(35, 18), 1.0, Qt::NoButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, start + QPoint(35, 18), 0.0, Qt::RightButton, Qt::NoButton);
  const auto after_origin = canvas.widget_position_for_document_point(QPoint(0, 0));

  CHECK(after_origin != before_origin);
  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels == 0);
}

void ui_pen_secondary_button_picks_color_without_painting() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(40, 160, 220, 255)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);

  QColor picked;
  canvas.set_color_picked_callback([&picked](QColor color) { picked = color; });
  canvas.show();
  QApplication::processEvents();

  const auto point = QPoint(70, 60);
  send_tablet(canvas, QEvent::TabletPress, point, 1.0, Qt::MiddleButton, Qt::MiddleButton);
  send_tablet(canvas, QEvent::TabletMove, point + QPoint(10, 6), 1.0, Qt::NoButton, Qt::MiddleButton);
  send_tablet(canvas, QEvent::TabletRelease, point + QPoint(10, 6), 0.0, Qt::MiddleButton, Qt::NoButton);

  CHECK(picked.isValid());
  CHECK(picked.red() == 40);
  CHECK(picked.green() == 160);
  CHECK(picked.blue() == 220);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  int black_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto pixel = pixels.pixel(x, y);
      if (pixel[0] == 0U && pixel[1] == 0U && pixel[2] == 0U && pixel[3] > 0U) {
        ++black_pixels;
      }
    }
  }
  CHECK(black_pixels == 0);
}

void ui_pen_button_action_routes_to_callback() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);

  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::Undo;
  canvas.set_pen_input_settings(settings);

  std::vector<patchy::ui::PenButtonAction> actions;
  canvas.set_pen_button_action_callback(
      [&actions](patchy::ui::PenButtonAction action) { actions.push_back(action); });
  canvas.show();
  QApplication::processEvents();

  const auto point = QPoint(70, 60);
  send_tablet(canvas, QEvent::TabletPress, point, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletMove, point + QPoint(12, 8), 1.0, Qt::NoButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, point + QPoint(12, 8), 0.0, Qt::RightButton, Qt::NoButton);

  CHECK(actions.size() == 1);
  CHECK(actions.front() == patchy::ui::PenButtonAction::Undo);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels == 0);
}

void ui_pen_zoom_button_drag_changes_zoom_without_painting() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);

  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::ZoomCanvas;
  canvas.set_pen_input_settings(settings);
  canvas.show();
  QApplication::processEvents();

  const auto before_zoom = canvas.zoom();
  const auto start = QPoint(90, 100);
  send_tablet(canvas, QEvent::TabletPress, start, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletMove, start + QPoint(0, -40), 1.0, Qt::NoButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, start + QPoint(0, -40), 0.0, Qt::RightButton, Qt::NoButton);
  const auto after_zoom = canvas.zoom();

  CHECK(after_zoom > before_zoom);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels == 0);
}

void ui_pen_button_mouse_clicks_follow_pen_actions_when_pen_hovering() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);

  // The user's Wacom setup: pen buttons emitted by the driver as plain
  // right/middle mouse clicks, Upper = pan, Lower = zoom drag.
  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::PanCanvas;
  settings.secondary_button_action = patchy::ui::PenButtonAction::ZoomCanvas;
  canvas.set_pen_input_settings(settings);
  canvas.show();
  QApplication::processEvents();

  // A bare mouse middle-drag (no pen in proximity) keeps the classic pan.
  const auto start = QPoint(90, 100);
  const auto before_mouse_zoom = canvas.zoom();
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(0, -40), Qt::NoButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(0, -40), Qt::MiddleButton, Qt::NoButton);
  CHECK(canvas.zoom() == before_mouse_zoom);

  // The same middle-drag right after a hover tablet event is the pen's lower
  // button: it must run the configured zoom drag instead of panning.
  send_tablet(canvas, QEvent::TabletMove, start, 0.0, Qt::NoButton, Qt::NoButton);
  const auto before_pen_zoom = canvas.zoom();
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(0, -40), Qt::NoButton, Qt::MiddleButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(0, -40), Qt::MiddleButton, Qt::NoButton);
  CHECK(canvas.zoom() > before_pen_zoom);

  // The gesture must not leave painting suppressed or a pan/zoom stuck on.
  const auto paint_from = canvas.widget_position_for_document_point(QPoint(30, 30));
  const auto paint_to = canvas.widget_position_for_document_point(QPoint(60, 30));
  drag(canvas, paint_from, paint_to);
  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels > 0);
}

void ui_pen_button_sets_clone_source() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);

  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::SetCloneSource;
  canvas.set_pen_input_settings(settings);

  QString status;
  canvas.set_status_callback([&status](QString message) { status = std::move(message); });
  canvas.show();
  QApplication::processEvents();

  const auto widget_point = canvas.widget_position_for_document_point(QPoint(40, 30));
  send_tablet(canvas, QEvent::TabletPress, widget_point, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, widget_point, 0.0, Qt::RightButton, Qt::NoButton);

  CHECK(status.contains(QStringLiteral("Clone source set")));
}

void ui_pen_tip_paints_after_dropped_barrel_release() {
  patchy::Document document(128, 96, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(128, 96, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(20);

  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::PickColor;
  canvas.set_pen_input_settings(settings);
  canvas.show();
  QApplication::processEvents();

  // Barrel button press with no matching release (as some tablet drivers send
  // the release as a plain mouse event) must not leave painting suppressed.
  const auto point = QPoint(80, 70);
  send_tablet(canvas, QEvent::TabletPress, point, 1.0, Qt::RightButton, Qt::RightButton);

  const auto stroke = QPoint(60, 50);
  send_tablet(canvas, QEvent::TabletPress, stroke, 1.0, Qt::LeftButton, Qt::LeftButton);
  send_tablet(canvas, QEvent::TabletMove, stroke + QPoint(6, 4), 1.0, Qt::NoButton, Qt::LeftButton);
  send_tablet(canvas, QEvent::TabletRelease, stroke + QPoint(6, 4), 0.0, Qt::LeftButton, Qt::NoButton);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 0U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels > 0);
}

void ui_pen_swap_colors_action_routes_to_callback() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(120, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);

  patchy::ui::CanvasWidget::PenInputSettings settings;
  settings.primary_button_action = patchy::ui::PenButtonAction::SwapColors;
  canvas.set_pen_input_settings(settings);

  std::vector<patchy::ui::PenButtonAction> actions;
  canvas.set_pen_button_action_callback(
      [&actions](patchy::ui::PenButtonAction action) { actions.push_back(action); });
  canvas.show();
  QApplication::processEvents();

  const auto point = QPoint(60, 60);
  send_tablet(canvas, QEvent::TabletPress, point, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, point, 0.0, Qt::RightButton, Qt::NoButton);

  CHECK(actions.size() == 1);
  CHECK(actions.front() == patchy::ui::PenButtonAction::SwapColors);
}

void ui_pen_tilt_shape_can_elongate_brush_dabs() {
  patchy::Document document(80, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("Paint",
                                         solid_pixels(80, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 140);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(31);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  auto settings = patchy::ui::CanvasWidget::PenInputSettings{};
  settings.pressure_size = false;
  settings.pressure_opacity = false;
  settings.tilt_shape = true;
  settings.tilt_min_roundness_percent = 20;
  canvas.set_pen_input_settings(settings);
  canvas.show();
  QApplication::processEvents();

  const auto center = canvas.widget_position_for_document_point(QPoint(40, 32));
  send_tablet(canvas, QEvent::TabletPress, center, 1.0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
              QPointingDevice::PointerType::Pen,
              QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                  QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
                  QInputDevice::Capability::Rotation | QInputDevice::Capability::TangentialPressure |
                  QInputDevice::Capability::ZPosition,
              80.0F, 0.0F, 0.0, 0.25F, 12.0F);
  send_tablet(canvas, QEvent::TabletRelease, center, 0.0, Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
              QPointingDevice::PointerType::Pen,
              QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                  QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
                  QInputDevice::Capability::Rotation | QInputDevice::Capability::TangentialPressure |
                  QInputDevice::Capability::ZPosition,
              80.0F, 0.0F, 0.0, 0.25F, 12.0F);

  const auto& pixels = document.find_layer(layer_id)->pixels();
  int horizontal = 0;
  for (int x = 0; x < pixels.width(); ++x) {
    if (pixels.pixel(x, 32)[3] > 0U) {
      ++horizontal;
    }
  }
  int vertical = 0;
  for (int y = 0; y < pixels.height(); ++y) {
    if (pixels.pixel(40, y)[3] > 0U) {
      ++vertical;
    }
  }
  CHECK(horizontal > vertical * 2);
  const auto sample = canvas.last_pen_input_sample();
  CHECK(sample.has_value());
  CHECK(sample->tilt_available);
  CHECK(sample->rotation_available);
  CHECK(sample->tangential_pressure_available);
  CHECK(sample->z_available);
  CHECK(sample->x_tilt == 80.0F);
}

}  // namespace

std::vector<patchy::test::TestCase> pen_tablet_input_tests() {
  return {
      {"ui_pen_pressure_controls_brush_size_and_opacity",
       ui_pen_pressure_controls_brush_size_and_opacity},
      {"ui_pen_pressure_respects_brush_control_override", ui_pen_pressure_respects_brush_control_override},
      {"ui_pen_missing_pressure_uses_full_pressure_and_hover_does_not_paint",
       ui_pen_missing_pressure_uses_full_pressure_and_hover_does_not_paint},
      {"ui_pen_eraser_tip_temporarily_erases_without_switching_tool",
       ui_pen_eraser_tip_temporarily_erases_without_switching_tool},
      {"ui_pen_barrel_button_pans_instead_of_painting",
       ui_pen_barrel_button_pans_instead_of_painting},
      {"ui_pen_secondary_button_picks_color_without_painting",
       ui_pen_secondary_button_picks_color_without_painting},
      {"ui_pen_button_action_routes_to_callback",
       ui_pen_button_action_routes_to_callback},
      {"ui_pen_zoom_button_drag_changes_zoom_without_painting",
       ui_pen_zoom_button_drag_changes_zoom_without_painting},
      {"ui_pen_button_mouse_clicks_follow_pen_actions_when_pen_hovering",
       ui_pen_button_mouse_clicks_follow_pen_actions_when_pen_hovering},
      {"ui_pen_button_sets_clone_source", ui_pen_button_sets_clone_source},
      {"ui_pen_tip_paints_after_dropped_barrel_release",
       ui_pen_tip_paints_after_dropped_barrel_release},
      {"ui_pen_swap_colors_action_routes_to_callback",
       ui_pen_swap_colors_action_routes_to_callback},
      {"ui_pen_tilt_shape_can_elongate_brush_dabs",
       ui_pen_tilt_shape_can_elongate_brush_dabs},
  };
}
