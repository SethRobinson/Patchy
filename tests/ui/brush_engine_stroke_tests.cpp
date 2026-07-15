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

void ui_cut_selection_clears_source_and_keeps_clipboard() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  const QColor paint_color(255, 80, 20);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(paint_color);
  canvas->set_brush_size(22);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(92, 92)),
       canvas->widget_position_for_document_point(QPoint(130, 110)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  const auto cut_selection_rect = canvas->selected_document_rect();
  CHECK(cut_selection_rect.has_value());
  const auto layers_before = layer_list->count();
  require_action(window, "editCutAction")->trigger();
  QApplication::processEvents();
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));
  const auto cut_clipboard_image = QApplication::clipboard()->image();
  CHECK(!cut_clipboard_image.isNull());
  QApplication::clipboard()->setImage(cut_clipboard_image);
  QApplication::processEvents();

  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == cut_selection_rect->topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 65));
  save_widget_artifact("ui_cut_selection", window);
}

void ui_brush_on_pasted_layer_expands_layer_bounds() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_brush_size(18);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(70, 60)));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(45, 45)),
       canvas->widget_position_for_document_point(QPoint(85, 85)));
  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  require_action(window, "editDeselectAction")->trigger();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(20, 80, 240));
  canvas->set_brush_size(24);
  canvas->set_brush_opacity(100);
  const QPoint outside_original_paste(260, 190);
  drag(*canvas, canvas->widget_position_for_document_point(outside_original_paste),
       canvas->widget_position_for_document_point(outside_original_paste + QPoint(1, 1)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, outside_original_paste), QColor(20, 80, 240), 45));
  save_widget_artifact("ui_brush_expands_pasted_layer", window);
}

void ui_brush_opacity_caps_per_stroke() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto scrub_stroke = [canvas](QPoint document_point) {
    const auto center = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    for (const auto offset : {QPoint(0, 0), QPoint(18, 0), QPoint(-18, 0), QPoint(18, 0), QPoint(-18, 0)}) {
      send_mouse(*canvas, QEvent::MouseMove, center + offset, Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(32);
  canvas->set_brush_opacity(20);
  canvas->set_brush_build_up(false);
  scrub_stroke(QPoint(175, 120));
  const auto first_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(first_stroke.red() >= 190);
  CHECK(first_stroke.red() <= 220);
  CHECK(std::abs(first_stroke.red() - first_stroke.green()) <= 3);
  CHECK(std::abs(first_stroke.green() - first_stroke.blue()) <= 3);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 120)),
       canvas->widget_position_for_document_point(QPoint(280, 120)));
  QApplication::processEvents();
  const auto second_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(second_stroke.red() < first_stroke.red() - 20);
  CHECK(second_stroke.red() >= 145);
  CHECK(second_stroke.red() <= 180);

  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(175, 180)),
       canvas->widget_position_for_document_point(QPoint(176, 180)));
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(175, 180)).red() < 20);

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(32);
  canvas->set_brush_softness(20);
  canvas->set_brush_opacity(20);
  canvas->set_brush_build_up(false);
  scrub_stroke(QPoint(175, 180));
  const auto first_erase = canvas_pixel(*canvas, QPoint(175, 180));
  CHECK(first_erase.red() >= 40);
  CHECK(first_erase.red() <= 70);

  scrub_stroke(QPoint(175, 180));
  const auto second_erase = canvas_pixel(*canvas, QPoint(175, 180));
  CHECK(second_erase.red() > first_erase.red() + 20);
  CHECK(second_erase.red() >= 80);
  CHECK(second_erase.red() <= 105);
  save_widget_artifact("ui_brush_opacity_per_stroke", window);
}

void ui_low_opacity_large_brush_whole_canvas_is_exact_fill_region() {
  patchy::Document document(48, 32, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(48, 32, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Uniform Brush"));
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(48);
  canvas->set_brush_opacity(20);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto send_document_mouse = [canvas](QEvent::Type type, QPoint document_point,
                                            Qt::MouseButton button, Qt::MouseButtons buttons) {
    send_mouse(*canvas, type, canvas->widget_position_for_document_point(document_point), button, buttons);
  };

  send_document_mouse(QEvent::MouseButtonPress, QPoint(0, 0), Qt::LeftButton, Qt::LeftButton);
  for (int y = 0; y < 32; y += 12) {
    send_document_mouse(QEvent::MouseMove, QPoint(47, y), Qt::NoButton, Qt::LeftButton);
    send_document_mouse(QEvent::MouseMove, QPoint(47, std::min(y + 12, 31)), Qt::NoButton, Qt::LeftButton);
    send_document_mouse(QEvent::MouseMove, QPoint(0, std::min(y + 12, 31)), Qt::NoButton, Qt::LeftButton);
    if (y + 24 < 32) {
      send_document_mouse(QEvent::MouseMove, QPoint(0, y + 24), Qt::NoButton, Qt::LeftButton);
    }
  }
  send_document_mouse(QEvent::MouseButtonRelease, QPoint(0, 31), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto& painted_layer = patchy::ui::MainWindowTestAccess::document(window).layers().front();
  const auto& painted_pixels = painted_layer.pixels();
  CHECK(painted_pixels.format() == patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < painted_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < painted_pixels.width(); ++x) {
      const auto* pixel = painted_pixels.pixel(x, y);
      CHECK(pixel[0] == 0);
      CHECK(pixel[1] == 0);
      CHECK(pixel[2] == 0);
      CHECK(pixel[3] == 51);
    }
  }

  use_solid_fill_settings(canvas);
  require_action_by_text(window, QStringLiteral("Fill"))->trigger();
  canvas->set_primary_color(QColor(220, 40, 90));
  canvas->set_brush_opacity(100);
  send_document_mouse(QEvent::MouseButtonPress, QPoint(24, 16), Qt::LeftButton, Qt::LeftButton);
  send_document_mouse(QEvent::MouseButtonRelease, QPoint(24, 16), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto& filled_pixels = painted_layer.pixels();
  for (std::int32_t y = 0; y < filled_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < filled_pixels.width(); ++x) {
      const auto* pixel = filled_pixels.pixel(x, y);
      CHECK(pixel[0] == 220);
      CHECK(pixel[1] == 40);
      CHECK(pixel[2] == 90);
      CHECK(pixel[3] == 255);
    }
  }
}

void ui_soft_brush_click_paints_single_dab() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(64);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(100);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const QPoint center_doc(150, 120);
  const auto center = canvas->widget_position_for_document_point(center_doc);
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas_pixel(*canvas, center_doc).red() < 12);
  const auto feather = canvas_pixel(*canvas, center_doc + QPoint(20, 0));
  CHECK(feather.red() > 160);
  CHECK(feather.red() < 225);

  const QPoint drag_start_doc(246, 120);
  const QPoint drag_end_doc(286, 120);
  drag(*canvas, canvas->widget_position_for_document_point(drag_start_doc),
       canvas->widget_position_for_document_point(drag_end_doc));
  QApplication::processEvents();
  const auto drag_feather = canvas_pixel(*canvas, QPoint(266, 120) + QPoint(20, 0));
  CHECK(drag_feather.red() < feather.red() - 12);
  CHECK(canvas_pixel(*canvas, QPoint(266, 120)).red() < 12);
  save_widget_artifact("ui_soft_brush_single_click", window);
}

void ui_brush_can_start_off_canvas_with_visible_soft_edge() {
  patchy::Document document(96, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(96, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(180, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(64);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const auto off_canvas_center = canvas.widget_position_for_document_point(QPoint(-16, 40));
  send_mouse(canvas, QEvent::MouseButtonPress, off_canvas_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, off_canvas_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto entered_edge = canvas_pixel(canvas, QPoint(0, 40));
  CHECK(entered_edge.red() < 220);
  CHECK(entered_edge.red() > 40);
  CHECK(color_close(canvas_pixel(canvas, QPoint(24, 40)), QColor(Qt::white), 2));
}

void ui_brush_stroke_continues_through_off_canvas_grey_area() {
  patchy::Document document(120, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(120, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(220, 170);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(18);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto widget_point = [&canvas](QPoint document_point) {
    return canvas.widget_position_for_document_point(document_point);
  };
  send_mouse(canvas, QEvent::MouseButtonPress, widget_point(QPoint(-12, 30)), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(50, 30)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(132, 30)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, widget_point(QPoint(80, 60)), Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, widget_point(QPoint(80, 60)), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas_pixel(canvas, QPoint(4, 30)).red() < 20);
  CHECK(canvas_pixel(canvas, QPoint(60, 30)).red() < 20);
  CHECK(canvas_pixel(canvas, QPoint(116, 30)).red() < 20);
  CHECK(canvas_pixel(canvas, QPoint(80, 60)).red() < 20);
}

void ui_large_soft_brush_repaints_after_small_drag() {
  patchy::Document document(1200, 900, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(1200, 900, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(1280, 980);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(patchy::ui::kMaxBrushSize);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const auto start = canvas.widget_position_for_document_point(QPoint(560, 450));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  PaintCounterFilter counter;
  canvas.installEventFilter(&counter);
  QApplication::processEvents();
  counter.paint_events = 0;
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
  CHECK(counter.paint_events > 0);
  canvas.removeEventFilter(&counter);

  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(20, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_large_soft_brush_drag_stays_responsive() {
  patchy::Document document(900, 640, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(900, 640, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(1000, 740);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(patchy::ui::kMaxBrushSize);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const auto start = canvas.widget_position_for_document_point(QPoint(240, 320));
  const auto end = canvas.widget_position_for_document_point(QPoint(660, 320));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  QElapsedTimer timer;
  timer.start();
  constexpr int kSteps = 8;
  for (int step = 1; step <= kSteps; ++step) {
    const auto x = start.x() + ((end.x() - start.x()) * step) / kSteps;
    send_mouse(canvas, QEvent::MouseMove, QPoint(x, start.y()), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto elapsed_ms = timer.elapsed();

  std::cout << "[brush_perf] ui_large_soft_brush_drag_stays_responsive elapsed_ms="
            << elapsed_ms << '\n';
  CHECK(elapsed_ms < 5000);
  save_widget_artifact("ui_large_soft_brush_drag_responsive", canvas);
}

void ui_soft_brush_shift_click_does_not_overpaint_anchor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(64);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(100);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto click = [canvas](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    const auto position = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(*canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::processEvents();
  };

  const QPoint start_doc(90, 160);
  const QPoint end_doc(280, 80);
  click(start_doc, Qt::NoModifier);
  click(end_doc, Qt::ShiftModifier);

  save_widget_artifact("ui_soft_brush_shift_click_anchor", window);
  CHECK(canvas_pixel(*canvas, QPoint(90, 160)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(185, 120)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(280, 80)).red() < 12);

  const auto before_anchor = canvas_pixel(*canvas, QPoint(93, 180));
  const auto after_anchor = canvas_pixel(*canvas, QPoint(102, 177));
  const auto body_feather = canvas_pixel(*canvas, QPoint(193, 138));
  CHECK(before_anchor.red() > 20);
  CHECK(before_anchor.red() < 220);
  CHECK(after_anchor.red() > 20);
  CHECK(after_anchor.red() < 220);
  CHECK(body_feather.red() > 20);
  CHECK(body_feather.red() < 220);
  CHECK(std::abs(before_anchor.red() - after_anchor.red()) <= 48);
  CHECK(std::abs(after_anchor.red() - body_feather.red()) <= 36);
}

void ui_soft_brush_line_ignores_mouse_event_density() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(64);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(100);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto drag_line = [canvas](QPoint start, QPoint end, int move_count) {
    const auto start_position = canvas->widget_position_for_document_point(start);
    send_mouse(*canvas, QEvent::MouseButtonPress, start_position, Qt::LeftButton, Qt::LeftButton);
    for (int index = 1; index <= move_count; ++index) {
      const auto t = static_cast<double>(index) / static_cast<double>(move_count);
      const QPointF point(static_cast<double>(start.x()) +
                              (static_cast<double>(end.x()) - static_cast<double>(start.x())) * t,
                          static_cast<double>(start.y()) +
                              (static_cast<double>(end.y()) - static_cast<double>(start.y())) * t);
      const QPoint document_point(static_cast<int>(std::lround(point.x())),
                                  static_cast<int>(std::lround(point.y())));
      send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(document_point),
                 Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(end),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  drag_line(QPoint(70, 128), QPoint(300, 128), 3);
  drag_line(QPoint(70, 216), QPoint(300, 216), 48);

  save_widget_artifact("ui_soft_brush_event_density", window);
  for (const auto x : {105, 145, 185, 225, 265}) {
    const auto sparse_body = canvas_pixel(*canvas, QPoint(x, 128));
    const auto dense_body = canvas_pixel(*canvas, QPoint(x, 216));
    CHECK(sparse_body.red() < 16);
    CHECK(dense_body.red() < 16);
    CHECK(color_close(sparse_body, dense_body, 8));

    const auto sparse_feather = canvas_pixel(*canvas, QPoint(x, 154));
    const auto dense_feather = canvas_pixel(*canvas, QPoint(x, 242));
    CHECK(sparse_feather.red() > 18);
    CHECK(sparse_feather.red() < 245);
    CHECK(dense_feather.red() > 18);
    CHECK(dense_feather.red() < 245);
    CHECK(color_close(sparse_feather, dense_feather, 16));
  }
}

void ui_layer_mask_brush_opacity_caps_per_stroke() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  auto& red = document.add_pixel_layer("Red Fill",
                                       solid_pixels(64, 64, patchy::PixelFormat::rgb8(), QColor(220, 30, 30)));
  patchy::PixelBuffer mask_pixels(64, 64, patchy::PixelFormat::gray8());
  mask_pixels.clear(0);
  red.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 64, 64}, std::move(mask_pixels), 0, false});

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Mask Brush Opacity"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_layer_edit_target(patchy::ui::CanvasWidget::LayerEditTarget::Mask);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::white);
  canvas->set_brush_size(24);
  canvas->set_brush_opacity(20);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);

  auto scrub_stroke = [canvas](QPoint document_point) {
    const auto center = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    for (const auto offset : {QPoint(0, 0), QPoint(14, 0), QPoint(-14, 0), QPoint(14, 0), QPoint(-14, 0)}) {
      send_mouse(*canvas, QEvent::MouseMove, center + offset, Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  scrub_stroke(QPoint(32, 32));
  const auto first_stroke = canvas_pixel(*canvas, QPoint(32, 32));
  CHECK(first_stroke.red() >= 245);
  CHECK(first_stroke.green() >= 200);
  CHECK(first_stroke.green() <= 222);
  CHECK(std::abs(first_stroke.green() - first_stroke.blue()) <= 4);

  scrub_stroke(QPoint(32, 32));
  const auto second_stroke = canvas_pixel(*canvas, QPoint(32, 32));
  CHECK(second_stroke.green() < first_stroke.green() - 20);
  CHECK(second_stroke.green() >= 165);
  CHECK(second_stroke.green() <= 190);

  canvas->set_brush_opacity(40);
  canvas->set_brush_flow(20);
  canvas->set_brush_build_up(true);
  const auto airbrush_point = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, airbrush_point, Qt::LeftButton, Qt::LeftButton);
  QTest::qWait(430);
  send_mouse(*canvas, QEvent::MouseButtonRelease, airbrush_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto airbrush_mask = canvas_pixel(*canvas, QPoint(16, 16));
  CHECK(airbrush_mask.red() >= 239);
  CHECK(airbrush_mask.red() <= 243);
  CHECK(airbrush_mask.green() >= 160);
  CHECK(airbrush_mask.green() <= 170);
  CHECK(std::abs(airbrush_mask.green() - airbrush_mask.blue()) <= 4);
  save_widget_artifact("ui_layer_mask_brush_opacity_per_stroke", window);
}

void ui_shift_constrains_brush_and_eraser_strokes_to_axis() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 80)),
       canvas->widget_position_for_document_point(QPoint(150, 110)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 80)), Qt::black, 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 99)), Qt::white, 12));

  canvas->set_brush_size(45);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 180)),
       canvas->widget_position_for_document_point(QPoint(170, 180)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 199)), Qt::black, 12));

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 180)),
       canvas->widget_position_for_document_point(QPoint(150, 210)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 180)), Qt::white, 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 199)), Qt::black, 12));
}

