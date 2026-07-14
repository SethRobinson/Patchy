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

void ui_warp_render_matches_photoshop_preview_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] e6 warp fixture missing\n";
    return;
  }
  // The fixture's stored pixels ARE Photoshop's warped render; re-rendering through
  // Patchy's warp pipeline must stay close to them.
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::Layer* layer = nullptr;
  for (auto& candidate : document.layers()) {
    if (candidate.name() == "e5_a_40x30") {
      layer = &candidate;
    }
  }
  CHECK(layer != nullptr);
  const auto photoshop_pixels = layer->pixels();
  const auto photoshop_bounds = layer->bounds();
  CHECK(patchy::smart_object_warp_from_layer(*layer).has_value());
  CHECK(patchy::ui::refresh_smart_object_layer_preview(document, *layer,
                                                       patchy::ui::CanvasWidget::TransformInterpolation::Bicubic));
  const auto& rendered_pixels = layer->pixels();
  const auto rendered_bounds = layer->bounds();
  std::cout << "  warp bounds: PS " << photoshop_bounds.x << "," << photoshop_bounds.y << " "
            << photoshop_bounds.width << "x" << photoshop_bounds.height << "  Patchy " << rendered_bounds.x
            << "," << rendered_bounds.y << " " << rendered_bounds.width << "x" << rendered_bounds.height
            << '\n';
  CHECK(std::abs(rendered_bounds.x - photoshop_bounds.x) <= 4);
  CHECK(std::abs(rendered_bounds.y - photoshop_bounds.y) <= 4);

  double total_delta = 0.0;
  int compared = 0;
  for (int y = 0; y < photoshop_bounds.height; ++y) {
    for (int x = 0; x < photoshop_bounds.width; ++x) {
      const auto* ps = photoshop_pixels.pixel(x, y);
      const int rx = photoshop_bounds.x + x - rendered_bounds.x;
      const int ry = photoshop_bounds.y + y - rendered_bounds.y;
      if (ps == nullptr || rx < 0 || ry < 0 || rx >= rendered_pixels.width() || ry >= rendered_pixels.height()) {
        continue;
      }
      const auto* ours = rendered_pixels.pixel(rx, ry);
      if (ours == nullptr || ps[3] < 200 || ours[3] < 200) {
        continue;  // compare solid interior pixels (edge AA differs slightly)
      }
      total_delta += std::abs(int(ps[0]) - int(ours[0])) + std::abs(int(ps[1]) - int(ours[1])) +
                     std::abs(int(ps[2]) - int(ours[2]));
      ++compared;
    }
  }
  CHECK(compared > 200);
  const double mean_delta = total_delta / (compared * 3.0);
  if (mean_delta >= 12.0) {
    std::cout << "  warp render mean channel delta " << mean_delta << " over " << compared << " px\n";
  }
  CHECK(mean_delta < 12.0);
}

void ui_warped_smart_object_free_transform_rerenders() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] e6 warp fixture missing\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(path.wstring()));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const patchy::Layer* warped = nullptr;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "e5_a_40x30") {
      warped = &layer;
    }
  }
  CHECK(warped != nullptr);
  const auto layer_id = warped->id();
  // A supported warp imports UNLOCKED: no badge, no import notice, transforms allowed.
  CHECK(patchy::smart_object_lock_reason(*warped).empty());
  CHECK(patchy::smart_object_warp_from_layer(*warped).has_value());

  // Select through the LIST so the canvas selection follows.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* item = require_layer_item(*layer_list, QStringLiteral("e5_a_40x30"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(item);
  item->setSelected(true);
  QApplication::processEvents();
  CHECK(document.active_layer_id() == layer_id);

  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto before_placement = patchy::smart_object_placement_from_layer(*document.find_layer(layer_id));
  CHECK(before_placement.has_value());
  CHECK(canvas->begin_free_transform());
  const auto controls = canvas->transform_controls_state();
  CHECK(controls.has_value());
  CHECK(canvas->set_transform_controls_state(controls->reference_position, 200.0, 200.0, 0.0));
  QApplication::processEvents();
  canvas->finish_free_transform();
  QApplication::processEvents();

  const auto* transformed = document.find_layer(layer_id);
  CHECK(transformed != nullptr);
  // The hull quad doubled and the warp survived (mesh is content-space, untouched).
  const auto warp_after = patchy::smart_object_warp_from_layer(*transformed);
  CHECK(warp_after.has_value());
  const auto after_placement = patchy::smart_object_placement_from_layer(*transformed);
  CHECK(after_placement.has_value());
  const auto before_width = before_placement->transform[2] - before_placement->transform[0];
  const auto after_width = after_placement->transform[2] - after_placement->transform[0];
  CHECK(std::abs(after_width - before_width * 2.0) < 0.6);
  CHECK(patchy::layer_smart_object_block_dirty(*transformed));
  const auto raster_status =
      std::as_const(*transformed).metadata().find(patchy::kLayerMetadataSmartObjectRasterStatus);
  CHECK(raster_status != std::as_const(*transformed).metadata().end() &&
        raster_status->second == patchy::kSmartObjectRasterStatusPatchy);
  // Re-rendered through the warp: pixels track the doubled hull quad, not the content rect.
  CHECK(std::abs(static_cast<double>(transformed->bounds().width) - after_width) < 4.0);

  // The regenerated SoLd resaves and re-parses with the mesh byte-exact and the
  // transformed quad intact.
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const patchy::Layer* reread_layer = nullptr;
  for (const auto& layer : reread.layers()) {
    if (layer.name() == "e5_a_40x30") {
      reread_layer = &layer;
    }
  }
  CHECK(reread_layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
  const auto reread_warp = patchy::smart_object_warp_from_layer(*reread_layer);
  CHECK(reread_warp.has_value());
  CHECK(reread_warp->mesh_xs == warp_after->mesh_xs);
  CHECK(reread_warp->mesh_ys == warp_after->mesh_ys);
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  CHECK(std::abs(reread_placement->transform[0] - after_placement->transform[0]) < 1e-6);
  CHECK(std::abs(reread_placement->transform[4] - after_placement->transform[4]) < 1e-6);
  // E11 acceptance artifact: a Photoshop-authored mesh transformed through Patchy,
  // for the PS open-and-resave gate.
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_warped_smart_object_transformed.psd"));
}

