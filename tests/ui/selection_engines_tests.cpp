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

void ui_gradient_and_magic_wand_render_visually() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(60, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, QPoint(280, 140), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto live_preview = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const auto live_fill_pixel = QColor(live_preview.pixel(170, 80));
  CHECK(live_fill_pixel.green() < 80);
  CHECK(live_fill_pixel.red() > 80);
  CHECK(live_fill_pixel.blue() > 80);

  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(280, 140), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto gradient_image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(gradient_image.pixel(62, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(gradient_image.pixel(278, 140)), QColor(0, 0, 255), 35));
  save_widget_artifact("ui_gradient_tool", *canvas);

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(62, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(62, 140), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->selected_document_rect().has_value());
  save_widget_artifact("ui_magic_wand_selection", *canvas);
}

void ui_radial_gradient_tool_renders_custom_transparency() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Gradient);
  canvas->set_gradient_method(patchy::GradientMethod::Radial);
  canvas->set_gradient_opacity(100);
  canvas->set_gradient_reverse(false);
  canvas->set_gradient_stops(std::vector<patchy::GradientStop>{
      patchy::GradientStop{0.0F, patchy::EditColor{255, 0, 0, 255}},
      patchy::GradientStop{1.0F, patchy::EditColor{0, 0, 255, 0}},
  });
  drag(*canvas, QPoint(160, 140), QPoint(220, 140));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(160, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(image.pixel(220, 140)), Qt::white, 35));
  save_widget_artifact("ui_radial_gradient_transparency", *canvas);
}

