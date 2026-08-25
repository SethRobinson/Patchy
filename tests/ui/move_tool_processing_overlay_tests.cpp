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
#include "local_psd_fixtures.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
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
#include <chrono>
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

struct ScopedSingleThreadedRender {
  ScopedSingleThreadedRender() {
#ifdef _WIN32
    _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "1");
#else
    setenv("PATCHY_RENDER_SINGLE_THREADED", "1", 1);
#endif
  }
  ~ScopedSingleThreadedRender() {
#ifdef _WIN32
    _putenv_s("PATCHY_RENDER_SINGLE_THREADED", "");
#else
    unsetenv("PATCHY_RENDER_SINGLE_THREADED");
#endif
  }
};

void ui_move_preview_preserves_layer_order() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 30));
  canvas->set_brush_size(30);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 110)),
       canvas->widget_position_for_document_point(QPoint(121, 110)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));

  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(70, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));
  save_widget_artifact("ui_move_preview_layer_order", window);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(70, 0), Qt::LeftButton, Qt::NoButton);
}

void ui_move_tool_moves_selected_layers_together() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(12);
  canvas->set_primary_color(QColor(230, 30, 30));
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(71, 70)));
  QApplication::processEvents();

  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 90, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 70)),
       canvas->widget_position_for_document_point(QPoint(141, 70)));
  QApplication::processEvents();

  auto* blue_layer = require_layer_item(*layer_list, QStringLiteral("Layer 3"));
  paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_layer);
  blue_layer->setSelected(true);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 20), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(30, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 90)), QColor(230, 30, 30), 70));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 90)), QColor(20, 90, 240), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(230, 30, 30), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(140, 70)), QColor(20, 90, 240), 70));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(blue_layer->isSelected());
  CHECK(paint_layer->isSelected());
  save_widget_artifact("ui_move_selected_layers", window);
}

void ui_move_auto_select_hover_outlines_with_multi_selection() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Hover Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Hover"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Hover Target"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(!target_item->isSelected());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(88, 57)), Qt::NoButton,
             Qt::NoButton);
  QApplication::processEvents();

  const auto image = canvas->grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas->widget_position_for_document_point(QPoint(80, 50)),
                               canvas->widget_position_for_document_point(QPoint(96, 64)));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 20);
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(!target_item->isSelected());
  save_widget_artifact("ui_move_auto_select_multi_hover", window);

  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(24, 24)), Qt::NoButton,
             Qt::NoButton);
  QApplication::processEvents();
  const auto selected_member_hover = canvas->grab().toImage();
  const QRect selected_member_outline(canvas->widget_position_for_document_point(QPoint(18, 18)),
                                      canvas->widget_position_for_document_point(QPoint(30, 30)));
  CHECK(count_pixels_close(selected_member_hover, selected_member_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) > 12);
}

void ui_move_auto_select_drag_replaces_multi_selection() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Auto Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Drag"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(88, 57));
  const auto end = canvas->widget_position_for_document_point(QPoint(108, 67));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(88, 57)), QColor(40, 180, 90), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(108, 67)), QColor(40, 180, 90), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Auto Target"));
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(!red_item->isSelected());
  CHECK(!blue_item->isSelected());
  CHECK(target_item->isSelected());
  CHECK(layer_list->currentItem() == target_item);
  save_widget_artifact("ui_move_auto_select_multi_drag", window);
}

void ui_move_auto_select_selected_member_drag_keeps_multi_selection() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Unselected Target",
                       solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{82, 18, 12, 12});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Selected Member"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Unselected Target"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(24, 24));
  const auto end = canvas->widget_position_for_document_point(QPoint(44, 34));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(44, 34)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(74, 34)), QColor(40, 90, 220), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(88, 24)), QColor(40, 180, 90), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  target_item = require_layer_item(*layer_list, QStringLiteral("Unselected Target"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(!target_item->isSelected());
  save_widget_artifact("ui_move_auto_select_selected_member_drag", window);
}

void ui_move_auto_select_blank_drag_keeps_multi_selection() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Multi Auto Select Blank"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(100, 70));
  const auto end = canvas->widget_position_for_document_point(QPoint(112, 82));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(36, 36)), QColor(220, 40, 40), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(66, 36)), QColor(40, 90, 220), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
}

void ui_move_ctrl_click_toggles_layer_selection() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Toggle Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Ctrl Toggle Selection"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);

  // Ctrl+click on an unselected layer adds it without moving any pixels.
  const auto target_point = canvas->widget_position_for_document_point(QPoint(88, 57));
  send_mouse(*canvas, QEvent::MouseButtonPress, target_point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, target_point, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Toggle Target"));
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(target_item->isSelected());
  CHECK(layer_list->currentItem() == target_item);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(88, 57)), QColor(40, 180, 90), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));

  // Ctrl+click on a selected layer removes it, promotes the active layer to a
  // remaining member, and moves nothing.
  send_mouse(*canvas, QEvent::MouseButtonPress, target_point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove,
             canvas->widget_position_for_document_point(QPoint(108, 67)), Qt::NoButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease,
             canvas->widget_position_for_document_point(QPoint(108, 67)), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  target_item = require_layer_item(*layer_list, QStringLiteral("Toggle Target"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(!target_item->isSelected());
  CHECK(layer_list->currentItem() != target_item);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(88, 57)), QColor(40, 180, 90), 40));

  // Ctrl+click that hits no layer leaves the selection alone and stays silent.
  window.statusBar()->clearMessage();
  const auto blank_point = canvas->widget_position_for_document_point(QPoint(120, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, blank_point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, blank_point, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(window.statusBar()->currentMessage().isEmpty());

  // Removing the only selected layer is a no-op.
  layer_list->clearSelection();
  auto* red_row = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  layer_list->setCurrentItem(red_row);
  red_row->setSelected(true);
  QApplication::processEvents();
  const auto red_point = canvas->widget_position_for_document_point(QPoint(24, 24));
  send_mouse(*canvas, QEvent::MouseButtonPress, red_point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, red_point, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  red_row = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  CHECK(layer_list->selectedItems().size() == 1);
  CHECK(red_row->isSelected());

  // The toggle is an explicit gesture: it works with Auto-Select off too.
  canvas->set_auto_select_layer(false);
  const auto blue_point = canvas->widget_position_for_document_point(QPoint(54, 24));
  send_mouse(*canvas, QEvent::MouseButtonPress, blue_point, Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, blue_point, Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();
  red_row = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_row = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(red_row->isSelected());
  CHECK(blue_row->isSelected());
  save_widget_artifact("ui_move_ctrl_click_toggles_selection", window);
}

void ui_move_ctrl_click_add_continues_into_drag() {
  patchy::Document document(140, 100, patchy::PixelFormat::rgba8());

  patchy::Layer red(document.allocate_layer_id(), "Selected Red",
                    solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  red.set_bounds(patchy::Rect{18, 18, 12, 12});
  document.add_layer(std::move(red));

  patchy::Layer blue(document.allocate_layer_id(), "Selected Blue",
                     solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  blue.set_bounds(patchy::Rect{48, 18, 12, 12});
  document.add_layer(std::move(blue));

  patchy::Layer target(document.allocate_layer_id(), "Drag Target",
                       solid_pixels(16, 14, patchy::PixelFormat::rgba8(), QColor(40, 180, 90, 255)));
  target.set_bounds(patchy::Rect{80, 50, 16, 14});
  document.add_layer(std::move(target));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Ctrl Toggle Drag"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_item);
  blue_item->setSelected(true);
  red_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);

  // A Ctrl+press that adds a layer keeps going as a drag of the whole
  // enlarged selection.
  const auto start = canvas->widget_position_for_document_point(QPoint(88, 57));
  const auto end = canvas->widget_position_for_document_point(QPoint(108, 67));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(220, 40, 40), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(40, 90, 220), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(44, 34)), QColor(220, 40, 40), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(74, 34)), QColor(40, 90, 220), 40));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(88, 57)), QColor(40, 180, 90), 40));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(108, 67)), QColor(40, 180, 90), 40));

  red_item = require_layer_item(*layer_list, QStringLiteral("Selected Red"));
  blue_item = require_layer_item(*layer_list, QStringLiteral("Selected Blue"));
  auto* target_item = require_layer_item(*layer_list, QStringLiteral("Drag Target"));
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(red_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(target_item->isSelected());
  CHECK(layer_list->currentItem() == target_item);
  save_widget_artifact("ui_move_ctrl_click_add_drag", window);
}

void ui_move_ctrl_click_selects_layer_inside_collapsed_folder() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(245, 245, 245, 255)));
  patchy::Layer group(document.allocate_layer_id(), "Collapsed Folder", patchy::LayerKind::Group);
  group.metadata()[patchy::kLayerMetadataGroupExpanded] = "false";
  auto child = patchy::Layer(document.allocate_layer_id(), "Hidden Child",
                             solid_pixels(12, 12, patchy::PixelFormat::rgba8(), QColor(40, 80, 220, 255)));
  child.set_bounds(patchy::Rect{12, 12, 12, 12});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Ctrl Toggle Collapsed"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hidden Child")) == nullptr);
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  QApplication::processEvents();

  background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Hidden Child"));
  CHECK(background_item->isSelected());
  CHECK(child_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(layer_list->currentItem() == child_item);
}

void ui_shift_constrains_move_tool_drag_to_axis() {
  patchy::Document document(120, 100, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 100, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Move Target",
                      solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{20, 20, 10, 10});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(480, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  auto* moved = document.find_layer(layer_id);
  CHECK(moved != nullptr);

  const auto first_start = canvas.widget_position_for_document_point(QPoint(24, 24));
  const auto first_end = canvas.widget_position_for_document_point(QPoint(59, 42));
  drag(canvas, first_start, first_end, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(moved->bounds().x == 55);
  CHECK(moved->bounds().y == 20);
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 24)), QColor(20, 90, 235), 35));
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 42)), QColor(Qt::white), 12));

  const auto second_start = canvas.widget_position_for_document_point(QPoint(59, 24));
  send_mouse(canvas, QEvent::MouseButtonPress, second_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(69, 28)), Qt::NoButton,
             Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(71, 54)), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(QPoint(71, 54)),
             Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();

  CHECK(moved->bounds().x == 55);
  CHECK(moved->bounds().y == 50);
  CHECK(color_close(canvas_pixel(canvas, QPoint(59, 54)), QColor(20, 90, 235), 35));
  CHECK(color_close(canvas_pixel(canvas, QPoint(71, 54)), QColor(Qt::white), 12));
}