void ui_warped_smart_object_edit_commit_keeps_warp() {
  const auto path = patchy::test::local_psd_fixture_path("ps2026_e6_warp_before.psd");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] e6 warp fixture missing\n";
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(path.wstring()));
  QApplication::processEvents();
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  const auto parent_tab_index = tabs->currentIndex();
  const auto tab_count_before = tabs->count();
  auto& parent_document = patchy::ui::MainWindowTestAccess::document(window);
  const patchy::Layer* warped = nullptr;
  for (const auto& layer : std::as_const(parent_document).layers()) {
    if (layer.name() == "e5_a_40x30") {
      warped = &layer;
    }
  }
  CHECK(warped != nullptr);
  const auto layer_id = warped->id();
  parent_document.set_active_layer(layer_id);
  QApplication::processEvents();

  // Edit Contents opens the embedded png child; a same-size edit commits back.
  patchy::ui::MainWindowTestAccess::open_smart_object_contents(window);
  QApplication::processEvents();
  CHECK(tabs->count() == tab_count_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_smart_object_child(window));
  auto& child_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto child_width = child_document.width();
  const auto child_height = child_document.height();
  CHECK(child_width == 40 && child_height == 30);
  CHECK(!child_document.layers().empty());
  auto& child_layer = child_document.layers().front();
  // Magenta: a color the fixture's artwork cannot plausibly contain.
  child_layer.set_pixels(
      solid_pixels(child_width, child_height, patchy::PixelFormat::rgba8(), QColor(220, 20, 200, 255)));
  child_layer.set_bounds(patchy::Rect{0, 0, child_width, child_height});
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  QApplication::processEvents();

  tabs->setCurrentIndex(parent_tab_index);
  QApplication::processEvents();
  auto& parent_after = patchy::ui::MainWindowTestAccess::document(window);
  const auto* committed = parent_after.find_layer(layer_id);
  CHECK(committed != nullptr);
  // Same-size commit re-renders through the WARP: bounds stay hull-quad sized
  // (PS bakes 66,80 67x35), never the flat 40x30 content rect.
  const auto bounds = committed->bounds();
  CHECK(std::abs(bounds.x - 66) <= 4 && std::abs(bounds.y - 80) <= 4);
  CHECK(std::abs(bounds.width - 67) <= 6 && std::abs(bounds.height - 35) <= 6);
  const auto& pixels = committed->pixels();
  int magenta = 0;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      if (px != nullptr && px[3] > 200 && px[0] > 150 && px[1] < 90 && px[2] > 150) {
        ++magenta;
      }
    }
  }
  CHECK(magenta > 800);
  // Warp metadata and the hull placement survive the commit untouched.
  CHECK(patchy::smart_object_warp_from_layer(*committed).has_value());
  const auto placement = patchy::smart_object_placement_from_layer(*committed);
  CHECK(placement.has_value());
  CHECK(std::abs(placement->transform[0] - 66.0) < 1e-6);
  CHECK(std::abs(placement->transform[1] - 78.0) < 1e-6);
}

void ui_warp_transform_bends_pixel_layer_and_undoes() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(120, 90, patchy::PixelFormat::rgba8());
  patchy::PixelBuffer pixels(48, 36, patchy::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < 36; ++y) {
    for (std::int32_t x = 0; x < 48; ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(40 + x * 4);
      px[1] = static_cast<std::uint8_t>(200 - y * 4);
      px[2] = 90;
      px[3] = 255;
    }
  }
  patchy::Layer layer(built.allocate_layer_id(), "warp me", std::move(pixels));
  layer.set_bounds(patchy::Rect{30, 25, 48, 36});
  built.add_layer(std::move(layer));
  const auto layer_id = built.layers().back().id();
  built.set_active_layer(layer_id);
  window.add_document_session(std::move(built), QStringLiteral("Warp"));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto original_bounds = document.find_layer(layer_id)->bounds();
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // The Edit menu action starts the cage; the options-bar warp widgets appear.
  require_action(window, "editWarpTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  CHECK(canvas->warp_handle_count() == 16);
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("warpStyleCombo"));
  CHECK(style_combo != nullptr && style_combo->isVisible());

  // Drag the top-left corner up-left: the bake must expand beyond the old bounds.
  const auto corner = canvas->warp_handle_document_position(0);
  CHECK(std::abs(corner.x() - original_bounds.x) < 0.5);
  CHECK(std::abs(corner.y() - original_bounds.y) < 0.5);
  canvas->set_warp_handle_document_position(0, corner + QPointF(-12.0, -9.0));
  CHECK(canvas->warp_style_preset() == QStringLiteral("warpCustom"));
  canvas->finish_warp_transform();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());

  const auto* warped = document.find_layer(layer_id);
  CHECK(warped != nullptr);
  const auto warped_bounds = warped->bounds();
  CHECK(warped_bounds.x < original_bounds.x - 6);
  CHECK(warped_bounds.y < original_bounds.y - 4);
  CHECK(warped_bounds.width > original_bounds.width);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);

  // One undo restores the un-warped pixels and bounds.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  auto& after_undo = patchy::ui::MainWindowTestAccess::document(window);
  const auto* restored = after_undo.find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->bounds().x == original_bounds.x);
  CHECK(restored->bounds().y == original_bounds.y);
  CHECK(restored->bounds().width == original_bounds.width);
  CHECK(restored->bounds().height == original_bounds.height);
}

