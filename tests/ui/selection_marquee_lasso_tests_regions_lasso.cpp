#include "ui/canvas_widget.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/app_settings.hpp"
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

#include <chrono>
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

void ui_complex_selection_draws_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(160, 90)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 90)),
       canvas->widget_position_for_document_point(QPoint(160, 210)), Qt::ShiftModifier);
  QApplication::processEvents();

  const auto& selection = canvas->selected_document_region();
  CHECK(selection.contains(QPoint(30, 30)));
  CHECK(selection.contains(QPoint(120, 150)));
  CHECK(!selection.contains(QPoint(40, 150)));

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int inner_outline_pixels = 0;
  for (int document_y = 96; document_y <= 204; ++document_y) {
    const auto boundary = canvas->widget_position_for_document_point(QPoint(80, document_y));
    bool found_dark_ant_pixel = false;
    for (int dx = -1; dx <= 1; ++dx) {
      const auto sample = image.pixelColor(boundary + QPoint(dx, 0));
      if (sample.red() < 70 && sample.green() < 70 && sample.blue() < 70) {
        found_dark_ant_pixel = true;
      }
    }
    if (found_dark_ant_pixel) {
      ++inner_outline_pixels;
    }
  }
  CHECK(inner_outline_pixels >= 8);
  save_widget_artifact("ui_complex_selection_outline", *canvas);
}

void ui_ctrl_h_hides_selection_edges_without_blue_tint() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(190, 155)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));

  const auto count_black_edge_pixels = [&canvas] {
    const auto image = canvas->grab().toImage();
    const auto left = canvas->widget_position_for_document_point(QPoint(90, 80));
    const auto right = canvas->widget_position_for_document_point(QPoint(190, 80));
    int pixels = 0;
    for (int y = left.y() - 2; y <= left.y() + 2; ++y) {
      for (int x = left.x(); x <= right.x(); ++x) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
          continue;
        }
        const auto color = image.pixelColor(x, y);
        if (color.red() < 70 && color.green() < 70 && color.blue() < 70) {
          ++pixels;
        }
      }
    }
    return pixels;
  };

  CHECK(count_black_edge_pixels() > 4);
  save_widget_artifact("ui_selection_edges_visible_no_tint", *canvas);

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  CHECK(canvas->has_selection());
  CHECK(count_black_edge_pixels() == 0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));
  save_widget_artifact("ui_selection_edges_hidden", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->selection_edges_visible());

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(80, 70)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
}

void ui_select_inverse_and_extended_blend_modes_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const QPoint inside(35, 35);
  const QPoint outside(150, 150);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(inside));
  CHECK(!canvas->selected_document_region().contains(outside));
  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(inside));
  CHECK(canvas->selected_document_region().contains(outside));
  save_widget_artifact("ui_select_inverse", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action(window, "selectReselectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(outside));
  CHECK(!canvas->selected_document_region().contains(inside));
  save_widget_artifact("ui_select_reselect", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto difference_index = blend_combo->findText(QStringLiteral("Difference"));
  CHECK(difference_index >= 0);
  blend_combo->setCurrentIndex(difference_index);
  QApplication::processEvents();

  const auto sample = canvas_pixel(*canvas, QPoint(30, 30));
  CHECK(sample.red() < 40);
  CHECK(sample.green() > 220);
  CHECK(sample.blue() > 220);
  save_widget_artifact("ui_extended_blend_modes", window);
}

void ui_selection_expand_contract_and_layer_transparency_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  canvas->set_primary_color(QColor(40, 180, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "selectLayerTransparencyAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(42, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(34, 50)));

  canvas->contract_selection(8);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(42, 50)));

  canvas->expand_selection(6);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(44, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 50)));

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(30, 50)));
  save_widget_artifact("ui_selection_expand_contract_transparency", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
       canvas->widget_position_for_document_point(QPoint(230, 110)));
  canvas->border_selection(7);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(162, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(226, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(195, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(148, 70)));
  save_widget_artifact("ui_selection_border", *canvas);
}

void ui_ctrl_click_layer_loads_layer_transparency() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(55, 45)),
       canvas->widget_position_for_document_point(QPoint(120, 95)));
  canvas->set_primary_color(QColor(20, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  QListWidgetItem* paint_layer_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Paint Layer")) {
      paint_layer_item = layer_list->item(row);
      break;
    }
  }
  CHECK(paint_layer_item != nullptr);
  auto* paint_layer_row = layer_list->itemWidget(paint_layer_item);
  CHECK(paint_layer_row != nullptr);
  auto* visibility = paint_layer_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  const auto check_state_before = paint_layer_item->checkState();
  send_mouse(*visibility, QEvent::MouseButtonPress, visibility->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*visibility, QEvent::MouseButtonRelease, visibility->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(!canvas->has_selection());
  CHECK(paint_layer_item->checkState() == check_state_before);

  auto* thumbnail = paint_layer_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  send_mouse(*thumbnail, QEvent::MouseButtonPress, thumbnail->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*thumbnail, QEvent::MouseButtonRelease, thumbnail->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 60)));
  CHECK(paint_layer_item->checkState() == check_state_before);
  save_widget_artifact("ui_ctrl_click_layer_transparency", window);
}

void ui_ctrl_click_layer_and_mask_preserve_soft_coverage() {
  patchy::Document document(72, 60, patchy::PixelFormat::rgb8());
  patchy::PixelBuffer pixels(72, 60, patchy::PixelFormat::rgba8());
  const auto coverage_for_coordinate = [](int coordinate) -> std::uint8_t {
    if (coordinate < 4) {
      return 0;
    }
    if (coordinate < 16) {
      return 1;
    }
    if (coordinate < 28) {
      return 60;
    }
    if (coordinate < 40) {
      return 127;
    }
    if (coordinate < 52) {
      return 128;
    }
    return 255;
  };
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      auto* pixel = pixels.pixel(x, y);
      pixel[0] = 210;
      pixel[1] = 120;
      pixel[2] = 40;
      pixel[3] = coverage_for_coordinate(x);
    }
  }
  auto& layer = document.add_pixel_layer("Soft Pixels", std::move(pixels));
  patchy::PixelBuffer mask_pixels(72, 60, patchy::PixelFormat::gray8());
  for (int y = 0; y < mask_pixels.height(); ++y) {
    auto row = mask_pixels.row(y);
    std::fill(row.begin(), row.end(), coverage_for_coordinate(y));
  }
  layer.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 72, 60}, std::move(mask_pixels), 0, false});

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Soft Transparency"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  click_layer_row_thumbnail(*layers, QStringLiteral("Soft Pixels"), QStringLiteral("layerContentThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  for (const auto& [x, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
           std::pair{2, std::uint8_t{0}}, std::pair{8, std::uint8_t{1}},
           std::pair{20, std::uint8_t{60}}, std::pair{32, std::uint8_t{127}},
           std::pair{44, std::uint8_t{128}}, std::pair{60, std::uint8_t{255}}}) {
    CHECK(canvas->selection_alpha_at(QPoint(x, 20)) == expected);
  }

  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  require_action(window, "channelSaveSelectionAction")->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().size() == 1);
  const auto& saved = active_document.channels().front().pixels();
  CHECK(*saved.pixel(8, 20) == 1);
  CHECK(*saved.pixel(20, 20) == 60);
  CHECK(*saved.pixel(32, 20) == 127);
  CHECK(*saved.pixel(44, 20) == 128);
  CHECK(*saved.pixel(60, 20) == 255);
  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  QApplication::processEvents();
  CHECK(active_document.channels().empty());

  click_layer_row_thumbnail(*layers, QStringLiteral("Soft Pixels"), QStringLiteral("layerMaskThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  for (const auto& [y, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
           std::pair{2, std::uint8_t{0}}, std::pair{8, std::uint8_t{1}},
           std::pair{20, std::uint8_t{60}}, std::pair{32, std::uint8_t{127}},
           std::pair{44, std::uint8_t{128}}, std::pair{56, std::uint8_t{255}}}) {
    CHECK(canvas->selection_alpha_at(QPoint(20, y)) == expected);
  }
}