void ui_magic_wand_contiguous_and_sample_all_layers_options_work() {
  patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
  auto lower_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(lower_pixels, QRect(210, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Lower Match", std::move(lower_pixels));

  auto active_pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(30, 42, 38, 38), QColor(220, 30, 55, 255));
  fill_pixel_rect(active_pixels, QRect(110, 42, 38, 38), QColor(220, 30, 55, 255));
  document.add_pixel_layer("Active Matches", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Options"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_selection_feather_radius(0);

  const auto click_wand = [canvas](QPoint document_point) {
    const auto widget_point = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(false);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(!canvas->selected_document_region().contains(QPoint(220, 52)));

  canvas->clear_selection();
  canvas->set_wand_contiguous(false);
  canvas->set_wand_sample_all_layers(true);
  click_wand(QPoint(40, 52));
  CHECK(canvas->selected_document_region().contains(QPoint(40, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(120, 52)));
  CHECK(canvas->selected_document_region().contains(QPoint(220, 52)));
  save_widget_artifact("ui_magic_wand_options", *canvas);
}

void ui_quick_mask_feathered_round_trip_is_exact_and_temporary() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(40, 150, 220)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Round Trip"));
  show_window(window);
  auto* canvas = require_canvas(window);

  patchy::PixelBuffer feathered(96, 72, patchy::PixelFormat::gray8());
  feathered.clear(0);
  for (int y = 16; y < 56; ++y) {
    for (int x = 20; x < 76; ++x) {
      const auto edge_distance = std::min({x - 20, 75 - x, y - 16, 55 - y});
      *feathered.pixel(x, y) = static_cast<std::uint8_t>(
          std::clamp(edge_distance * 32, 0, 255));
    }
  }
  canvas->replace_selection_from_grayscale(
      feathered, QStringLiteral("Feathered Quick Mask fixture"));
  CHECK(canvas->selection_has_partial_alpha());

  std::vector<std::uint8_t> alpha_before;
  alpha_before.reserve(96U * 72U);
  for (int y = 0; y < 72; ++y) {
    for (int x = 0; x < 96; ++x) {
      alpha_before.push_back(canvas->selection_alpha_at(QPoint(x, y)));
    }
  }
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  auto* quick_mask = require_hotkey_action(window, QStringLiteral("select.quick_mask"));
  CHECK(quick_mask == require_action(window, "selectQuickMaskAction"));
  CHECK(quick_mask->isCheckable());
  CHECK(quick_mask->shortcut() == QKeySequence(Qt::Key_Q));
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  CHECK(quick_mask->isChecked());
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(!canvas->quick_mask_active());
  CHECK(!quick_mask->isChecked());

  std::size_t index = 0;
  for (int y = 0; y < 72; ++y) {
    for (int x = 0; x < 96; ++x) {
      CHECK(canvas->selection_alpha_at(QPoint(x, y)) == alpha_before[index++]);
    }
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_mask_overlay_channels_and_tab_state_are_temporary() {
  const auto make_document = [](QColor color) {
    patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
    document.add_pixel_layer(
        "Pixels", solid_pixels(80, 60, patchy::PixelFormat::rgba8(), color));
    return document;
  };

  patchy::ui::MainWindow window;
  window.add_document_session(make_document(QColor(40, 150, 220)),
                              QStringLiteral("Quick Mask First"));
  auto* first_canvas = require_canvas(window);
  const QColor first_primary(31, 92, 201);
  const QColor first_secondary(241, 188, 63);
  first_canvas->set_primary_color(first_primary);
  first_canvas->set_secondary_color(first_secondary);

  window.add_document_session(make_document(QColor(80, 130, 190)),
                              QStringLiteral("Quick Mask Second"));
  auto* second_canvas = require_canvas(window);
  const QColor second_primary(183, 44, 96);
  const QColor second_secondary(24, 202, 125);
  second_canvas->set_primary_color(second_primary);
  second_canvas->set_secondary_color(second_secondary);
  show_window(window);

  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  auto* channels_dock =
      window.findChild<QDockWidget*>(QStringLiteral("channelsDock"));
  auto* quick_mask_button =
      window.findChild<QToolButton*>(QStringLiteral("quickMaskButton"));
  auto* quick_mask = require_hotkey_action(window, QStringLiteral("select.quick_mask"));
  CHECK(tabs != nullptr);
  CHECK(channels != nullptr);
  CHECK(channels_dock != nullptr);
  CHECK(quick_mask_button != nullptr);
  CHECK(quick_mask_button->defaultAction() == quick_mask);
  CHECK(quick_mask_button->isCheckable());

  tabs->setCurrentIndex(tabs->indexOf(first_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == first_canvas);
  CHECK(first_canvas->primary_color() == first_primary);
  CHECK(first_canvas->secondary_color() == first_secondary);
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(first_canvas->quick_mask_active());
  CHECK(first_canvas->primary_color() == QColor(Qt::black));
  CHECK(first_canvas->secondary_color() == QColor(Qt::white));
  CHECK(channels->count() == 5);
  CHECK(channels->currentRow() == 4);
  auto* row = channels->item(4);
  CHECK(row != nullptr);
  CHECK(row->text() == QStringLiteral("Quick Mask"));
  CHECK(row->flags().testFlag(Qt::ItemIsEnabled));
  CHECK(row->flags().testFlag(Qt::ItemIsSelectable));
  CHECK(!row->flags().testFlag(Qt::ItemIsDragEnabled));
  CHECK(!row->flags().testFlag(Qt::ItemIsDropEnabled));
  CHECK(!row->flags().testFlag(Qt::ItemIsUserCheckable));
  CHECK(!row->toolTip().isEmpty());
  CHECK(std::all_of(first_canvas->quick_mask_pixels().data().begin(),
                    first_canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0; }));

  const auto overlaid = canvas_pixel_center(*first_canvas, QPoint(40, 30));
  CHECK(overlaid.red() > 135);
  CHECK(overlaid.green() >= 70 && overlaid.green() <= 82);
  CHECK(overlaid.blue() >= 105 && overlaid.blue() <= 116);
  channels_dock->raise();
  process_events_for(100);
  CHECK(channels->isVisible());
  save_widget_artifact("ui_quick_mask_overlay_channels", window);

  tabs->setCurrentIndex(tabs->indexOf(second_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == second_canvas);
  CHECK(!quick_mask->isChecked());
  CHECK(second_canvas->primary_color() == second_primary);
  CHECK(second_canvas->secondary_color() == second_secondary);
  CHECK(channels->count() == 4);

  tabs->setCurrentIndex(tabs->indexOf(first_canvas));
  QApplication::processEvents();
  CHECK(require_canvas(window) == first_canvas);
  CHECK(quick_mask->isChecked());
  CHECK(channels->count() == 5);
  quick_mask->trigger();
  QApplication::processEvents();
  CHECK(!first_canvas->quick_mask_active());
  CHECK(first_canvas->primary_color() == first_primary);
  CHECK(first_canvas->secondary_color() == first_secondary);
  CHECK(channels->count() == 4);

  tabs->setCurrentIndex(tabs->indexOf(second_canvas));
  QApplication::processEvents();
  CHECK(second_canvas->primary_color() == second_primary);
  CHECK(second_canvas->secondary_color() == second_secondary);
}

void ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(55, 125, 205)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Brush"));
  show_window(window);
  auto* canvas = require_canvas(window);

  patchy::PixelBuffer initial_selection(120, 90, patchy::PixelFormat::gray8());
  initial_selection.clear(0);
  for (int y = 28; y <= 62; ++y) {
    auto row = initial_selection.row(y);
    std::fill(row.begin() + 44, row.begin() + 78, std::uint8_t{255});
  }
  canvas->replace_selection_from_grayscale(initial_selection,
                                           QStringLiteral("Quick Mask initial selection"));
  const auto& active_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto layer_id = active_document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* layer = active_document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  const auto layer_pixels_before = layer->pixels();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_brush_size(10);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);

  const auto paint_at = [&](QPoint point, QColor color) {
    canvas->set_primary_color(color);
    const auto widget_point = canvas->widget_position_for_document_point(point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton,
               Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point,
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  const QPoint white_outside(16, 18);
  paint_at(white_outside, Qt::white);
  CHECK(*canvas->quick_mask_pixels().pixel(white_outside.x(), white_outside.y()) ==
        255);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  const QPoint black_inside(58, 44);
  paint_at(black_inside, Qt::black);
  CHECK(*canvas->quick_mask_pixels().pixel(black_inside.x(), black_inside.y()) ==
        0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 2U);

  const QPoint gray_outside(102, 70);
  paint_at(gray_outside, QColor(128, 128, 128));
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        128);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 3U);

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->quick_mask_active());
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        0);
  CHECK(*canvas->quick_mask_pixels().pixel(white_outside.x(), white_outside.y()) ==
        255);
  CHECK(*canvas->quick_mask_pixels().pixel(black_inside.x(), black_inside.y()) ==
        0);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(*canvas->quick_mask_pixels().pixel(gray_outside.x(), gray_outside.y()) ==
        128);
  const auto undo_after_gestures =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->quick_mask_active());
  CHECK(canvas->selection_alpha_at(white_outside) == 255);
  CHECK(canvas->selection_alpha_at(black_inside) == 0);
  CHECK(canvas->selection_alpha_at(gray_outside) == 128);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_after_gestures);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(gray_outside) == 0);
  CHECK(canvas->selection_alpha_at(white_outside) == 255);
  CHECK(canvas->selection_alpha_at(black_inside) == 0);
  require_hotkey_action(window, QStringLiteral("edit.redo"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->selection_alpha_at(gray_outside) == 128);

  const auto& after_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* after_layer = after_document.find_layer(*layer_id);
  CHECK(after_layer != nullptr);
  CHECK(patchy::ui::pixel_buffers_equal(after_layer->pixels(), layer_pixels_before));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only() {
  patchy::Document document(84, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer(
      "Pixels", solid_pixels(84, 64, patchy::PixelFormat::rgba8(), QColor(70, 135, 210)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Quick Mask Commands"));
  show_window(window);
  auto* canvas = require_canvas(window);
  const auto& initial_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto layer_id = initial_document.active_layer_id();
  CHECK(layer_id.has_value());
  const auto* initial_layer = initial_document.find_layer(*layer_id);
  CHECK(initial_layer != nullptr);
  const auto layer_pixels_before = initial_layer->pixels();
  const auto undo_before =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_hotkey_action(window, QStringLiteral("select.quick_mask"))->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(128, 128, 128));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 128; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 1U);

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 127; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 2U);

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 0; }));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before + 3U);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(std::all_of(canvas->quick_mask_pixels().data().begin(),
                    canvas->quick_mask_pixels().data().end(),
                    [](std::uint8_t value) { return value == 127; }));

  CHECK(!require_hotkey_action(window, QStringLiteral("tools.clone"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("tools.smudge"))->isEnabled());
  CHECK(!require_hotkey_action(window, QStringLiteral("tools.type"))->isEnabled());
  auto* filter = require_action(window, "filterAction_patchy_filters_edge_detect");
  CHECK(!filter->isEnabled());
  const auto mask_before_blocked_tools = canvas->quick_mask_pixels();
  const auto undo_before_blocked_tools =
      patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto click_with_tool = [&](patchy::ui::CanvasTool tool, QPoint point) {
    canvas->set_tool(tool);
    const auto widget_point = canvas->widget_position_for_document_point(point);
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton,
               Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point,
               Qt::LeftButton, Qt::NoButton);
  };
  click_with_tool(patchy::ui::CanvasTool::Clone, QPoint(18, 18));
  click_with_tool(patchy::ui::CanvasTool::Smudge, QPoint(34, 28));
  click_with_tool(patchy::ui::CanvasTool::Text, QPoint(52, 42));
  filter->trigger();
  QApplication::processEvents();
  CHECK(patchy::ui::pixel_buffers_equal(canvas->quick_mask_pixels(),
                                        mask_before_blocked_tools));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) ==
        undo_before_blocked_tools);

  const auto& final_document =
      std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* final_layer = final_document.find_layer(*layer_id);
  CHECK(final_layer != nullptr);
  CHECK(patchy::ui::pixel_buffers_equal(final_layer->pixels(), layer_pixels_before));
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
}