void ui_warp_transform_on_smart_object_writes_mesh_and_survives_resave() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto placement_before = patchy::smart_object_placement_from_layer(*document.find_layer(layer_id));
  CHECK(placement_before.has_value());

  CHECK(canvas->begin_warp_transform());
  CHECK(canvas->warp_transform_active());
  // Style presets bake through the same generators the parser uses.
  canvas->apply_warp_style_preset(QStringLiteral("warpArc"), 50.0);
  CHECK(canvas->warp_style_preset() == QStringLiteral("warpArc"));
  canvas->finish_warp_transform();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());

  const auto* warped = document.find_layer(layer_id);
  CHECK(warped != nullptr);
  const auto warp = patchy::smart_object_warp_from_layer(*warped);
  CHECK(warp.has_value());
  CHECK(warp->style == "warpCustom");  // commits bake PS-style: custom mesh, value 0
  CHECK(warp->u_order == 4 && warp->v_order == 4);
  CHECK(warp->mesh_xs.size() == 16U);
  CHECK(!warp->mesh_generated);
  CHECK(patchy::layer_smart_object_block_dirty(*warped));
  CHECK(patchy::smart_object_lock_reason(*warped).empty());
  const auto placement_after = patchy::smart_object_placement_from_layer(*warped);
  CHECK(placement_after.has_value());
  // Arc 50 swings the top corners outward: the stored quad is the new mesh hull in
  // document space, so it must extend past the original placement.
  CHECK(placement_after->transform[0] < placement_before->transform[0] - 1.0);
  CHECK(placement_after->transform[2] > placement_before->transform[2] + 1.0);
  // The re-rendered preview tracks the hull quad.
  const auto bounds = warped->bounds();
  CHECK(std::abs(bounds.x - placement_after->transform[0]) <= 3.0);
  CHECK(static_cast<double>(bounds.width) >=
        placement_after->transform[2] - placement_after->transform[0] - 3.0);

  // The regenerated SoLd survives a resave: same mesh, same quad, still unlocked.
  const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const patchy::Layer* reread_layer = nullptr;
  for (const auto& layer : reread.layers()) {
    if (layer.id() == layer_id || layer.name() == "small") {
      reread_layer = &layer;
    }
  }
  CHECK(reread_layer != nullptr);
  CHECK(patchy::smart_object_lock_reason(*reread_layer).empty());
  const auto reread_warp = patchy::smart_object_warp_from_layer(*reread_layer);
  CHECK(reread_warp.has_value());
  CHECK(reread_warp->mesh_xs == warp->mesh_xs);
  CHECK(reread_warp->mesh_ys == warp->mesh_ys);
  const auto reread_placement = patchy::smart_object_placement_from_layer(*reread_layer);
  CHECK(reread_placement.has_value());
  CHECK(std::abs(reread_placement->transform[0] - placement_after->transform[0]) < 1e-6);
  // E11 acceptance artifact: Photoshop must open a Patchy-authored warp cleanly.
  ensure_artifact_dir();
  patchy::psd::DocumentIo::write_layered_rgb8_file(
      document, std::filesystem::path("test-artifacts/ui_warp_tool_smart_object.psd"));
}

void ui_warp_transform_refuses_text_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(96, 64, patchy::PixelFormat::rgba8());
  patchy::Layer text_layer(built.allocate_layer_id(), "headline",
                           solid_pixels(40, 16, patchy::PixelFormat::rgba8(), QColor(20, 20, 20, 255)));
  text_layer.set_bounds(patchy::Rect{8, 8, 40, 16});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Sample";
  built.add_layer(std::move(text_layer));
  const auto text_id = built.layers().back().id();
  built.set_active_layer(text_id);
  window.add_document_session(std::move(built), QStringLiteral("WarpText"));
  QApplication::processEvents();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  CHECK(!canvas->begin_warp_transform());
  CHECK(!canvas->warp_transform_active());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("rasterize"), Qt::CaseInsensitive));
}

void ui_warp_text_dialog_applies_and_undoes() {
  // Photoshop's Warp Text: the options-bar dialog applies a style warp to the
  // active text layer as ONE undo step; Cancel restores the pre-dialog state.
  patchy::Document built(420, 260, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background",
                        solid_pixels(420, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer text_layer(built.allocate_layer_id(), "Warp Me",
                           solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto text_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{90, 110, 1, 1});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Warplify";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  built.add_layer(std::move(text_layer));
  built.set_active_layer(text_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("WarpTextDialog"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  // Give the layer its real unwarped render first (what a committed layer holds).
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, patchy::TextWarp{}));
  const auto unwarped_bounds = layer->bounds();
  const auto unwarped_pixels = layer->pixels();
  const auto same_rect = [](const patchy::Rect& a, const patchy::Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  const auto same_pixels = [](const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.data().begin(), a.data().end(), b.data().begin(), b.data().end());
  };
  CHECK(unwarped_bounds.width > 4 && unwarped_bounds.height > 4);
  CHECK(!patchy::text_warp_from_layer(*layer).has_value());

  // Cancel restores the original state even after live previews.
  bool drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    CHECK(dialog != nullptr);
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
    auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
    CHECK(style_combo != nullptr && bend_spin != nullptr);
    style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpArc")));
    bend_spin->setValue(50);
    QApplication::processEvents();
    // The live preview should already show warped pixels on the layer.
    auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(text_id);
    CHECK(preview_layer != nullptr);
    CHECK(preview_layer->bounds().height > unwarped_bounds.height + 4);
    drove_dialog = true;
    dialog->reject();
  });
  patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
  CHECK(drove_dialog);
  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  CHECK(same_rect(layer->bounds(), unwarped_bounds));
  CHECK(same_pixels(layer->pixels(), unwarped_pixels));
  CHECK(!patchy::text_warp_from_layer(*layer).has_value());

  // OK applies the warp: metadata + taller bounds (arc bows the text upward).
  drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    CHECK(dialog != nullptr);
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
    auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
    auto* vertical_radio = dialog->findChild<QRadioButton*>(QStringLiteral("warpTextVerticalRadio"));
    CHECK(style_combo != nullptr && bend_spin != nullptr && vertical_radio != nullptr);
    CHECK(!vertical_radio->isChecked());  // defaults to horizontal
    style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpArc")));
    bend_spin->setValue(50);
    QApplication::processEvents();
    save_widget_artifact("ui_warp_text_dialog", *dialog);
    drove_dialog = true;
    if (auto* buttons = dialog->findChild<QDialogButtonBox*>(); buttons != nullptr) {
      buttons->button(QDialogButtonBox::Ok)->click();
    }
  });
  patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
  CHECK(drove_dialog);
  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  const auto warped_bounds = layer->bounds();
  const auto warp = patchy::text_warp_from_layer(*layer);
  CHECK(warp.has_value());
  CHECK(warp->style == "warpArc");
  CHECK(warp->value == 50.0);
  CHECK(warp->bounds_right - warp->bounds_left > 4.0);
  CHECK(warped_bounds.height > unwarped_bounds.height + 4);

  // One undo step restores the unwarped layer; redo re-applies.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  CHECK(same_rect(layer->bounds(), unwarped_bounds));
  CHECK(!patchy::text_warp_from_layer(*layer).has_value());
  require_action_by_text(window, QStringLiteral("Redo"))->trigger();
  QApplication::processEvents();
  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  CHECK(same_rect(layer->bounds(), warped_bounds));
  CHECK(patchy::text_warp_from_layer(*layer).has_value());

  // Re-opening with style None removes the warp (back to the plain render).
  drove_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    CHECK(dialog != nullptr);
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
    CHECK(style_combo != nullptr);
    CHECK(style_combo->currentData().toString() == QStringLiteral("warpArc"));  // remembers
    auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
    CHECK(bend_spin != nullptr && bend_spin->value() == 50);
    style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpNone")));
    QApplication::processEvents();
    drove_dialog = true;
    if (auto* buttons = dialog->findChild<QDialogButtonBox*>(); buttons != nullptr) {
      buttons->button(QDialogButtonBox::Ok)->click();
    }
  });
  patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
  CHECK(drove_dialog);
  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  CHECK(!patchy::text_warp_from_layer(*layer).has_value());
  CHECK(std::abs(layer->bounds().height - unwarped_bounds.height) <= 2);
}