void ui_shift_constrains_clone_stamp_strokes_to_axis() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 40, 30));
  canvas->set_brush_size(45);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 90)),
       canvas->widget_position_for_document_point(QPoint(130, 90)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  canvas->set_brush_size(5);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  const auto source = canvas->widget_position_for_document_point(QPoint(70, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 100)),
       canvas->widget_position_for_document_point(QPoint(230, 130)), Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 100)), QColor(230, 40, 30), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 120)), Qt::white, 12));
}

void ui_brush_alt_right_drag_adjusts_size_and_softness() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(20);
  canvas.set_brush_softness(50);
  canvas.show();
  QApplication::processEvents();
  CHECK(canvas.zoom() == 1.0);

  // Alt+Right-drag right grows the size so the brush edge tracks the pointer
  // (2 document pixels of diameter per pixel dragged at 100% zoom); dragging
  // up softens the edge.
  const QPoint origin(180, 160);
  send_mouse(canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(30, 0), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 50);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(30, -50), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 70);
  save_widget_artifact("ui_brush_alt_right_drag_hud", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, origin + QPoint(30, -50), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 70);
  // The pointer snaps back to the gesture anchor so the brush stays centered
  // on the spot that was adjusted instead of jumping by the drag distance.
  CHECK(QCursor::pos() == canvas.mapToGlobal(origin));

  // Escape cancels an in-flight adjustment and restores the committed values.
  send_mouse(canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(20, 30), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas.brush_size() == 120);
  CHECK(canvas.brush_softness() == 58);
  send_key_press(canvas, Qt::Key_Escape);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 70);
  CHECK(QCursor::pos() == canvas.mapToGlobal(origin));
  send_mouse(canvas, QEvent::MouseButtonRelease, origin + QPoint(20, 30), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 70);

  // Dragging softness all the way to 0% must render the HUD as a solid hard
  // disc (a duplicate 1.0 gradient stop used to wipe the fill to a fade).
  send_mouse(canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(0, 200), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas.brush_size() == 80);
  CHECK(canvas.brush_softness() == 0);
  const auto hard_hud = canvas.grab().toImage();
  const auto hard_edge_sample = hard_hud.pixelColor(origin + QPoint(34, 0));
  CHECK(hard_edge_sample.red() > hard_edge_sample.green() + 60);
  save_widget_artifact("ui_brush_alt_right_drag_hud_hard", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, origin + QPoint(0, 200), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  CHECK(canvas.brush_softness() == 0);

  // The gesture never paints.
  const auto& pixels = document.find_layer(layer_id)->pixels();
  for (int x = 30; x <= 60; ++x) {
    const auto* pixel = pixels.pixel(x, 40);
    CHECK(pixel[0] == 255U && pixel[1] == 255U && pixel[2] == 255U);
  }

  // A move without the right button (lost release) commits and ends the drag
  // instead of leaving the overlay stuck.
  send_mouse(canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(10, 0), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  CHECK(canvas.brush_size() == 100);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(60, 0), Qt::NoButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 100);
  send_mouse(canvas, QEvent::MouseMove, origin + QPoint(90, 0), Qt::NoButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 100);
}

void ui_pen_alt_barrel_button_adjusts_brush_size_and_softness() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(20);
  canvas.set_brush_softness(50);
  canvas.show();
  QApplication::processEvents();
  CHECK(canvas.zoom() == 1.0);

  // Alt + a barrel button mapped to right-click matches the Alt+Right mouse
  // gesture. The press is hover-only (pressure 0), which is how a Wacom
  // side-button right-click arrives.
  const auto before_origin = canvas.widget_position_for_document_point(QPoint(0, 0));
  const QPoint origin(180, 160);
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(30, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  // Pen gestures use 4 px of diameter per dragged pixel (the disc is centered
  // on the pen, so the radius must outpace the pen's own travel).
  CHECK(canvas.brush_size() == 140);
  CHECK(canvas.brush_softness() == 50);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(30, -50), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 140);
  CHECK(canvas.brush_softness() == 70);
  send_tablet(canvas, QEvent::TabletRelease, origin + QPoint(30, -50), 0.0, Qt::RightButton, Qt::NoButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 140);
  CHECK(canvas.brush_softness() == 70);
  // The chord must beat the barrel's configured action (pan by default).
  CHECK(canvas.widget_position_for_document_point(QPoint(0, 0)) == before_origin);

  // Escape cancels an in-flight pen adjustment; the barrel release that
  // follows must not leave painting suppressed or re-trigger the pan action.
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(20, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 220);
  send_key_press(canvas, Qt::Key_Escape);
  CHECK(canvas.brush_size() == 140);
  send_tablet(canvas, QEvent::TabletRelease, origin + QPoint(20, 0), 0.0, Qt::RightButton, Qt::NoButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 140);
  CHECK(canvas.brush_softness() == 70);

  // Without Alt the barrel button keeps its configured pen action.
  send_tablet(canvas, QEvent::TabletPress, origin, 1.0, Qt::RightButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(35, 18), 1.0, Qt::NoButton, Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, origin + QPoint(35, 18), 0.0, Qt::RightButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 140);
  CHECK(canvas.widget_position_for_document_point(QPoint(0, 0)) != before_origin);

  // A lost barrel release (the pen left proximity mid-drag, or the driver
  // delivered the release as a plain mouse event) must not leave the overlay
  // stuck: the first hover move without the button commits and ends the drag.
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(10, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 180);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(60, 0), 0.0, Qt::NoButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 180);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(90, 0), 0.0, Qt::NoButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 180);

  // Same when the next event is a quick tip touch instead of a hover move:
  // the stale drag ends and the press paints immediately.
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(10, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 220);
  const auto tip_position = canvas.widget_position_for_document_point(QPoint(20, 20));
  send_tablet(canvas, QEvent::TabletPress, tip_position, 1.0);
  send_tablet(canvas, QEvent::TabletMove, tip_position + QPoint(5, 0), 1.0);
  send_tablet(canvas, QEvent::TabletRelease, tip_position + QPoint(5, 0), 0.0, Qt::LeftButton, Qt::NoButton);
  CHECK(canvas.brush_size() == 220);
  {
    const auto& stroke_pixels = document.find_layer(layer_id)->pixels();
    const auto* touched = stroke_pixels.pixel(20, 20);
    CHECK(touched[0] != 255U || touched[1] != 255U || touched[2] != 255U);
  }

  // The pen tip still paints normally afterwards.
  const auto paint_position = canvas.widget_position_for_document_point(QPoint(48, 32));
  send_tablet(canvas, QEvent::TabletPress, paint_position, 1.0);
  send_tablet(canvas, QEvent::TabletMove, paint_position + QPoint(6, 0), 1.0);
  send_tablet(canvas, QEvent::TabletRelease, paint_position + QPoint(6, 0), 0.0, Qt::LeftButton, Qt::NoButton);
  const auto& pixels = document.find_layer(layer_id)->pixels();
  int painted_pixels = 0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* pixel = pixels.pixel(x, y);
      if (pixel[0] != 255U || pixel[1] != 255U || pixel[2] != 255U) {
        ++painted_pixels;
      }
    }
  }
  CHECK(painted_pixels > 0);

  // Wacom drivers can keep reporting the barrel as held until the pen leaves
  // proximity. A tip touch with that phantom button state must still end the
  // gesture and paint immediately — not get swallowed or turned into a pan.
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(-10, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 180);
  canvas.set_primary_color(QColor(0, 200, 0));
  const auto latched_tip = canvas.widget_position_for_document_point(QPoint(70, 40));
  send_tablet(canvas, QEvent::TabletPress, latched_tip, 1.0, Qt::LeftButton,
              Qt::LeftButton | Qt::RightButton);
  send_tablet(canvas, QEvent::TabletMove, latched_tip + QPoint(4, 0), 1.0, Qt::NoButton,
              Qt::LeftButton | Qt::RightButton);
  send_tablet(canvas, QEvent::TabletRelease, latched_tip + QPoint(4, 0), 0.0, Qt::LeftButton, Qt::RightButton);
  CHECK(canvas.brush_size() == 180);
  {
    const auto& latched_pixels = document.find_layer(layer_id)->pixels();
    const auto* touched = latched_pixels.pixel(70, 40);
    CHECK(touched[1] > 100U);
    CHECK(touched[0] < 100U);
  }
}