void ui_deko_layer_transparency_selection_and_channel_save_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("deko_test.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(path.wstring()));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* layer_item = require_layer_item(*layers, QStringLiteral("Layer 2"));
  const auto layer_id =
      static_cast<patchy::LayerId>(layer_item->data(patchy::ui::kLayerIdRole).toULongLong());
  auto& active_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = static_cast<const patchy::Document&>(active_document).find_layer(layer_id);
  CHECK(layer != nullptr);
  CHECK(layer->pixels().format() == patchy::PixelFormat::rgba8());

  std::optional<QPoint> faint_point;
  std::optional<QPoint> strong_point;
  std::uint8_t faint_alpha = 0;
  std::uint8_t strong_alpha = 0;
  for (int y = 0; y < layer->pixels().height() && (!faint_point.has_value() || !strong_point.has_value()); ++y) {
    for (int x = 0; x < layer->pixels().width(); ++x) {
      const auto alpha = layer->pixels().pixel(x, y)[3];
      const QPoint document_point(layer->bounds().x + x, layer->bounds().y + y);
      if (!faint_point.has_value() && alpha > 0 && alpha < 16) {
        faint_point = document_point;
        faint_alpha = alpha;
      }
      if (!strong_point.has_value() && alpha >= 128 && alpha < 255) {
        strong_point = document_point;
        strong_alpha = alpha;
      }
      if (faint_point.has_value() && strong_point.has_value()) {
        break;
      }
    }
  }
  CHECK(faint_point.has_value());
  CHECK(strong_point.has_value());

  click_layer_row_thumbnail(*layers, QStringLiteral("Layer 2"), QStringLiteral("layerContentThumbnail"),
                            Qt::ControlModifier);
  CHECK(canvas->selection_has_partial_alpha());
  CHECK(canvas->selection_alpha_at(*faint_point) == faint_alpha);
  CHECK(canvas->selection_alpha_at(*strong_point) == strong_alpha);

  const auto channel_count_before = active_document.channels().size();
  QElapsedTimer timer;
  timer.start();
  require_action(window, "channelSaveSelectionAction")->trigger();
  const auto elapsed_ms = timer.elapsed();
  QApplication::processEvents();
  CHECK(elapsed_ms < 5000);
  CHECK(active_document.channels().size() == channel_count_before + 1);
  const auto& saved = active_document.channels().back().pixels();
  CHECK(*saved.pixel(faint_point->x(), faint_point->y()) == faint_alpha);
  CHECK(*saved.pixel(strong_point->x(), strong_point->y()) == strong_alpha);
  save_widget_artifact("ui_deko_soft_layer_selection", window);
}

void ui_select_grow_and_similar_use_magic_wand_tolerance() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);
  canvas->set_wand_tolerance(8);

  canvas->set_primary_color(QColor(220, 20, 40));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 20)),
       canvas->widget_position_for_document_point(QPoint(190, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 140)),
       canvas->widget_position_for_document_point(QPoint(70, 190)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(36, 36)));
  CHECK(!canvas->selected_document_region().contains(QPoint(66, 66)));

  require_action(window, "selectGrowAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(!canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  save_widget_artifact("ui_select_grow", *canvas);

  require_action(window, "selectSimilarAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(240, 40)));
  save_widget_artifact("ui_select_similar", *canvas);
}

void ui_complex_selection_stroke_uses_region_outline() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(180, 180)), Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(98, 98)));

  canvas->set_primary_color(QColor(20, 230, 90));
  accept_stroke_selection_dialog(7, QStringLiteral("center"));
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 45)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(132, 150)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(98, 98)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_complex_stroke_selection", *canvas);
}