void ui_warp_text_render_matches_photoshop_if_available() {
  // COM captures (July 2026): Photoshop-rendered warped text next to the same text
  // unwarped. Re-rendering the warp through Patchy's pipeline must land on the
  // same geometry (IoU + bounds tolerances absorb the Qt-vs-PS anti-aliasing and
  // font-metric differences).
  if (skip_without_arial_for_psd_text_preview()) {
    return;
  }
  struct WarpRenderCase {
    const char* name;
    double min_iou;
    int max_bounds_delta;
  };
  // Floors sit well under the observed IoUs (~0.68-0.72 for point text): a
  // geometry bug drops to ~0.0-0.1, so these separate real breaks from
  // anti-aliasing and font-metric noise. Box text observes ~0.30 with a ~6-9 px
  // vertical offset: Photoshop hangs the first line's ascent above the frame top
  // while Qt lays it inside - a pre-existing box-text divergence the warp merely
  // inherits (the unwarped render shows the same offset), so its floors only pin
  // gross warp geometry.
  const WarpRenderCase cases[] = {
      {"wt_arc_p50", 0.62, 4},
      {"wt_rise_p50", 0.62, 4},
      {"wt_fisheye_p50", 0.62, 4},
      {"wt_squeeze_p100", 0.60, 4},
      {"wt_twist_p60_h30", 0.60, 4},
      {"wt_arch_p50_para", 0.22, 12},
      // Box-text frame semantics (a short line riding the frame warp's shoulder):
      // the corner geometry is dramatic, so even loose floors pin it hard; the
      // first-line layout offset amplifies along the surface gradient, hence the
      // wider bounds tolerance.
      {"wt_bulge_p50_para_smalltext", 0.30, 18},
      {"wt_arc_p50_para_smalltext", 0.20, 20},
      {"wt_bulge_p50_para_2lines", 0.30, 18},
  };
  patchy::ui::MainWindow window;
  show_window(window);
  int verified = 0;
  for (const auto& render_case : cases) {
    const auto psd_path =
        patchy::test::local_psd_fixture_path(std::string("ps2026_warptext/") + render_case.name + ".psd");
    const auto png_path =
        patchy::test::local_psd_fixture_path(std::string("ps2026_warptext/") + render_case.name + ".png");
    if (!std::filesystem::exists(psd_path) || !std::filesystem::exists(png_path)) {
      continue;
    }
    auto document = patchy::psd::DocumentIo::read_file(psd_path);
    CHECK(!document.layers().empty());
    auto& layer = document.layers().front();
    const auto warp = patchy::text_warp_from_layer(layer);
    CHECK(warp.has_value());
    // Re-render Photoshop's warp through Patchy's own pipeline.
    CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, layer, *warp));
    const QImage reference(QString::fromStdString(png_path.string()));
    CHECK(!reference.isNull());
    const auto reference_alpha = reference.convertToFormat(QImage::Format_RGBA8888);
    QImage rendered(reference.width(), reference.height(), QImage::Format_RGBA8888);
    rendered.fill(Qt::transparent);
    {
      QPainter painter(&rendered);
      painter.drawImage(QPoint(layer.bounds().x, layer.bounds().y),
                        image_from_pixels_for_visuals(layer.pixels()));
    }
    std::int64_t intersection = 0;
    std::int64_t union_count = 0;
    int min_x = reference.width();
    int max_x = -1;
    int min_y = reference.height();
    int max_y = -1;
    int ref_min_x = reference.width();
    int ref_max_x = -1;
    int ref_min_y = reference.height();
    int ref_max_y = -1;
    for (int y = 0; y < reference.height(); ++y) {
      const auto* ref_line = reference_alpha.constScanLine(y);
      const auto* out_line = rendered.constScanLine(y);
      for (int x = 0; x < reference.width(); ++x) {
        const bool ref_on = ref_line[x * 4 + 3] > 96;
        const bool out_on = out_line[x * 4 + 3] > 96;
        intersection += (ref_on && out_on) ? 1 : 0;
        union_count += (ref_on || out_on) ? 1 : 0;
        if (out_on) {
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
          min_y = std::min(min_y, y);
          max_y = std::max(max_y, y);
        }
        if (ref_on) {
          ref_min_x = std::min(ref_min_x, x);
          ref_max_x = std::max(ref_max_x, x);
          ref_min_y = std::min(ref_min_y, y);
          ref_max_y = std::max(ref_max_y, y);
        }
      }
    }
    CHECK(union_count > 0);
    const double iou = static_cast<double>(intersection) / static_cast<double>(union_count);
    std::cout << "  " << render_case.name << ": IoU " << iou << ", bounds delta ("
              << std::abs(min_x - ref_min_x) << "," << std::abs(min_y - ref_min_y) << ","
              << std::abs(max_x - ref_max_x) << "," << std::abs(max_y - ref_max_y) << ")\n";
    CHECK(iou >= render_case.min_iou);
    CHECK(std::abs(min_x - ref_min_x) <= render_case.max_bounds_delta);
    CHECK(std::abs(min_y - ref_min_y) <= render_case.max_bounds_delta);
    CHECK(std::abs(max_x - ref_max_x) <= render_case.max_bounds_delta);
    CHECK(std::abs(max_y - ref_max_y) <= render_case.max_bounds_delta);
    ++verified;
  }
  if (verified == 0) {
    std::cout << "[SKIP] ps2026_warptext capture fixtures missing\n";
    return;
  }
}