void ui_move_tool_uses_opaque_bounds_for_transparent_layer() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(72, 42, 18, 12), QColor(20, 20, 20, 255));
  patchy::Layer small_layer(document.allocate_layer_id(), "Small Opaque", std::move(pixels));
  const auto small_layer_id = small_layer.id();
  small_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(small_layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({small_layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint document_delta(12, 8);
  const auto start = canvas.widget_position_for_document_point(QPoint(20, 20));
  const auto end = canvas.widget_position_for_document_point(QPoint(20, 20) + document_delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto image = canvas.grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(
      canvas.widget_position_for_document_point(QPoint(72, 42) + document_delta),
      canvas.widget_position_for_document_point(QPoint(72 + 18, 42 + 12) + document_delta));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 18);

  const QRect full_layer_top_edge(
      canvas.widget_position_for_document_point(QPoint(0, 0) + document_delta),
      canvas.widget_position_for_document_point(QPoint(180, 0) + document_delta));
  CHECK(count_pixels_close(image, full_layer_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);

  save_widget_artifact("ui_move_opaque_bounds", canvas);
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_move_preview_keeps_underlying_layers_steady_when_zoomed_out() {
  // Regression: at zoomed-out (mip-rendered) zoom levels the move-drag
  // preview drew its base image and dirty-rect patches with plain bilinear
  // scaling while the steady-state canvas renders from box-filtered mips, so
  // the artwork under the moving layer appeared to shift a pixel or two
  // inside every repainted dirty rect until the drag ended.
  patchy::Document document(512, 384, patchy::PixelFormat::rgba8());
  auto background = solid_pixels(512, 384, patchy::PixelFormat::rgba8(), QColor(Qt::white));
  // High-contrast single-pixel noise so any resampling phase difference is
  // far larger than the comparison tolerance.
  for (int y = 0; y < 384; ++y) {
    for (int x = 0; x < 512; ++x) {
      if ((x * 7 + y * 13) % 5 < 2) {
        fill_pixel_rect(background, QRect(x, y, 1, 1), QColor(20, 40, 60));
      }
    }
  }
  document.add_pixel_layer("Noisy Background", std::move(background));

  patchy::Layer layer(document.allocate_layer_id(), "Move Target",
                      solid_pixels(32, 32, patchy::PixelFormat::rgba8(), QColor(220, 60, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{64, 64, 32, 32});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(240, 200);
  canvas.set_document(&document);
  canvas.set_zoom(0.25);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const auto before = render_widget_image(canvas);

  const QPoint document_delta(60, 40);
  const auto start = canvas.widget_position_for_document_point(QPoint(80, 80));
  const auto end = canvas.widget_position_for_document_point(QPoint(80, 80) + document_delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto during = render_widget_image(canvas);
  save_widget_artifact("ui_move_preview_zoomed_out_steady", canvas);

  // Compare the background between the steady view and the live drag preview
  // everywhere except around the layer's old and new positions (padded for
  // mip-block alignment and the dashed move outline).
  const QRect old_doc_rect(64, 64, 32, 32);
  const QRect new_doc_rect = old_doc_rect.translated(document_delta);
  const auto excluded_widget_rect = [&canvas](QRect document_rect) {
    const auto padded = document_rect.adjusted(-10, -10, 10, 10);
    return QRect(canvas.widget_position_for_document_point(padded.topLeft()),
                 canvas.widget_position_for_document_point(padded.bottomRight() + QPoint(1, 1)))
        .adjusted(-2, -2, 2, 2);
  };
  const auto compare_outside_moved_rects = [&](const QImage& reference, const QImage& actual) {
    const QRect canvas_widget_rect(canvas.widget_position_for_document_point(QPoint(0, 0)),
                                   canvas.widget_position_for_document_point(QPoint(512, 384)));
    const auto old_excluded = excluded_widget_rect(old_doc_rect);
    const auto new_excluded = excluded_widget_rect(new_doc_rect);
    int mismatches = 0;
    for (int y = canvas_widget_rect.top(); y < canvas_widget_rect.bottom(); ++y) {
      for (int x = canvas_widget_rect.left(); x < canvas_widget_rect.right(); ++x) {
        const QPoint point(x, y);
        if (old_excluded.contains(point) || new_excluded.contains(point) || !reference.rect().contains(point)) {
          continue;
        }
        if (!color_close(reference.pixelColor(point), actual.pixelColor(point), 3)) {
          ++mismatches;
        }
      }
    }
    return mismatches;
  };
  CHECK(compare_outside_moved_rects(before, during) == 0);

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = render_widget_image(canvas);
  CHECK(compare_outside_moved_rects(before, after) == 0);
}

void ui_move_tool_hover_outlines_opaque_bounds() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(70, 40, 20, 14), QColor(25, 25, 25, 255));
  patchy::Layer hover_layer(document.allocate_layer_id(), "Hover Target", std::move(pixels));
  hover_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(hover_layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_auto_select_layer(true);
  canvas.set_show_transform_controls(false);
  canvas.show();
  QApplication::processEvents();

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto image = canvas.grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas.widget_position_for_document_point(QPoint(70, 40)),
                               canvas.widget_position_for_document_point(QPoint(90, 54)));
  CHECK(count_pixels_close(image, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) > 20);

  const QRect full_layer_top_edge(canvas.widget_position_for_document_point(QPoint(0, 0)),
                                  canvas.widget_position_for_document_point(QPoint(180, 0)));
  CHECK(count_pixels_close(image, full_layer_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);
  save_widget_artifact("ui_move_hover_opaque_bounds", canvas);

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(20, 20)), Qt::NoButton,
             Qt::NoButton);
  const auto cleared = canvas.grab().toImage();
  CHECK(count_pixels_close(cleared, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) < 6);
}

void ui_move_tool_uses_text_rect_for_hit_and_hover() {
  patchy::Document document(220, 140, patchy::PixelFormat::rgba8());
  auto& background =
      document.add_pixel_layer("Background", solid_pixels(220, 140, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto background_id = background.id();

  auto pixels = solid_pixels(120, 48, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(0, 0, 32, 18), QColor(25, 25, 25, 255));
  patchy::Layer text_layer(document.allocate_layer_id(), "Text: Wide Label", std::move(pixels));
  text_layer.set_bounds(patchy::Rect{40, 36, 120, 48});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Wide Label";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = "Arial";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));
  document.set_active_layer(background_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Text Rect"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Wide Label"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  CHECK(background_item->isSelected());
  CHECK(!text_item->isSelected());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  canvas->set_snap_enabled(false);

  const QPoint transparent_text_rect_point(138, 70);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(transparent_text_rect_point),
             Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();

  const auto hover_image = canvas->grab().toImage();
  const QColor outline_color(95, 170, 255);
  const QRect expected_text_outline(canvas->widget_position_for_document_point(QPoint(40, 36)),
                                    canvas->widget_position_for_document_point(QPoint(160, 84)));
  CHECK(count_pixels_close(hover_image, expected_text_outline.normalized().adjusted(-2, -2, 2, 2), outline_color,
                           18) > 30);
  const QRect background_top_edge(canvas->widget_position_for_document_point(QPoint(0, 0)),
                                  canvas->widget_position_for_document_point(QPoint(220, 0)));
  CHECK(count_pixels_close(hover_image, background_top_edge.normalized().adjusted(-2, -2, 2, 2), outline_color,
                           18) < 6);

  const QPoint delta(14, 9);
  drag(*canvas, canvas->widget_position_for_document_point(transparent_text_rect_point),
       canvas->widget_position_for_document_point(transparent_text_rect_point + delta));
  QApplication::processEvents();

  text_item = require_layer_item(*layer_list, QStringLiteral("Text: Wide Label"));
  background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  CHECK(text_item->isSelected());
  CHECK(!background_item->isSelected());
  const auto moved_text = canvas->active_layer_document_rect();
  CHECK(moved_text.has_value());
  CHECK(moved_text->topLeft() == QPoint(40, 36) + delta);
  CHECK(moved_text->size() == QSize(120, 48));
  save_widget_artifact("ui_move_text_rect_hit_hover", window);
}

void ui_move_transform_controls_do_not_block_auto_select_hover() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());
  auto& background =
      document.add_pixel_layer("Background", solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto background_id = background.id();

  auto pixels = solid_pixels(180, 120, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, QRect(70, 40, 20, 14), QColor(25, 25, 25, 255));
  patchy::Layer hover_layer(document.allocate_layer_id(), "Hover Target", std::move(pixels));
  hover_layer.set_bounds(patchy::Rect{0, 0, 180, 120});
  document.add_layer(std::move(hover_layer));
  document.set_active_layer(background_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 360);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_auto_select_layer(true);
  canvas.set_show_transform_controls(true);
  canvas.show();
  QApplication::processEvents();

  const QColor outline_color(95, 170, 255);
  const QRect expected_outline(canvas.widget_position_for_document_point(QPoint(70, 40)),
                               canvas.widget_position_for_document_point(QPoint(90, 54)));
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto highlighted = canvas.grab().toImage();
  CHECK(count_pixels_close(highlighted, expected_outline.normalized().adjusted(-2, -2, 2, 2), outline_color, 18) >
        20);

  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(20, 20)), Qt::NoButton,
             Qt::NoButton);
  const auto active_background_hover = canvas.grab().toImage();
  CHECK(count_pixels_close(active_background_hover, expected_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) < 6);

  canvas.set_auto_select_layer(false);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(QPoint(75, 45)), Qt::NoButton,
             Qt::NoButton);
  const auto auto_select_disabled = canvas.grab().toImage();
  CHECK(count_pixels_close(auto_select_disabled, expected_outline.normalized().adjusted(-2, -2, 2, 2),
                           outline_color, 18) < 6);
}

void ui_move_transform_handles_drag_past_canvas_edge() {
  // A layer larger than the document leaves its Move-tool transform handles
  // hanging outside the canvas. Pressing such an off-canvas handle must still
  // begin a transform drag. Previously the document-bounds guard in the press
  // handler discarded the event, so the handles showed the resize cursor on
  // hover but could not actually be grabbed once they passed the canvas edge.
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background",
                           solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer oversized(document.allocate_layer_id(), "Oversized",
                          solid_pixels(200, 160, patchy::PixelFormat::rgba8(), QColor(40, 120, 220)));
  oversized.set_bounds(patchy::Rect{-40, -30, 200, 160});  // extends past every canvas edge
  const auto oversized_id = oversized.id();
  document.add_layer(std::move(oversized));
  document.set_active_layer(oversized_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(640, 480);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(true);
  canvas.set_selected_layer_ids({oversized_id});
  canvas.show();
  QApplication::processEvents();

  // Bottom-right resize handle lives at document (160, 130) — past the 120x90
  // canvas on both axes.
  const auto handle_point = canvas.widget_position_for_document_point(QPoint(160, 130));

  // Hovering an off-canvas handle still shows the resize cursor.
  send_mouse(canvas, QEvent::MouseMove, handle_point, Qt::NoButton, Qt::NoButton);
  CHECK(canvas.cursor().shape() == Qt::SizeFDiagCursor);

  // Pressing it must start a transform drag rather than being discarded.
  CHECK(!canvas.free_transform_active());
  send_mouse(canvas, QEvent::MouseButtonPress, handle_point, Qt::LeftButton, Qt::LeftButton);
  CHECK(canvas.free_transform_active());

  send_mouse(canvas, QEvent::MouseButtonRelease, handle_point, Qt::LeftButton, Qt::NoButton);
}

void ui_move_tool_moves_selected_folder_tree() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Move Folder", patchy::LayerKind::Group);
  auto red = patchy::Layer(document.allocate_layer_id(), "Red Child",
                              solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(patchy::Rect{20, 20, 10, 10});
  folder.add_child(std::move(red));

  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Move Folder", patchy::LayerKind::Group);
  auto blue = patchy::Layer(document.allocate_layer_id(), "Blue Grandchild",
                               solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  blue.set_bounds(patchy::Rect{50, 20, 10, 10});
  nested_folder.add_child(std::move(blue));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Folder Tree"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(230, 30, 30), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(20, 90, 240), 20));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Move Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 1);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(80, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(18, 12), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(18, 12), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(42, 36)), QColor(230, 30, 30), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 36)), QColor(20, 90, 240), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(Qt::white), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(Qt::white), 12));
  CHECK(folder_item->isSelected());
  save_widget_artifact("ui_move_selected_folder_tree", window);
}

void ui_move_tool_moves_selected_masked_folder_tree() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Masked Folder", patchy::LayerKind::Group);
  auto red = patchy::Layer(document.allocate_layer_id(), "Masked Red",
                           solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(patchy::Rect{20, 20, 10, 10});
  patchy::PixelBuffer red_mask(10, 10, patchy::PixelFormat::gray8());
  red_mask.clear(255);
  red.set_mask(patchy::LayerMask{patchy::Rect{20, 20, 10, 10}, std::move(red_mask), 0, false});
  folder.add_child(std::move(red));

  patchy::Layer nested_folder(document.allocate_layer_id(), "Nested Masked Folder", patchy::LayerKind::Group);
  auto blue = patchy::Layer(document.allocate_layer_id(), "Masked Blue",
                            solid_pixels(10, 10, patchy::PixelFormat::rgba8(), QColor(20, 90, 240)));
  blue.set_bounds(patchy::Rect{50, 20, 10, 10});
  patchy::PixelBuffer blue_mask(10, 10, patchy::PixelFormat::gray8());
  blue_mask.clear(255);
  blue.set_mask(patchy::LayerMask{patchy::Rect{50, 20, 10, 10}, std::move(blue_mask), 0, false});
  nested_folder.add_child(std::move(blue));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Masked Folder Tree"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Masked Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(80, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(18, 12), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(18, 12), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(42, 36)), QColor(230, 30, 30), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 36)), QColor(20, 90, 240), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(Qt::white), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(Qt::white), 12));
  save_widget_artifact("ui_move_selected_masked_folder_tree", window);
}

void ui_move_preview_clears_transparent_trails_and_keeps_layer_styles() {
  patchy::Document document(180, 120, patchy::PixelFormat::rgba8());

  patchy::PixelBuffer gradient_pixels(16, 16, patchy::PixelFormat::rgba8());
  gradient_pixels.clear(0);
  for (std::int32_t y = 0; y < gradient_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < gradient_pixels.width(); ++x) {
      auto* px = gradient_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  patchy::Layer gradient_layer(document.allocate_layer_id(), "Gradient Move", std::move(gradient_pixels));
  gradient_layer.set_bounds(patchy::Rect{20, 30, 16, 16});
  patchy::LayerGradientFill gradient_fill;
  gradient_fill.enabled = true;
  gradient_fill.blend_mode = patchy::BlendMode::Normal;
  gradient_fill.opacity = 1.0F;
  gradient_fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{30, 210, 80}});
  gradient_fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{30, 210, 80}});
  gradient_layer.layer_style().gradient_fills.push_back(gradient_fill);
  document.add_layer(std::move(gradient_layer));

  patchy::PixelBuffer color_pixels(16, 16, patchy::PixelFormat::rgba8());
  color_pixels.clear(0);
  for (std::int32_t y = 0; y < color_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < color_pixels.width(); ++x) {
      auto* px = color_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  patchy::Layer color_layer(document.allocate_layer_id(), "Color Move", std::move(color_pixels));
  color_layer.set_bounds(patchy::Rect{20, 60, 16, 16});
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = patchy::BlendMode::Normal;
  overlay.color = patchy::RgbColor{40, 90, 235};
  overlay.opacity = 1.0F;
  color_layer.layer_style().color_overlays.push_back(overlay);
  document.add_layer(std::move(color_layer));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Style Cache"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* gradient_item = require_layer_item(*layer_list, QStringLiteral("Gradient Move"));
  auto* color_item = require_layer_item(*layer_list, QStringLiteral("Color Move"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(color_item);
  color_item->setSelected(true);
  gradient_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(24, 34));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));

  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 34)), QColor(210, 20, 20), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));
  save_widget_artifact("ui_move_preview_style_cache", window);
}