void ui_selection_stroke_region_bands_have_exact_widths() {
  using patchy::ui::SelectionStrokeLocation;
  using patchy::ui::selection_stroke_region;
  const auto area = [](const QRegion& region) {
    long long total = 0;
    for (const auto& rect : region) {
      total += static_cast<long long>(rect.width()) * rect.height();
    }
    return total;
  };
  const QRect bounds(0, 0, 200, 200);
  const QRegion selection(QRect(40, 40, 40, 40));  // columns 40..79

  const auto inside = selection_stroke_region(selection, 4, SelectionStrokeLocation::Inside, bounds);
  CHECK(inside.boundingRect() == QRect(40, 40, 40, 40));
  CHECK(inside.contains(QPoint(40, 60)));
  CHECK(inside.contains(QPoint(43, 60)));
  CHECK(!inside.contains(QPoint(44, 60)));
  CHECK(!inside.contains(QPoint(39, 60)));
  CHECK(area(inside) == 40 * 40 - 32 * 32);

  const auto outside = selection_stroke_region(selection, 4, SelectionStrokeLocation::Outside, bounds);
  CHECK(outside.boundingRect() == QRect(36, 36, 48, 48));
  CHECK(outside.contains(QPoint(39, 60)));
  CHECK(outside.contains(QPoint(36, 60)));
  CHECK(!outside.contains(QPoint(35, 60)));
  CHECK(!outside.contains(QPoint(40, 60)));
  CHECK(area(outside) == 48 * 48 - 40 * 40);

  // Center splits an odd width with the larger half inside.
  const auto center = selection_stroke_region(selection, 7, SelectionStrokeLocation::Center, bounds);
  CHECK(center.boundingRect() == QRect(37, 37, 46, 46));
  CHECK(center.contains(QPoint(43, 60)));
  CHECK(!center.contains(QPoint(44, 60)));
  CHECK(center.contains(QPoint(37, 60)));
  CHECK(!center.contains(QPoint(36, 60)));
  CHECK(area(center) == 46 * 46 - 32 * 32);

  // A one-pixel centered stroke is the inner rim.
  const auto thin = selection_stroke_region(selection, 1, SelectionStrokeLocation::Center, bounds);
  CHECK(thin.boundingRect() == QRect(40, 40, 40, 40));
  CHECK(area(thin) == 40 * 40 - 38 * 38);

  // Outside bands clip to the canvas; inside bands still trace a canvas-flush edge.
  const QRegion corner(QRect(0, 0, 20, 20));
  const auto clipped = selection_stroke_region(corner, 3, SelectionStrokeLocation::Outside, bounds);
  CHECK(clipped.boundingRect() == QRect(0, 0, 23, 23));
  CHECK(area(clipped) == 23 * 23 - 20 * 20);
  const auto flush = selection_stroke_region(corner, 2, SelectionStrokeLocation::Inside, bounds);
  CHECK(flush.contains(QPoint(0, 10)));
  CHECK(flush.contains(QPoint(1, 10)));
  CHECK(!flush.contains(QPoint(2, 10)));

  // Two separate islands stroke independently; the gap between them stays clear.
  const auto islands = QRegion(QRect(10, 100, 10, 10)).united(QRect(40, 100, 10, 10));
  const auto both = selection_stroke_region(islands, 2, SelectionStrokeLocation::Center, bounds);
  CHECK(both.contains(QPoint(9, 105)));
  CHECK(both.contains(QPoint(20, 105)));
  CHECK(!both.contains(QPoint(25, 105)));
  CHECK(both.contains(QPoint(39, 105)));

  // The separable dilation matches the square structuring element exactly.
  CHECK(patchy::ui::expanded_region(QRegion(QRect(10, 10, 1, 1)), 2, bounds) == QRegion(QRect(8, 8, 5, 5)));
  // Radii past 16 px dilate a mask instead of uniting translated regions; the
  // pixels must equal the union reference, clipped to the bounds, including a
  // ragged ellipse and a shape that runs off the bounds.
  {
    const auto shape = QRegion(QRect(30, 40, 50, 20), QRegion::Ellipse)
                           .united(QRect(120, 10, 7, 90))
                           .united(QRect(-20, 150, 40, 12))
                           .united(QRect(200, 5, 1, 1));
    for (const int radius : {17, 40}) {
      QRegion horizontal;
      for (int dx = -radius; dx <= radius; ++dx) {
        horizontal = horizontal.united(shape.translated(dx, 0));
      }
      QRegion reference;
      for (int dy = -radius; dy <= radius; ++dy) {
        reference = reference.united(horizontal.translated(0, dy));
      }
      CHECK(patchy::ui::expanded_region(shape, radius, bounds) == reference.intersected(bounds));
    }
    // Photoshop's 500 px Expand maximum.
    const QRect wide(-2000, -2000, 5000, 5000);
    CHECK(patchy::ui::expanded_region(QRegion(QRect(0, 0, 1, 1)), 800, wide) ==
          QRegion(QRect(-500, -500, 1001, 1001)));
  }
  CHECK(selection_stroke_region(QRegion(), 5, SelectionStrokeLocation::Center, bounds).isEmpty());
  CHECK(selection_stroke_region(selection, 0, SelectionStrokeLocation::Center, bounds).isEmpty());
}

void ui_stroke_selection_dialog_paints_inside_center_and_outside_bands() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  const auto select_rect = [canvas](QPoint from, QPoint to) {
    drag(*canvas, canvas->widget_position_for_document_point(from), canvas->widget_position_for_document_point(to));
    const auto region = canvas->selected_document_region();
    CHECK(!region.isEmpty());
    CHECK(region == QRegion(region.boundingRect()));
    return region.boundingRect();
  };
  const auto stroke = [&](int width, const char* location, QColor color) {
    accept_stroke_selection_dialog(width, QString::fromLatin1(location), color);
    require_action(window, "editStrokeSelectionAction")->trigger();
    QApplication::processEvents();
    require_action(window, "editDeselectAction")->trigger();
    QApplication::processEvents();
  };
  const QColor white(255, 255, 255);
  const QColor red(255, 0, 0);
  const QColor blue(0, 0, 255);
  const QColor green(0, 200, 0);

  // Inside: the band starts on the selection's first column and never leaves it.
  const auto inside_rect = select_rect(QPoint(40, 40), QPoint(80, 80));
  stroke(4, "inside", red);
  const int inside_y = inside_rect.center().y();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(inside_rect.left(), inside_y)), red, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(inside_rect.left() + 3, inside_y)), red, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(inside_rect.left() + 4, inside_y)), white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(inside_rect.left() - 1, inside_y)), white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(inside_rect.center().x(), inside_rect.bottom())), red, 8));
  CHECK(color_close(canvas_pixel(*canvas, inside_rect.center()), white, 8));

  // Outside: the band hugs the selection from the outside and leaves the interior alone.
  const auto outside_rect = select_rect(QPoint(140, 40), QPoint(180, 80));
  stroke(4, "outside", blue);
  const int outside_y = outside_rect.center().y();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(outside_rect.left() - 1, outside_y)), blue, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(outside_rect.left() - 4, outside_y)), blue, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(outside_rect.left() - 5, outside_y)), white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(outside_rect.left(), outside_y)), white, 8));

  // Center: an even width splits evenly across the edge. The stroke is one undo entry.
  const auto center_rect = select_rect(QPoint(40, 140), QPoint(80, 180));
  const auto depth_before_stroke = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  stroke(6, "center", green);
  const auto depth_after_deselect = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(depth_after_deselect >= depth_before_stroke + 1);
  const int center_y = center_rect.center().y();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(center_rect.left() - 3, center_y)), green, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(center_rect.left() + 2, center_y)), green, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(center_rect.left() - 4, center_y)), white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(center_rect.left() + 3, center_y)), white, 8));

  // The last width and location are remembered for the next stroke; the color is not.
  {
    const auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("tools/strokeSelectionWidth")).toInt() == 6);
    CHECK(settings.value(QStringLiteral("tools/strokeSelectionLocation")).toString() == QStringLiteral("center"));
  }

  // Undoing back past the stroke removes the whole band at once.
  for (auto depth = depth_after_deselect; depth > depth_before_stroke; --depth) {
    patchy::ui::MainWindowTestAccess::undo(window);
    QApplication::processEvents();
  }
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before_stroke);
  // The history restore repaints in the background; wait for the canvas to settle before sampling.
  const auto settle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!canvas->render_settled() && std::chrono::steady_clock::now() < settle_deadline) {
    QApplication::processEvents();
  }
  CHECK(canvas->render_settled());
  // The undo also brought the marquee back, whose overlay tints the canvas sample, so read the
  // layer pixels: the green band is gone and the earlier red band is untouched.
  {
    const auto& doc = patchy::ui::MainWindowTestAccess::document(window);
    const auto* layer = doc.find_layer(*doc.active_layer_id());
    CHECK(layer != nullptr);
    const auto* undone = layer->pixels().pixel(center_rect.left() + 2, center_y);
    CHECK(undone[3] == 0);
    const auto* kept = layer->pixels().pixel(inside_rect.left(), inside_y);
    CHECK(kept[0] == 255 && kept[1] == 0 && kept[2] == 0 && kept[3] == 255);
  }
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  // Cancelling the dialog paints nothing.
  select_rect(QPoint(140, 140), QPoint(180, 180));
  QTimer::singleShot(0, [] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("patchyStrokeSelectionDialog")) {
        qobject_cast<QDialog*>(widget)->reject();
      }
    }
  });
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();  // drop the marquee tint before sampling
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(140, 160)), white, 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(139, 160)), white, 8));
}