void ui_quick_select_stroke_selects_object_and_is_undoable() {
  patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(80, 50, 120, 90), QColor(200, 30, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(24);
  canvas->set_selection_feather_radius(0);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(105, 95)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 95)), Qt::NoButton, Qt::LeftButton);
  // No live classification during the drag: the selection must only appear on release (this
  // also pins the US-8050498 patent-avoidance behaviour, see finish_quick_select_stroke).
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(175, 95)), Qt::NoButton, Qt::LeftButton);
  CHECK(!canvas->has_selection());
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(175, 95)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(90, 60)));
  CHECK(region.contains(QPoint(190, 130)));
  CHECK(!region.contains(QPoint(40, 30)));
  CHECK(!region.contains(QPoint(260, 180)));
  // A stroke in New mode leaves the tool in Add (Photoshop behaviour).
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Add);
  save_widget_artifact("ui_quick_select_selection", *canvas);

  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_magnetic_lasso_traces_edge_and_commits_selection() {
  patchy::Document document(320, 280, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(60, 60, 160, 160), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magnetic Lasso"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);
  canvas->set_magnetic_lasso_frequency(57);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Click near the left edge of the dark square, then trace all four sides with
  // the button up. The wire must snap onto the edge and drop anchors on its own.
  click(QPoint(56, 70));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(56, 110));
  hover(QPoint(56, 150));
  hover(QPoint(56, 190));
  hover(QPoint(57, 212));
  CHECK(canvas->magnetic_lasso_anchor_count() >= 2);
  // The trace is only a path preview: no selection region may exist mid-trace.
  CHECK(!canvas->has_selection());
  save_widget_artifact("ui_magnetic_lasso_trace", *canvas);

  click(QPoint(70, 223));  // manual anchor around the bottom-left corner
  hover(QPoint(120, 223));
  hover(QPoint(170, 223));
  hover(QPoint(214, 223));
  hover(QPoint(224, 190));
  hover(QPoint(224, 140));
  hover(QPoint(224, 90));
  hover(QPoint(190, 56));
  hover(QPoint(120, 56));
  hover(QPoint(80, 56));
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseButtonDblClick, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(64, 60)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(140, 140)));
  CHECK(region.contains(QPoint(70, 140)));   // hugging the left edge (boundary ~x 59-61)
  CHECK(!region.contains(QPoint(50, 140)));
  CHECK(!region.contains(QPoint(140, 30)));
  CHECK(!region.contains(QPoint(240, 140)));
  CHECK(!region.contains(QPoint(140, 250)));
  // Unlike Quick Select there is no auto-switch to Add: the latched mode stays.
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Replace);
  save_widget_artifact("ui_magnetic_lasso_selection", *canvas);

  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_magnetic_lasso_backspace_escape_and_enter() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_frequency(57);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // On the flat default document the wire is a straight line; long hovers still
  // cool into auto anchors, and Backspace pops them one at a time.
  click(QPoint(100, 100));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(200, 100));
  hover(QPoint(260, 100));
  const auto anchors = canvas->magnetic_lasso_anchor_count();
  CHECK(anchors >= 3);
  send_key(*canvas, Qt::Key_Backspace);
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 1);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(!canvas->has_selection());

  // Enter closes the current path with a straight segment back to the start.
  click(QPoint(100, 100));
  hover(QPoint(200, 100));
  click(QPoint(200, 100));
  hover(QPoint(150, 180));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  CHECK(canvas->selected_document_region().contains(QPoint(150, 120)));
}

void ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear() {
  // Backspace binds layer.clear on macOS (and Delete everywhere): a live trace must keep
  // both keys for anchor-popping. QTest::keyClick dispatches through the platform path so
  // QShortcutMap participates -- what is under test is CanvasWidget::event() accepting
  // the ShortcutOverride while a trace is live (send_key would bypass the shortcut map).
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_frequency(57);
  canvas->setFocus();
  QApplication::processEvents();

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  click(QPoint(100, 100));
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(200, 100));
  hover(QPoint(260, 100));
  click(QPoint(260, 100));
  const auto anchors = canvas->magnetic_lasso_anchor_count();
  CHECK(anchors >= 3);

  QTest::keyClick(canvas, Qt::Key_Delete);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_active());
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 1);

  QTest::keyClick(canvas, Qt::Key_Backspace);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_active());
  CHECK(canvas->magnetic_lasso_anchor_count() == anchors - 2);

  // layer.clear must NOT have fired: the background layer is still opaque.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(!document.layers().empty());
  const auto* pixel = document.layers().front().pixels().pixel(5, 5);
  CHECK(pixel != nullptr);
  CHECK(pixel[3] == 255);

  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
}

void ui_magnetic_lasso_anchor_density_follows_zoom() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_magnetic_lasso_frequency(57);
  canvas->set_zoom(4.0);
  QApplication::processEvents();

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  // Anchor spacing is SCREEN pixels: at 400% zoom a short 40-document-px trace
  // covers 160 screen px and must cool into several fastening points (the
  // pre-fix document-space spacing dropped none, which is why the boxes never
  // appeared at high zoom).
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(100, 100)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(100, 100)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(120, 100)), Qt::NoButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(140, 100)), Qt::NoButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_anchor_count() >= 4);
  send_key(*canvas, Qt::Key_Escape);
  CHECK(!canvas->magnetic_lasso_active());
}

