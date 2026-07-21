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
  for (const auto [x, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
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
  for (const auto [y, expected] : std::array<std::pair<int, std::uint8_t>, 6>{
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
  canvas->set_brush_size(7);
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 45)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(132, 150)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(98, 98)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_complex_stroke_selection", *canvas);
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
      {"ui_layer_lock_transparency_and_keyboard_nudge_work", ui_layer_lock_transparency_and_keyboard_nudge_work},
      {"ui_layer_full_lock_row_control_blocks_edits_and_move",
       ui_layer_full_lock_row_control_blocks_edits_and_move},
      {"ui_folder_lock_inherits_to_child_layers", ui_folder_lock_inherits_to_child_layers},
      {"ui_move_auto_select_ignores_locked_layers", ui_move_auto_select_ignores_locked_layers},
      {"ui_lasso_selection_draws_freeform_region", ui_lasso_selection_draws_freeform_region},
      {"ui_lasso_click_deselects", ui_lasso_click_deselects},
      {"ui_marquee_drag_moves_selection", ui_marquee_drag_moves_selection},
      {"ui_lasso_drag_moves_selection", ui_lasso_drag_moves_selection},
      {"ui_selection_arrow_keys_nudge", ui_selection_arrow_keys_nudge},
      {"ui_selection_moves_coalesce_into_one_undo_step", ui_selection_moves_coalesce_into_one_undo_step},
      {"ui_selection_cursor_shows_combine_mode_badge", ui_selection_cursor_shows_combine_mode_badge},
      {"ui_brush_alt_shows_eyedropper_cursor", ui_brush_alt_shows_eyedropper_cursor},
  };
}