void ui_pen_brush_adjust_overlay_centers_on_pen() {
  patchy::Document document(96, 64, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(96, 64, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 300);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_size(20);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();
  CHECK(canvas.zoom() == 1.0);

  // The pen cannot be warped back to an anchor when the gesture ends, so the
  // preview disc is centered on the pen the whole time (the gesture finishes
  // with the brush already under the pen), and the radius outgrows the pen's
  // travel so both edges keep expanding (a pinned trailing edge reads as
  // growing out of the top-left corner).
  const QPoint origin(180, 160);
  send_tablet(canvas, QEvent::TabletPress, origin, 0.0, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_tablet(canvas, QEvent::TabletMove, origin + QPoint(20, 0), 0.0, Qt::NoButton, Qt::RightButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 100);
  const auto hud = canvas.grab().toImage();
  // Size 100 means a 50 px radius around the pen at origin+20. A sample 45 px
  // ahead of the pen is inside only when the disc is centered on the pen...
  const auto ahead_of_pen = hud.pixelColor(origin + QPoint(65, 0));
  CHECK(ahead_of_pen.red() > ahead_of_pen.green() + 60);
  // ...and a sample behind the press point is inside only when the trailing
  // edge expanded too instead of staying pinned.
  const auto behind_anchor = hud.pixelColor(origin - QPoint(25, 0));
  CHECK(behind_anchor.red() > behind_anchor.green() + 60);
  const auto outside = hud.pixelColor(origin - QPoint(60, 0));
  CHECK(outside.red() <= outside.green() + 60);
  save_widget_artifact("ui_pen_brush_adjust_overlay_centers_on_pen", canvas);
  send_tablet(canvas, QEvent::TabletRelease, origin + QPoint(20, 0), 0.0, Qt::RightButton, Qt::NoButton,
              Qt::AltModifier);
  CHECK(canvas.brush_size() == 100);
}

void ui_brush_alt_right_drag_syncs_options_bar_spins() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_brush_size(20);
  canvas->set_brush_softness(50);
  QApplication::processEvents();

  const auto origin = canvas->widget_position_for_document_point(QPoint(60, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, origin, Qt::RightButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseMove, origin + QPoint(25, -25), Qt::NoButton, Qt::RightButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, origin + QPoint(25, -25), Qt::RightButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();

  CHECK(canvas->brush_size() == 70);
  CHECK(canvas->brush_softness() == 60);
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* softness_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  CHECK(size_spin != nullptr);
  CHECK(softness_spin != nullptr);
  CHECK(size_spin->value() == 70);
  CHECK(softness_spin->value() == 60);
}

void ui_brush_shift_click_connects_strokes() {
  patchy::Document document(200, 60, patchy::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer(
      "Paint", solid_pixels(200, 60, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto layer_id = layer.id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(320, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(8);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto click = [&canvas](QPoint document_point, Qt::KeyboardModifiers modifiers) {
    const auto position = canvas.widget_position_for_document_point(document_point);
    send_mouse(canvas, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
    send_mouse(canvas, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
  };
  const auto alpha_at = [&document, layer_id](int x, int y) {
    return document.find_layer(layer_id)->pixels().pixel(x, y)[3];
  };

  // Plain clicks paint isolated dabs.
  click(QPoint(30, 10), Qt::NoModifier);
  click(QPoint(170, 10), Qt::NoModifier);
  CHECK(alpha_at(30, 10) >= 250U);
  CHECK(alpha_at(170, 10) >= 250U);
  CHECK(alpha_at(100, 10) == 0U);

  // Shift+click joins the new dab to the previous stroke end with a line.
  click(QPoint(30, 30), Qt::NoModifier);
  click(QPoint(170, 30), Qt::ShiftModifier);
  CHECK(alpha_at(30, 30) >= 250U);
  CHECK(alpha_at(100, 30) >= 250U);
  CHECK(alpha_at(170, 30) >= 250U);

  // The eraser connects the same way.
  canvas.set_tool(patchy::ui::CanvasTool::Eraser);
  click(QPoint(30, 30), Qt::ShiftModifier);
  CHECK(alpha_at(100, 30) == 0U);
  CHECK(alpha_at(30, 30) == 0U);
  CHECK(alpha_at(170, 30) == 0U);
}

void ui_brush_opacity_and_flow_digit_keys_set_values() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_brush_opacity(100);
  canvas.set_brush_flow(100);
  canvas.set_brush_build_up(false);
  canvas.set_gradient_opacity(80);
  canvas.show();
  QApplication::processEvents();

  // Single digits jump to that opacity decade; a quick second digit refines it
  // (Photoshop-style pairing), and 0 alone means 100%.
  send_key(canvas, Qt::Key_5);
  CHECK(canvas.brush_opacity() == 50);
  send_key(canvas, Qt::Key_2);
  CHECK(canvas.brush_opacity() == 52);
  send_key(canvas, Qt::Key_2);
  CHECK(canvas.brush_opacity() == 20);
  send_key(canvas, Qt::Key_5);
  CHECK(canvas.brush_opacity() == 25);
  send_key(canvas, Qt::Key_0);
  CHECK(canvas.brush_opacity() == 100);
  send_key(canvas, Qt::Key_7);
  CHECK(canvas.brush_opacity() == 7);

  // Shift targets Flow while Airbrush is off. With Airbrush on, Photoshop's
  // shortcut assignment flips: bare digits target Flow and Shift targets
  // Opacity.
  send_key(canvas, Qt::Key_4, Qt::ShiftModifier);
  CHECK(canvas.brush_flow() == 40);
  send_key(canvas, Qt::Key_5, Qt::ShiftModifier);
  CHECK(canvas.brush_flow() == 45);
  canvas.set_brush_build_up(true);
  send_key(canvas, Qt::Key_6, Qt::ShiftModifier);
  CHECK(canvas.brush_opacity() == 60);
  send_key(canvas, Qt::Key_3);
  CHECK(canvas.brush_flow() == 30);

  // The gradient tool routes digits to the gradient opacity instead.
  canvas.set_tool(patchy::ui::CanvasTool::Gradient);
  send_key(canvas, Qt::Key_4);
  CHECK(canvas.gradient_opacity() == 40);
  CHECK(canvas.brush_opacity() == 60);

  // Non-painting tools ignore the digit keys.
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  send_key(canvas, Qt::Key_9);
  CHECK(canvas.brush_opacity() == 60);
  CHECK(canvas.gradient_opacity() == 40);
}

void ui_undo_shortcut_includes_ctrl_alt_z() {
  patchy::ui::MainWindow window;
  show_window(window);

  bool found_undo = false;
  for (auto* action : window.findChildren<QAction*>()) {
    if (action->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Z))) {
      CHECK(action->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z)));
      found_undo = true;
    }
  }
  CHECK(found_undo);
}

void ui_one_pixel_brush_drag_paints_fractional_smoothed_line() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(4.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(1);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  QApplication::processEvents();

  const auto from = canvas->widget_position_for_document_point(QPoint(50, 120)) + QPoint(0, 1);
  const auto to = canvas->widget_position_for_document_point(QPoint(240, 120)) + QPoint(0, 1);
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 12; ++step) {
    const auto x = from.x() + ((to.x() - from.x()) * step) / 12;
    send_mouse(*canvas, QEvent::MouseMove, QPoint(x, from.y()), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  save_widget_artifact("ui_one_pixel_brush_fractional_line", *canvas);

  for (const auto x : {80, 100, 120}) {
    const auto top_left = canvas->widget_position_for_document_point(QPoint(x - 1, 118));
    const auto bottom_right = canvas->widget_position_for_document_point(QPoint(x + 2, 123));
    const auto search_rect = QRect(top_left, bottom_right).normalized();
    CHECK(count_pixels_close(canvas->grab().toImage(), search_rect, Qt::black, 70) > 0);
  }
}

void ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(64, 64, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());

  patchy::ui::CanvasWidget canvas;
  canvas.resize(240, 240);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(32.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const QPoint target(34, 28);
  const auto painted_alpha = [&paint_layer](QPoint point) {
    const auto bounds = paint_layer.bounds();
    CHECK(bounds.contains(point.x(), point.y()));
    return paint_layer.pixels().pixel(point.x() - bounds.x, point.y() - bounds.y)[3];
  };
  const auto target_origin = canvas.widget_position_for_document_point(target);
  send_mouse(canvas, QEvent::MouseButtonPress, target_origin + QPoint(5, 16), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target_origin + QPoint(24, 16), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(target) == 255);
  CHECK(painted_alpha(target + QPoint(1, 0)) == 0);
  CHECK(painted_alpha(target + QPoint(0, 1)) == 0);

  const QPoint neighbor = target + QPoint(1, 0);
  const auto neighbor_center = canvas.widget_position_for_document_point(neighbor) + QPoint(16, 16);
  send_mouse(canvas, QEvent::MouseButtonPress, neighbor_center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, neighbor_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(neighbor) == 255);

  canvas.set_tool(patchy::ui::CanvasTool::Eraser);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  send_mouse(canvas, QEvent::MouseButtonPress, target_origin + QPoint(6, 10), Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target_origin + QPoint(25, 10), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(painted_alpha(target) == 0);
  CHECK(painted_alpha(neighbor) == 255);
}

void ui_max_zoom_brush_skips_noop_stroke_repaints() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(32.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(2);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(0);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto start = canvas->widget_position_for_document_point(QPoint(10, 10));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  PaintCounterFilter counter;
  canvas->installEventFilter(&counter);
  QApplication::processEvents();
  counter.paint_events = 0;
  for (int offset = 1; offset <= 10; ++offset) {
    send_mouse(*canvas, QEvent::MouseMove, start + QPoint(offset, 0), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(counter.paint_events == 0);
  canvas->removeEventFilter(&counter);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(10, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_deep_zoom_brush_repaint_stays_responsive() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), Qt::white));
  auto& paint_layer = document.add_pixel_layer("Paint",
                                               solid_pixels(64, 64, patchy::PixelFormat::rgba8(), Qt::transparent));
  document.set_active_layer(paint_layer.id());
  document.grid_settings().horizontal_cycle_32 = 32;
  document.grid_settings().vertical_cycle_32 = 32;

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(128.0);
  canvas.set_grid_visible(true);
  canvas.set_grid_subdivisions(1);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(4);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto cell_center = [&canvas](QPoint document_point) {
    const auto a = canvas.widget_position_for_document_point(document_point);
    const auto b = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
    return QPoint((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
  };
  const auto start = cell_center(QPoint(1, 1));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();

  PaintCounterFilter counter;
  canvas.installEventFilter(&counter);
  QElapsedTimer timer;
  timer.start();
  constexpr int kSteps = 36;
  for (int step = 1; step <= kSteps; ++step) {
    send_mouse(canvas, QEvent::MouseMove, start + QPoint(step * 8, (step % 3) - 1), Qt::NoButton, Qt::LeftButton);
  }
  const auto elapsed_ms = timer.elapsed();
  canvas.removeEventFilter(&counter);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(kSteps * 8, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(counter.paint_events <= kSteps + 4);
  CHECK(elapsed_ms < 2500);
  CHECK(paint_layer.pixels().pixel(1, 1)[3] == 255);
}

void ui_airbrush_preset_builds_while_stationary() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  patchy::BrushDynamics stale_round_dynamics;
  stale_round_dynamics.scatter = 2.0;
  stale_round_dynamics.count = 3;
  patchy::ui::MainWindowTestAccess::set_round_brush_session(window, stale_round_dynamics,
                                                            42.0, 55.0);
  QImage sampled_tip(8, 8, QImage::Format_Grayscale8);
  sampled_tip.fill(255);
  const auto sampled_tip_id =
      window.brush_tip_library().add_tip(QStringLiteral("Airbrush preset reset probe"),
                                         sampled_tip, 0.25);
  CHECK(!sampled_tip_id.isEmpty());
  window.set_active_brush_tip(sampled_tip_id, false);
  CHECK(canvas->has_brush_tip());

  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  QApplication::processEvents();
  CHECK(!canvas->has_brush_tip());
  CHECK(!canvas->brush_dynamics().active());
  CHECK(std::abs(canvas->brush_base_angle_degrees()) < 1e-9);
  CHECK(canvas->brush_base_roundness() == 100);
  CHECK(canvas->brush_build_up());
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 12);

  const auto center = canvas->widget_position_for_document_point(QPoint(150, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto initial = canvas_pixel(*canvas, QPoint(150, 120));
  CHECK(initial.red() >= 215);
  CHECK(initial.red() <= 232);

  QTest::qWait(260);
  QApplication::processEvents();
  const auto held = canvas_pixel(*canvas, QPoint(150, 120));
  CHECK(held.red() < initial.red() - 40);
  CHECK(held.red() > 70);

  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto released = canvas_pixel(*canvas, QPoint(150, 120));
  QTest::qWait(120);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(150, 120)), released, 1));

  // A stationary timer tick advances the same Transfer/Fade state as a spatial dab. With a
  // one-step Flow fade, the press paints normally and every held tick has zero Flow. The old
  // stateless timer path would ignore the fade and keep darkening this point.
  patchy::BrushDynamics fade;
  fade.flow_control = patchy::BrushDynamicControl::Fade;
  fade.flow_fade_steps = 1;
  canvas->set_brush_dynamics(fade);
  const auto faded_center = canvas->widget_position_for_document_point(QPoint(240, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, faded_center, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto faded_initial = canvas_pixel(*canvas, QPoint(240, 120));
  CHECK(faded_initial.red() >= 215);
  CHECK(faded_initial.red() <= 232);
  QTest::qWait(160);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(240, 120)), faded_initial, 1));
  send_mouse(*canvas, QEvent::MouseButtonRelease, faded_center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(window.brush_tip_library().remove_tip(sampled_tip_id));
  save_widget_artifact("ui_airbrush_stationary_build_up", window);
}

void ui_brush_flow_builds_only_to_opacity_cap_and_round_trips_psd() {
  patchy::Document document(96, 96, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer(
      "Paint", solid_pixels(96, 96, patchy::PixelFormat::rgba8(), Qt::white)).id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 192);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Brush);
  canvas.set_zoom(1.0);
  canvas.set_primary_color(Qt::black);
  canvas.set_brush_size(32);
  canvas.set_brush_opacity(40);
  canvas.set_brush_flow(20);
  canvas.set_brush_softness(0);
  canvas.set_brush_build_up(true);
  canvas.show();
  QApplication::processEvents();

  const auto point = canvas.widget_position_for_document_point(QPoint(48, 48));
  send_mouse(canvas, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
  QTest::qWait(430);
  send_mouse(canvas, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* capped = layer->pixels().pixel(48, 48);
  CHECK(capped[0] >= 151U && capped[0] <= 155U);
  CHECK(capped[1] == capped[0]);
  CHECK(capped[2] == capped[0]);
  CHECK(capped[3] == 255U);

  ensure_artifact_dir();
  const auto path = std::filesystem::path("test-artifacts") / "ui_brush_flow_airbrush.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  const auto reread = patchy::psd::DocumentIo::read_file(path);
  CHECK(reread.layers().size() == 1U);
  const auto* reopened = reread.layers().front().pixels().pixel(48, 48);
  CHECK(std::equal(capped, capped + 4, reopened));
  save_widget_artifact("ui_brush_flow_opacity_cap", canvas);
}

void ui_pattern_stamp_alignment_palette_and_psd_round_trip() {
  patchy::PatternResource pattern;
  pattern.id = "patchy-test-pattern-stamp";
  pattern.name = "Pattern Stamp Test";
  pattern.tile = patchy::PixelBuffer(4, 4, patchy::PixelFormat::rgba8());
  for (int y = 0; y < pattern.tile.height(); ++y) {
    for (int x = 0; x < pattern.tile.width(); ++x) {
      auto* pixel = pattern.tile.pixel(x, y);
      pixel[0] = static_cast<std::uint8_t>(30 + x * 45);
      pixel[1] = static_cast<std::uint8_t>(20 + y * 55);
      pixel[2] = static_cast<std::uint8_t>(10 + x * 8 + y * 6);
      pixel[3] = static_cast<std::uint8_t>(x == 2 && y == 2 ? 128 : 255);
    }
  }

  {
    patchy::ui::MainWindow window;
    show_window(window);
    if (const auto* stale = window.pattern_library().find_entry_by_pattern_id(
            QString::fromStdString(pattern.id));
        stale != nullptr) {
      CHECK(window.pattern_library().remove_pattern(stale->storage_id));
    }
    const auto storage_id = window.pattern_library().add_pattern(
        QString::fromStdString(pattern.name), pattern.tile, QStringLiteral("Tests"),
        QString::fromStdString(pattern.id));
    CHECK(!storage_id.isEmpty());
    require_action_by_text(window, QStringLiteral("Pattern Stamp"))->trigger();
    QApplication::processEvents();
    auto* pattern_combo =
        window.findChild<QComboBox*>(QStringLiteral("patternStampPatternCombo"));
    auto* aligned = window.findChild<QCheckBox*>(QStringLiteral("patternStampAlignedCheck"));
    auto* manage = window.findChild<QPushButton*>(QStringLiteral("patternStampManageButton"));
    auto* size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
    auto* softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
    CHECK(pattern_combo != nullptr);
    const auto pattern_index = pattern_combo->findData(QString::fromStdString(pattern.id));
    CHECK(pattern_index >= 0);
    pattern_combo->setCurrentIndex(pattern_index);
    CHECK(pattern_combo->isVisible());
    CHECK(aligned != nullptr && aligned->isVisible());
    CHECK(manage != nullptr && manage->isVisible());
    CHECK(size != nullptr && size->isVisible());
    CHECK(opacity != nullptr && opacity->isVisible());
    CHECK(softness != nullptr && softness->isVisible());

    const auto exercise_popup = [&window](const QString& base_name,
                                          QSpinBox* spin, int value) {
      auto* action = window.findChild<QAction*>(base_name + QStringLiteral("PopupAction"));
      CHECK(action != nullptr);
      CHECK(action->icon().isNull());
      action->trigger();
      QApplication::processEvents();
      auto* popup = window.findChild<QFrame*>(base_name + QStringLiteral("Popup"));
      auto* slider = window.findChild<QSlider*>(base_name + QStringLiteral("PopupSlider"));
      CHECK(popup != nullptr && popup->isVisible());
      CHECK(slider != nullptr);
      slider->setValue(value);
      CHECK(spin->value() == value);
      popup->close();
      QApplication::processEvents();
    };
    exercise_popup(QStringLiteral("brushSize"), size, 77);
    exercise_popup(QStringLiteral("brushOpacity"), opacity, 66);
    exercise_popup(QStringLiteral("brushSoftness"), softness, 44);
    auto* canvas = require_canvas(window);
    CHECK(canvas->brush_size() == 77);
    CHECK(canvas->brush_opacity() == 66);
    CHECK(canvas->brush_softness() == 44);

    for (const auto* base_name : {
             "selectionFeather", "selectionCornerRadius",
             "healingDiffusion", "localAdjustmentStrength",
             "magneticLassoWidth", "magneticLassoContrast",
             "magneticLassoFrequency", "wandTolerance",
             "shapeCornerRadius", "textSize"}) {
      CHECK(window.findChild<QAction*>(
                QLatin1String(base_name) + QStringLiteral("PopupAction")) != nullptr);
    }
    save_widget_artifact("ui_pattern_stamp_options", window);
    CHECK(window.pattern_library().remove_pattern(storage_id));
  }

  patchy::Document document(64, 40, patchy::PixelFormat::rgba8());
  const auto layer_id = document.add_pixel_layer(
      "Pattern", solid_pixels(64, 40, patchy::PixelFormat::rgba8(), Qt::transparent)).id();
  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::PatternStamp);
  canvas.set_pattern_stamp_pattern(pattern);
  canvas.set_pattern_stamp_aligned(true);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_flow(100);
  canvas.set_brush_softness(0);
  canvas.show();
  QApplication::processEvents();

  const auto click = [&canvas](QPoint document_point) {
    const auto widget_point = canvas.widget_position_for_document_point(document_point);
    send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  click(QPoint(20, 20));
  click(QPoint(25, 20));
  canvas.set_pattern_stamp_aligned(false);
  click(QPoint(35, 20));

  const auto* layer = std::as_const(document).find_layer(layer_id);
  CHECK(layer != nullptr);
  const auto* centered = layer->pixels().pixel(20, 20);
  const auto* continued = layer->pixels().pixel(25, 20);
  const auto* restarted = layer->pixels().pixel(35, 20);
  CHECK(std::equal(centered, centered + 4, pattern.tile.pixel(2, 2)));
  CHECK(std::equal(continued, continued + 4, pattern.tile.pixel(3, 2)));
  CHECK(std::equal(restarted, restarted + 4, pattern.tile.pixel(2, 2)));

  ensure_artifact_dir();
  const auto path = std::filesystem::path("test-artifacts") / "ui_pattern_stamp.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  const auto reopened = patchy::psd::DocumentIo::read_file(path);
  CHECK(reopened.layers().size() == 1U);
  const auto* reopened_pixel = reopened.layers().front().pixels().pixel(25, 20);
  CHECK(std::equal(continued, continued + 4, reopened_pixel));

  patchy::Document palette_document(24, 24, patchy::PixelFormat::rgba8());
  const auto palette_layer_id = palette_document.add_pixel_layer(
      "Palette Pattern", solid_pixels(24, 24, patchy::PixelFormat::rgba8(), Qt::transparent)).id();
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {patchy::RgbColor{0, 0, 0}, patchy::RgbColor{255, 255, 255}};
  editing.palette_revision = 7001;
  palette_document.palette_editing() = editing;
  canvas.set_document(&palette_document);
  canvas.set_pattern_stamp_pattern(pattern);
  canvas.set_pattern_stamp_aligned(false);
  click(QPoint(12, 12));
  const auto* palette_layer = std::as_const(palette_document).find_layer(palette_layer_id);
  CHECK(palette_layer != nullptr);
  const auto* snapped = palette_layer->pixels().pixel(12, 12);
  CHECK((snapped[0] == 0U && snapped[1] == 0U && snapped[2] == 0U) ||
        (snapped[0] == 255U && snapped[1] == 255U && snapped[2] == 255U));
  CHECK(snapped[3] == 255U);
  save_widget_artifact("ui_pattern_stamp_alignment", canvas);
}

void ui_airbrush_fast_strokes_ignore_mouse_event_density() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  QApplication::processEvents();

  auto send_stroke = [canvas](const std::vector<QPoint>& points) {
    CHECK(points.size() >= 2U);
    send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(points.front()),
               Qt::LeftButton, Qt::LeftButton);
    for (std::size_t index = 1; index < points.size(); ++index) {
      send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(points[index]),
                 Qt::NoButton, Qt::LeftButton);
    }
    send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(points.back()),
               Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  send_stroke({QPoint(70, 112), QPoint(280, 112)});
  send_stroke({QPoint(70, 188), QPoint(76, 188), QPoint(83, 188), QPoint(91, 188), QPoint(102, 188),
               QPoint(113, 188), QPoint(127, 188), QPoint(139, 188), QPoint(154, 188), QPoint(166, 188),
               QPoint(181, 188), QPoint(193, 188), QPoint(207, 188), QPoint(219, 188), QPoint(234, 188),
               QPoint(247, 188), QPoint(261, 188), QPoint(273, 188), QPoint(280, 188)});

  for (const auto x : {110, 145, 180, 215, 250}) {
    const auto sparse = canvas_pixel(*canvas, QPoint(x, 112));
    const auto dense = canvas_pixel(*canvas, QPoint(x, 188));
    CHECK(sparse.red() < 245);
    CHECK(dense.red() < 245);
    CHECK(color_close(sparse, dense, 26));
  }
  save_widget_artifact("ui_airbrush_event_density", window);
}

void ui_airbrush_jittered_stroke_uses_smoothed_path() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(64);
  canvas->set_brush_softness(100);
  QApplication::processEvents();

  const std::vector<QPoint> points = {
      QPoint(70, 150),  QPoint(100, 159), QPoint(130, 141), QPoint(160, 159),
      QPoint(190, 141), QPoint(220, 159), QPoint(250, 141), QPoint(280, 150),
  };
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(points.front()),
             Qt::LeftButton, Qt::LeftButton);
  for (std::size_t index = 1; index < points.size(); ++index) {
    send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(points[index]), Qt::NoButton,
               Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(points.back()),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage();
  auto darkness_center_y = [canvas, &image](int x) {
    double weighted_y = 0.0;
    double total_darkness = 0.0;
    for (int y = 128; y <= 172; ++y) {
      const auto widget_point = canvas->widget_position_for_document_point(QPoint(x, y));
      const auto color = image.pixelColor(widget_point);
      const auto darkness = static_cast<double>(255 - color.red());
      if (darkness <= 4.0) {
        continue;
      }
      weighted_y += static_cast<double>(y) * darkness;
      total_darkness += darkness;
    }
    CHECK(total_darkness > 20.0);
    return weighted_y / total_darkness;
  };

  std::vector<double> centerline;
  for (const auto x : {100, 130, 160, 190, 220, 250}) {
    centerline.push_back(darkness_center_y(x));
  }
  const auto [min_center, max_center] = std::minmax_element(centerline.begin(), centerline.end());
  CHECK(min_center != centerline.end());
  CHECK((*max_center - *min_center) <= 12.0);
  save_widget_artifact("ui_airbrush_smoothed_jitter", window);
}

void ui_soft_opaque_brush_l_corner_stacks_soft_edges() {
  patchy::Document document(280, 240, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Paint", solid_pixels(280, 240, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Soft Brush L Corner"));
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_zoom(2.0);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(64);
  canvas->set_brush_opacity(100);
  canvas->set_brush_softness(100);
  canvas->set_brush_build_up(false);
  QApplication::processEvents();

  const auto send_document_mouse = [canvas](QEvent::Type type, QPointF document_point,
                                            Qt::MouseButton button, Qt::MouseButtons buttons) {
    send_mouse(*canvas, type, canvas->widget_position_for_document_point(document_point.toPoint()), button,
               buttons);
  };
  const auto drag_segment = [&](QPointF from, QPointF to) {
    const auto dx = to.x() - from.x();
    const auto dy = to.y() - from.y();
    const auto steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / 5.0)));
    for (int step = 1; step <= steps; ++step) {
      const auto t = static_cast<double>(step) / static_cast<double>(steps);
      send_document_mouse(QEvent::MouseMove, QPointF(from.x() + dx * t, from.y() + dy * t), Qt::NoButton,
                          Qt::LeftButton);
    }
  };

  const QPointF start(48, 80);
  const QPointF corner(184, 80);
  const QPointF end(184, 204);
  send_document_mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  drag_segment(start, corner);
  drag_segment(corner, end);
  send_document_mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto& layer = patchy::ui::MainWindowTestAccess::document(window).layers().front();
  const auto& pixels = layer.pixels();
  const auto single_edge_alpha = static_cast<int>(pixels.pixel(152, 64)[3]);
  const auto inside_corner_alpha = static_cast<int>(pixels.pixel(168, 96)[3]);
  std::cout << "[soft_artifact] ui_soft_opaque_brush_l_corner single_edge_alpha="
            << single_edge_alpha << " inside_corner_alpha=" << inside_corner_alpha << '\n';
  save_widget_artifact("ui_soft_opaque_brush_l_corner", window);

  CHECK(inside_corner_alpha >= single_edge_alpha + 20);
}