void ui_warp_text_box_text_warps_over_frame() {
  // Editor-created point text warps about its own layout; BOX text warps over the
  // dragged FRAME like Photoshop (wt_*_para_smalltext captures: a short line in a
  // big box rides the warp surface's shoulder in Photoshop too, so a "nicer"
  // text-extent box here would diverge from PS).
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(QPoint(100, 140));
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  editor->insertPlainText(QStringLiteral("Hey man!"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto active_id = document.active_layer_id();
  CHECK(active_id.has_value());
  auto* layer = document.find_layer(*active_id);
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  auto unwarped = image_from_pixels_for_visuals(layer->pixels());
  // Drive the REAL dialog like a user: pick Bulge, bend 50, OK.
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("warpTextDialog"));
    CHECK(dialog != nullptr);
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("warpTextStyleCombo"));
    auto* bend_spin = dialog->findChild<QSpinBox*>(QStringLiteral("warpTextBendSpin"));
    CHECK(style_combo != nullptr && bend_spin != nullptr);
    style_combo->setCurrentIndex(style_combo->findData(QStringLiteral("warpBulge")));
    bend_spin->setValue(50);
    QApplication::processEvents();
    if (auto* buttons = dialog->findChild<QDialogButtonBox*>(); buttons != nullptr) {
      buttons->button(QDialogButtonBox::Ok)->click();
    }
  });
  patchy::ui::MainWindowTestAccess::request_warp_text_dialog(window);
  layer = document.find_layer(*active_id);
  CHECK(layer != nullptr);
  const auto stored = patchy::text_warp_from_layer(*layer);
  CHECK(stored.has_value());
  CHECK(stored->style == "warpBulge");
  CHECK(stored->value == 50.0);
  auto warped = image_from_pixels_for_visuals(layer->pixels());
  QImage sheet(std::max(unwarped.width(), warped.width()),
               unwarped.height() + warped.height() + 8, QImage::Format_RGBA8888);
  sheet.fill(Qt::white);
  {
    QPainter painter(&sheet);
    painter.drawImage(0, 0, unwarped);
    painter.drawImage(0, unwarped.height() + 8, warped);
  }
  ensure_artifact_dir();
  CHECK(sheet.save(QStringLiteral("test-artifacts/ui_warp_text_point_bulge.png")));
  std::cout << "  unwarped " << unwarped.width() << "x" << unwarped.height() << " warped "
            << warped.width() << "x" << warped.height() << " at " << layer->bounds().x << ","
            << layer->bounds().y << "\n";

  // Second scenario: BOX text with a short line in a big frame (the dragged-box flow).
  patchy::Layer box_layer(document.allocate_layer_id(), "BoxText",
                          solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto box_id = box_layer.id();
  box_layer.set_bounds(patchy::Rect{80, 40, 1, 1});
  box_layer.metadata()[patchy::kLayerMetadataText] = "Hi!";
  box_layer.metadata()[patchy::kLayerMetadataTextSize] = "48";
  box_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  box_layer.metadata()[patchy::kLayerMetadataTextFlow] = "box";
  box_layer.metadata()[patchy::kLayerMetadataTextBoxWidth] = "300";
  box_layer.metadata()[patchy::kLayerMetadataTextBoxHeight] = "140";
  document.add_layer(std::move(box_layer));
  auto* boxed = document.find_layer(box_id);
  CHECK(boxed != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *boxed, patchy::TextWarp{}));
  auto box_unwarped = image_from_pixels_for_visuals(boxed->pixels());
  const auto ink_height = [](const QImage& image) {
    int min_y = image.height();
    int max_y = -1;
    for (int y = 0; y < image.height(); ++y) {
      const auto* line = image.constScanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        if (line[x * 4 + 3] > 96) {
          min_y = std::min(min_y, y);
          max_y = std::max(max_y, y);
          break;
        }
      }
    }
    return max_y >= min_y ? (max_y - min_y + 1) : 0;
  };
  const auto unwarped_ink_height = ink_height(box_unwarped.convertToFormat(QImage::Format_RGBA8888));
  CHECK(unwarped_ink_height > 20);
  patchy::TextWarp box_bulge;
  box_bulge.style = "warpBulge";
  box_bulge.value = 50.0;
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *boxed, box_bulge));
  auto box_warped = image_from_pixels_for_visuals(boxed->pixels());
  // Frame semantics: the corner-sitting line shears up the frame bulge's shoulder,
  // growing its ink height far past what a text-extent bulge would (PS: 36 -> 68).
  const auto warped_ink_height = ink_height(box_warped.convertToFormat(QImage::Format_RGBA8888));
  CHECK(warped_ink_height >= unwarped_ink_height + 20);
  QImage box_sheet(std::max(box_unwarped.width(), box_warped.width()),
                   box_unwarped.height() + box_warped.height() + 8, QImage::Format_RGBA8888);
  box_sheet.fill(Qt::white);
  {
    QPainter painter(&box_sheet);
    painter.drawImage(0, 0, box_unwarped);
    painter.drawImage(0, box_unwarped.height() + 8, box_warped);
  }
  CHECK(box_sheet.save(QStringLiteral("test-artifacts/ui_warp_text_box_frame_bulge.png")));
  std::cout << "  box ink height " << unwarped_ink_height << " -> " << warped_ink_height << "\n";
}