void ui_layer_lock_transparency_and_keyboard_nudge_work() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* lock = window.findChild<QToolButton*>(QStringLiteral("layerLockTransparentButton"));
  CHECK(lock != nullptr);

  canvas->set_primary_color(QColor(220, 20, 40));
  use_solid_fill_settings(canvas);
  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 255), 8));

  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 20, 40), 8));

  canvas->set_primary_color(QColor(20, 90, 220));
  lock->click();
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_layer_lock_transparency", window);

  canvas->setFocus();
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 30)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_keyboard_nudge_layer", window);
}

void ui_layer_full_lock_row_control_blocks_edits_and_move() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(220, 30, 40));
  use_solid_fill_settings(canvas);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  CHECK(!lock_all->isChecked());
  lock_all->click();
  QApplication::processEvents();
  CHECK(lock_all->isChecked());

  paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  paint_row = layer_list->itemWidget(paint_item);
  CHECK(paint_row != nullptr);
  const auto badges = paint_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(badges.size() == 3);

  canvas->set_primary_color(QColor(20, 90, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("pixels are locked")));

  const auto before = canvas->active_layer_document_rect();
  CHECK(before.has_value());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  canvas->set_show_transform_controls(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(30, 30));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->active_layer_document_rect() == before);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  save_widget_artifact("ui_layer_full_lock_controls", window);
}

void ui_folder_lock_inherits_to_child_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer folder(document.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  auto child_pixels = solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer child(document.allocate_layer_id(), "Child", std::move(child_pixels));
  const auto child_id = child.id();
  child.set_bounds(patchy::Rect{10, 10, 20, 20});
  folder.add_child(std::move(child));
  document.add_layer(std::move(folder));
  document.set_active_layer(child_id);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Folder Lock"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();
  auto* folder_row = layer_list->itemWidget(folder_item);
  CHECK(folder_row != nullptr);
  auto* lock_all = window.findChild<QToolButton*>(QStringLiteral("layerLockAllButton"));
  CHECK(lock_all != nullptr);
  lock_all->click();
  QApplication::processEvents();

  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Child"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(child_item);
  child_item->setSelected(true);
  QApplication::processEvents();
  auto* child_row = layer_list->itemWidget(child_item);
  CHECK(child_row != nullptr);
  const auto child_badges = child_row->findChildren<QLabel*>(QStringLiteral("layerLockBadge"));
  CHECK(child_badges.size() == 3);
  CHECK(std::all_of(child_badges.begin(), child_badges.end(), [](QLabel* badge) {
    return badge != nullptr && badge->property("inherited").toBool() &&
           badge->toolTip().contains(QStringLiteral("folder"));
  }));

  canvas->set_primary_color(QColor(20, 80, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(15, 15)), QColor(230, 40, 40), 8));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("pixels are locked")));
  save_widget_artifact("ui_folder_lock_inheritance", window);
}

void ui_move_auto_select_ignores_locked_layers() {
  patchy::Document document(80, 80, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(80, 80, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  auto bottom_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(30, 90, 220, 255));
  patchy::Layer bottom(document.allocate_layer_id(), "Unlocked", std::move(bottom_pixels));
  bottom.set_bounds(patchy::Rect{16, 16, 24, 24});
  document.add_layer(std::move(bottom));
  auto top_pixels = solid_pixels(24, 24, patchy::PixelFormat::rgba8(), QColor(230, 40, 40, 255));
  patchy::Layer top(document.allocate_layer_id(), "Locked", std::move(top_pixels));
  top.set_bounds(patchy::Rect{16, 16, 24, 24});
  patchy::set_layer_locks_position(top, true);
  document.add_layer(std::move(top));

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Locked Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  canvas->set_show_transform_controls(false);
  const auto click = canvas->widget_position_for_document_point(QPoint(20, 20));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* unlocked_item = require_layer_item(*layer_list, QStringLiteral("Unlocked"));
  CHECK(unlocked_item->isSelected());
  CHECK(layer_list->currentItem() == unlocked_item);
  save_widget_artifact("ui_move_auto_select_locked_layer", window);
}

void ui_lasso_selection_draws_freeform_region() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  save_widget_artifact("ui_lasso_selection", *canvas);
}