void ui_soft_brush_overlap_artifact_gallery() {
  const auto distance_to_segment = [](QPointF point, QPointF a, QPointF b) {
    const auto dx = b.x() - a.x();
    const auto dy = b.y() - a.y();
    const auto length_squared = dx * dx + dy * dy;
    const auto along =
        length_squared <= std::numeric_limits<double>::epsilon()
            ? 0.0
            : std::clamp(((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / length_squared, 0.0, 1.0);
    return std::hypot(point.x() - (a.x() + dx * along), point.y() - (a.y() + dy * along));
  };
  const auto soft_coverage = [](double distance, double radius, int softness) {
    if (distance >= radius) {
      return 0.0;
    }
    softness = std::clamp(softness, 0, 100);
    if (softness <= 0) {
      return 1.0;
    }
    const auto edge_width = std::max(1.0, radius * static_cast<double>(softness) / 100.0);
    const auto inner_radius = std::max(0.0, radius - edge_width);
    if (distance <= inner_radius) {
      return 1.0;
    }
    const auto t = std::clamp((distance - inner_radius) / edge_width, 0.0, 1.0);
    const auto smooth = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    return 1.0 - smooth;
  };
  const auto expected_alpha = [&](QPoint point, const std::vector<QPointF>& points, double radius,
                                  int opacity, int softness) {
    auto coverage = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
      coverage = std::max(coverage,
                          soft_coverage(distance_to_segment(point, points[index - 1], points[index]), radius,
                                        softness));
    }
    return static_cast<int>(std::lround(coverage * static_cast<double>(opacity) * 2.55));
  };

  const auto run_case = [&](const char* artifact_name, const std::vector<QPointF>& points,
                            QSize canvas_size, int brush_size, int opacity, int softness, double event_spacing) {
    patchy::Document document(canvas_size.width(), canvas_size.height(), patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Paint", solid_pixels(canvas_size.width(), canvas_size.height(),
                                                   patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QString::fromLatin1(artifact_name));
    show_window(window);
    auto* canvas = require_canvas(window);

    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    canvas->set_zoom(canvas_size.width() > 500 ? 1.0 : 2.0);
    canvas->set_primary_color(Qt::black);
    canvas->set_brush_size(brush_size);
    canvas->set_brush_opacity(opacity);
    canvas->set_brush_softness(softness);
    canvas->set_brush_build_up(false);
    QApplication::processEvents();

    const auto send_document_mouse = [canvas](QEvent::Type type, QPointF document_point,
                                              Qt::MouseButton button, Qt::MouseButtons buttons) {
      send_mouse(*canvas, type, canvas->widget_position_for_document_point(document_point.toPoint()), button,
                 buttons);
    };
    const auto drag_segment = [&](QPointF from, QPointF to) {
      const auto dx = to.x() - from.x();
      const auto dy = to.y() - from.y();
      const auto steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / event_spacing)));
      for (int step = 1; step <= steps; ++step) {
        const auto t = static_cast<double>(step) / static_cast<double>(steps);
        send_document_mouse(QEvent::MouseMove, QPointF(from.x() + dx * t, from.y() + dy * t), Qt::NoButton,
                            Qt::LeftButton);
      }
    };

    send_document_mouse(QEvent::MouseButtonPress, points.front(), Qt::LeftButton, Qt::LeftButton);
    for (std::size_t index = 1; index < points.size(); ++index) {
      drag_segment(points[index - 1], points[index]);
    }
    send_document_mouse(QEvent::MouseButtonRelease, points.back(), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    const auto& layer = patchy::ui::MainWindowTestAccess::document(window).layers().front();
    const auto& pixels = layer.pixels();
    auto max_underpaint = 0;
    auto underpaint_pixels = 0;
    auto max_inner_jump = 0;
    auto inner_jump_pixels = 0;
    QPoint max_underpaint_point;
    QPoint max_jump_point;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const QPoint point(x, y);
        const auto expected = expected_alpha(point, points, static_cast<double>(brush_size) * 0.5,
                                             opacity, softness);
        const auto actual = static_cast<int>(pixels.pixel(x, y)[3]);
        const auto underpaint = expected - actual;
        if (underpaint > max_underpaint) {
          max_underpaint = underpaint;
          max_underpaint_point = point;
        }
        if (underpaint > 24) {
          ++underpaint_pixels;
        }
        const auto check_inner_jump = [&](QPoint neighbor) {
          if (neighbor.x() < 0 || neighbor.y() < 0 || neighbor.x() >= pixels.width() ||
              neighbor.y() >= pixels.height()) {
            return;
          }
          const auto neighbor_expected = expected_alpha(neighbor, points,
                                                        static_cast<double>(brush_size) * 0.5,
                                                        opacity, softness);
          if (expected < 192 || neighbor_expected < 192) {
            return;
          }
          const auto neighbor_alpha = static_cast<int>(pixels.pixel(neighbor.x(), neighbor.y())[3]);
          const auto jump = std::abs(actual - neighbor_alpha);
          if (jump > max_inner_jump) {
            max_inner_jump = jump;
            max_jump_point = point;
          }
          if (jump > 36) {
            ++inner_jump_pixels;
          }
        };
        check_inner_jump(point + QPoint(1, 0));
        check_inner_jump(point + QPoint(0, 1));
      }
    }
    std::cout << "[soft_artifact] " << artifact_name
              << " max_underpaint=" << max_underpaint
              << " at " << max_underpaint_point.x() << ',' << max_underpaint_point.y()
              << " pixels_under_24=" << underpaint_pixels
              << " max_inner_jump=" << max_inner_jump
              << " at " << max_jump_point.x() << ',' << max_jump_point.y()
              << " inner_jump_pixels_36=" << inner_jump_pixels << '\n';
    CHECK(max_underpaint <= 24);
    CHECK(underpaint_pixels == 0);
    CHECK(max_inner_jump <= 36);
    CHECK(inner_jump_pixels == 0);
    save_widget_artifact(artifact_name, window);
  };

  const auto run_default_layer_case = [&](const char* artifact_name, const std::vector<QPointF>& points,
                                          QSize canvas_size, int brush_size, int opacity, int softness,
                                          double event_spacing) {
    patchy::Document document(canvas_size.width(), canvas_size.height(), patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background", solid_pixels(canvas_size.width(), canvas_size.height(),
                                                        patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
    document.add_pixel_layer("Paint Layer", solid_pixels(canvas_size.width(), canvas_size.height(),
                                                         patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

    patchy::ui::MainWindow window;
    window.add_document_session(std::move(document), QString::fromLatin1(artifact_name));
    show_window(window);
    auto* canvas = require_canvas(window);

    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    canvas->set_zoom(canvas_size.width() > 500 ? 1.0 : 2.0);
    canvas->set_primary_color(Qt::black);
    canvas->set_brush_size(brush_size);
    canvas->set_brush_opacity(opacity);
    canvas->set_brush_softness(softness);
    canvas->set_brush_build_up(false);
    QApplication::processEvents();

    const auto send_document_mouse = [canvas](QEvent::Type type, QPointF document_point,
                                              Qt::MouseButton button, Qt::MouseButtons buttons) {
      send_mouse(*canvas, type, canvas->widget_position_for_document_point(document_point.toPoint()), button,
                 buttons);
    };
    const auto drag_segment = [&](QPointF from, QPointF to) {
      const auto dx = to.x() - from.x();
      const auto dy = to.y() - from.y();
      const auto steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / event_spacing)));
      for (int step = 1; step <= steps; ++step) {
        const auto t = static_cast<double>(step) / static_cast<double>(steps);
        send_document_mouse(QEvent::MouseMove, QPointF(from.x() + dx * t, from.y() + dy * t), Qt::NoButton,
                            Qt::LeftButton);
      }
    };

    send_document_mouse(QEvent::MouseButtonPress, points.front(), Qt::LeftButton, Qt::LeftButton);
    for (std::size_t index = 1; index < points.size(); ++index) {
      drag_segment(points[index - 1], points[index]);
    }
    send_document_mouse(QEvent::MouseButtonRelease, points.back(), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    QEvent leave_event(QEvent::Leave);
    QApplication::sendEvent(canvas, &leave_event);
    QApplication::processEvents();

    const auto image = canvas->grab().toImage();
    auto max_too_light = 0;
    auto too_light_pixels = 0;
    auto max_inner_jump = 0;
    auto inner_jump_pixels = 0;
    QPoint max_light_point;
    QPoint max_jump_point;
    auto sampled_pixels = 0;
    for (std::int32_t y = 2; y < canvas_size.height() - 2; ++y) {
      for (std::int32_t x = 2; x < canvas_size.width() - 2; ++x) {
        const QPoint point(x, y);
        const auto top_left = canvas->widget_position_for_document_point(point);
        const auto bottom_right = canvas->widget_position_for_document_point(point + QPoint(1, 1));
        const QPoint widget_point((top_left.x() + bottom_right.x()) / 2,
                                  (top_left.y() + bottom_right.y()) / 2);
        if (!image.rect().contains(widget_point)) {
          continue;
        }
        ++sampled_pixels;
        const auto expected_darkness = expected_alpha(point, points, static_cast<double>(brush_size) * 0.5,
                                                      opacity, softness);
        const auto actual_darkness = 255 - image.pixelColor(widget_point).red();
        const auto too_light = expected_darkness - actual_darkness;
        if (too_light > max_too_light) {
          max_too_light = too_light;
          max_light_point = point;
        }
        if (too_light > 24) {
          ++too_light_pixels;
        }

        const auto check_inner_jump = [&](QPoint neighbor) {
          const auto neighbor_expected =
              expected_alpha(neighbor, points, static_cast<double>(brush_size) * 0.5, opacity, softness);
          if (expected_darkness < 192 || neighbor_expected < 192) {
            return;
          }
          const auto neighbor_top_left = canvas->widget_position_for_document_point(neighbor);
          const auto neighbor_bottom_right = canvas->widget_position_for_document_point(neighbor + QPoint(1, 1));
          const QPoint neighbor_widget_point((neighbor_top_left.x() + neighbor_bottom_right.x()) / 2,
                                             (neighbor_top_left.y() + neighbor_bottom_right.y()) / 2);
          if (!image.rect().contains(neighbor_widget_point)) {
            return;
          }
          const auto neighbor_darkness = 255 - image.pixelColor(neighbor_widget_point).red();
          const auto jump = std::abs(actual_darkness - neighbor_darkness);
          if (jump > max_inner_jump) {
            max_inner_jump = jump;
            max_jump_point = point;
          }
          if (jump > 36) {
            ++inner_jump_pixels;
          }
        };
        check_inner_jump(point + QPoint(1, 0));
        check_inner_jump(point + QPoint(0, 1));
      }
    }

    std::cout << "[soft_artifact] " << artifact_name << " sampled=" << sampled_pixels
              << " max_too_light=" << max_too_light
              << " at " << max_light_point.x() << ',' << max_light_point.y()
              << " pixels_too_light_24=" << too_light_pixels
              << " max_inner_jump=" << max_inner_jump
              << " at " << max_jump_point.x() << ',' << max_jump_point.y()
              << " inner_jump_pixels_36=" << inner_jump_pixels << '\n';
    save_widget_artifact(artifact_name, window);
    CHECK(sampled_pixels > canvas_size.width() * canvas_size.height() / 2);
    CHECK(max_inner_jump <= 36);
    CHECK(inner_jump_pixels == 0);
  };

  run_case("ui_soft_artifact_acute_zigzag",
           {QPointF(44, 44), QPointF(330, 78), QPointF(52, 124), QPointF(330, 166), QPointF(46, 214)},
           QSize(380, 260), 64, 100, 100, 5.0);
  run_case("ui_soft_artifact_crossing_star",
           {QPointF(188, 30), QPointF(238, 216), QPointF(42, 96), QPointF(338, 98), QPointF(138, 216),
            QPointF(188, 30)},
           QSize(380, 260), 64, 100, 100, 5.0);
  run_case("ui_soft_artifact_large_sparse_star",
           {QPointF(376, 60), QPointF(476, 432), QPointF(84, 192), QPointF(676, 196), QPointF(276, 432),
            QPointF(376, 60)},
           QSize(760, 560), 160, 100, 100, 10000.0);
  run_default_layer_case("ui_soft_artifact_large_sparse_star_default_layers",
                         {QPointF(376, 60), QPointF(476, 432), QPointF(84, 192), QPointF(676, 196), QPointF(276, 432),
                          QPointF(376, 60)},
                         QSize(760, 560), 160, 100, 100, 10000.0);
}