void ui_warp_text_psd_writes_baseline_anchored_geometry() {
  // Photoshop anchors a point-text transform at the first-line BASELINE; a warped
  // Patchy save must do the same (box top = -ascent-ish, negative) or Photoshop's
  // type re-render drops the text by one descent (the buldge_test jump).
  patchy::Document built(420, 260, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background",
                        solid_pixels(420, 260, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer text_layer(built.allocate_layer_id(), "Anchored",
                           solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto text_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{90, 120, 1, 1});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Jumpy";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "48";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  built.add_layer(std::move(text_layer));
  built.set_active_layer(text_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("WarpBaseline"));
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  patchy::TextWarp warp;
  warp.style = "warpBulge";
  warp.value = 60.0;
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, warp));
  const auto stored = patchy::text_warp_from_layer(*layer);
  CHECK(stored.has_value());
  const auto box_height = stored->bounds_bottom - stored->bounds_top;
  CHECK(box_height > 10.0);
  // The metadata baseline sits in the box's lower half (ascent of the first line).
  CHECK(stored->baseline > box_height * 0.5);
  CHECK(stored->baseline < box_height);

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  ensure_artifact_dir();
  {
    // Kept as an artifact so the Photoshop re-render jump can be re-measured by COM.
    std::ofstream out("test-artifacts/warp_baseline_check.psd", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto reopened = patchy::psd::DocumentIo::read(bytes);
  const patchy::Layer* reopened_layer = nullptr;
  for (const auto& candidate : reopened.layers()) {
    if (candidate.name() == "Anchored") {
      reopened_layer = &candidate;
      break;
    }
  }
  CHECK(reopened_layer != nullptr);
  const auto reopened_warp = patchy::text_warp_from_layer(*reopened_layer);
  CHECK(reopened_warp.has_value());
  CHECK(reopened_warp->style == "warpBulge");
  // Baseline-relative box in the file: ascent above the origin, descent below.
  CHECK(reopened_warp->bounds_top < -0.5);
  CHECK(std::abs(-reopened_warp->bounds_top - stored->baseline) < 1.0);
  CHECK(std::abs(reopened_warp->bounds_bottom - (box_height - stored->baseline)) < 1.0);
}

void ui_warp_text_survives_editor_commit() {
  // Editing warped text re-renders through the warp with a box freshly derived
  // from the new layout (Photoshop recomputes its warp box on every change).
  patchy::Document built(480, 280, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background",
                        solid_pixels(480, 280, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer text_layer(built.allocate_layer_id(), "Warped",
                           solid_pixels(1, 1, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));
  const auto text_id = text_layer.id();
  text_layer.set_bounds(patchy::Rect{110, 130, 1, 1});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Bend";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "36";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  built.add_layer(std::move(text_layer));
  built.set_active_layer(text_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(built), QStringLiteral("WarpCommit"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  patchy::TextWarp warp;
  warp.style = "warpArc";
  warp.value = 60.0;
  CHECK(patchy::ui::MainWindowTestAccess::apply_text_warp(window, *layer, warp));
  const auto warped_bounds = layer->bounds();
  const auto warp_before = patchy::text_warp_from_layer(*layer);
  CHECK(warp_before.has_value());
  const auto box_width_before = warp_before->bounds_right - warp_before->bounds_left;

  // Edit the text through the inline editor: click into the warped INK (the middle
  // of the warped bounds can be empty air under an arc), append, commit.
  QPoint ink_document_point(warped_bounds.x, warped_bounds.y);
  {
    const auto& pixels = layer->pixels();
    const auto data = pixels.data();
    bool found_ink = false;
    for (int y = 0; y < pixels.height() && !found_ink; ++y) {
      for (int x = 0; x < pixels.width() && !found_ink; ++x) {
        if (data[(static_cast<std::size_t>(y) * pixels.width() + x) * 4U + 3U] > 200) {
          ink_document_point = QPoint(warped_bounds.x + x + 1, warped_bounds.y + y + 1);
          found_ink = true;
        }
      }
    }
    CHECK(found_ink);
  }
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto hit_point = canvas->widget_position_for_document_point(ink_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->toPlainText() == QStringLiteral("Bend"));
  QTextCursor cursor(editor->document());
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  editor->insertPlainText(QStringLiteral("ier"));
  QApplication::processEvents();
  // Switching tools commits the edit (the established commit path in these tests).
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  layer = document.find_layer(text_id);
  CHECK(layer != nullptr);
  const auto found = layer->metadata().find(patchy::kLayerMetadataText);
  CHECK(found != layer->metadata().end() && found->second == "Bendier");
  const auto warp_after = patchy::text_warp_from_layer(*layer);
  CHECK(warp_after.has_value());
  CHECK(warp_after->style == "warpArc");
  CHECK(warp_after->value == 60.0);
  // Longer text = wider layout box; the warp box must follow it.
  CHECK(warp_after->bounds_right - warp_after->bounds_left > box_width_before + 4.0);
  CHECK(layer->bounds().width > warped_bounds.width + 4);
  // Still warped: the arc lifts the ends well above a flat line of this size.
  CHECK(layer->bounds().height > warped_bounds.height - 4);
}

void ui_options_bar_transform_session_replaces_tool_controls() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  window.resize(1500, 800);
  QApplication::processEvents();
  process_events_for(60);
  // The default document starts transparent; Ctrl+T needs opaque pixels.
  use_solid_fill_settings(canvas);
  canvas->set_primary_color(QColor(40, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("warpStyleCombo"));
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(options_bar != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(x_spin != nullptr);
  CHECK(style_combo != nullptr);
  CHECK(toggle != nullptr);
  CHECK(apply != nullptr);

  // Brush is the active tool: its controls show, no session widgets.
  CHECK(brush_size->isVisible());
  CHECK(!x_spin->isVisible());
  CHECK(!toggle->isVisible());
  const int single_row_height = options_bar->height();

  // Ctrl+T: the session OWNS the bar (Photoshop) - the brush row swaps out for
  // the transform controls and the bar keeps its single-row height, so the
  // canvas never shifts down.
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  process_events_for(60);
  CHECK(canvas->free_transform_active());
  CHECK(!brush_size->isVisible());
  CHECK(x_spin->isVisible());
  CHECK(!style_combo->isVisible());
  CHECK(toggle->isVisible());
  CHECK(!toggle->isChecked());
  CHECK(apply->isVisible());
  CHECK(options_bar->height() == single_row_height);
  save_widget_artifact("ui_options_bar_transform_mode", window);

  // The warp-mode toggle swaps to the warp controls, same single row.
  toggle->click();
  QApplication::processEvents();
  process_events_for(60);
  CHECK(canvas->warp_transform_active());
  CHECK(!canvas->free_transform_active());
  CHECK(!brush_size->isVisible());
  CHECK(!x_spin->isVisible());
  CHECK(style_combo->isVisible());
  CHECK(toggle->isVisible());
  CHECK(toggle->isChecked());
  CHECK(apply->isVisible());
  CHECK(options_bar->height() == single_row_height);
  save_widget_artifact("ui_options_bar_warp_mode", window);

  // Esc restores the tool's own controls.
  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  process_events_for(60);
  CHECK(!canvas->warp_transform_active());
  CHECK(brush_size->isVisible());
  CHECK(!x_spin->isVisible());
  CHECK(!toggle->isVisible());
  CHECK(options_bar->height() == single_row_height);
}

void ui_free_transform_warp_toggle_composes_single_commit() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(200, 150, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background", solid_pixels(200, 150, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(built.allocate_layer_id(), "warp me",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(200, 40, 160, 255)));
  layer.set_bounds(patchy::Rect{76, 57, 48, 36});
  built.add_layer(std::move(layer));
  const auto layer_id = built.layers().back().id();
  built.set_active_layer(layer_id);
  window.add_document_session(std::move(built), QStringLiteral("ComposeWarp"));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  // Free transform first: scale to 200% about the center (box (52,39) 96x72).
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  CHECK(scale_x != nullptr && scale_x->isVisible());
  scale_x->setValue(200.0);  // the link button mirrors it into the vertical scale
  QApplication::processEvents();

  // The warp toggle carries the pending scale into the cage instead of dropping
  // it: the corner handle sits on the SCALED box, not the stored layer bounds.
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  CHECK(toggle != nullptr && toggle->isVisible());
  toggle->click();
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  CHECK(!canvas->free_transform_active());
  CHECK(canvas->warp_handle_count() == 16);
  const auto corner = canvas->warp_handle_document_position(0);
  CHECK(std::abs(corner.x() - 52.0) < 1.5);
  CHECK(std::abs(corner.y() - 39.0) < 1.5);

  // Warp the corner outward, then commit through the shared Apply button: ONE
  // undo step bakes scale + warp together.
  canvas->set_warp_handle_document_position(0, corner + QPointF(-10.0, -8.0));
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr && apply->isVisible());
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());
  CHECK(!canvas->free_transform_active());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);

  const auto* committed = document.find_layer(layer_id);
  CHECK(committed != nullptr);
  const auto bounds = committed->bounds();
  CHECK(bounds.x <= 44);       // the scaled left edge (52) pulled further left
  CHECK(bounds.width >= 100);  // 2x scale (96) plus the warp bulge
  CHECK(bounds.height >= 74);

  // One undo restores the original layer exactly.
  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  auto& after_undo = patchy::ui::MainWindowTestAccess::document(window);
  const auto* restored = after_undo.find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->bounds().x == 76 && restored->bounds().y == 57);
  CHECK(restored->bounds().width == 48 && restored->bounds().height == 36);
}

void ui_warp_toggle_back_to_free_transform_keeps_pending_warp() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(220, 160, patchy::PixelFormat::rgba8());
  built.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(built.allocate_layer_id(), "flag",
                      solid_pixels(60, 40, patchy::PixelFormat::rgba8(), QColor(30, 120, 220, 255)));
  layer.set_bounds(patchy::Rect{80, 60, 60, 40});
  built.add_layer(std::move(layer));
  const auto layer_id = built.layers().back().id();
  built.set_active_layer(layer_id);
  window.add_document_session(std::move(built), QStringLiteral("WarpThenMove"));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_action(window, "editWarpTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  canvas->apply_warp_style_preset(QStringLiteral("warpArc"), 50.0);
  QApplication::processEvents();

  // Toggle back to free transform: the warp rides along uncommitted and the
  // affine stage edits the warped hull box.
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  CHECK(toggle != nullptr && toggle->isChecked());
  toggle->click();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  CHECK(!canvas->warp_transform_active());
  CHECK(!toggle->isChecked());
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value());
  CHECK(state->active);

  // Move the pending result 30 px right via the reference X spin, then commit:
  // one undo step containing warp + move, baked in a single resample.
  auto* x_spin = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformXSpin"));
  CHECK(x_spin != nullptr && x_spin->isVisible());
  x_spin->setValue(x_spin->value() + 30.0);
  QApplication::processEvents();
  auto* apply = window.findChild<QPushButton*>(QStringLiteral("freeTransformApplyButton"));
  CHECK(apply != nullptr);
  apply->click();
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);

  const auto* committed = document.find_layer(layer_id);
  CHECK(committed != nullptr);
  const auto bounds = committed->bounds();
  CHECK(bounds.width > 66);  // arc 50 swings the corners outward past the 60 px content
  // The symmetric arc keeps the hull centered on the content, so the committed
  // center sits at the original center (110) + the 30 px nudge.
  const auto center_x = bounds.x + bounds.width / 2.0;
  CHECK(std::abs(center_x - 140.0) <= 4.0);

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  auto& after_undo = patchy::ui::MainWindowTestAccess::document(window);
  const auto* restored = after_undo.find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->bounds().x == 80 && restored->bounds().y == 60);
  CHECK(restored->bounds().width == 60 && restored->bounds().height == 40);
}