void ui_lasso_click_deselects() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  // A plain click (no drag) inside the canvas deselects in Replace mode.
  const auto inside = canvas->widget_position_for_document_point(QPoint(70, 70));
  send_mouse(*canvas, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  // Re-select, then verify a plain click in the grey area also deselects.
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  const auto grey = canvas->widget_position_for_document_point(QPoint(-40, -40));
  CHECK(canvas->rect().contains(grey));
  send_mouse(*canvas, QEvent::MouseButtonPress, grey, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, grey, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_marquee_drag_moves_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(130, 130)));
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());
  CHECK(canvas->selected_document_region().contains(QPoint(60, 60)));

  // Grab inside the selection and drag it down-right: the outline moves and the
  // size is preserved.
  const auto grab = canvas->widget_position_for_document_point(before->center());
  const auto drop = canvas->widget_position_for_document_point(before->center() + QPoint(40, 30));
  drag(*canvas, grab, drop);

  const auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());
  CHECK(after->left() >= before->left() + 39);
  CHECK(after->left() <= before->left() + 41);
  CHECK(after->top() >= before->top() + 29);
  CHECK(after->top() <= before->top() + 31);
  // The area it moved off is no longer selected; the new area is.
  CHECK(!canvas->selected_document_region().contains(QPoint(60, 60)));
  CHECK(canvas->selected_document_region().contains(after->center()));

  // A plain click inside the selection (no drag) still deselects.
  const auto click = canvas->widget_position_for_document_point(after->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

namespace {
// Widget position of a resize handle on the committed marquee rect. The shared
// transform handles sit on the exclusive right/bottom edge, so the right-edge
// handle is at document x = rect.x() + rect.width().
QPoint marquee_handle_position(const patchy::ui::CanvasWidget& canvas, QRect rect, int x_edge, int y_edge) {
  // x_edge / y_edge: 0 = left/top, 1 = middle, 2 = right/bottom.
  const auto x = rect.x() + (x_edge == 0 ? 0 : x_edge == 1 ? rect.width() / 2 : rect.width());
  const auto y = rect.y() + (y_edge == 0 ? 0 : y_edge == 1 ? rect.height() / 2 : rect.height());
  return canvas.widget_position_for_document_point(QPoint(x, y));
}

void drag_marquee_handle(patchy::ui::CanvasWidget& canvas, QPoint from, QPoint to) {
  send_mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  send_mouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

bool within_one(int value, int expected) {
  return value >= expected - 1 && value <= expected + 1;
}
}  // namespace

void ui_marquee_edge_handle_drag_resizes_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  auto* redo_action = require_action_by_text(window, QStringLiteral("Redo"));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  // Hovering the right-edge handle shows the horizontal resize cursor.
  const auto right_handle = marquee_handle_position(*canvas, *original, 2, 1);
  send_mouse(*canvas, QEvent::MouseMove, right_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  // Off the handle the tool cursor comes back.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(10, 10)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() != Qt::SizeHorCursor);

  // Dragging the right edge 30 px right widens the selection and nothing else.
  const auto wider = canvas->widget_position_for_document_point(
      QPoint(original->x() + original->width() + 30, original->y() + original->height() / 2));
  drag_marquee_handle(*canvas, right_handle, wider);
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->x() == original->x());
  CHECK(resized->y() == original->y());
  CHECK(resized->height() == original->height());
  CHECK(within_one(resized->width(), original->width() + 30));
  CHECK(canvas->selected_document_region().contains(QPoint(original->x() + original->width() + 15,
                                                            original->y() + original->height() / 2)));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Resize Selection"));

  // Undo restores the drawn rect and the handles come back with it, so a
  // second handle drag works; Redo lands on the resized rect again.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == original);
  send_mouse(*canvas, QEvent::MouseMove, right_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  redo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == resized);
  undo_action->trigger();
  QApplication::processEvents();
  const auto top_handle = marquee_handle_position(*canvas, *original, 1, 0);
  const auto taller = canvas->widget_position_for_document_point(
      QPoint(original->x() + original->width() / 2, original->y() - 20));
  drag_marquee_handle(*canvas, top_handle, taller);
  const auto taller_rect = canvas->selected_document_rect();
  CHECK(taller_rect.has_value());
  CHECK(within_one(taller_rect->y(), original->y() - 20));
  CHECK(within_one(taller_rect->height(), original->height() + 20));
  CHECK(taller_rect->x() == original->x());
  CHECK(taller_rect->width() == original->width());
  save_widget_artifact("ui_marquee_resize_handles", *canvas);
}

void ui_marquee_corner_handle_drag_and_shift_aspect() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  // A plain corner drag moves both axes.
  const auto corner = marquee_handle_position(*canvas, *original, 2, 2);
  send_mouse(*canvas, QEvent::MouseMove, corner, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeFDiagCursor);
  drag_marquee_handle(*canvas, corner,
                      canvas->widget_position_for_document_point(
                          QPoint(original->x() + original->width() + 30, original->y() + original->height() + 30)));
  auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == original->topLeft());
  CHECK(within_one(resized->width(), original->width() + 30));
  CHECK(within_one(resized->height(), original->height() + 30));

  // Shift held while dragging a corner holds the aspect ratio the rect had when
  // the drag began. (Shift at the press means Add, exactly as for the interior
  // move, so it is pressed after the handle is grabbed.)
  const auto start = *resized;
  const auto ratio = static_cast<double>(start.width()) / start.height();
  const auto start_corner = marquee_handle_position(*canvas, start, 2, 2);
  const auto shift_target = canvas->widget_position_for_document_point(
      QPoint(start.x() + start.width() + 60, start.y() + start.height() + 10));
  send_mouse(*canvas, QEvent::MouseButtonPress, start_corner, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, shift_target, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, shift_target, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == start.topLeft());
  CHECK(resized->height() > start.height());
  CHECK(std::abs(resized->width() - static_cast<int>(std::round(resized->height() * ratio))) <= 2);

  // Dragging the left edge through the right edge flips cleanly.
  const auto before_flip = *resized;
  drag_marquee_handle(*canvas, marquee_handle_position(*canvas, before_flip, 0, 1),
                      canvas->widget_position_for_document_point(
                          QPoint(before_flip.x() + before_flip.width() + 20,
                                 before_flip.y() + before_flip.height() / 2)));
  resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(within_one(resized->x(), before_flip.x() + before_flip.width()));
  CHECK(within_one(resized->width(), 20));
  CHECK(resized->height() == before_flip.height());
}

void ui_elliptical_marquee_handle_drag_keeps_ellipse() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::EllipticalMarquee);
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());
  CHECK(canvas->selected_document_region().rectCount() > 1);

  drag_marquee_handle(*canvas, marquee_handle_position(*canvas, *original, 2, 1),
                      canvas->widget_position_for_document_point(
                          QPoint(original->x() + original->width() + 40, original->y() + original->height() / 2)));
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(within_one(resized->width(), original->width() + 40));
  CHECK(resized->height() == original->height());
  // Still an ellipse drawn into the new bounds, not a stretched region.
  CHECK(canvas->selected_document_region() == QRegion(*resized, QRegion::Ellipse));
  CHECK(!canvas->selected_document_region().contains(resized->topLeft()));
  CHECK(canvas->selected_document_region().contains(resized->center()));
}