void ui_clone_tool_samples_source_and_paints_offset() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 40, 30));
  canvas->set_brush_size(22);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 72)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(110, 72)),
       canvas->widget_position_for_document_point(QPoint(112, 72)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Clone);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  canvas->set_brush_softness(100);
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  CHECK(clone_aligned != nullptr);
  CHECK(clone_aligned->isChecked());
  CHECK(canvas->clone_aligned());

  const auto source = canvas->widget_position_for_document_point(QPoint(70, 72));
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  const auto target = canvas->widget_position_for_document_point(QPoint(170, 102));
  drag(*canvas, target, target + QPoint(2, 0));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 102)), QColor(230, 40, 30), 45));
  const auto feathered_edge = canvas_pixel(*canvas, QPoint(178, 102));
  CHECK(feathered_edge.red() > feathered_edge.green());
  CHECK(feathered_edge.green() > 80);
  CHECK(feathered_edge.green() < 245);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 102)),
       canvas->widget_position_for_document_point(QPoint(212, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 102)), QColor(30, 80, 230), 45));

  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(250, 102)),
       canvas->widget_position_for_document_point(QPoint(252, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 102)), QColor(230, 40, 30), 45));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(290, 102)),
       canvas->widget_position_for_document_point(QPoint(292, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(290, 102)), QColor(230, 40, 30), 45));

  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Clone")));
  save_widget_artifact("ui_clone_tool_stamp", window);
}