void ui_magnetic_lasso_enter_closes_along_edges() {
  patchy::Document document(320, 280, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(60, 60, 160, 160), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magnetic Close"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Trace left, bottom, and right edges of the dark square, then press Enter
  // from near the top-right corner. The closing segment must snap along the
  // TOP edge (Photoshop's magnetic close), not cut a straight chord through
  // the square: the chord from (220, 66) to (59, 70) would exclude (140, 62).
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(56, 70)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(56, 70)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(56, 120));
  hover(QPoint(56, 180));
  hover(QPoint(57, 214));
  hover(QPoint(80, 224));
  hover(QPoint(140, 224));
  hover(QPoint(200, 224));
  hover(QPoint(224, 200));
  hover(QPoint(224, 140));
  hover(QPoint(224, 90));
  hover(QPoint(222, 66));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(140, 140)));
  CHECK(region.contains(QPoint(140, 62)));   // inside only when the close hugged the top edge
  CHECK(!region.contains(QPoint(140, 40)));
  CHECK(!region.contains(QPoint(50, 140)));
  CHECK(!region.contains(QPoint(240, 140)));
  save_widget_artifact("ui_magnetic_lasso_magnetic_close", *canvas);
}

void ui_magnetic_lasso_line_trace_double_click_closes_straight() {
  // A dark base slab with a bump on top. Tracing the top profile (up and over
  // the bump) and double-clicking must connect finish to start with a straight
  // segment, enclosing the bump. The naive magnetic close retraces the traced
  // edge backwards and winding-fill collapses the polygon to two tiny slivers
  // (the July 2026 report).
  patchy::Document document(320, 240, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(320, 240, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(40, 120, 240, 100), QColor(20, 20, 20, 255));
  fill_pixel_rect(pixels, QRect(120, 70, 80, 50), QColor(20, 20, 20, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Line Trace"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_magnetic_lasso_width(10);
  canvas->set_magnetic_lasso_edge_contrast(10);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(50, 116)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(50, 116)), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->magnetic_lasso_active());
  hover(QPoint(90, 116));
  hover(QPoint(116, 100));
  hover(QPoint(124, 72));
  hover(QPoint(170, 66));
  hover(QPoint(196, 76));
  hover(QPoint(204, 110));
  hover(QPoint(240, 116));
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::NoButton);
  send_mouse(*canvas, QEvent::MouseButtonDblClick, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(240, 118)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(160, 95)));    // inside the bump: enclosed by the straight close
  CHECK(!region.contains(QPoint(160, 170)));  // base body below the close stays out
  CHECK(!region.contains(QPoint(60, 100)));   // above the slab, left of the bump
  CHECK(!region.contains(QPoint(260, 100)));  // above the slab, right of the bump
  save_widget_artifact("ui_magnetic_lasso_line_trace_close", *canvas);
}

void ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels() {
  // Anti-alias (feather 0) must commit the traced boundary as a coverage mask,
  // not a hard QRegion: clearing under the selection then leaves partially
  // erased pixels along diagonal edges like Photoshop (the July 2026 hard
  // staircase-delete report).
  patchy::Document document(200, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White background",
                           solid_pixels(200, 160, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  document.add_pixel_layer("Ink", solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 255)));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("AA Clear"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);
  canvas->set_selection_antialias(true);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  // Flat black layer: the wire draws straight lines, giving a triangle whose
  // top edge runs at slope 1:4 from (30, 30) to (150, 60).
  click(QPoint(30, 30));
  hover(QPoint(150, 60));
  click(QPoint(150, 60));
  hover(QPoint(30, 90));
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  // The committed selection itself must carry partial coverage on the diagonal.
  CHECK(canvas->selection_has_partial_alpha());
  bool mask_has_partial = false;
  for (int x = 86; x <= 94 && !mask_has_partial; ++x) {
    for (int y = 40; y <= 50 && !mask_has_partial; ++y) {
      const auto alpha = canvas->selection_alpha_at(QPoint(x, y));
      mask_has_partial = alpha > 16 && alpha < 240;
    }
  }
  CHECK(mask_has_partial);

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(90, 70)), Qt::white, 12));  // cleared interior
  CHECK(color_close(canvas_pixel_center(*canvas, QPoint(90, 15)), Qt::black, 12));  // untouched outside
  // The diagonal boundary near (90, 45) must hold at least one partially
  // erased pixel (mid-gray composite over the white background).
  bool found_partial = false;
  for (int x = 86; x <= 94 && !found_partial; ++x) {
    for (int y = 40; y <= 50 && !found_partial; ++y) {
      const auto color = canvas_pixel_center(*canvas, QPoint(x, y));
      const auto value = (color.red() + color.green() + color.blue()) / 3;
      found_partial = value > 40 && value < 215;
    }
  }
  CHECK(found_partial);
  save_widget_artifact("ui_magnetic_lasso_aa_clear", *canvas);
}