void ui_move_preview_leaves_no_trail_when_zoomed_out() {
  patchy::Document document(240, 180, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(240, 180, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Zoomed Move",
                      solid_pixels(60, 60, patchy::PixelFormat::rgba8(), QColor(255, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{40, 50, 60, 60});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  // Zoom < 1.0 exercises the smooth-downscaled display path, where the moving
  // layer would otherwise bleed past its bounds in the base image and leave a
  // residual outline at the drag-start position.
  canvas.set_zoom(0.37);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  // Drag the layer far enough that its original footprint no longer overlaps
  // its destination. Once the layer has moved away, its original location must
  // show only the (white) background: no residual layer pixels and no faint
  // rectangular seam where the original bounds used to be.
  const QPoint origin(70, 80);
  const QPoint move_delta(90, 60);
  const auto start = canvas.widget_position_for_document_point(origin);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, canvas.widget_position_for_document_point(origin + move_delta), Qt::NoButton,
             Qt::LeftButton);
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  // grab() honours the device pixel ratio, so convert logical widget points to
  // device pixels before sampling (otherwise HiDPI runs sample the wrong spot).
  const auto dpr = preview.devicePixelRatio();
  const auto device_point = [dpr](QPoint widget_point) {
    return QPoint(static_cast<int>(std::lround(widget_point.x() * dpr)),
                  static_cast<int>(std::lround(widget_point.y() * dpr)));
  };
  int trail_pixels = 0;
  const QRect original_region(40 - 3, 50 - 3, 60 + 6, 60 + 6);
  for (int y = original_region.top(); y <= original_region.bottom(); ++y) {
    for (int x = original_region.left(); x <= original_region.right(); ++x) {
      const auto sample = device_point(canvas.widget_position_for_document_point(QPoint(x, y)));
      if (!preview.rect().contains(sample)) {
        continue;
      }
      if (!color_close(preview.pixelColor(sample), QColor(Qt::white), 24)) {
        ++trail_pixels;
      }
    }
  }
  if (trail_pixels != 0) {
    ensure_artifact_dir();
    CHECK(preview.save(QStringLiteral("test-artifacts/ui_move_preview_zoomed_ghost.png")));
    std::cerr << "ui_move_preview_leaves_no_trail_when_zoomed_out trail_pixels=" << trail_pixels << '\n';
  }
  CHECK(trail_pixels == 0);

  // The moved layer should still be visible at its destination.
  const auto destination_sample = device_point(canvas.widget_position_for_document_point(origin + move_delta));
  CHECK(color_close(preview.pixelColor(destination_sample), QColor(255, 40, 40), 60));

  send_mouse(canvas, QEvent::MouseButtonRelease, canvas.widget_position_for_document_point(origin + move_delta),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

void ui_move_preview_mid_drag_partial_repaint_matches_full_preview() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::PixelBuffer pixels(64, 46, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  fill_pixel_rect(pixels, QRect(10, 8, 42, 27), QColor(35, 105, 225, 235));
  fill_pixel_rect(pixels, QRect(20, 17, 18, 10), QColor(240, 80, 45, 230));
  patchy::Layer layer(document.allocate_layer_id(), "Mid Drag Preview", std::move(pixels));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{54, 45, 64, 46});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 8.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.5F;
  shadow.color = patchy::RgbColor{0, 0, 0};
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 5.0F;
  glow.opacity = 0.35F;
  glow.color = patchy::RgbColor{255, 225, 80};
  layer.layer_style().outer_glows.push_back(glow);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(500, 360);
  canvas.set_document(&document);
  canvas.set_zoom(1.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  PaintRegionRecorder recorder(&canvas);
  canvas.installEventFilter(&recorder);
  auto render_without_recording = [&]() {
    recorder.set_recording(false);
    auto image = render_widget_image(canvas);
    recorder.set_recording(true);
    return image;
  };

  const auto start = canvas.widget_position_for_document_point(QPoint(76, 62));
  const QPoint first_delta(34, 16);
  const QPoint second_delta(62, 31);

  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  recorder.reset();
  send_mouse(canvas, QEvent::MouseMove,
             canvas.widget_position_for_document_point(QPoint(76, 62) + first_delta), Qt::NoButton, Qt::LeftButton);
  auto first_region = recorder.region();
  CHECK(!first_region.isEmpty());

  recorder.reset();
  send_mouse(canvas, QEvent::MouseMove,
             canvas.widget_position_for_document_point(QPoint(76, 62) + second_delta), Qt::NoButton, Qt::LeftButton);
  auto second_region = recorder.region();
  CHECK(!second_region.isEmpty());
  const auto original_probe = canvas.widget_position_for_document_point(QPoint(54, 45));
  CHECK(second_region.contains(original_probe));
  const auto backing = grab_widget_window_image(canvas);
  const auto full_mid_drag = render_without_recording();
  const auto matches_full_mid_drag = images_equal_rgba(backing, full_mid_drag);
  if (!matches_full_mid_drag) {
    ensure_artifact_dir();
    CHECK(backing.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_backing.png")));
    CHECK(full_mid_drag.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_full.png")));
    if (const auto mismatch = image_mismatch_bounds_rgba(backing, full_mid_drag); mismatch.has_value()) {
      std::cerr << "ui_move_preview_mid_drag_partial_repaint mismatch bounds "
                << mismatch->x() << "," << mismatch->y() << "," << mismatch->width() << ","
                << mismatch->height() << '\n';
    }
  }
  CHECK(matches_full_mid_drag);

  send_mouse(canvas, QEvent::MouseButtonRelease,
             canvas.widget_position_for_document_point(QPoint(76, 62) + second_delta), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  ensure_artifact_dir();
  CHECK(backing.save(QStringLiteral("test-artifacts/ui_move_preview_mid_drag_partial_repaint.png")));
}

void ui_dirty_region_move_preview_matches_force_refresh() {
  patchy::Document document(180, 130, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(180, 130, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::PixelBuffer pixels(44, 34, patchy::PixelFormat::rgba8());
  pixels.clear(0);
  fill_pixel_rect(pixels, QRect(8, 7, 25, 18), QColor(20, 90, 235, 230));
  patchy::Layer layer(document.allocate_layer_id(), "Styled Transparent Move", std::move(pixels));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{42, 38, 44, 34});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 7.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.55F;
  shadow.color = patchy::RgbColor{0, 0, 0};
  layer.layer_style().drop_shadows.push_back(shadow);
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 6.0F;
  glow.opacity = 0.45F;
  glow.color = patchy::RgbColor{255, 220, 80};
  layer.layer_style().outer_glows.push_back(glow);
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.opacity = 1.0F;
  stroke.color = patchy::RgbColor{30, 30, 35};
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 390);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(24, 10);
  const auto start = canvas.widget_position_for_document_point(QPoint(55, 50));
  const auto end = canvas.widget_position_for_document_point(QPoint(55, 50) + delta);
  const auto before = canvas.render_cache_diagnostics();
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move = canvas.render_cache_diagnostics();
  CHECK(after_move.full_refreshes == before.full_refreshes);
  CHECK(after_move.move_precommit_patches == before.move_precommit_patches + 1);
  CHECK(after_move.move_preview_patch_reuses == before.move_preview_patch_reuses + 1);

  const auto dirty_rendered = canvas.grab().toImage();
  canvas.force_refresh();
  QApplication::processEvents();
  const auto forced = canvas.grab().toImage();
  CHECK(images_equal_rgba(dirty_rendered, forced));
  CHECK(color_close(canvas_pixel(canvas, QPoint(55, 50)), QColor(Qt::white), 12));
  CHECK(!color_close(canvas_pixel(canvas, QPoint(55, 50) + delta), QColor(Qt::white), 20));
  save_widget_artifact("ui_dirty_region_move_preview_force_refresh", canvas);
}

void ui_processing_overlay_animates_for_slow_dirty_render() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Nudge Me",
                      solid_pixels(24, 22, patchy::PixelFormat::rgba8(), QColor(230, 40, 35, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{30, 28, 24, 22});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_test_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("260"));

  const auto before = canvas.render_cache_diagnostics();
  send_key(canvas, Qt::Key_Right);
  const auto after = canvas.render_cache_diagnostics();

  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(after.processing_overlay_frames > before.processing_overlay_frames);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(31, 39)), QColor(230, 40, 35), 3));
  CHECK(color_close(canvas_pixel(canvas, QPoint(30, 39)), QColor(Qt::white), 3));
}

void ui_processing_overlay_stays_top_aligned_without_dimming_canvas() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(),
                                                      QColor(88, 196, 128)));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(420, 320);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  const auto baseline = canvas.grab().toImage();
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  canvas.begin_processing_operation();
  canvas.tick_processing_operation();
  QApplication::processEvents();

  CHECK(canvas.processing_overlay_visible());
  const auto with_overlay = canvas.grab().toImage();
  const auto mismatch = image_mismatch_bounds_rgba(baseline, with_overlay);
  CHECK(mismatch.has_value());
  CHECK(mismatch->top() <= 24);
  CHECK(mismatch->bottom() < 100);

  const auto lower_document_sample = canvas.widget_position_for_document_point(QPoint(80, 100));
  CHECK(with_overlay.rect().contains(lower_document_sample));
  CHECK(color_close(with_overlay.pixelColor(lower_document_sample),
                    baseline.pixelColor(lower_document_sample), 0));

  canvas.end_processing_operation();
  QApplication::processEvents();
  CHECK(!canvas.processing_overlay_visible());
}