void ui_clone_tool_feathered_rgba_edges_keep_source_color() {
  patchy::Document document(64, 64, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(64, 64, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  for (std::int32_t y = 34; y <= 50; ++y) {
    for (std::int32_t x = 8; x <= 56; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 255;
      px[1] = 220;
      px[2] = 0;
      px[3] = 255;
    }
  }
  auto& layer = document.add_pixel_layer("Paint", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 160);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Clone);
  canvas.set_brush_size(24);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(100);
  canvas.show();
  QApplication::processEvents();

  const auto source = canvas.widget_position_for_document_point(QPoint(32, 42));
  send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  const auto target = canvas.widget_position_for_document_point(QPoint(32, 16));
  send_mouse(canvas, QEvent::MouseButtonPress, target, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* center = layer.pixels().pixel(32, 16);
  CHECK(center[0] == 255);
  CHECK(center[1] == 220);
  CHECK(center[2] == 0);
  CHECK(center[3] == 255);

  const auto* feathered = layer.pixels().pixel(40, 16);
  CHECK(feathered[3] > 20);
  CHECK(feathered[3] < 240);
  CHECK(feathered[0] >= 245);
  CHECK(feathered[1] >= 210);
  CHECK(feathered[2] <= 5);
}

void ui_healing_brush_transfers_detail_and_preserves_destination_tone() {
  patchy::Document document(48, 24, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(48, 24, patchy::PixelFormat::rgba8(), QColor(40, 80, 120, 255));
  for (std::int32_t y = 11; y <= 13; ++y) {
    for (std::int32_t x = 9; x <= 11; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 100;
      px[1] = 100;
      px[2] = 100;
      px[3] = 255;
    }
  }
  auto* source_center = pixels.pixel(10, 12);
  source_center[0] = 180;
  source_center[1] = 160;
  source_center[2] = 140;
  auto& layer = document.add_pixel_layer("Healing", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(192, 96);
  canvas.set_document(&document);
  canvas.set_tool(patchy::ui::CanvasTool::Healing);
  canvas.set_brush_size(1);
  canvas.set_brush_opacity(100);
  canvas.set_brush_softness(0);
  canvas.set_healing_diffusion(5);
  canvas.show();
  QApplication::processEvents();

  const auto source = canvas.widget_position_for_document_point(QPoint(10, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  const auto target = canvas.widget_position_for_document_point(QPoint(34, 12));
  send_mouse(canvas, QEvent::MouseButtonPress, target, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto* healed = layer.pixels().pixel(34, 12);
  CHECK(healed[0] == 120);
  CHECK(healed[1] == 140);
  CHECK(healed[2] == 160);
  CHECK(healed[3] == 255);
  const auto* untouched = layer.pixels().pixel(35, 12);
  CHECK(untouched[0] == 40 && untouched[1] == 80 && untouched[2] == 120 && untouched[3] == 255);

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = std::filesystem::path(temp.path().toStdString()) / "healed.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  const auto reread = patchy::psd::DocumentIo::read_file(path);
  CHECK(reread.layers().size() == 1);
  const auto* round_tripped = reread.layers().front().pixels().pixel(34, 12);
  CHECK(round_tripped[0] == 120);
  CHECK(round_tripped[1] == 140);
  CHECK(round_tripped[2] == 160);
  CHECK(round_tripped[3] == 255);
}

void ui_local_adjustment_brushes_use_fixed_math_and_round_trip_psd() {
  patchy::Document document(25, 5, patchy::PixelFormat::rgba8());
  auto pixels = solid_pixels(25, 5, patchy::PixelFormat::rgba8(), QColor(100, 100, 100, 255));
  auto set_pixel = [&pixels](int x, QColor color) {
    auto* pixel = pixels.pixel(x, 2);
    pixel[0] = static_cast<std::uint8_t>(color.red());
    pixel[1] = static_cast<std::uint8_t>(color.green());
    pixel[2] = static_cast<std::uint8_t>(color.blue());
    pixel[3] = static_cast<std::uint8_t>(color.alpha());
  };
  set_pixel(2, QColor(220, 220, 220, 255));
  set_pixel(7, QColor(140, 140, 140, 255));
  set_pixel(12, QColor(64, 64, 64, 255));
  set_pixel(17, QColor(192, 192, 192, 255));
  set_pixel(22, QColor(200, 100, 50, 128));
  auto& layer = document.add_pixel_layer("Local adjustments", std::move(pixels));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(250, 80);
  canvas.set_document(&document);
  canvas.set_brush_size(1);
  canvas.set_brush_softness(0);
  canvas.set_local_adjustment_strength(100);
  canvas.set_local_protect_tones(false);
  canvas.show();
  QApplication::processEvents();

  const auto apply = [&canvas](patchy::ui::CanvasTool tool, QPoint point) {
    canvas.set_tool(tool);
    const auto widget_point = canvas.widget_position_for_document_point(point);
    send_mouse(canvas, QEvent::MouseButtonPress, widget_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(canvas, QEvent::MouseButtonRelease, widget_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };

  apply(patchy::ui::CanvasTool::BlurBrush, QPoint(2, 2));
  apply(patchy::ui::CanvasTool::SharpenBrush, QPoint(7, 2));
  canvas.set_local_tone_range(patchy::ui::CanvasWidget::LocalToneRange::Shadows);
  apply(patchy::ui::CanvasTool::Dodge, QPoint(12, 2));
  canvas.set_local_tone_range(patchy::ui::CanvasWidget::LocalToneRange::Highlights);
  apply(patchy::ui::CanvasTool::Burn, QPoint(17, 2));
  canvas.set_sponge_mode(patchy::ui::CanvasWidget::SpongeMode::Desaturate);
  canvas.set_sponge_vibrance(false);
  apply(patchy::ui::CanvasTool::Sponge, QPoint(22, 2));

  const auto* blurred = layer.pixels().pixel(2, 2);
  CHECK(blurred[0] == 130 && blurred[1] == 130 && blurred[2] == 130 && blurred[3] == 255);
  const auto* sharpened = layer.pixels().pixel(7, 2);
  CHECK(sharpened[0] == 170 && sharpened[1] == 170 && sharpened[2] == 170 && sharpened[3] == 255);
  const auto* dodged = layer.pixels().pixel(12, 2);
  CHECK(dodged[0] == 207 && dodged[1] == 207 && dodged[2] == 207 && dodged[3] == 255);
  const auto* burned = layer.pixels().pixel(17, 2);
  CHECK(burned[0] == 47 && burned[1] == 47 && burned[2] == 47 && burned[3] == 255);
  const auto* desaturated = layer.pixels().pixel(22, 2);
  CHECK(desaturated[0] == 117 && desaturated[1] == 117 && desaturated[2] == 117 && desaturated[3] == 128);
  const auto* untouched = layer.pixels().pixel(23, 2);
  CHECK(untouched[0] == 100 && untouched[1] == 100 && untouched[2] == 100 && untouched[3] == 255);

  ensure_artifact_dir();
  const auto path = std::filesystem::path("test-artifacts") / "ui_local_adjustment_brushes.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  const auto reread = patchy::psd::DocumentIo::read_file(path);
  CHECK(reread.layers().size() == 1);
  const auto& round_tripped = reread.layers().front().pixels();
  CHECK(round_tripped.width() == layer.pixels().width());
  CHECK(round_tripped.height() == layer.pixels().height());
  for (std::int32_t y = 0; y < round_tripped.height(); ++y) {
    for (std::int32_t x = 0; x < round_tripped.width(); ++x) {
      const auto* actual = round_tripped.pixel(x, y);
      const auto* expected = layer.pixels().pixel(x, y);
      CHECK(std::equal(expected, expected + 4, actual));
    }
  }
}

void ui_smudge_tool_drags_painted_pixels() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 50, 30));
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(102, 120)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::Smudge);
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(70);
  canvas->set_brush_softness(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 120)));
  QApplication::processEvents();

  const auto smeared = canvas_pixel(*canvas, QPoint(165, 120));
  CHECK(smeared.red() > 175);
  CHECK(smeared.green() < 170);
  CHECK(smeared.blue() < 160);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(230, 120)), Qt::white, 10));
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Smudge")));
  save_widget_artifact("ui_smudge_tool", window);
}