void ui_magnetic_lasso_click_near_start_closes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::MagneticLasso);
  canvas->set_selection_feather_radius(0);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  const auto click = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(document_point), Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(document_point), Qt::LeftButton, Qt::NoButton);
  };
  const auto hover = [canvas, widget_point](QPoint document_point) {
    send_mouse(*canvas, QEvent::MouseMove, widget_point(document_point), Qt::NoButton, Qt::NoButton);
  };

  click(QPoint(100, 100));
  hover(QPoint(200, 100));
  click(QPoint(200, 100));
  hover(QPoint(150, 180));
  click(QPoint(150, 180));
  hover(QPoint(104, 103));
  click(QPoint(100, 100));  // lands on the start anchor: closes and commits
  QApplication::processEvents();
  CHECK(!canvas->magnetic_lasso_active());
  CHECK(canvas->has_selection());
  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(150, 120)));
  CHECK(!region.contains(QPoint(115, 170)));
}

void ui_magnetic_lasso_options_apply_to_new_documents() {
  SettingsValueRestorer saved_width(QStringLiteral("tools/magneticLassoWidth"));
  SettingsValueRestorer saved_contrast(QStringLiteral("tools/magneticLassoEdgeContrast"));
  SettingsValueRestorer saved_frequency(QStringLiteral("tools/magneticLassoFrequency"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Magnetic Lasso"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagneticLasso);

  auto* width_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoWidthSpin"));
  auto* contrast_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoContrastSpin"));
  auto* frequency_spin = window.findChild<QSpinBox*>(QStringLiteral("magneticLassoFrequencySpin"));
  CHECK(width_spin != nullptr);
  CHECK(contrast_spin != nullptr);
  CHECK(frequency_spin != nullptr);
  CHECK(width_spin->isVisible());
  width_spin->setValue(25);
  contrast_spin->setValue(30);
  frequency_spin->setValue(80);
  QApplication::processEvents();
  CHECK(canvas->magnetic_lasso_width() == 25);
  CHECK(canvas->magnetic_lasso_edge_contrast() == 30);
  CHECK(canvas->magnetic_lasso_frequency() == 80);

  accept_new_document_dialog(320, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* new_canvas = require_canvas(window);
  CHECK(new_canvas != canvas);
  CHECK(new_canvas->tool() == patchy::ui::CanvasTool::MagneticLasso);
  CHECK(new_canvas->magnetic_lasso_width() == 25);
  CHECK(new_canvas->magnetic_lasso_edge_contrast() == 30);
  CHECK(new_canvas->magnetic_lasso_frequency() == 80);
  CHECK(width_spin->value() == 25);
  CHECK(contrast_spin->value() == 30);
  CHECK(frequency_spin->value() == 80);
}

void ui_quick_select_add_and_subtract_strokes() {
  patchy::Document document(360, 200, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(360, 200, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  fill_pixel_rect(pixels, QRect(40, 50, 90, 70), QColor(30, 90, 200, 255));
  fill_pixel_rect(pixels, QRect(210, 50, 90, 70), QColor(30, 90, 200, 255));
  document.add_pixel_layer("Art", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select Modes"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(20);
  canvas->set_selection_feather_radius(0);

  const auto stroke = [canvas](QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
    const auto from_widget = canvas->widget_position_for_document_point(from);
    const auto to_widget = canvas->widget_position_for_document_point(to);
    send_mouse(*canvas, QEvent::MouseButtonPress, from_widget, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseMove, (from_widget + to_widget) / 2, Qt::NoButton, Qt::LeftButton,
               modifiers);
    send_mouse(*canvas, QEvent::MouseMove, to_widget, Qt::NoButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, to_widget, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };

  // New mode stroke grabs blob A and flips the tool to Add.
  stroke(QPoint(70, 85), QPoint(100, 85), Qt::NoModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(250, 85)));
  CHECK(canvas->selection_mode() == patchy::ui::CanvasWidget::SelectionMode::Add);
  auto* add_mode_action = window.findChild<QAction*>(QStringLiteral("selectionAddModeAction"));
  CHECK(add_mode_action != nullptr);
  CHECK(add_mode_action->isChecked());

  // Add stroke joins blob B.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::NoModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(canvas->selected_document_region().contains(QPoint(290, 110)));

  // Alt = subtract: blob B leaves, blob A stays.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::AltModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(250, 85)));

  // Quick Select has no Intersect: Shift+Alt clamps to Add.
  stroke(QPoint(240, 85), QPoint(270, 85), Qt::ShiftModifier | Qt::AltModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(50, 60)));
  CHECK(canvas->selected_document_region().contains(QPoint(250, 85)));
  save_widget_artifact("ui_quick_select_modes", *canvas);
}

// Photo-like content (noisy skin-tone gradient + soft dark ellipse): the stroke must select
// the "eye" without flooding the surrounding "skin". Regression artifact for the July 2026
// over-selection report; eyeball ui_quick_select_photo_texture.png after model/lambda changes.
void ui_quick_select_photo_texture_selects_eye_not_face() {
  const std::int32_t width = 320;
  const std::int32_t height = 240;
  auto pixels = solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  std::uint32_t noise_state = 0xFACE0FF5u;
  const auto next_noise = [&noise_state] {
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return noise_state;
  };
  const double center_x = 160.0;
  const double center_y = 120.0;
  const double radius_x = 55.0;
  const double radius_y = 34.0;
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const int gradient = x * 30 / width;
      const int jitter = static_cast<int>(next_noise() % 25) - 12;
      const int skin_r = 215 + gradient / 2 + jitter;
      const int skin_g = 170 + gradient / 3 + jitter;
      const int skin_b = 140 + gradient / 3 + jitter;
      const int dark_r = 70 + jitter / 2;
      const int dark_g = 45 + jitter / 2;
      const int dark_b = 35 + jitter / 2;
      const double dx = (x - center_x) / radius_x;
      const double dy = (y - center_y) / radius_y;
      const double edge = std::clamp((std::sqrt(dx * dx + dy * dy) - 1.0) * (radius_x / 4.0), 0.0, 1.0);
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_r + (skin_r - dark_r) * edge)), 0, 255));
      px[1] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_g + (skin_g - dark_g) * edge)), 0, 255));
      px[2] = static_cast<std::uint8_t>(std::clamp(
          static_cast<int>(std::lround(dark_b + (skin_b - dark_b) * edge)), 0, 255));
      px[3] = 255;
    }
  }
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Photo", std::move(pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Quick Select Photo"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::QuickSelect);
  canvas->set_quick_select_size(20);
  canvas->set_selection_feather_radius(0);

  const auto widget_point = [canvas](QPoint document_point) {
    return canvas->widget_position_for_document_point(document_point);
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, widget_point(QPoint(140, 118)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(165, 120)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, widget_point(QPoint(185, 120)), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, widget_point(QPoint(185, 120)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto region = canvas->selected_document_region();
  CHECK(region.contains(QPoint(160, 120)));   // eye interior selected
  CHECK(!region.contains(QPoint(40, 40)));    // skin corners stay unselected
  CHECK(!region.contains(QPoint(280, 200)));
  CHECK(!region.contains(QPoint(160, 30)));   // skin above the eye stays unselected
  CHECK(!region.contains(QPoint(160, 210)));  // and below
  save_widget_artifact("ui_quick_select_photo_texture", *canvas);
}

void ui_quick_select_options_persist_across_windows() {
  SettingsValueRestorer size_restorer(QStringLiteral("tools/quickSelectSize"));
  SettingsValueRestorer sample_restorer(QStringLiteral("tools/quickSelectSampleAllLayers"));
  SettingsValueRestorer enhance_restorer(QStringLiteral("tools/quickSelectEnhanceEdge"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    require_canvas(window);
    auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("quickSelectSizeSpin"));
    CHECK(size_spin != nullptr);
    size_spin->setValue(77);
    auto* sample_check = window.findChild<QCheckBox*>(QStringLiteral("quickSelectSampleAllLayersCheck"));
    CHECK(sample_check != nullptr);
    sample_check->setChecked(true);
    auto* enhance_check = window.findChild<QCheckBox*>(QStringLiteral("quickSelectEnhanceEdgeCheck"));
    CHECK(enhance_check != nullptr);
    enhance_check->setChecked(true);  // checkbox toggles run an immediate save_tool_settings()
    QApplication::processEvents();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  CHECK(canvas->quick_select_size() == 77);
  CHECK(canvas->quick_select_sample_all_layers());
  CHECK(canvas->quick_select_enhance_edge());
}

void ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("White background",
                           solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(Qt::white)));
  auto active_pixels = solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(active_pixels, QRect(48, 36, 36, 28), QColor(35, 95, 220, 255));
  document.add_pixel_layer("Active art", std::move(active_pixels));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Magic Wand Delete Noop"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  CHECK(!undo_action->isEnabled());

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->set_wand_tolerance(0);
  canvas->set_wand_contiguous(true);
  canvas->set_wand_sample_all_layers(true);
  const auto click_point = canvas->widget_position_for_document_point(QPoint(8, 8));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->has_selection());
  CHECK(canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(55, 44)));
  // The wand selection itself is an undoable step.
  CHECK(undo_action->isEnabled());

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();

  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Nothing to clear")));
  // The no-op clear added no history: undoing once reverts the wand selection
  // and leaves the stack empty, and the pixels were never touched.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(!undo_action->isEnabled());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(55, 44)), QColor(35, 95, 220), 8));
}