void ui_brush_family_strokes_do_not_trigger_processing_overlay() {
  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto exercise_tool = [](patchy::ui::CanvasTool tool) {
    patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
    auto& layer = document.add_pixel_layer("Paint", solid_pixels(120, 90, patchy::PixelFormat::rgba8(),
                                                                 QColor(Qt::white)));
    document.set_active_layer(layer.id());

    patchy::ui::CanvasWidget canvas;
    canvas.resize(360, 260);
    canvas.set_document(&document);
    canvas.set_zoom(2.0);
    canvas.set_tool(tool);
    canvas.set_primary_color(QColor(20, 20, 20));
    canvas.set_brush_size(12);
    canvas.set_brush_opacity(100);
    canvas.set_brush_softness(20);
    canvas.show();
    QApplication::processEvents();
    canvas.force_refresh();
    QApplication::processEvents();

    if (tool == patchy::ui::CanvasTool::Clone) {
      const auto source = canvas.widget_position_for_document_point(QPoint(28, 28));
      send_mouse(canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
      send_mouse(canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
      QApplication::processEvents();
    }

    const auto before = canvas.render_cache_diagnostics();
    const auto start = canvas.widget_position_for_document_point(QPoint(40, 44));
    const auto end = canvas.widget_position_for_document_point(QPoint(76, 44));
    send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    CHECK(!canvas.processing_operation_active());
    CHECK(!canvas.processing_overlay_visible());

    send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    CHECK(!canvas.processing_operation_active());
    CHECK(!canvas.processing_overlay_visible());

    send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    const auto after = canvas.render_cache_diagnostics();
    CHECK(after.processing_overlays_shown == before.processing_overlays_shown);
    CHECK(!canvas.processing_overlay_visible());
  };

  exercise_tool(patchy::ui::CanvasTool::Brush);
  exercise_tool(patchy::ui::CanvasTool::Eraser);
  exercise_tool(patchy::ui::CanvasTool::Smudge);
  exercise_tool(patchy::ui::CanvasTool::Clone);
}

void ui_processing_overlay_animates_for_slow_nudge_undo_snapshot() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(96, 72, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Nudge Snapshot",
                      solid_pixels(18, 16, patchy::PixelFormat::rgba8(), QColor(35, 185, 90, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{24, 22, 18, 16});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Nudge Snapshot Processing"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_undo_delay("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", QByteArray("240"));

  const auto before = canvas->render_cache_diagnostics();
  send_key(*canvas, Qt::Key_Right);
  const auto after = canvas->render_cache_diagnostics();

  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(after.processing_overlay_frames > before.processing_overlay_frames);
  CHECK(!canvas->processing_overlay_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(25, 30)), QColor(35, 185, 90), 3));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 30)), QColor(Qt::white), 3));
}

void ui_processing_overlay_is_visible_before_slow_move_commit_callback() {
  patchy::Document document(120, 90, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Commit Wait",
                      solid_pixels(28, 24, patchy::PixelFormat::rgba8(), QColor(45, 130, 230, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{32, 30, 28, 24});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(360, 260);
  canvas.set_document(&document);
  canvas.set_zoom(2.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  bool saw_processing_during_commit_callback = false;
  canvas.set_before_edit_callback([&](QString) {
    saw_processing_during_commit_callback = canvas.processing_overlay_visible();
  });

  const auto before = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(40, 38));
  const auto end = canvas.widget_position_for_document_point(QPoint(46, 38));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = canvas.render_cache_diagnostics();

  CHECK(saw_processing_during_commit_callback);
  CHECK(after.processing_overlays_shown == before.processing_overlays_shown + 1);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(64, 38)), QColor(45, 130, 230), 3));
  CHECK(color_close(canvas_pixel(canvas, QPoint(34, 38)), QColor(Qt::white), 3));
}

void ui_processing_overlay_ticks_during_filter_apply() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(30, 120, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto before = canvas->render_cache_diagnostics();
  accept_filter_dialog({{QStringLiteral("filterStrengthSpin"), 150}});
  require_action(window, "filterAction_patchy_filters_edge_detect")->trigger();
  QApplication::processEvents();
  const auto after = canvas->render_cache_diagnostics();

  CHECK(after.processing_overlays_shown > before.processing_overlays_shown);
  CHECK(!canvas->processing_overlay_visible());
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(30, 120, 220), 8));
}

void ui_processing_overlay_ticks_during_fill_tool_loop() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Fill Target",
                      solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto layer_id = layer.id();
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::CanvasWidget canvas;
  canvas.resize(380, 300);
  canvas.set_document(&document);
  canvas.set_zoom(1.5);
  canvas.set_tool(patchy::ui::CanvasTool::Fill);
  canvas.set_primary_color(QColor(210, 45, 80));
  canvas.show();
  QApplication::processEvents();
  canvas.force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));

  const auto before = canvas.render_cache_diagnostics();
  const auto click = canvas.widget_position_for_document_point(QPoint(40, 40));
  send_mouse(canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after = canvas.render_cache_diagnostics();

  CHECK(after.processing_overlays_shown > before.processing_overlays_shown);
  CHECK(!canvas.processing_overlay_visible());
  CHECK(color_close(canvas_pixel(canvas, QPoint(40, 40)), QColor(210, 45, 80), 3));
}

void ui_layer_style_cache_invalidates_after_pixel_mutation() {
  patchy::Document document(80, 60, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Cached Stroke",
                      solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(220, 40, 40, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{20, 15, 20, 20});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  const auto first = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(first.pixelColor(30, 25), QColor(220, 40, 40), 2));
  const auto cached = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(cached.pixelColor(30, 25), QColor(220, 40, 40), 2));

  auto* editable_layer = document.find_layer(layer_id);
  CHECK(editable_layer != nullptr);
  auto* center = editable_layer->pixels().pixel(10, 10);
  center[0] = 35;
  center[1] = 95;
  center[2] = 235;
  center[3] = 255;

  const auto updated = patchy::ui::qimage_from_document(document, true);
  CHECK(color_close(updated.pixelColor(30, 25), QColor(35, 95, 235), 2));
  CHECK(color_close(updated.pixelColor(18, 15), QColor(40, 180, 80), 2));
}

void ui_move_expensive_styled_layer_uses_proxy_until_release() {
  patchy::Document document(1500, 1300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1500, 1300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Large Styled Move",
                      solid_pixels(1000, 1000, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 100, 1000, 1000});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(300, 0);
  const QPoint old_only_point(150, 500);
  const QPoint moved_only_point(1250, 500);
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(20, 90, 235), 45));
  const auto before_release_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(150, 150));
  const auto end = canvas.widget_position_for_document_point(QPoint(150, 150) + delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  // The proxy latch shows the actual content translating: the vacated area
  // reads from the base cache (white) and the new position blits the snapshot.
  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.move_proxy_previews == before_release_stats.move_proxy_previews + 1);
  CHECK(mid_drag_stats.move_outline_previews == before_release_stats.move_outline_previews);
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(Qt::white), 45));
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(20, 90, 235), 45));

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_release_stats = canvas.render_cache_diagnostics();
  CHECK(after_release_stats.full_refreshes == before_release_stats.full_refreshes);
  CHECK(after_release_stats.move_precommit_patches == before_release_stats.move_precommit_patches + 1);
  CHECK(after_release_stats.move_preview_patch_reuses == before_release_stats.move_preview_patch_reuses);
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(Qt::white), 45));
  save_widget_artifact("ui_move_expensive_style_proxy", canvas);
}

// A style on the dragged FOLDER (not on any leaf) must count as expensive:
// the folder's silhouette and exterior effects re-render for every preview
// patch, so the styled proxy threshold has to see it even though the move
// machinery flattens the folder to its leaves.
void ui_move_styled_folder_drag_uses_proxy_preview() {
  patchy::Document document(1500, 1300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1500, 1300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer folder(document.allocate_layer_id(), "Styled Folder", patchy::LayerKind::Group);
  const auto folder_id = folder.id();
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 0.0F;
  shadow.size = 8.0F;
  folder.layer_style().drop_shadows.push_back(shadow);
  patchy::Layer child(document.allocate_layer_id(), "Plain Child",
                      solid_pixels(1000, 1000, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  child.set_bounds(patchy::Rect{100, 100, 1000, 1000});
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({folder_id});
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(300, 0);
  const QPoint old_only_point(150, 500);
  const QPoint moved_only_point(1250, 500);
  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(150, 150));
  const auto end = canvas.widget_position_for_document_point(QPoint(150, 150) + delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  // Latching proves the ancestor detection; the blit shows the moved content.
  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.move_proxy_previews == before_stats.move_proxy_previews + 1);
  CHECK(mid_drag_stats.move_outline_previews == before_stats.move_outline_previews);
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(Qt::white), 45));

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, moved_only_point), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, old_only_point), QColor(Qt::white), 45));
  save_widget_artifact("ui_move_styled_folder_proxy", canvas);
}

// Dragging a stack of overlapping layers costs one composite per layer per
// preview patch, so the proxy threshold sums the per-layer areas instead of
// taking their (small) shared bounding box.
void ui_move_overlapping_stack_drag_uses_proxy_preview() {
  patchy::Document document(1300, 950, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1300, 950, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  std::vector<patchy::LayerId> stack_ids;
  for (int index = 0; index < 6; ++index) {
    patchy::Layer layer(document.allocate_layer_id(), "Stack Layer",
                        solid_pixels(900, 800, patchy::PixelFormat::rgba8(), QColor(200, 60, 40)));
    layer.set_bounds(patchy::Rect{150, 80, 900, 800});
    stack_ids.push_back(layer.id());
    document.add_layer(std::move(layer));
  }

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids(stack_ids);
  canvas.show();
  QApplication::processEvents();

  const QPoint delta(220, 0);
  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(200, 200));
  const auto end = canvas.widget_position_for_document_point(QPoint(200, 200) + delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.move_proxy_previews == before_stats.move_proxy_previews + 1);
  CHECK(mid_drag_stats.move_outline_previews == before_stats.move_outline_previews);
  CHECK(color_close(canvas_pixel(canvas, QPoint(1200, 400)), QColor(200, 60, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(200, 400)), QColor(Qt::white), 45));

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, QPoint(1200, 400)), QColor(200, 60, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(200, 400)), QColor(Qt::white), 45));
  save_widget_artifact("ui_move_overlapping_stack_proxy", canvas);
}

// A live (sub-threshold) drag of a styled folder must repaint the folder's
// shadow spill around the vacated position: the preview/commit patch regions
// pad each leaf by its styled ancestors' SUMMED padding (the outer group's
// shadow blurs the inner group's already-shadowed silhouette, so the spill
// reaches farther than either style alone).
void ui_move_styled_folder_live_preview_clears_shadow_trail() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer outer(document.allocate_layer_id(), "Outer Styled Folder", patchy::LayerKind::Group);
  const auto outer_id = outer.id();
  patchy::LayerDropShadow outer_shadow;
  outer_shadow.enabled = true;
  outer_shadow.opacity = 1.0F;
  outer_shadow.distance = 0.0F;
  outer_shadow.spread = 0.8F;
  outer_shadow.size = 12.0F;
  outer.layer_style().drop_shadows.push_back(outer_shadow);

  patchy::Layer inner(document.allocate_layer_id(), "Inner Styled Folder", patchy::LayerKind::Group);
  patchy::LayerDropShadow inner_shadow;
  inner_shadow.enabled = true;
  inner_shadow.opacity = 1.0F;
  inner_shadow.angle_degrees = 180.0F;  // hard offset straight to the +x side
  inner_shadow.distance = 10.0F;
  inner_shadow.spread = 0.8F;
  inner_shadow.size = 2.0F;
  inner.layer_style().drop_shadows.push_back(inner_shadow);

  patchy::Layer child(document.allocate_layer_id(), "Shadowed Child",
                      solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(230, 30, 30)));
  child.set_bounds(patchy::Rect{60, 70, 24, 24});
  inner.add_child(std::move(child));
  outer.add_child(std::move(inner));
  document.add_layer(std::move(outer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({outer_id});
  canvas.show();
  QApplication::processEvents();

  // Old child right edge is x=84: the inner folder's hard offset shadow covers
  // roughly x=70..96 there, well outside the child's own (style-less) bounds,
  // so both probes start dark and turn stale if the patch region ignores the
  // styled ancestors' padding.
  const QPoint near_trail_point(92, 82);
  const QPoint far_trail_point(95, 82);
  CHECK(!color_close(canvas_pixel(canvas, near_trail_point), QColor(Qt::white), 40));
  CHECK(!color_close(canvas_pixel(canvas, far_trail_point), QColor(Qt::white), 40));

  const QPoint delta(80, 0);
  const auto start = canvas.widget_position_for_document_point(QPoint(70, 80));
  const auto end = canvas.widget_position_for_document_point(QPoint(70, 80) + delta);
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.move_outline_previews == 0);
  CHECK(mid_drag_stats.move_proxy_previews == 0);
  CHECK(color_close(canvas_pixel(canvas, near_trail_point), QColor(Qt::white), 20));
  CHECK(color_close(canvas_pixel(canvas, far_trail_point), QColor(Qt::white), 20));

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, near_trail_point), QColor(Qt::white), 20));
  CHECK(color_close(canvas_pixel(canvas, far_trail_point), QColor(Qt::white), 20));
  // The shadow travelled with the folder: probe the same offset from the new
  // child right edge (x=164).
  CHECK(!color_close(canvas_pixel(canvas, QPoint(172, 82)), QColor(Qt::white), 40));
  save_widget_artifact("ui_move_styled_folder_shadow_trail", canvas);
}