void ui_marquee_handle_drag_space_repositions_then_resumes() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  // Start widening by the right edge.
  const auto right_handle = marquee_handle_position(*canvas, *original, 2, 1);
  const auto mid_y = original->y() + original->height() / 2;
  send_mouse(*canvas, QEvent::MouseButtonPress, right_handle, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove,
             canvas->widget_position_for_document_point(QPoint(original->x() + original->width() + 30, mid_y)),
             Qt::NoButton, Qt::LeftButton);
  const auto widened = canvas->selected_document_rect();
  CHECK(widened.has_value());
  CHECK(within_one(widened->width(), original->width() + 30));

  // Space held: the pointer now slides the whole rect, size intact.
  send_key_press(*canvas, Qt::Key_Space);
  CHECK(canvas->cursor().shape() == Qt::SizeAllCursor);
  send_mouse(*canvas, QEvent::MouseMove,
             canvas->widget_position_for_document_point(
                 QPoint(original->x() + original->width() + 30 + 20, mid_y + 15)),
             Qt::NoButton, Qt::LeftButton);
  const auto slid = canvas->selected_document_rect();
  CHECK(slid.has_value());
  CHECK(slid->size() == widened->size());
  CHECK(slid->topLeft() == widened->topLeft() + QPoint(20, 15));

  // Space released: the same drag resumes as a resize from the slid position,
  // the right edge tracking the pointer and the left edge staying put.
  send_key_release(*canvas, Qt::Key_Space);
  const auto final_pointer = canvas->widget_position_for_document_point(
      QPoint(original->x() + original->width() + 30 + 20 + 10, mid_y + 15));
  send_mouse(*canvas, QEvent::MouseMove, final_pointer, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, final_pointer, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto resumed = canvas->selected_document_rect();
  CHECK(resumed.has_value());
  CHECK(resumed->topLeft() == slid->topLeft());
  CHECK(resumed->height() == slid->height());
  CHECK(within_one(resumed->width(), slid->width() + 10));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Resize Selection"));

  // One history entry covers the slide and the resize.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == original);
  save_widget_artifact("ui_marquee_handle_space_reposition", *canvas);
}

void ui_marquee_gestures_never_snap_to_their_own_selection() {
  // Snapping stays at its defaults (all targets on): the live selection must not
  // be a target for the gesture that is writing it, or 1 px pointer steps would
  // snap the rect back to its previous position every move.
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_zoom(1.0);
  const auto at = [canvas](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x, y)); };

  // Drag-out in 1 px steps lands exactly where the pointer stops (the drag-out
  // rect includes both the anchor and the current pixel).
  send_mouse(*canvas, QEvent::MouseButtonPress, at(40, 40), Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 60; ++step) {
    send_mouse(*canvas, QEvent::MouseMove, at(40 + step, 40 + std::min(step, 40)), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(canvas->selected_document_rect() == QRect(40, 40, 61, 41));

  // Space slide in 1 px steps moves by exactly the pointer delta.
  send_key_press(*canvas, Qt::Key_Space);
  for (int step = 1; step <= 20; ++step) {
    send_mouse(*canvas, QEvent::MouseMove, at(100 + step, 80 + step), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(canvas->selected_document_rect() == QRect(60, 60, 61, 41));
  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(120, 100), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == QRect(60, 60, 61, 41));

  // Handle resize in 1 px steps (the moved edge takes the pointer coordinate
  // on the exclusive right edge), then a Space slide inside the same drag.
  send_mouse(*canvas, QEvent::MouseButtonPress, marquee_handle_position(*canvas, QRect(60, 60, 61, 41), 2, 1),
             Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 20; ++step) {
    send_mouse(*canvas, QEvent::MouseMove, at(121 + step, 80), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(canvas->selected_document_rect() == QRect(60, 60, 81, 41));
  send_key_press(*canvas, Qt::Key_Space);
  for (int step = 1; step <= 10; ++step) {
    send_mouse(*canvas, QEvent::MouseMove, at(141 + step, 80 + step), Qt::NoButton, Qt::LeftButton);
  }
  CHECK(canvas->selected_document_rect() == QRect(70, 70, 81, 41));
  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(151, 90), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == QRect(70, 70, 81, 41));

  // A committed selection is still a target for other gestures: a fresh
  // Add-mode drag-out 3 px short of the existing right edge (151) snaps to it.
  send_mouse(*canvas, QEvent::MouseButtonPress, at(200, 200), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseMove, at(154, 240), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, at(154, 240), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
  QApplication::processEvents();
  const auto added = canvas->selected_document_rect();
  CHECK(added.has_value());
  CHECK(added->left() == 70);
  CHECK(canvas->selected_document_region().contains(QPoint(152, 220)));
}

void ui_marquee_feathered_resize_rerasterizes_soft_edge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);
  canvas->set_selection_feather_radius(4);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  CHECK(canvas->has_selection());
  const QRect drawn(40, 40, 60, 40);
  const auto mid_y = drawn.y() + drawn.height() / 2;
  CHECK(canvas->selection_alpha_at(QPoint(drawn.x() + drawn.width() + 15, mid_y)) == 0);

  // The handle sits on the drawn rect, not on the feather-padded bounds.
  const auto right_handle = marquee_handle_position(*canvas, drawn, 2, 1);
  send_mouse(*canvas, QEvent::MouseMove, right_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  drag_marquee_handle(*canvas, right_handle,
                      canvas->widget_position_for_document_point(QPoint(drawn.x() + drawn.width() + 30, mid_y)));

  // The area the edge moved over is fully selected and the new edge is soft.
  CHECK(canvas->selection_alpha_at(QPoint(drawn.x() + drawn.width() + 15, mid_y)) == 255);
  const auto edge_alpha = canvas->selection_alpha_at(QPoint(drawn.x() + drawn.width() + 31, mid_y));
  CHECK(edge_alpha > 0);
  CHECK(edge_alpha < 255);
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  // Bounds include the feather padding on every side.
  CHECK(resized->x() < drawn.x());
  CHECK(resized->x() + resized->width() > drawn.x() + drawn.width() + 30);
}

void ui_marquee_handles_follow_move_and_vanish_after_other_edits() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  // Moving the selection takes the handles along.
  drag(*canvas, canvas->widget_position_for_document_point(original->center()),
       canvas->widget_position_for_document_point(original->center() + QPoint(40, 30)));
  const auto moved = canvas->selected_document_rect();
  CHECK(moved.has_value());
  CHECK(moved->size() == original->size());
  const auto moved_right_handle = marquee_handle_position(*canvas, *moved, 2, 1);
  send_mouse(*canvas, QEvent::MouseMove, moved_right_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
  drag_marquee_handle(*canvas, moved_right_handle,
                      canvas->widget_position_for_document_point(
                          QPoint(moved->x() + moved->width() + 20, moved->y() + moved->height() / 2)));
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == moved->topLeft());
  CHECK(within_one(resized->width(), moved->width() + 20));

  // Adding a second rectangle (Shift) leaves a combined shape with no handles:
  // the former handle spot shows the tool cursor and a press there starts a
  // new marquee instead of resizing.
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(10, 10)),
       canvas->widget_position_for_document_point(QPoint(30, 30)), Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(20, 20)));
  CHECK(canvas->selected_document_region().contains(resized->center()));
  const auto stale_handle = marquee_handle_position(*canvas, *resized, 2, 1);
  send_mouse(*canvas, QEvent::MouseMove, stale_handle, Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() != Qt::SizeHorCursor);
  drag_marquee_handle(*canvas, stale_handle,
                      canvas->widget_position_for_document_point(
                          QPoint(resized->x() + resized->width() + 25, resized->y() + resized->height() + 25)));
  const auto fresh = canvas->selected_document_rect();
  CHECK(fresh.has_value());
  CHECK(within_one(fresh->x(), resized->x() + resized->width()));
  CHECK(!canvas->selected_document_region().contains(QPoint(20, 20)));

  // The Lasso tool never shows marquee handles even on a marquee-drawn rect.
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);
  send_mouse(*canvas, QEvent::MouseMove, marquee_handle_position(*canvas, *fresh, 2, 1), Qt::NoButton,
             Qt::NoButton);
  CHECK(canvas->cursor().shape() != Qt::SizeHorCursor);
}