void ui_magic_wand_complex_selection_is_responsive() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(10);
  for (int y = 80; y <= 240; y += 40) {
    for (int x = 90; x <= 290; x += 40) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      drag(*canvas, point, point + QPoint(1, 1));
    }
  }
  QApplication::processEvents();

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  QElapsedTimer timer;
  timer.start();
  const auto click_point = canvas->widget_position_for_document_point(QPoint(25, 25));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  const auto elapsed_ms = timer.elapsed();
  CHECK(elapsed_ms < 1500);
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(!canvas->selected_document_region().contains(QPoint(90, 80)));
  save_widget_artifact("ui_magic_wand_complex_selection", *canvas);
}

void ui_bundled_legacy_plugin_action_applies_filter() {
#ifndef Q_OS_WIN
  // The bundled shim plug-ins are Windows PE binaries; the probe rejects them off-Windows
  // (legacy 8BF support is Windows-only), so no legacyPluginAction exists to trigger.
  std::cout << "[SKIP] bundled legacy 8BF plug-ins are Windows-only\n";
  return;
#endif
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_brush_size(24);
  const QPoint sample_document_point(72, 72);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 72)),
       canvas->widget_position_for_document_point(QPoint(95, 72)));
  QApplication::processEvents();

  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  const auto before = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(before.red() > 180);
  CHECK(before.green() < 100);
  CHECK(before.blue() < 100);

  QAction* greyscale = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(QStringLiteral("Greyscale"), Qt::CaseInsensitive)) {
      greyscale = action;
      break;
    }
  }
  CHECK(greyscale != nullptr);
  greyscale->trigger();
  QApplication::processEvents();

  const auto after = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(std::abs(after.red() - after.green()) <= 8);
  CHECK(std::abs(after.green() - after.blue()) <= 8);
  CHECK(after.red() > 45);
  CHECK(after.red() < 120);
  save_widget_artifact("ui_legacy_plugin_greyscale", window);
}

}  // namespace