// The area gate only prices the moving layers, so a cheap layer dragged
// across an expensive stack stays live no matter how slow the frames are; the
// time escape hatch latches the proxy after a slow live frame. The env
// override (0) makes any live frame count as slow so the test is
// deterministic on every machine.
void ui_move_slow_live_frame_latches_proxy() {
  EnvironmentVariableRestorer restore_latch("PATCHY_MOVE_LIVE_LATCH_MS");
  qputenv("PATCHY_MOVE_LIVE_LATCH_MS", QByteArray("0"));

  patchy::Document document(300, 200, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(300, 200, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Slow Frame Move",
                      solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{30, 40, 40, 40});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(50, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  // First move renders a live frame (which the zero threshold marks slow);
  // the second move must latch the proxy.
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(80, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 1);
  CHECK(canvas.render_cache_diagnostics().move_outline_previews == before_stats.move_outline_previews);

  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(80, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, QPoint(120, 60)), QColor(220, 40, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(40, 60)), QColor(Qt::white), 20));
}

// At zoom <= 50% the live move preview composites from the preview-scaled
// document (display-resolution compositing): the counter latches once per
// drag, the preview shows the moved content, and the release renders full-res
// with no trail.
void ui_move_scaled_preview_composites_at_display_resolution() {
  patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(800, 600, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Scaled Move",
                      solid_pixels(120, 100, patchy::PixelFormat::rgba8(), QColor(210, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 80, 120, 100});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(160, 130));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(50, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  // (widget +50 at zoom 0.5 = +100 document px.)
  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.move_scaled_previews == before_stats.move_scaled_previews + 1);
  CHECK(mid_drag_stats.move_proxy_previews == before_stats.move_proxy_previews);
  CHECK(mid_drag_stats.move_outline_previews == before_stats.move_outline_previews);
  CHECK(color_close(canvas_pixel(canvas, QPoint(280, 130)), QColor(210, 40, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(140, 130)), QColor(Qt::white), 30));

  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_stats = canvas.render_cache_diagnostics();
  CHECK(after_stats.move_scaled_previews == before_stats.move_scaled_previews + 1);
  // Release is full-res accurate: moved content present, vacated area clean.
  CHECK(color_close(canvas_pixel(canvas, QPoint(280, 130)), QColor(210, 40, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(140, 130)), QColor(Qt::white), 15));
  save_widget_artifact("ui_move_scaled_preview", canvas);
}

// A committed move whose release patches the render cache keeps the
// preview-scaled document alive. Its copies of the moved layers must be
// refitted to the committed positions: the next drag's proxy snapshot renders
// them WITHOUT bounds overrides, so stale positions made the moving content
// vanish (the snapshot rect derives from the real document's new positions
// while the stale copy still sat at the old ones).
void ui_move_scaled_proxy_after_commit_shows_moved_content() {
  EnvironmentVariableRestorer restore_latch("PATCHY_MOVE_LIVE_LATCH_MS");
  qputenv("PATCHY_MOVE_LIVE_LATCH_MS", QByteArray("0"));

  patchy::Document document(800, 600, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(800, 600, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Committed Proxy Move",
                      solid_pixels(120, 100, patchy::PixelFormat::rgba8(), QColor(210, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 80, 120, 100});
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  // Drag 1: the zero latch threshold marks the first live frame slow, the
  // second move latches the proxy, and the release patches the cache
  // (keeping the scaled document). Commit +100 document px: layer 200..320.
  const auto start = canvas.widget_position_for_document_point(QPoint(160, 130));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(50, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, QPoint(260, 130)), QColor(210, 40, 40), 45));

  // Drag 2 of the same layer, another +100 document px: the proxy frame must
  // show the moving content at its dragged position (300..420 mid-drag).
  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start2 = canvas.widget_position_for_document_point(QPoint(260, 130));
  send_mouse(canvas, QEvent::MouseButtonPress, start2, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start2 + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseMove, start2 + QPoint(50, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 1);
  CHECK(color_close(canvas_pixel(canvas, QPoint(360, 130)), QColor(210, 40, 40), 45));
  // Vacated origin repaints from the base, not a stale copy.
  CHECK(color_close(canvas_pixel(canvas, QPoint(220, 130)), QColor(Qt::white), 30));

  send_mouse(canvas, QEvent::MouseButtonRelease, start2 + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, QPoint(360, 130)), QColor(210, 40, 40), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(220, 130)), QColor(Qt::white), 15));
}

// Starting a move drag with a dirty render cache must not synchronously
// recomposite the whole document inside the mouse handler (that was a
// multi-second stall on heavy PSDs whose commit invalidated the cache): the
// proxy latch builds its base from the preview machinery instead, and the
// full refresh runs after release.
void ui_move_drag_with_dirty_render_cache_skips_sync_composite() {
  patchy::Document document(1500, 1300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1500, 1300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Dirty Cache Move",
                      solid_pixels(1000, 1000, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 100, 1000, 1000});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  // Dirty the cache the way a fallback move commit does, then drag WITHOUT
  // pumping events (raw sendEvent, not the send_mouse helper): a paint would
  // refresh this small document synchronously and mask the drag-path behavior
  // (the old bug lived inside mouseMoveEvent itself).
  canvas.document_changed();
  const auto before_stats = canvas.render_cache_diagnostics();
  const auto start = canvas.widget_position_for_document_point(QPoint(150, 150));
  const auto end = canvas.widget_position_for_document_point(QPoint(450, 150));
  {
    QMouseEvent press(QEvent::MouseButtonPress, start, canvas.mapToGlobal(start), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);
    QMouseEvent move(QEvent::MouseMove, end, canvas.mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);
  }

  // The styled area gate latches the proxy on the first move; no synchronous
  // full recomposite may have run inside the drag.
  const auto mid_drag_stats = canvas.render_cache_diagnostics();
  CHECK(mid_drag_stats.full_refreshes == before_stats.full_refreshes);
  CHECK(mid_drag_stats.move_proxy_previews == before_stats.move_proxy_previews + 1);
  CHECK(mid_drag_stats.move_outline_previews == before_stats.move_outline_previews);

  send_mouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!canvas.render_settled() && std::chrono::steady_clock::now() < settle_deadline) {
    QApplication::processEvents();
  }
  CHECK(canvas.render_settled());
  // The full refresh ran after release (paint-owned), and the commit landed.
  CHECK(canvas.render_cache_diagnostics().full_refreshes > before_stats.full_refreshes);
  CHECK(color_close(canvas_pixel(canvas, QPoint(1250, 500)), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(150, 500)), QColor(Qt::white), 45));
}

// Consecutive drags of the SAME selection reuse the retained base cache and
// proxy snapshot from the previous commit (the proxy rect translated by the
// committed delta): the second press skips both rebuilds and the proxy still
// blits the content at its dragged position. A click that never becomes a
// drag keeps the retention; an external document change or a different
// selection drops it.
void ui_move_repeat_drag_reuses_retained_caches() {
  patchy::Document document(1500, 1300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1500, 1300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer layer(document.allocate_layer_id(), "Retained Move",
                      solid_pixels(1000, 1000, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{100, 100, 1000, 1000});
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = patchy::BlendMode::Normal;
  stroke.color = patchy::RgbColor{40, 180, 80};
  stroke.opacity = 1.0F;
  stroke.size = 2.0F;
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));
  const auto background_id = std::as_const(document).layers().front().id();

  patchy::ui::CanvasWidget canvas;
  canvas.resize(900, 720);
  canvas.set_document(&document);
  canvas.set_zoom(0.5);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const auto before = canvas.render_cache_diagnostics();
  // Drag 1: the styled area gate latches the proxy on the first move; the
  // release patches the cache and retains base+proxy. Layer moves to 400..1400.
  auto start = canvas.widget_position_for_document_point(QPoint(150, 150));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(150, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(150, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before.move_proxy_previews + 1);
  CHECK(canvas.render_cache_diagnostics().move_preview_cache_reuses == before.move_preview_cache_reuses);

  // A click that never becomes a drag keeps the retained caches.
  start = canvas.widget_position_for_document_point(QPoint(450, 150));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // Drag 2 of the same selection: the press reuses the retained caches and
  // the proxy latches on the first move. Layer moves to 460..1460.
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_preview_cache_reuses == before.move_preview_cache_reuses + 1);
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before.move_proxy_previews + 2);
  CHECK(color_close(canvas_pixel(canvas, QPoint(430, 150)), QColor(Qt::white), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(520, 150)), QColor(20, 90, 235), 45));
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(30, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(canvas, QPoint(520, 150)), QColor(20, 90, 235), 45));
  CHECK(color_close(canvas_pixel(canvas, QPoint(430, 150)), QColor(Qt::white), 45));

  // An external document change drops the retention: the next press rebuilds.
  canvas.document_changed();
  QApplication::processEvents();
  start = canvas.widget_position_for_document_point(QPoint(510, 150));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(-30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_preview_cache_reuses == before.move_preview_cache_reuses + 1);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(-30, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // A different selection never reuses the retained caches.
  canvas.set_selected_layer_ids({background_id});
  start = canvas.widget_position_for_document_point(QPoint(50, 650));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_preview_cache_reuses == before.move_preview_cache_reuses + 1);
  send_mouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!canvas.render_settled() && std::chrono::steady_clock::now() < settle_deadline) {
    QApplication::processEvents();
  }
  CHECK(canvas.render_settled());
  // Layer ended at 400..1400 after the third drag.
  CHECK(color_close(canvas_pixel(canvas, QPoint(450, 150)), QColor(20, 90, 235), 45));
}