void ui_smart_object_transform_then_warp_commits_composed_placement() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layer_id = open_smart_object_fixture(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  const auto placement_before = patchy::smart_object_placement_from_layer(*document.find_layer(layer_id));
  CHECK(placement_before.has_value());
  const auto width_before = placement_before->transform[2] - placement_before->transform[0];
  const auto undo_depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  auto* scale_x = window.findChild<QDoubleSpinBox*>(QStringLiteral("freeTransformScaleXSpin"));
  CHECK(scale_x != nullptr);
  scale_x->setValue(150.0);
  QApplication::processEvents();

  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  CHECK(toggle != nullptr);
  toggle->click();
  QApplication::processEvents();
  CHECK(canvas->warp_transform_active());
  canvas->apply_warp_style_preset(QStringLiteral("warpArc"), 30.0);
  canvas->finish_warp_transform();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_depth_before + 1);

  const auto* warped = document.find_layer(layer_id);
  CHECK(warped != nullptr);
  const auto warp = patchy::smart_object_warp_from_layer(*warped);
  CHECK(warp.has_value());
  CHECK(warp->u_order == 4 && warp->v_order == 4);
  CHECK(patchy::smart_object_lock_reason(*warped).empty());
  CHECK(patchy::layer_smart_object_block_dirty(*warped));
  const auto placement_after = patchy::smart_object_placement_from_layer(*warped);
  CHECK(placement_after.has_value());
  // 150% scale composed with the arc hull growth: the stored quad ends up
  // clearly wider than the original placement.
  const auto width_after = placement_after->transform[2] - placement_after->transform[0];
  CHECK(width_after > width_before * 1.4);
}