void ui_marquee_handle_click_keeps_selection_and_hint_shows() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action(window, "toolMarqueeAction")->trigger();
  QApplication::processEvents();
  CHECK(window.statusBar()->currentMessage().startsWith(
      QStringLiteral("Rectangular Marquee: drag to select. Drag a handle to resize")));
  canvas->set_snap_enabled(false);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 80)));
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  // A click on a handle with no travel neither resizes nor deselects, and a
  // click inside the selection still deselects as before.
  const auto handle = marquee_handle_position(*canvas, *original, 2, 2);
  send_mouse(*canvas, QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, handle, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == original);
  CHECK(window.statusBar()->currentMessage() != QStringLiteral("Resize Selection"));
  const auto inside = canvas->widget_position_for_document_point(original->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  // Reselect brings the handles back with the selection.
  require_action(window, "selectReselectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect() == original);
  send_mouse(*canvas, QEvent::MouseMove, marquee_handle_position(*canvas, *original, 2, 1), Qt::NoButton,
             Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::SizeHorCursor);
}

void ui_lasso_drag_moves_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Lasso);

  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(QPoint(50, 50)),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(120, 50)),
             Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(120, 120)),
             Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(QPoint(50, 120)),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());

  // Grabbing inside the lasso selection drags the outline (does not start a new lasso).
  const auto grab = canvas->widget_position_for_document_point(before->center());
  const auto drop = canvas->widget_position_for_document_point(before->center() + QPoint(35, 25));
  drag(*canvas, grab, drop);

  const auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());
  CHECK(after->left() >= before->left() + 34);
  CHECK(after->left() <= before->left() + 36);
}

void ui_selection_arrow_keys_nudge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(120, 110)));
  const auto before = canvas->selected_document_rect();
  CHECK(before.has_value());

  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  auto after = canvas->selected_document_rect();
  CHECK(after.has_value());
  CHECK(after->left() == before->left() + 1);
  CHECK(after->top() == before->top() + 1);
  CHECK(after->width() == before->width());
  CHECK(after->height() == before->height());

  // Shift nudges by 10px.
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  after = canvas->selected_document_rect();
  CHECK(after->left() == before->left() + 11);

  // Holding an arrow (auto-repeat key presses) keeps nudging the selection.
  const auto left_before_hold = canvas->selected_document_rect()->left();
  for (int i = 0; i < 3; ++i) {
    QKeyEvent autorep(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(canvas, &autorep);
    QApplication::processEvents();
  }
  CHECK(canvas->selected_document_rect()->left() == left_before_hold + 3);

  // With the Move tool active, arrow keys move the layer, not the selection.
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  const auto sel_before = canvas->selected_document_rect();
  send_key(*canvas, Qt::Key_Right);
  const auto sel_after = canvas->selected_document_rect();
  CHECK(sel_after.has_value());
  CHECK(sel_after->left() == sel_before->left());
  CHECK(sel_after->top() == sel_before->top());
}

void ui_selection_moves_coalesce_into_one_undo_step() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* undo_action = require_action_by_text(window, QStringLiteral("Undo"));
  auto* redo_action = require_action_by_text(window, QStringLiteral("Redo"));
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(50, 50)),
       canvas->widget_position_for_document_point(QPoint(120, 110)));
  CHECK(canvas->has_selection());
  const auto origin = canvas->selected_document_rect()->topLeft();

  // A run of nudges (including key auto-repeat) moves the selection but collapses
  // into a single undo step.
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Right);
  send_key(*canvas, Qt::Key_Down);
  for (int i = 0; i < 3; ++i) {
    QKeyEvent autorep(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(canvas, &autorep);
    QApplication::processEvents();
  }
  CHECK(canvas->selected_document_rect()->topLeft() == origin + QPoint(5, 1));

  // One undo returns to the pre-move position (not just one nudge back); one redo
  // restores the final moved position.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin);
  redo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin + QPoint(5, 1));

  // The whole run is one entry sitting on top of the marquee: undoing twice
  // removes the move, then the selection itself.
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_rect()->topLeft() == origin);
  undo_action->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_selection_cursor_shows_combine_mode_badge() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Marquee);
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  // The marquee uses the same drawn crosshair (BitmapCursor) in every mode, so
  // toggling a modifier never shifts or recolours it; Replace just omits the
  // badge.
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(40, 40)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  CHECK(canvas->cursor().hotSpot() == QPoint(10, 10));
  const auto replace_image = canvas->cursor().pixmap().toImage();
  CHECK(!replace_image.isNull());

  // The toolbar combine mode badges the crosshair with no modifier held.
  // (Checked before any synthetic key events, which would leave the global
  // modifier state dirty for set_selection_mode's lookup.)
  canvas->set_selection_mode(patchy::ui::CanvasWidget::SelectionMode::Intersect);
  CHECK(canvas->cursor().hotSpot() == QPoint(10, 10));
  const auto intersect_image = canvas->cursor().pixmap().toImage();
  CHECK(intersect_image != replace_image);
  canvas->set_selection_mode(patchy::ui::CanvasWidget::SelectionMode::Replace);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);

  // With no active selection there is nothing to add to / subtract from, so
  // Shift and Alt do NOT switch the combine mode (they act as geometry
  // constraints instead). The badge therefore stays on the tool's own mode.
  send_key_press(window, Qt::Key_Shift, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);
  send_key_release(window, Qt::Key_Shift, Qt::ShiftModifier);
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);

  // Once there is a selection to combine with, the same modifiers badge the
  // crosshair. The app-level event filter catches a Shift press delivered to the
  // window even with the pointer stationary. (The press event reports no
  // modifier yet, so the cursor logic folds the pressed key in.)
  canvas->select_all();
  QApplication::processEvents();
  send_key_press(window, Qt::Key_Shift, Qt::NoModifier);
  const auto add_image = canvas->cursor().pixmap().toImage();
  CHECK(add_image != replace_image);
  CHECK(add_image != intersect_image);

  // The release event still reports Shift; the handler must clear it to return
  // to the plain crosshair.
  send_key_release(window, Qt::Key_Shift, Qt::ShiftModifier);
  CHECK(canvas->cursor().pixmap().toImage() == replace_image);

  // Alt shows a distinct "-" badge.
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  const auto subtract_image = canvas->cursor().pixmap().toImage();
  CHECK(subtract_image != replace_image);
  CHECK(subtract_image != add_image);
  CHECK(subtract_image != intersect_image);
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
}