std::vector<patchy::test::TestCase> selection_engines_tests() {
  return {
      {"ui_gradient_and_magic_wand_render_visually", ui_gradient_and_magic_wand_render_visually},
      {"ui_radial_gradient_tool_renders_custom_transparency",
       ui_radial_gradient_tool_renders_custom_transparency},
      {"ui_magic_wand_contiguous_and_sample_all_layers_options_work",
       ui_magic_wand_contiguous_and_sample_all_layers_options_work},
      {"ui_quick_mask_feathered_round_trip_is_exact_and_temporary",
       ui_quick_mask_feathered_round_trip_is_exact_and_temporary},
      {"ui_quick_mask_overlay_channels_and_tab_state_are_temporary",
       ui_quick_mask_overlay_channels_and_tab_state_are_temporary},
      {"ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history",
       ui_quick_mask_brush_gestures_edit_outside_selection_with_selection_history},
      {"ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only",
       ui_quick_mask_fill_clear_invert_and_blocked_tools_are_selection_only},
      {"ui_quick_select_stroke_selects_object_and_is_undoable",
       ui_quick_select_stroke_selects_object_and_is_undoable},
      {"ui_quick_select_add_and_subtract_strokes", ui_quick_select_add_and_subtract_strokes},
      {"ui_magnetic_lasso_traces_edge_and_commits_selection",
       ui_magnetic_lasso_traces_edge_and_commits_selection},
      {"ui_magnetic_lasso_backspace_escape_and_enter", ui_magnetic_lasso_backspace_escape_and_enter},
      {"ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear",
       ui_magnetic_lasso_delete_and_backspace_pop_anchors_not_layer_clear},
      {"ui_magnetic_lasso_anchor_density_follows_zoom", ui_magnetic_lasso_anchor_density_follows_zoom},
      {"ui_magnetic_lasso_enter_closes_along_edges", ui_magnetic_lasso_enter_closes_along_edges},
      {"ui_magnetic_lasso_line_trace_double_click_closes_straight",
       ui_magnetic_lasso_line_trace_double_click_closes_straight},
      {"ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels",
       ui_magnetic_lasso_antialias_clear_leaves_partial_edge_pixels},
      {"ui_magnetic_lasso_click_near_start_closes", ui_magnetic_lasso_click_near_start_closes},
      {"ui_magnetic_lasso_options_apply_to_new_documents", ui_magnetic_lasso_options_apply_to_new_documents},
      {"ui_quick_select_photo_texture_selects_eye_not_face", ui_quick_select_photo_texture_selects_eye_not_face},
      {"ui_quick_select_options_persist_across_windows", ui_quick_select_options_persist_across_windows},
      {"ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop",
       ui_magic_wand_sample_all_layers_clear_transparent_active_layer_is_noop},
      {"ui_magic_wand_complex_selection_is_responsive", ui_magic_wand_complex_selection_is_responsive},
      {"ui_bundled_legacy_plugin_action_applies_filter", ui_bundled_legacy_plugin_action_applies_filter},
  };
}