void ui_warp_toggle_refuses_text_layer_and_keeps_transform() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document built(96, 64, patchy::PixelFormat::rgba8());
  patchy::Layer text_layer(built.allocate_layer_id(), "headline",
                           solid_pixels(40, 16, patchy::PixelFormat::rgba8(), QColor(20, 20, 20, 255)));
  text_layer.set_bounds(patchy::Rect{8, 8, 40, 16});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Sample";
  built.add_layer(std::move(text_layer));
  const auto text_id = built.layers().back().id();
  built.set_active_layer(text_id);
  window.add_document_session(std::move(built), QStringLiteral("WarpToggleText"));
  QApplication::processEvents();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  // The toggle refuses text layers (warp would too) but must keep the pending
  // free-transform session alive instead of discarding it.
  auto* toggle = window.findChild<QPushButton*>(QStringLiteral("transformWarpModeButton"));
  CHECK(toggle != nullptr && toggle->isVisible());
  toggle->click();
  QApplication::processEvents();
  CHECK(!canvas->warp_transform_active());
  CHECK(canvas->free_transform_active());
  CHECK(!toggle->isChecked());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("rasterize"), Qt::CaseInsensitive));

  send_key(*canvas, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(!canvas->free_transform_active());
}

}  // namespace

std::vector<patchy::test::TestCase> warp_tests() {
  return {
      {"ui_warp_render_matches_photoshop_preview_if_available",
       ui_warp_render_matches_photoshop_preview_if_available},
      {"ui_warped_smart_object_free_transform_rerenders", ui_warped_smart_object_free_transform_rerenders},
      {"ui_warped_smart_object_edit_commit_keeps_warp", ui_warped_smart_object_edit_commit_keeps_warp},
      {"ui_warp_transform_bends_pixel_layer_and_undoes", ui_warp_transform_bends_pixel_layer_and_undoes},
      {"ui_warp_transform_on_smart_object_writes_mesh_and_survives_resave",
       ui_warp_transform_on_smart_object_writes_mesh_and_survives_resave},
      {"ui_warp_transform_refuses_text_layer", ui_warp_transform_refuses_text_layer},
      {"ui_warp_text_dialog_applies_and_undoes", ui_warp_text_dialog_applies_and_undoes},
      {"ui_warp_text_box_text_warps_over_frame", ui_warp_text_box_text_warps_over_frame},
      {"ui_warp_text_psd_writes_baseline_anchored_geometry",
       ui_warp_text_psd_writes_baseline_anchored_geometry},
      {"ui_warp_text_render_matches_photoshop_if_available",
       ui_warp_text_render_matches_photoshop_if_available},
      {"ui_warp_text_survives_editor_commit", ui_warp_text_survives_editor_commit},
      {"ui_options_bar_transform_session_replaces_tool_controls",
       ui_options_bar_transform_session_replaces_tool_controls},
      {"ui_free_transform_warp_toggle_composes_single_commit", ui_free_transform_warp_toggle_composes_single_commit},
      {"ui_warp_toggle_back_to_free_transform_keeps_pending_warp",
       ui_warp_toggle_back_to_free_transform_keeps_pending_warp},
      {"ui_smart_object_transform_then_warp_commits_composed_placement",
       ui_smart_object_transform_then_warp_commits_composed_placement},
      {"ui_warp_toggle_refuses_text_layer_and_keeps_transform", ui_warp_toggle_refuses_text_layer_and_keeps_transform},
  };
}