// Desktop simulation of the wasm move-commit re-entrancy bug: Qt's wasm
// platform delivers DOM input synchronously into the nested wait loops the
// release commit runs (undo snapshot + accurate patch render), which used to
// keep driving the drag after release, commit with a different delta than
// the cache patches were rendered for, and push ghost undo entries. The
// canvas handlers now drop (releases: park-and-replay) input while
// processing_render_wait_active_. This drives a commit with slowed waits and
// injects input mid-wait from timers, exactly the way wasm delivers it.
void ui_move_commit_ignores_reentrant_input_during_processing_wait() {
  patchy::Document document(400, 300, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(400, 300, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Reentrant Move",
                      solid_pixels(60, 60, patchy::PixelFormat::rgba8(), QColor(20, 90, 235)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{40, 40, 60, 60});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Reentrant Move"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  canvas->set_snap_enabled(false);
  canvas->set_zoom(1.0);
  canvas->set_selected_layer_ids({layer_id});
  QApplication::processEvents();
  canvas->force_refresh();
  QApplication::processEvents();
  const auto settle = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!canvas->render_settled() && std::chrono::steady_clock::now() < deadline) {
      QApplication::processEvents();
    }
    CHECK(canvas->render_settled());
  };
  settle();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_render_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  EnvironmentVariableRestorer restore_undo_delay("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("250"));
  qputenv("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", QByteArray("250"));

  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  const auto start = canvas->widget_position_for_document_point(QPoint(70, 70));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);

  // The desktop wait loops pump timers once the overlay shows (delay 0 shows
  // it on the first tick), so these fire inside the undo-snapshot wait
  // (~60 ms into a 250 ms sleep) and the patch-render wait (~300 ms): a
  // buttonless drift move plus a full press/move/release triplet, the shape
  // that used to drift the commit and push a ghost undo entry.
  int injections_during_wait = 0;
  const auto inject = [&] {
    if (!canvas->processing_overlay_visible()) {
      return;
    }
    ++injections_during_wait;
    const auto drift = start + QPoint(160, 40);
    QMouseEvent drift_move(QEvent::MouseMove, drift, canvas->mapToGlobal(drift), Qt::NoButton, Qt::NoButton,
                           Qt::NoModifier);
    QApplication::sendEvent(canvas, &drift_move);
    const auto ghost = start + QPoint(120, 80);
    QMouseEvent ghost_press(QEvent::MouseButtonPress, ghost, canvas->mapToGlobal(ghost), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &ghost_press);
    const auto ghost_end = ghost + QPoint(30, 30);
    QMouseEvent ghost_move(QEvent::MouseMove, ghost_end, canvas->mapToGlobal(ghost_end), Qt::NoButton,
                           Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &ghost_move);
    QMouseEvent ghost_release(QEvent::MouseButtonRelease, ghost_end, canvas->mapToGlobal(ghost_end), Qt::LeftButton,
                              Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &ghost_release);
  };
  QTimer::singleShot(60, canvas, inject);
  QTimer::singleShot(300, canvas, inject);

  // Release at a position past the last live move so the commit renders
  // fresh patches (the second wait) instead of reusing the live ones.
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(50, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(injections_during_wait >= 1);

  // The commit landed exactly at the release delta (+50, 0), unmoved by the
  // injected input; exactly one undo state was pushed.
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto* moved = std::as_const(doc).find_layer(layer_id);
  CHECK(moved != nullptr);
  if (moved != nullptr) {
    CHECK(moved->bounds().x == 90);
    CHECK(moved->bounds().y == 40);
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);

  // Speed the tail up before settling (the delays also slow the async
  // refresh the release scheduled).
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_UNDO_SNAPSHOT_TEST_DELAY_MS", QByteArray("0"));
  settle();
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 70)), QColor(20, 90, 235), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(45, 70)), QColor(Qt::white), 8));

  // The committed pixels match a from-scratch composite (no stale or
  // misaligned patch survived in the render cache).
  const auto committed = render_widget_image(*canvas);
  canvas->force_refresh();
  settle();
  const auto reference = render_widget_image(*canvas);
  CHECK(images_equal_rgba(committed, reference));

  // Undo restores the original position, and a subsequent normal drag still
  // commits (the guard did not latch).
  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  settle();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(45, 70)), QColor(20, 90, 235), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 70)), QColor(Qt::white), 8));
  const auto redo_start = canvas->widget_position_for_document_point(QPoint(70, 70));
  send_mouse(*canvas, QEvent::MouseButtonPress, redo_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, redo_start + QPoint(20, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, redo_start + QPoint(20, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  settle();
  const auto* redragged = std::as_const(doc).find_layer(layer_id);
  CHECK(redragged != nullptr);
  if (redragged != nullptr) {
    CHECK(redragged->bounds().x == 60);
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
}

// Real-file regression for the pinball poster report: with the "ARCADE" and
// "3D" folders selected (autoselect off), the SECOND drag used to stall
// ~1.5 s paying a synchronous full-resolution composite inside its first
// mouse-move before the fast proxy path engaged. Drives the exact flow on
// the real PSD when the local fixture is present.
void ui_move_pinball_poster_second_folder_drag_has_no_sync_composite_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("pinball_retronight_poster_a3.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] pinball poster fixture missing: " << path.string() << '\n';
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::LayerId arcade_id = 0;
  patchy::LayerId three_d_id = 0;
  // The folders are not necessarily top-level; match anywhere in the tree.
  const std::function<void(const patchy::Layer&)> find_folders = [&](const patchy::Layer& layer) {
    if (layer.kind() == patchy::LayerKind::Group) {
      const auto name = QString::fromStdString(layer.name());
      if (name.compare(QStringLiteral("ARCADE"), Qt::CaseInsensitive) == 0) {
        arcade_id = layer.id();
      } else if (name.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0) {
        three_d_id = layer.id();
      }
      for (const auto& child : layer.children()) {
        find_folders(child);
      }
    }
  };
  for (const auto& layer : std::as_const(document).layers()) {
    find_folders(layer);
  }
  CHECK(arcade_id != 0);
  CHECK(three_d_id != 0);
  if (arcade_id == 0 || three_d_id == 0) {
    return;
  }
  const QPoint doc_center(document.width() / 2, document.height() / 2);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Pinball Poster"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  canvas->set_show_transform_controls(false);
  canvas->set_auto_select_layer(false);
  canvas->set_snap_enabled(false);
  canvas->set_zoom(0.25);
  canvas->set_selected_layer_ids({arcade_id, three_d_id});
  QApplication::processEvents();
  const auto settle = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!canvas->render_settled() && std::chrono::steady_clock::now() < deadline) {
      QApplication::processEvents();
    }
    CHECK(canvas->render_settled());
  };
  settle();

  // Drag 1 (the fast one right after open), then commit.
  const auto before = canvas->render_cache_diagnostics();
  const auto start = canvas->widget_position_for_document_point(doc_center);
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_first = canvas->render_cache_diagnostics();
  const auto first_release_patched =
      after_first.move_precommit_patches == before.move_precommit_patches + 1;

  // Drag 2 of the same selection, delivered WITHOUT pumping events so nothing
  // but the mouse handlers themselves can run: the old bug composited the
  // full document synchronously right here.
  {
    QMouseEvent press(QEvent::MouseButtonPress, start, canvas->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    const auto target = start + QPoint(40, 0);
    QMouseEvent move(QEvent::MouseMove, target, canvas->mapToGlobal(target), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
  }
  const auto mid_second = canvas->render_cache_diagnostics();
  CHECK(mid_second.full_refreshes == after_first.full_refreshes);
  if (first_release_patched) {
    // The patch-success release retains base+proxy, so the re-drag reused them.
    CHECK(mid_second.move_preview_cache_reuses == after_first.move_preview_cache_reuses + 1);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  settle();

  // The committed canvas must match a from-scratch composite: a stale or
  // misaligned release patch (the corruption half of the pinball report)
  // shows up as a mismatch here.
  const auto committed = render_widget_image(*canvas);
  canvas->force_refresh();
  settle();
  const auto reference = render_widget_image(*canvas);
  CHECK(images_equal_rgba(committed, reference));
}

// The slow-live-frame latch persists across drags of the same selection: a
// re-drag latches the proxy on its FIRST move instead of re-paying a slow
// live frame, while dragging a different layer re-prices from a live frame.
void ui_move_live_slow_latch_persists_across_drags() {
  EnvironmentVariableRestorer restore_latch("PATCHY_MOVE_LIVE_LATCH_MS");
  qputenv("PATCHY_MOVE_LIVE_LATCH_MS", QByteArray("0"));

  patchy::Document document(300, 200, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(300, 200, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Persistent Latch Move",
                      solid_pixels(40, 40, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{30, 40, 40, 40});
  document.add_layer(std::move(layer));
  patchy::Layer other(document.allocate_layer_id(), "Other Latch Move",
                      solid_pixels(30, 30, patchy::PixelFormat::rgba8(), QColor(40, 60, 220)));
  const auto other_id = other.id();
  other.set_bounds(patchy::Rect{200, 140, 30, 30});
  document.add_layer(std::move(other));

  patchy::ui::CanvasWidget canvas;
  canvas.resize(520, 380);
  canvas.set_document(&document);
  canvas.set_zoom(1.0);
  canvas.set_tool(patchy::ui::CanvasTool::Move);
  canvas.set_show_transform_controls(false);
  canvas.set_auto_select_layer(false);
  canvas.set_snap_enabled(false);
  canvas.set_selected_layer_ids({layer_id});
  canvas.show();
  QApplication::processEvents();

  const auto before_stats = canvas.render_cache_diagnostics();
  // Drag 1: first move live (zero threshold marks it slow), second latches.
  auto start = canvas.widget_position_for_document_point(QPoint(50, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 1);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // Drag 2 of the same layer: the latch persisted, so the FIRST move already
  // blits the proxy (no slow live frame).
  start = canvas.widget_position_for_document_point(QPoint(110, 60));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 2);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  // Drag 3 of a DIFFERENT layer: the key mismatch re-prices, so the first
  // move renders live again and only the second latches.
  canvas.set_selected_layer_ids({other_id});
  start = canvas.widget_position_for_document_point(QPoint(215, 155));
  send_mouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(-40, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 2);
  send_mouse(canvas, QEvent::MouseMove, start + QPoint(-60, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(canvas.render_cache_diagnostics().move_proxy_previews == before_stats.move_proxy_previews + 3);
  send_mouse(canvas, QEvent::MouseButtonRelease, start + QPoint(-60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

// Multi-rect region renders fan out across workers; every rect is the same
// render_document_rect call either way, so the patch bytes must match the
// PATCHY_RENDER_SINGLE_THREADED sequential loop exactly.
void ui_parallel_region_patches_match_single_threaded() {
  patchy::Document document(1400, 1000, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(1400, 1000, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::Layer styled(document.allocate_layer_id(), "Styled Region Layer",
                       solid_pixels(600, 500, patchy::PixelFormat::rgba8(), QColor(30, 120, 220)));
  styled.set_bounds(patchy::Rect{120, 90, 600, 500});
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 1.0F;
  shadow.distance = 6.0F;
  shadow.size = 9.0F;
  styled.layer_style().drop_shadows.push_back(shadow);
  document.add_layer(std::move(styled));

  patchy::Layer overlay_layer(document.allocate_layer_id(), "Multiply Region Layer",
                              solid_pixels(700, 600, patchy::PixelFormat::rgba8(), QColor(240, 200, 60)));
  overlay_layer.set_bounds(patchy::Rect{600, 350, 700, 600});
  overlay_layer.set_blend_mode(patchy::BlendMode::Multiply);
  document.add_layer(std::move(overlay_layer));

  QRegion region;
  region += QRect(0, 0, 700, 520);
  region += QRect(680, 400, 700, 560);
  region += QRect(200, 640, 420, 300);

  const auto parallel_patches =
      patchy::ui::qimage_patches_from_document_region_with_layer_bounds(document, region, true, {});
  std::vector<patchy::ui::RenderedDocumentPatch> sequential_patches;
  {
    ScopedSingleThreadedRender single_threaded;
    sequential_patches = patchy::ui::qimage_patches_from_document_region_with_layer_bounds(document, region, true, {});
  }

  CHECK(!parallel_patches.empty());
  CHECK(parallel_patches.size() == sequential_patches.size());
  for (std::size_t index = 0; index < parallel_patches.size() && index < sequential_patches.size(); ++index) {
    CHECK(parallel_patches[index].document_rect == sequential_patches[index].document_rect);
    CHECK(parallel_patches[index].image == sequential_patches[index].image);
  }
}

void ui_layer_move_repaints_only_active_document_tab() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  std::vector<patchy::ui::CanvasWidget*> canvases;
  std::vector<std::unique_ptr<PaintCounterFilter>> counters;
  for (int index = 0; index < tabs->count(); ++index) {
    auto* canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->widget(index));
    CHECK(canvas != nullptr);
    canvases.push_back(canvas);
    auto counter = std::make_unique<PaintCounterFilter>();
    canvas->installEventFilter(counter.get());
    counters.push_back(std::move(counter));
  }

  tabs->setCurrentIndex(2);
  QApplication::processEvents();
  auto* active_canvas = canvases[2];
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  active_canvas->set_primary_color(QColor(20, 150, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  for (auto& counter : counters) {
    counter->paint_events = 0;
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  active_canvas->set_show_transform_controls(false);
  active_canvas->set_auto_select_layer(false);
  const auto start = active_canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*active_canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 8; ++step) {
    send_mouse(*active_canvas, QEvent::MouseMove, start + QPoint(step * 12, step * 3), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*active_canvas, QEvent::MouseButtonRelease, start + QPoint(96, 24), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(counters[2]->paint_events > 0);
  CHECK(counters[0]->paint_events == 0);
  CHECK(counters[1]->paint_events == 0);
  save_widget_artifact("ui_move_active_tab_only", window);
}

void ui_arduboy_psd_render_path_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = patchy::psd::DocumentIo::read_file(path);
  const auto image = patchy::ui::qimage_from_document(document, true);
  CHECK(!image.isNull());

  std::size_t non_white_pixels = 0;
  for (int y = 0; y < image.height(); y += 16) {
    for (int x = 0; x < image.width(); x += 16) {
      const auto color = image.pixelColor(x, y);
      if (color.alpha() != 0 && (color.red() < 245 || color.green() < 245 || color.blue() < 245)) {
        ++non_white_pixels;
      }
    }
  }
  CHECK(non_white_pixels > 1000);

  ensure_artifact_dir();
  const auto preview = image.scaled(QSize(360, 480), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  CHECK(preview.save(QStringLiteral("test-artifacts/ui_arduboy_psd_render.png")));
}

void ui_duke_psd_text_edit_stays_responsive_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  QElapsedTimer timer;
  timer.start();
  auto document = patchy::psd::DocumentIo::read_file(path);
  const auto load_elapsed_ms = timer.elapsed();
  CHECK(load_elapsed_ms < 10000);

  struct TextTarget {
    QRect bounds;
  };
  std::optional<TextTarget> target;
  std::function<void(const std::vector<patchy::Layer>&)> find_target;
  find_target = [&](const std::vector<patchy::Layer>& layers) {
    for (auto it = layers.rbegin(); it != layers.rend() && !target.has_value(); ++it) {
      const auto& layer = *it;
      if (layer.kind() == patchy::LayerKind::Group) {
        find_target(layer.children());
        continue;
      }
      const auto text = layer.metadata().find(patchy::kLayerMetadataText);
      const auto name = QString::fromStdString(layer.name());
      const auto metadata_text = text != layer.metadata().end() ? QString::fromStdString(text->second) : QString();
      if (!name.contains(QStringLiteral("Duke Nukem Mobile"), Qt::CaseInsensitive) &&
          !metadata_text.contains(QStringLiteral("Duke Nukem Mobile"), Qt::CaseInsensitive)) {
        continue;
      }
      const auto bounds = layer.bounds();
      target = TextTarget{QRect(bounds.x, bounds.y, bounds.width, bounds.height)};
    }
  };
  find_target(document.layers());
  CHECK(target.has_value());
  CHECK(!target->bounds.isEmpty());

  timer.restart();
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Duke Nukem Mobile"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->fit_to_view();
  QApplication::processEvents();
  const auto display_elapsed_ms = timer.elapsed();
  CHECK(display_elapsed_ms < 5000);

  const auto hit_document_point = target->bounds.center();
  const auto hit_widget_point = canvas->widget_position_for_document_point(hit_document_point);
  CHECK(canvas->rect().contains(hit_widget_point));

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  timer.restart();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(320);
  // The session renders live from entry (no source-raster phase) so caret/selection geometry and
  // the on-screen glyphs share one layout.
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  const auto editor_block_tops = [](const QTextEdit& text_editor) {
    std::vector<int> tops;
    const auto* layout = text_editor.document()->documentLayout();
    for (auto block = text_editor.document()->begin(); block.isValid(); block = block.next()) {
      tops.push_back(static_cast<int>(std::round(layout->blockBoundingRect(block).top())));
    }
    return tops;
  };
  const auto block_tops_close = [](const std::vector<int>& expected, const std::vector<int>& actual) {
    if (actual.size() != expected.size()) {
      return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (std::abs(actual[index] - expected[index]) > 2) {
        return false;
      }
    }
    return true;
  };
  const auto initial_block_tops = editor_block_tops(*editor);
  CHECK(initial_block_tops.size() >= 5U);
  const auto plain_text = editor->toPlainText();
  const auto selection_end = plain_text.indexOf(QStringLiteral("Ever heard"));
  CHECK(selection_end > 0);
  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(selection_end, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  CHECK(editor->textCursor().hasSelection());
  const auto edit_elapsed_ms = timer.elapsed();
  CHECK(edit_elapsed_ms < 3000);
  QTextCursor end_cursor(editor->document());
  end_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(end_cursor);
  QApplication::processEvents();
  CHECK(!editor->textCursor().hasSelection());

  timer.restart();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  canvas->set_show_transform_controls(false);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(timer.elapsed() < 4000);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  const auto reedit_block_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, reedit_block_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, reedit_block_tops));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_duke_psd_seth_text_edit_preview_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  auto document = patchy::psd::DocumentIo::read_file(path);
  struct TextTarget {
    patchy::LayerId id{};
    QRect bounds;
  };
  std::optional<TextTarget> target;
  const std::function<void(const std::vector<patchy::Layer>&)> find_target =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (target.has_value()) {
            return;
          }
          if (layer.kind() == patchy::LayerKind::Group) {
            find_target(layer.children());
            continue;
          }
          const auto text = layer.metadata().find(patchy::kLayerMetadataText);
          if (text == layer.metadata().end() ||
              !QString::fromStdString(text->second).contains(QStringLiteral("I did all the programming"),
                                                             Qt::CaseInsensitive)) {
            continue;
          }
          const auto bounds = layer.bounds();
          target = TextTarget{layer.id(), QRect(bounds.x, bounds.y, bounds.width, bounds.height)};
        }
      };
  find_target(document.layers());
  CHECK(target.has_value());
  CHECK(!target->bounds.isEmpty());
  document.set_active_layer(target->id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Duke Nukem Mobile Seth"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->zoom_to_document_rect(target->bounds.adjusted(-280, -220, 280, 220));
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  const QPoint move_delta(26, -14);
  const auto move_start = canvas->widget_position_for_document_point(target->bounds.center());
  const auto move_end = canvas->widget_position_for_document_point(target->bounds.center() + move_delta);
  drag(*canvas, move_start, move_end);
  target->bounds.translate(move_delta);
  canvas->set_show_transform_controls(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_widget_point = canvas->widget_position_for_document_point(target->bounds.center());
  // The layer keeps Photoshop's raster since import no longer regenerates big-effect
  // text, so entering the editor may warn about the fixture's missing font.
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  process_events_for(420);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.textPreviewLayerId").isValid());
  save_widget_artifact("ui_duke_seth_text_edit_preview", *canvas);

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

// The fixture's point-text layers store a small base point size scaled up ~3.7x by the text
// transform.  The live edit preview used to resample the base-size raster through the transform,
// so the text went blurry for the whole session and only snapped sharp on commit; the preview must
// come from the same crisp render-through-transform path the commit uses.  Committing twice with
// no text change must also be stable: same pixels, same stored point size (no "reflow" between
// sessions).
void ui_audio_splitter_scaled_psd_text_edit_preview_stays_crisp_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("AudioSplitterProject.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  // The fixture's text face (Century Gothic); offscreen does not enumerate installed fonts.
  register_test_fonts(TestFontRole::CenturyGothic);

  auto document = patchy::psd::DocumentIo::read_file(path);
  struct TextTarget {
    patchy::LayerId id{};
    QRect bounds;
  };
  std::optional<TextTarget> target;
  for (const auto& layer : document.layers()) {
    const auto text = layer.metadata().find(patchy::kLayerMetadataText);
    if (text != layer.metadata().end() &&
        QString::fromStdString(text->second).contains(QStringLiteral("Dual TRRS Female"), Qt::CaseInsensitive)) {
      const auto bounds = layer.bounds();
      target = TextTarget{layer.id(), QRect(bounds.x, bounds.y, bounds.width, bounds.height)};
    }
  }
  CHECK(target.has_value());
  CHECK(!target->bounds.isEmpty());

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Audio Splitter"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->zoom_to_document_rect(target->bounds.adjusted(-160, -160, 160, 160));
  QApplication::processEvents();

  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto run_noop_edit_session = [&](QPoint document_hit, patchy::PixelBuffer& preview_pixels,
                                         patchy::Rect& preview_bounds) -> bool {
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    QApplication::processEvents();
    accept_missing_psd_text_font_warning_if_present();
    const auto hit = canvas->widget_position_for_document_point(document_hit);
    send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor == nullptr) {
      return false;
    }
    process_events_for(380);
    if (editor->property("patchy.textPreviewLayerId").isValid()) {
      const auto preview_id =
          static_cast<patchy::LayerId>(editor->property("patchy.textPreviewLayerId").toULongLong());
      if (auto* preview_layer = doc.find_layer(preview_id); preview_layer != nullptr) {
        preview_pixels = preview_layer->pixels();
        preview_bounds = preview_layer->bounds();
      }
    }
    // Commit with no text change (apply keeps what you saw on screen).
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    return canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr;
  };
  const auto mean_alpha_difference = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) -> double {
    if (a.empty() || b.empty() || a.width() != b.width() || a.height() != b.height()) {
      return 255.0;
    }
    double total = 0.0;
    for (std::int32_t y = 0; y < a.height(); ++y) {
      for (std::int32_t x = 0; x < a.width(); ++x) {
        total += std::abs(static_cast<int>(a.pixel(x, y)[3]) - static_cast<int>(b.pixel(x, y)[3]));
      }
    }
    return total / (static_cast<double>(a.width()) * static_cast<double>(a.height()));
  };
  const auto layer_text_size = [&]() -> std::string {
    const auto* layer = doc.find_layer(target->id);
    if (layer == nullptr) {
      return std::string();
    }
    const auto found = layer->metadata().find(patchy::kLayerMetadataTextSize);
    return found == layer->metadata().end() ? std::string() : found->second;
  };

  patchy::PixelBuffer first_preview_pixels;
  patchy::Rect first_preview_bounds{};
  const bool first_session_ok = run_noop_edit_session(target->bounds.center(), first_preview_pixels,
                                                      first_preview_bounds);
  CHECK(first_session_ok);
  CHECK(!first_preview_pixels.empty());
  auto* committed_layer = doc.find_layer(target->id);
  CHECK(committed_layer != nullptr);
  const auto first_commit_pixels = committed_layer->pixels();
  const auto first_commit_bounds = committed_layer->bounds();
  const auto first_commit_size = layer_text_size();
  CHECK(!first_commit_size.empty());

  ensure_artifact_dir();
  CHECK(flattened_on_white(first_preview_pixels)
            .save(QStringLiteral("test-artifacts/ui_audio_splitter_text_edit_preview.png")));
  CHECK(flattened_on_white(first_commit_pixels)
            .save(QStringLiteral("test-artifacts/ui_audio_splitter_text_first_commit.png")));

  // The on-screen edit preview must be the same crisp pixels the commit produces.
  CHECK(first_preview_bounds.x == first_commit_bounds.x);
  CHECK(first_preview_bounds.y == first_commit_bounds.y);
  CHECK(first_preview_bounds.width == first_commit_bounds.width);
  CHECK(first_preview_bounds.height == first_commit_bounds.height);
  CHECK(mean_alpha_difference(first_preview_pixels, first_commit_pixels) < 1.5);

  // A second no-op session must not change the raster or the stored point size.
  const auto second_hit = QPoint(first_commit_bounds.x + first_commit_bounds.width / 2,
                                 first_commit_bounds.y + first_commit_bounds.height / 2);
  patchy::PixelBuffer second_preview_pixels;
  patchy::Rect second_preview_bounds{};
  const bool second_session_ok = run_noop_edit_session(second_hit, second_preview_pixels, second_preview_bounds);
  CHECK(second_session_ok);
  CHECK(!second_preview_pixels.empty());
  committed_layer = doc.find_layer(target->id);
  CHECK(committed_layer != nullptr);
  const auto second_commit_pixels = committed_layer->pixels();
  const auto second_commit_bounds = committed_layer->bounds();
  CHECK(flattened_on_white(second_commit_pixels)
            .save(QStringLiteral("test-artifacts/ui_audio_splitter_text_second_commit.png")));
  CHECK(layer_text_size() == first_commit_size);
  CHECK(second_commit_bounds.x == first_commit_bounds.x);
  CHECK(second_commit_bounds.y == first_commit_bounds.y);
  CHECK(second_commit_bounds.width == first_commit_bounds.width);
  CHECK(second_commit_bounds.height == first_commit_bounds.height);
  CHECK(mean_alpha_difference(first_commit_pixels, second_commit_pixels) < 1.5);
}

void ui_text_reedit_preserves_rich_text_spacing() {
  patchy::Document document(900, 700, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(900, 700, patchy::PixelFormat::rgba8(), QColor(Qt::white)));

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Text Stability"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.75);
  QApplication::processEvents();

  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("textStyleCombo"));
  CHECK(text_size != nullptr);
  CHECK(style_combo != nullptr);
  // Directly-constructed Document: core default 300 ppi, not the startup doc's 72.
  text_size->setValue(text_points_for_pixels(72, 300.0));
  // New text seeds from the style picker now that the bar has no B button.
  const auto bold_row = style_combo->findData(QStringLiteral("Bold"));
  CHECK(bold_row > 0);
  style_combo->setCurrentIndex(bold_row);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 80)),
       canvas->widget_position_for_document_point(QPoint(820, 620)));
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  const auto title_size = std::max(8, static_cast<int>(std::round(72.0 * canvas->zoom())));
  const auto body_size = std::max(8, static_cast<int>(std::round(36.0 * canvas->zoom())));
  QFont title_font = editor->font();
  title_font.setPixelSize(title_size);
  title_font.setBold(true);
  QFont body_font = editor->font();
  body_font.setPixelSize(body_size);
  body_font.setBold(true);
  QTextCharFormat title_format;
  title_format.setFont(title_font);
  title_format.setForeground(QBrush(QColor(35, 30, 59)));
  QTextCharFormat body_format;
  body_format.setFont(body_font);
  body_format.setForeground(QBrush(QColor(35, 30, 59)));
  QTextCursor rich_cursor(editor->document());
  rich_cursor.select(QTextCursor::Document);
  rich_cursor.removeSelectedText();
  rich_cursor.insertText(QStringLiteral("Duke Nukem Mobile\n\n"), title_format);
  rich_cursor.insertText(QStringLiteral("(for the Tapwave Zodiac released by Machineworks Northwest, 2004)\n\n"),
                         body_format);
  rich_cursor.insertText(
      QStringLiteral("Ever heard of the the Tapwave Zodiac?  It's a failed handheld that was released in 2003.\n\n"),
      body_format);
  rich_cursor.insertText(QStringLiteral(
                             "All the Zodiacs today have gross ass disintegrated left and right shoulder buttons due "
                             "to the poor choice of materials."),
                         body_format);
  editor->setTextCursor(rich_cursor);
  QApplication::processEvents();
  const auto editor_block_tops = [](const QTextEdit& text_editor) {
    std::vector<int> tops;
    const auto* layout = text_editor.document()->documentLayout();
    for (auto block = text_editor.document()->begin(); block.isValid(); block = block.next()) {
      tops.push_back(static_cast<int>(std::round(layout->blockBoundingRect(block).top())));
    }
    return tops;
  };
  const auto block_tops_close = [](const std::vector<int>& expected, const std::vector<int>& actual) {
    if (actual.size() != expected.size()) {
      return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (std::abs(actual[index] - expected[index]) > 2) {
        return false;
      }
    }
    return true;
  };
  const auto initial_block_tops = editor_block_tops(*editor);
  CHECK(initial_block_tops.size() >= 6U);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonDblClick, canvas->widget_position_for_document_point(QPoint(100, 100)),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  // The committed raster keeps the straddling last line's glyph bottoms (the boxed-clip
  // rule in docs/text-tool.md), so its ink extends past the frame and the re-edit arms
  // the line-aware extended-box preview instead of plain editor painting.
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.extendedBoxPreview").toBool());
  const auto first_reedit_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, first_reedit_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, first_reedit_tops));

  const auto plain_text = editor->toPlainText();
  const auto selection_end = plain_text.indexOf(QStringLiteral("All the Zodiacs"));
  CHECK(selection_end > 0);
  QTextCursor selection_cursor(editor->document());
  selection_cursor.setPosition(0);
  selection_cursor.setPosition(selection_end, QTextCursor::KeepAnchor);
  editor->setTextCursor(selection_cursor);
  QApplication::processEvents();
  CHECK(editor->textCursor().hasSelection());
  QTextCursor end_cursor(editor->document());
  end_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(end_cursor);
  QApplication::processEvents();
  CHECK(!editor->textCursor().hasSelection());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  send_mouse(*canvas, QEvent::MouseButtonDblClick, canvas->widget_position_for_document_point(QPoint(100, 100)),
             Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  process_events_for(80);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  CHECK(editor->property("patchy.extendedBoxPreview").toBool());
  const auto second_reedit_tops = editor_block_tops(*editor);
  if (!block_tops_close(initial_block_tops, second_reedit_tops)) {
    send_key(*editor, Qt::Key_Escape);
    QApplication::processEvents();
  }
  CHECK(block_tops_close(initial_block_tops, second_reedit_tops));
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
}

}  // namespace

std::vector<patchy::test::TestCase> move_tool_processing_overlay_tests() {
  return {
      {"ui_move_preview_preserves_layer_order", ui_move_preview_preserves_layer_order},
      {"ui_move_tool_moves_selected_layers_together", ui_move_tool_moves_selected_layers_together},
      {"ui_move_auto_select_hover_outlines_with_multi_selection",
       ui_move_auto_select_hover_outlines_with_multi_selection},
      {"ui_move_auto_select_drag_replaces_multi_selection", ui_move_auto_select_drag_replaces_multi_selection},
      {"ui_move_auto_select_selected_member_drag_keeps_multi_selection",
       ui_move_auto_select_selected_member_drag_keeps_multi_selection},
      {"ui_move_auto_select_blank_drag_keeps_multi_selection",
       ui_move_auto_select_blank_drag_keeps_multi_selection},
      {"ui_move_ctrl_click_toggles_layer_selection", ui_move_ctrl_click_toggles_layer_selection},
      {"ui_move_ctrl_click_add_continues_into_drag", ui_move_ctrl_click_add_continues_into_drag},
      {"ui_move_ctrl_click_selects_layer_inside_collapsed_folder",
       ui_move_ctrl_click_selects_layer_inside_collapsed_folder},
      {"ui_shift_constrains_move_tool_drag_to_axis", ui_shift_constrains_move_tool_drag_to_axis},
      {"ui_move_tool_uses_opaque_bounds_for_transparent_layer",
       ui_move_tool_uses_opaque_bounds_for_transparent_layer},
      {"ui_move_preview_keeps_underlying_layers_steady_when_zoomed_out",
       ui_move_preview_keeps_underlying_layers_steady_when_zoomed_out},
      {"ui_move_tool_hover_outlines_opaque_bounds", ui_move_tool_hover_outlines_opaque_bounds},
      {"ui_move_tool_uses_text_rect_for_hit_and_hover",
       ui_move_tool_uses_text_rect_for_hit_and_hover},
      {"ui_move_transform_controls_do_not_block_auto_select_hover",
       ui_move_transform_controls_do_not_block_auto_select_hover},
      {"ui_move_transform_handles_drag_past_canvas_edge",
       ui_move_transform_handles_drag_past_canvas_edge},
      {"ui_move_tool_moves_selected_folder_tree", ui_move_tool_moves_selected_folder_tree},
      {"ui_move_tool_moves_selected_masked_folder_tree", ui_move_tool_moves_selected_masked_folder_tree},
      {"ui_move_preview_clears_transparent_trails_and_keeps_layer_styles",
       ui_move_preview_clears_transparent_trails_and_keeps_layer_styles},
      {"ui_move_preview_leaves_no_trail_when_zoomed_out", ui_move_preview_leaves_no_trail_when_zoomed_out},
      {"ui_move_preview_mid_drag_partial_repaint_matches_full_preview",
       ui_move_preview_mid_drag_partial_repaint_matches_full_preview},
      {"ui_dirty_region_move_preview_matches_force_refresh",
       ui_dirty_region_move_preview_matches_force_refresh},
      {"ui_processing_overlay_animates_for_slow_dirty_render",
       ui_processing_overlay_animates_for_slow_dirty_render},
      {"ui_processing_overlay_stays_top_aligned_without_dimming_canvas",
       ui_processing_overlay_stays_top_aligned_without_dimming_canvas},
      {"ui_brush_family_strokes_do_not_trigger_processing_overlay",
       ui_brush_family_strokes_do_not_trigger_processing_overlay},
      {"ui_processing_overlay_animates_for_slow_nudge_undo_snapshot",
       ui_processing_overlay_animates_for_slow_nudge_undo_snapshot},
      {"ui_processing_overlay_is_visible_before_slow_move_commit_callback",
       ui_processing_overlay_is_visible_before_slow_move_commit_callback},
      {"ui_processing_overlay_ticks_during_filter_apply",
       ui_processing_overlay_ticks_during_filter_apply},
      {"ui_processing_overlay_ticks_during_fill_tool_loop",
       ui_processing_overlay_ticks_during_fill_tool_loop},
      {"ui_layer_style_cache_invalidates_after_pixel_mutation",
       ui_layer_style_cache_invalidates_after_pixel_mutation},
      {"ui_move_expensive_styled_layer_uses_proxy_until_release",
       ui_move_expensive_styled_layer_uses_proxy_until_release},
      {"ui_move_styled_folder_drag_uses_proxy_preview", ui_move_styled_folder_drag_uses_proxy_preview},
      {"ui_move_overlapping_stack_drag_uses_proxy_preview",
       ui_move_overlapping_stack_drag_uses_proxy_preview},
      {"ui_move_styled_folder_live_preview_clears_shadow_trail",
       ui_move_styled_folder_live_preview_clears_shadow_trail},
      {"ui_move_slow_live_frame_latches_proxy", ui_move_slow_live_frame_latches_proxy},
      {"ui_move_scaled_preview_composites_at_display_resolution",
       ui_move_scaled_preview_composites_at_display_resolution},
      {"ui_move_scaled_proxy_after_commit_shows_moved_content",
       ui_move_scaled_proxy_after_commit_shows_moved_content},
      {"ui_move_drag_with_dirty_render_cache_skips_sync_composite",
       ui_move_drag_with_dirty_render_cache_skips_sync_composite},
      {"ui_move_repeat_drag_reuses_retained_caches", ui_move_repeat_drag_reuses_retained_caches},
      {"ui_move_commit_ignores_reentrant_input_during_processing_wait",
       ui_move_commit_ignores_reentrant_input_during_processing_wait},
      {"ui_move_pinball_poster_second_folder_drag_has_no_sync_composite_if_available",
       ui_move_pinball_poster_second_folder_drag_has_no_sync_composite_if_available},
      {"ui_move_live_slow_latch_persists_across_drags",
       ui_move_live_slow_latch_persists_across_drags},
      {"ui_parallel_region_patches_match_single_threaded", ui_parallel_region_patches_match_single_threaded},
      {"ui_layer_move_repaints_only_active_document_tab", ui_layer_move_repaints_only_active_document_tab},
      {"ui_arduboy_psd_render_path_if_available", ui_arduboy_psd_render_path_if_available},
      {"ui_duke_psd_text_edit_stays_responsive_if_available",
       ui_duke_psd_text_edit_stays_responsive_if_available},
      {"ui_duke_psd_seth_text_edit_preview_if_available",
       ui_duke_psd_seth_text_edit_preview_if_available},
      {"ui_audio_splitter_scaled_psd_text_edit_preview_stays_crisp_if_available",
       ui_audio_splitter_scaled_psd_text_edit_preview_stays_crisp_if_available},
      {"ui_text_reedit_preserves_rich_text_spacing",
       ui_text_reedit_preserves_rich_text_spacing},
  };
}