void ui_brush_alt_shows_eyedropper_cursor() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_brush_size(1);  // small footprint: a real OS cursor, not the overlay path
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  // Establish the normal brush cursor through the Alt handler: an Alt-up with no
  // Alt held rebuilds the tool cursor from the folded modifier state, so this is
  // independent of any stale global keyboard-modifier state a prior test may have
  // left behind (the offscreen platform never clears a synthetic Alt press).
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
  const auto brush_hotspot = canvas->cursor().hotSpot();
  const auto brush_image = canvas->cursor().pixmap().toImage();
  CHECK(!brush_image.isNull());
  CHECK(brush_hotspot != QPoint(5, 27));

  // Holding Alt turns the brush into a temporary colour picker; the cursor swaps
  // to the eyedropper immediately (pointer stationary), with its hotspot on the
  // lower-left sampling tip.
  send_key_press(window, Qt::Key_Alt, Qt::NoModifier);
  CHECK(canvas->cursor().hotSpot() == QPoint(5, 27));
  const auto eyedropper_image = canvas->cursor().pixmap().toImage();
  CHECK(!eyedropper_image.isNull());
  CHECK(eyedropper_image != brush_image);

  // Releasing Alt restores the exact brush cursor. The release event carries the
  // authoritative modifier state (Alt cleared) folded in by the event filter, so
  // the swap-back does not depend on the global keyboard-modifier state being
  // refreshed yet.
  send_key_release(window, Qt::Key_Alt, Qt::AltModifier);
  CHECK(canvas->cursor().hotSpot() == brush_hotspot);
  CHECK(canvas->cursor().pixmap().toImage() == brush_image);

  // The standalone Eyedropper tool uses the same cursor with no modifier held.
  canvas->set_tool(patchy::ui::CanvasTool::Eyedropper);
  send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(QPoint(41, 41)),
             Qt::NoButton, Qt::NoButton);
  CHECK(canvas->cursor().hotSpot() == QPoint(5, 27));
  CHECK(canvas->cursor().pixmap().toImage() == eyedropper_image);
}

}  // namespace

std::vector<patchy::test::TestCase> selection_marquee_lasso_tests_part2() {
  return {
      {"ui_complex_selection_draws_region_outline", ui_complex_selection_draws_region_outline},
      {"ui_ctrl_h_hides_selection_edges_without_blue_tint", ui_ctrl_h_hides_selection_edges_without_blue_tint},
      {"ui_select_inverse_and_extended_blend_modes_work", ui_select_inverse_and_extended_blend_modes_work},
      {"ui_selection_expand_contract_and_layer_transparency_work",
       ui_selection_expand_contract_and_layer_transparency_work},
      {"ui_ctrl_click_layer_loads_layer_transparency", ui_ctrl_click_layer_loads_layer_transparency},
      {"ui_ctrl_click_layer_and_mask_preserve_soft_coverage",
       ui_ctrl_click_layer_and_mask_preserve_soft_coverage},
      {"ui_deko_layer_transparency_selection_and_channel_save_if_available",
       ui_deko_layer_transparency_selection_and_channel_save_if_available},
      {"ui_select_grow_and_similar_use_magic_wand_tolerance",
       ui_select_grow_and_similar_use_magic_wand_tolerance},
      {"ui_complex_selection_stroke_uses_region_outline", ui_complex_selection_stroke_uses_region_outline},
      {"ui_selection_stroke_region_bands_have_exact_widths", ui_selection_stroke_region_bands_have_exact_widths},
      {"ui_stroke_selection_dialog_paints_inside_center_and_outside_bands",
       ui_stroke_selection_dialog_paints_inside_center_and_outside_bands},
      {"ui_layer_lock_transparency_and_keyboard_nudge_work", ui_layer_lock_transparency_and_keyboard_nudge_work},
      {"ui_layer_full_lock_row_control_blocks_edits_and_move",
       ui_layer_full_lock_row_control_blocks_edits_and_move},
      {"ui_folder_lock_inherits_to_child_layers", ui_folder_lock_inherits_to_child_layers},
      {"ui_move_auto_select_ignores_locked_layers", ui_move_auto_select_ignores_locked_layers},
      {"ui_lasso_selection_draws_freeform_region", ui_lasso_selection_draws_freeform_region},
      {"ui_lasso_click_deselects", ui_lasso_click_deselects},
      {"ui_marquee_drag_moves_selection", ui_marquee_drag_moves_selection},
      {"ui_marquee_edge_handle_drag_resizes_selection", ui_marquee_edge_handle_drag_resizes_selection},
      {"ui_marquee_handle_drag_space_repositions_then_resumes",
       ui_marquee_handle_drag_space_repositions_then_resumes},
      {"ui_marquee_gestures_never_snap_to_their_own_selection",
       ui_marquee_gestures_never_snap_to_their_own_selection},
      {"ui_marquee_corner_handle_drag_and_shift_aspect", ui_marquee_corner_handle_drag_and_shift_aspect},
      {"ui_elliptical_marquee_handle_drag_keeps_ellipse", ui_elliptical_marquee_handle_drag_keeps_ellipse},
      {"ui_marquee_feathered_resize_rerasterizes_soft_edge", ui_marquee_feathered_resize_rerasterizes_soft_edge},
      {"ui_marquee_handles_follow_move_and_vanish_after_other_edits",
       ui_marquee_handles_follow_move_and_vanish_after_other_edits},
      {"ui_marquee_handle_click_keeps_selection_and_hint_shows",
       ui_marquee_handle_click_keeps_selection_and_hint_shows},
      {"ui_lasso_drag_moves_selection", ui_lasso_drag_moves_selection},
      {"ui_selection_arrow_keys_nudge", ui_selection_arrow_keys_nudge},
      {"ui_selection_moves_coalesce_into_one_undo_step", ui_selection_moves_coalesce_into_one_undo_step},
      {"ui_selection_cursor_shows_combine_mode_badge", ui_selection_cursor_shows_combine_mode_badge},
      {"ui_brush_alt_shows_eyedropper_cursor", ui_brush_alt_shows_eyedropper_cursor},
  };
}