void ui_wet_edges_uses_one_continuous_stroke_boundary() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(40);
  canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100);
  canvas->set_brush_softness(0);
  patchy::BrushDynamics wet_edges;
  wet_edges.wet_edges = true;
  canvas->set_brush_dynamics(wet_edges);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 200)),
       canvas->widget_position_for_document_point(QPoint(500, 200)));
  QApplication::processEvents();

  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.active_layer_id().has_value());
  const auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  const auto alpha_at = [layer](QPoint point) {
    if (!layer->bounds().contains(point.x(), point.y())) {
      return std::uint8_t{0};
    }
    return layer->pixels().pixel(point.x() - layer->bounds().x,
                                 point.y() - layer->bounds().y)[3];
  };

  std::uint8_t minimum_center = 255;
  std::uint8_t maximum_center = 0;
  for (int x = 210; x <= 450; x += 5) {
    const auto alpha = alpha_at(QPoint(x, 200));
    minimum_center = std::min(minimum_center, alpha);
    maximum_center = std::max(maximum_center, alpha);
  }
  // The calibrated hard-Round wash keeps the Photoshop 2026 center alpha (about 58.6%).
  CHECK(minimum_center >= 145);
  CHECK(maximum_center <= 153);
  CHECK(maximum_center - minimum_center <= 4);  // no periodic per-dab rings
  const auto rim_alpha = alpha_at(QPoint(330, 181));
  CHECK(rim_alpha >= maximum_center + 5);  // one subtle, darker outer stroke rim
  CHECK(rim_alpha <= maximum_center + 25);

  // Photoshop treats a released-and-restarted mark as a new translucent wash, so ordinary
  // source-over composition darkens the intersection without introducing internal dab rings.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(330, 140)),
       canvas->widget_position_for_document_point(QPoint(330, 260)));
  QApplication::processEvents();
  const auto overlap_alpha = alpha_at(QPoint(330, 200));
  CHECK(overlap_alpha >= maximum_center + 50);
  CHECK(overlap_alpha <= 225);
  save_widget_artifact("ui_wet_edges_continuous_stroke", window);
}

void ui_mixer_brush_uses_compact_controls_and_round_trips_raster_pixels() {
  SettingsValueRestorer restore_wet(QStringLiteral("tools/mixerWet"));
  SettingsValueRestorer restore_load(QStringLiteral("tools/mixerLoad"));
  SettingsValueRestorer restore_mix(QStringLiteral("tools/mixerMix"));
  SettingsValueRestorer restore_flow(QStringLiteral("tools/mixerFlow"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // Put a known color on the active layer. Mixer samples this one pixel once at mouse-down.
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 30, 240));
  canvas->set_brush_size(36);
  canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100);
  canvas->set_brush_softness(0);
  const auto sample_point = canvas->widget_position_for_document_point(QPoint(150, 200));
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_point, Qt::LeftButton, Qt::NoButton);

  canvas->set_brush_opacity(20);  // hidden Brush opacity must not weaken Mixer strokes
  require_action_by_text(window, QStringLiteral("Mixer Brush"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MixerBrush);
  auto* wet = window.findChild<QSpinBox*>(QStringLiteral("mixerWetSpin"));
  auto* load = window.findChild<QSpinBox*>(QStringLiteral("mixerLoadSpin"));
  auto* mix = window.findChild<QSpinBox*>(QStringLiteral("mixerMixSpin"));
  auto* flow = window.findChild<QSpinBox*>(QStringLiteral("mixerFlowSpin"));
  CHECK(wet != nullptr && wet->isVisible());
  CHECK(load != nullptr && load->isVisible());
  CHECK(mix != nullptr && mix->isVisible());
  CHECK(flow != nullptr && flow->isVisible());
  CHECK(!window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"))->isVisible());
  CHECK(!window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"))->isVisible());
  CHECK(!window.findChild<QToolButton*>(QStringLiteral("brushDynamicsButton"))->isVisible());
  QTest::mouseClick(wet, Qt::LeftButton, Qt::NoModifier,
                    QPoint(wet->width() - 7, wet->height() / 2));
  QApplication::processEvents();
  QWidget* wet_popup = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("mixerWetPopup") && widget->isVisible()) {
      wet_popup = widget;
    }
  }
  CHECK(wet_popup != nullptr);
  CHECK(wet_popup->findChild<QSlider*>(QStringLiteral("mixerWetPopupSlider")) != nullptr);
  wet_popup->close();
  wet->setValue(100);
  load->setValue(100);
  mix->setValue(75);
  flow->setValue(100);
  CHECK(canvas->mixer_wet() == 100);
  CHECK(canvas->mixer_load() == 100);
  CHECK(canvas->mixer_mix() == 75);
  CHECK(canvas->mixer_flow() == 100);

  canvas->set_primary_color(QColor(240, 20, 0));
  canvas->set_brush_size(30);
  drag(*canvas, sample_point,
       canvas->widget_position_for_document_point(QPoint(500, 200)));
  QApplication::processEvents();

  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(document.active_layer_id().has_value());
  const auto* layer = document.find_layer(*document.active_layer_id());
  CHECK(layer != nullptr);
  const auto pixel_at = [layer](QPoint point) {
    CHECK(layer->bounds().contains(point.x(), point.y()));
    const auto* pixel = layer->pixels().pixel(point.x() - layer->bounds().x,
                                               point.y() - layer->bounds().y);
    return std::array<std::uint8_t, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
  };
  const auto near_color = pixel_at(QPoint(200, 200));
  const auto far_color = pixel_at(QPoint(450, 200));
  CHECK(near_color[2] > near_color[0]);
  CHECK(far_color[0] > far_color[2]);
  CHECK(far_color[3] < near_color[3]);
  CHECK(near_color[3] > 150);

  ensure_artifact_dir();
  const auto path = std::filesystem::path("test-artifacts") / "ui_mixer_brush.psd";
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, path);
  const auto reread = patchy::psd::DocumentIo::read_file(path);
  const auto* reopened = reread.find_layer(*reread.active_layer_id());
  CHECK(reopened != nullptr);
  CHECK(reopened->bounds().x == layer->bounds().x);
  CHECK(reopened->bounds().y == layer->bounds().y);
  CHECK(reopened->bounds().width == layer->bounds().width);
  CHECK(reopened->bounds().height == layer->bounds().height);
  CHECK(reopened->pixels().data().size() == layer->pixels().data().size());
  CHECK(std::equal(reopened->pixels().data().begin(), reopened->pixels().data().end(),
                   layer->pixels().data().begin()));

  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr && history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Mixer Brush")));
  save_widget_artifact("ui_mixer_brush", window);
}

void ui_copy_ignores_hidden_active_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  QApplication::clipboard()->clear();

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  drag(*canvas, QPoint(80, 80), QPoint(150, 110));
  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before);
  save_widget_artifact("ui_hidden_layer_copy_ignored", window);
}

void ui_copy_selected_layers_copies_composited_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(22);
  canvas->set_primary_color(QColor(230, 20, 30));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 42)),
       canvas->widget_position_for_document_point(QPoint(42, 42)));

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 70, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(82, 42)),
       canvas->widget_position_for_document_point(QPoint(84, 42)));

  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Layer 3"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setSelected(true);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(120, 80)));

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 42)), QColor(230, 20, 30), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(82, 42)), QColor(20, 70, 240), 45));
  save_widget_artifact("ui_copy_selected_layers", window);
}

void ui_eraser_on_background_reveals_transparency_and_size_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(38);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  canvas->set_brush_size(38);
  canvas->set_brush_opacity(92);
  canvas->set_brush_softness(20);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();

  auto* background_row = layer_list->itemWidget(background);
  CHECK(background_row != nullptr);
  const auto badges = background_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(badges.size() == 1);
  CHECK(badges.front()->toolTip().contains(QStringLiteral("Position locked")));

  const auto erase_point = canvas->widget_position_for_document_point(QPoint(90, 90));
  drag(*canvas, erase_point, erase_point + QPoint(1, 1));
  QApplication::processEvents();
  const auto erased = canvas_pixel(*canvas, QPoint(90, 90));
  CHECK(erased.red() >= 170);
  CHECK(erased.red() <= 245);
  CHECK(std::abs(erased.red() - erased.green()) <= 4);
  CHECK(std::abs(erased.green() - erased.blue()) <= 4);
  save_widget_artifact("ui_background_eraser_transparency", window);
}

void ui_brush_and_eraser_remember_separate_settings() {
  SettingsValueRestorer restore_brush_size(QStringLiteral("tools/brushSize"));
  SettingsValueRestorer restore_brush_opacity(QStringLiteral("tools/brushOpacity"));
  SettingsValueRestorer restore_brush_softness(QStringLiteral("tools/brushSoftness"));
  SettingsValueRestorer restore_eraser_size(QStringLiteral("tools/eraserSize"));
  SettingsValueRestorer restore_eraser_opacity(QStringLiteral("tools/eraserOpacity"));
  SettingsValueRestorer restore_eraser_softness(QStringLiteral("tools/eraserSoftness"));

  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto* canvas = require_canvas(window);
    auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
    auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
    auto* flow_spin = window.findChild<QSpinBox*>(QStringLiteral("brushFlowSpin"));
    auto* airbrush_check = window.findChild<QCheckBox*>(QStringLiteral("brushAirbrushCheck"));
    auto* softness_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
    CHECK(size_spin != nullptr);
    CHECK(opacity_spin != nullptr);
    CHECK(flow_spin != nullptr);
    CHECK(airbrush_check != nullptr);
    CHECK(softness_spin != nullptr);

    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    size_spin->setValue(31);
    opacity_spin->setValue(81);
    flow_spin->setValue(35);
    airbrush_check->setChecked(true);
    softness_spin->setValue(11);

    require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
    size_spin->setValue(62);
    opacity_spin->setValue(52);
    flow_spin->setValue(65);
    airbrush_check->setChecked(false);
    softness_spin->setValue(92);
    CHECK(canvas->brush_size() == 62);
    CHECK(canvas->brush_opacity() == 52);
    CHECK(canvas->brush_flow() == 65);
    CHECK(!canvas->brush_build_up());
    CHECK(canvas->brush_softness() == 92);

    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    CHECK(size_spin->value() == 31);
    CHECK(opacity_spin->value() == 81);
    CHECK(flow_spin->value() == 35);
    CHECK(airbrush_check->isChecked());
    CHECK(softness_spin->value() == 11);
    CHECK(canvas->brush_size() == 31);
    CHECK(canvas->brush_opacity() == 81);
    CHECK(canvas->brush_flow() == 35);
    CHECK(canvas->brush_build_up());
    CHECK(canvas->brush_softness() == 11);

    require_action_by_text(window, QStringLiteral("Clone"))->trigger();
    CHECK(size_spin->value() == 31);
    CHECK(canvas->brush_size() == 31);

    require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
    CHECK(size_spin->value() == 62);
    CHECK(opacity_spin->value() == 52);
    CHECK(flow_spin->value() == 65);
    CHECK(!airbrush_check->isChecked());
    CHECK(softness_spin->value() == 92);
    CHECK(canvas->brush_size() == 62);

    patchy::Document second(64, 64, patchy::PixelFormat::rgb8());
    second.add_pixel_layer("Background", solid_pixels(64, 64, patchy::PixelFormat::rgb8(), Qt::white));
    window.add_document_session(std::move(second), QStringLiteral("Second"));
    auto* second_canvas = require_canvas(window);
    CHECK(second_canvas != canvas);
    CHECK(second_canvas->brush_size() == 62);
    CHECK(second_canvas->brush_flow() == 65);
    CHECK(!second_canvas->brush_build_up());
    size_spin->setValue(48);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
    CHECK(tabs != nullptr);
    tabs->setCurrentIndex(0);
    CHECK(require_canvas(window) == canvas);
    CHECK(canvas->brush_size() == 48);
    CHECK(size_spin->value() == 48);

    require_action_by_text(window, QStringLiteral("Brush"))->trigger();
    CHECK(canvas->brush_size() == 31);
    CHECK(canvas->brush_flow() == 35);
    CHECK(canvas->brush_build_up());
  }

  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size_spin != nullptr);
  // A restart resets brush and eraser opacity/softness (and the paint brush size)
  // to the Round startup preset; only the eraser size survives.
  CHECK(canvas->brush_size() == 25);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 0);
  CHECK(!canvas->brush_build_up());
  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  CHECK(canvas->brush_size() == 48);
  CHECK(canvas->brush_opacity() == 100);
  CHECK(canvas->brush_flow() == 100);
  CHECK(canvas->brush_softness() == 0);
  CHECK(!canvas->brush_build_up());
  CHECK(size_spin->value() == 48);
}

void ui_magic_wand_cursor_marks_click_hotspot() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::MagicWand);
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  CHECK(canvas->tool() == patchy::ui::CanvasTool::MagicWand);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(6, 6));

  send_key_press(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_release(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(6, 6));
}

void ui_spacebar_pan_works_from_layer_panel_but_not_text_entry() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->viewport() != nullptr);

  layer_list->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto origin_before_panel_drag = canvas->widget_position_for_document_point(QPoint(0, 0));

  send_key_press(*layer_list, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::OpenHandCursor);

  const auto panel_drag_start = layer_list->viewport()->rect().center();
  const auto panel_drag_end = panel_drag_start + QPoint(42, 27);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonPress, panel_drag_start, Qt::LeftButton, Qt::LeftButton);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::ClosedHandCursor);
  send_mouse(*layer_list->viewport(), QEvent::MouseMove, panel_drag_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*layer_list->viewport(), QEvent::MouseButtonRelease, panel_drag_end, Qt::LeftButton, Qt::NoButton);

  const auto origin_after_panel_drag = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(origin_after_panel_drag.x() - origin_before_panel_drag.x() >= 30);
  CHECK(origin_after_panel_drag.y() - origin_before_panel_drag.y() >= 18);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::OpenHandCursor);
  send_key_release(*layer_list, Qt::Key_Space);
  CHECK(QApplication::overrideCursor() == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_position = canvas->widget_position_for_document_point(QPoint(120, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_position, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->clear();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  const auto origin_before_text_space = canvas->widget_position_for_document_point(QPoint(0, 0));
  QKeyEvent text_space_press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  QApplication::sendEvent(editor, &text_space_press);
  QKeyEvent text_space_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  QApplication::sendEvent(editor, &text_space_release);
  QApplication::processEvents();
  CHECK(editor->toPlainText() == QStringLiteral(" "));
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == origin_before_text_space);
  CHECK(QApplication::overrideCursor() == nullptr);

  send_key(*editor, Qt::Key_Escape);
}

void ui_move_tool_after_text_edit_keeps_spacebar_pan_active() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_position = canvas->widget_position_for_document_point(QPoint(120, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_position, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->setPlainText(QStringLiteral("Pan Focus"));
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(QApplication::focusWidget() == canvas);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);

  auto* focused = QApplication::focusWidget();
  CHECK(focused == canvas);
  send_key_press(*focused, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::OpenHandCursor);
  send_key_release(*focused, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
}

void ui_spacebar_pan_works_while_child_dialog_focused() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  // A non-modal child dialog (layer style / adjustment / filter) is a separate
  // top-level window. Holding Space while it holds focus should still pan the
  // canvas behind it, without the user first having to click the canvas.
  QDialog dialog(&window);
  dialog.setObjectName(QStringLiteral("spacebarPanChildDialog"));
  dialog.resize(240, 120);
  auto* button = new QPushButton(QStringLiteral("Apply"), &dialog);
  button->setGeometry(20, 12, 160, 32);
  auto* text_field = new QLineEdit(&dialog);
  text_field->setObjectName(QStringLiteral("spacebarPanChildDialogEdit"));
  text_field->setGeometry(20, 60, 160, 32);
  dialog.show();
  dialog.raise();
  QApplication::processEvents();

  button->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  // The intra-app guard in the fix depends on the application staying active
  // while focus moves between the dialog and the main window.
  CHECK(QGuiApplication::applicationState() == Qt::ApplicationActive);

  // Arm panning from the focused dialog button.
  send_key_press(*button, Qt::Key_Space);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::OpenHandCursor);

  // Clicking the canvas deactivates the dialog window and activates the main
  // window. That intra-app WindowDeactivate must NOT disarm the pan, otherwise
  // the drag below never grabs the canvas.
  QEvent dialog_deactivate(QEvent::WindowDeactivate);
  QApplication::sendEvent(&dialog, &dialog_deactivate);
  QApplication::processEvents();
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::OpenHandCursor);

  // A single press-drag-release on the canvas pans it, with no re-arming.
  const auto origin_before_drag = canvas->widget_position_for_document_point(QPoint(0, 0));
  const auto canvas_drag_start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto canvas_drag_end = canvas_drag_start + QPoint(50, 34);
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas_drag_start, Qt::LeftButton, Qt::LeftButton);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::ClosedHandCursor);
  send_mouse(*canvas, QEvent::MouseMove, canvas_drag_end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas_drag_end, Qt::LeftButton, Qt::NoButton);

  const auto origin_after_drag = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(origin_after_drag.x() - origin_before_drag.x() >= 40);
  CHECK(origin_after_drag.y() - origin_before_drag.y() >= 25);
  CHECK(QApplication::overrideCursor() != nullptr);
  CHECK(QApplication::overrideCursor()->shape() == Qt::OpenHandCursor);
  send_key_release(*button, Qt::Key_Space);
  CHECK(QApplication::overrideCursor() == nullptr);

  // Space typed into a text field inside the same dialog must still type a
  // space and never engage panning.
  text_field->clear();
  text_field->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto origin_before_text_space = canvas->widget_position_for_document_point(QPoint(0, 0));
  QKeyEvent text_space_press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  QApplication::sendEvent(text_field, &text_space_press);
  QKeyEvent text_space_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
  QApplication::sendEvent(text_field, &text_space_release);
  QApplication::processEvents();
  CHECK(text_field->text() == QStringLiteral(" "));
  CHECK(canvas->widget_position_for_document_point(QPoint(0, 0)) == origin_before_text_space);
  CHECK(QApplication::overrideCursor() == nullptr);
}

}  // namespace

std::vector<patchy::test::TestCase> brush_engine_stroke_tests() {
  return {
      {"ui_cut_selection_clears_source_and_keeps_clipboard", ui_cut_selection_clears_source_and_keeps_clipboard},
      {"ui_brush_on_pasted_layer_expands_layer_bounds", ui_brush_on_pasted_layer_expands_layer_bounds},
      {"ui_brush_opacity_caps_per_stroke", ui_brush_opacity_caps_per_stroke},
      {"ui_low_opacity_large_brush_whole_canvas_is_exact_fill_region",
       ui_low_opacity_large_brush_whole_canvas_is_exact_fill_region},
      {"ui_soft_brush_click_paints_single_dab", ui_soft_brush_click_paints_single_dab},
      {"ui_brush_can_start_off_canvas_with_visible_soft_edge",
       ui_brush_can_start_off_canvas_with_visible_soft_edge},
      {"ui_brush_stroke_continues_through_off_canvas_grey_area",
       ui_brush_stroke_continues_through_off_canvas_grey_area},
      {"ui_large_soft_brush_repaints_after_small_drag",
       ui_large_soft_brush_repaints_after_small_drag},
      {"ui_large_soft_brush_drag_stays_responsive",
       ui_large_soft_brush_drag_stays_responsive},
      {"ui_soft_brush_shift_click_does_not_overpaint_anchor",
       ui_soft_brush_shift_click_does_not_overpaint_anchor},
      {"ui_soft_brush_line_ignores_mouse_event_density", ui_soft_brush_line_ignores_mouse_event_density},
      {"ui_layer_mask_brush_opacity_caps_per_stroke",
       ui_layer_mask_brush_opacity_caps_per_stroke},
      {"ui_shift_constrains_brush_and_eraser_strokes_to_axis",
       ui_shift_constrains_brush_and_eraser_strokes_to_axis},
      {"ui_shift_constrains_clone_stamp_strokes_to_axis",
       ui_shift_constrains_clone_stamp_strokes_to_axis},
      {"ui_brush_alt_right_drag_adjusts_size_and_softness",
       ui_brush_alt_right_drag_adjusts_size_and_softness},
      {"ui_pen_alt_barrel_button_adjusts_brush_size_and_softness",
       ui_pen_alt_barrel_button_adjusts_brush_size_and_softness},
      {"ui_pen_brush_adjust_overlay_centers_on_pen", ui_pen_brush_adjust_overlay_centers_on_pen},
      {"ui_brush_alt_right_drag_syncs_options_bar_spins",
       ui_brush_alt_right_drag_syncs_options_bar_spins},
      {"ui_brush_shift_click_connects_strokes", ui_brush_shift_click_connects_strokes},
      {"ui_brush_opacity_and_flow_digit_keys_set_values",
       ui_brush_opacity_and_flow_digit_keys_set_values},
      {"ui_undo_shortcut_includes_ctrl_alt_z", ui_undo_shortcut_includes_ctrl_alt_z},
      {"ui_one_pixel_brush_drag_paints_fractional_smoothed_line",
       ui_one_pixel_brush_drag_paints_fractional_smoothed_line},
      {"ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel",
       ui_one_pixel_brush_and_eraser_same_cell_drag_touches_one_pixel},
      {"ui_max_zoom_brush_skips_noop_stroke_repaints",
       ui_max_zoom_brush_skips_noop_stroke_repaints},
      {"ui_deep_zoom_brush_repaint_stays_responsive",
       ui_deep_zoom_brush_repaint_stays_responsive},
      {"ui_airbrush_preset_builds_while_stationary",
       ui_airbrush_preset_builds_while_stationary},
      {"ui_brush_flow_builds_only_to_opacity_cap_and_round_trips_psd",
       ui_brush_flow_builds_only_to_opacity_cap_and_round_trips_psd},
      {"ui_pattern_stamp_alignment_palette_and_psd_round_trip",
       ui_pattern_stamp_alignment_palette_and_psd_round_trip},
      {"ui_airbrush_fast_strokes_ignore_mouse_event_density",
       ui_airbrush_fast_strokes_ignore_mouse_event_density},
      {"ui_airbrush_jittered_stroke_uses_smoothed_path", ui_airbrush_jittered_stroke_uses_smoothed_path},
      {"ui_soft_opaque_brush_l_corner_stacks_soft_edges",
       ui_soft_opaque_brush_l_corner_stacks_soft_edges},
      {"ui_soft_brush_overlap_artifact_gallery", ui_soft_brush_overlap_artifact_gallery},
      {"ui_clone_tool_samples_source_and_paints_offset", ui_clone_tool_samples_source_and_paints_offset},
      {"ui_clone_tool_feathered_rgba_edges_keep_source_color",
       ui_clone_tool_feathered_rgba_edges_keep_source_color},
      {"ui_healing_brush_transfers_detail_and_preserves_destination_tone",
       ui_healing_brush_transfers_detail_and_preserves_destination_tone},
      {"ui_local_adjustment_brushes_use_fixed_math_and_round_trip_psd",
       ui_local_adjustment_brushes_use_fixed_math_and_round_trip_psd},
      {"ui_smudge_tool_drags_painted_pixels", ui_smudge_tool_drags_painted_pixels},
      {"ui_wet_edges_uses_one_continuous_stroke_boundary",
       ui_wet_edges_uses_one_continuous_stroke_boundary},
      {"ui_mixer_brush_uses_compact_controls_and_round_trips_raster_pixels",
       ui_mixer_brush_uses_compact_controls_and_round_trips_raster_pixels},
      {"ui_copy_ignores_hidden_active_layer", ui_copy_ignores_hidden_active_layer},
      {"ui_copy_selected_layers_copies_composited_selection", ui_copy_selected_layers_copies_composited_selection},
      {"ui_eraser_on_background_reveals_transparency_and_size_cursor",
       ui_eraser_on_background_reveals_transparency_and_size_cursor},
      {"ui_brush_and_eraser_remember_separate_settings", ui_brush_and_eraser_remember_separate_settings},
      {"ui_magic_wand_cursor_marks_click_hotspot", ui_magic_wand_cursor_marks_click_hotspot},
      {"ui_spacebar_pan_works_from_layer_panel_but_not_text_entry",
       ui_spacebar_pan_works_from_layer_panel_but_not_text_entry},
      {"ui_move_tool_after_text_edit_keeps_spacebar_pan_active",
       ui_move_tool_after_text_edit_keeps_spacebar_pan_active},
      {"ui_spacebar_pan_works_while_child_dialog_focused",
       ui_spacebar_pan_works_while_child_dialog_focused},
  };
}
